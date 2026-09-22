// Lab 0 app: a COMPLETE lattice-based public-key encryption round, end to end.
//
//   keygen    a_hat = uniform poly           (already in the NTT domain)
//             s, e  = small noise polys      (centered binomial)
//             t_hat = a_hat . NTT(s) + NTT(e)
//   encrypt   r, e1, e2 = noise
//             u = INTT(a_hat . NTT(r)) + e1
//             v = INTT(t_hat . NTT(r)) + e2 + encode(message)
//             ciphertext = compress(u, 10 bits) , compress(v, 4 bits)
//   decrypt   m' = decode( v - INTT(NTT(u) . NTT(s)) )
//             check m' == m
//
// This is the CPA-secure public-key scheme of Kyber (Bos et al., EuroS&P 2018)
// and NewHope, with one polynomial instead of a module of them: same ring,
// same noise, same compression, same failure analysis -- shrunk so a whole
// keygen/encrypt/decrypt round fits in an RTL simulation. The ring is
// Z_q[x]/(x^n+1) with q = 12289 and n = 256; all arithmetic is exact integer
// arithmetic mod q, so host, Verilator and FPGA agree bit for bit.
//
// Six kernels, six very different shapes:
//   * NTT / INTT   the number-theoretic transform: the FFT butterfly with the
//                  complex unit circle replaced by the integers mod q. O(n log
//                  n), 1024 butterflies per transform, seven transforms per
//                  round, and one multiply-mod inside each butterfly;
//   * pointwise    the polynomial multiply itself once you are in the NTT
//                  domain: n independent multiply-mods, embarrassingly
//                  parallel, and O(n) against the transform's O(n log n);
//   * sample       noise and uniform generation -- bit twiddling and rejection,
//                  no multiplier at all;
//   * polyadd      n add-mods, the cheapest thing in the app;
//   * compress     a multiply, a rounding add and a divide per coefficient;
//   * encode/decode the message layer, one compare per bit.
//
// The decision Lab 0 asks for: the NTT is the kernel every published Kyber
// accelerator builds, but is it actually where this app's cycles go? The
// pointwise multiply does the "real" work and the transforms are only there to
// make it possible. Read the table this app prints before you assume.
//
// Note on the modular reduction: `k_modq` is a plain `%`, which the compiler
// turns into a multiply and a shift. Replacing it with Montgomery or Barrett
// reduction is a textbook optimization and a good Lab 2 experiment -- but
// measure its share here first.

#include <stdint.h>
#include <stdio.h>
#include "profile.h"
#include "kernels.h"

#ifndef N
#define N      256                // ring degree (power of two, n | 2048 for q)
#endif
#ifndef NROUND
#define NROUND 1                  // keygen+encrypt+decrypt rounds per run
#endif

#define ETA    2                  // noise parameter of the binomial sampler
#define DU     10                 // bits per coefficient of the u ciphertext
#define DV      4                 // bits per coefficient of the v ciphertext
#define QHALF  ((K_NTT_Q + 1) / 2)   // the "1" of the message encoding

#ifndef GOLDEN
#define GOLDEN 0x0001fbbau
#endif

// Twiddle tables: built once, shared by every transform (a hardware NTT holds
// them in a small ROM exactly like this).
static int32_t psi_rev[N], psi_inv_rev[N], n_inv;

// Key material and work polynomials.
static int32_t a_hat[N], s[N], s_hat[N], e[N], e_hat[N], t_hat[N];
static int32_t r[N], r_hat[N], e1[N], e2[N];
static int32_t u[N], v[N], tmp[N];
static int32_t cu[N], cv[N];              // the compressed ciphertext
static int32_t msg[N], dec[N];            // message bits in, bits out

PROFILE_ACC_DECL(total);
PROFILE_ACC_DECL(ntt);
PROFILE_ACC_DECL(intt);
PROFILE_ACC_DECL(pointwise);
PROFILE_ACC_DECL(polyadd);
PROFILE_ACC_DECL(sample);
PROFILE_ACC_DECL(compress);
PROFILE_ACC_DECL(message);

// One message bit per coefficient: 0 -> 0, 1 -> q/2. Half the ring apart is
// the largest distance available, which is exactly the noise budget: decryption
// succeeds while the accumulated noise stays below q/4.
static void encode_msg(const int32_t *bits, int32_t *poly) {
    for (int i = 0; i < N; i++)
        poly[i] = bits[i] ? QHALF : 0;
}

// Decode by distance: a coefficient nearer to q/2 than to 0 (or q) was a 1.
static void decode_msg(const int32_t *poly, int32_t *bits) {
    for (int i = 0; i < N; i++) {
        int32_t d = poly[i];
        if (d > K_NTT_Q / 2) d = K_NTT_Q - d;   // fold into [0, q/2]
        bits[i] = (d > K_NTT_Q / 4) ? 1 : 0;
    }
}

// --- Key generation ---------------------------------------------------------
static void keygen(uint32_t *st) {
    PROFILE_ACC_START(sample);
    k_poly_uniform_q(a_hat, N, st);      // the public "matrix", NTT domain
    k_poly_cbd_q(s, N, ETA, st);         // the secret
    k_poly_cbd_q(e, N, ETA, st);         // the error that hides it
    PROFILE_ACC_END(sample);

    for (int i = 0; i < N; i++) { s_hat[i] = s[i]; e_hat[i] = e[i]; }

    PROFILE_ACC_START(ntt);
    k_ntt_fwd(s_hat, N, psi_rev);
    k_ntt_fwd(e_hat, N, psi_rev);
    PROFILE_ACC_END(ntt);

    PROFILE_ACC_START(pointwise);
    k_poly_pointwise_q(a_hat, s_hat, t_hat, N);
    PROFILE_ACC_END(pointwise);

    PROFILE_ACC_START(polyadd);
    k_poly_add_q(t_hat, e_hat, t_hat, N);
    PROFILE_ACC_END(polyadd);
}

// --- Encryption -------------------------------------------------------------
static void encrypt(uint32_t *st) {
    PROFILE_ACC_START(sample);
    k_poly_cbd_q(r,  N, ETA, st);
    k_poly_cbd_q(e1, N, ETA, st);
    k_poly_cbd_q(e2, N, ETA, st);
    PROFILE_ACC_END(sample);

    for (int i = 0; i < N; i++) r_hat[i] = r[i];

    PROFILE_ACC_START(ntt);
    k_ntt_fwd(r_hat, N, psi_rev);
    PROFILE_ACC_END(ntt);

    // u = a * r + e1
    PROFILE_ACC_START(pointwise);
    k_poly_pointwise_q(a_hat, r_hat, u, N);
    PROFILE_ACC_END(pointwise);
    PROFILE_ACC_START(intt);
    k_ntt_inv(u, N, psi_inv_rev, n_inv);
    PROFILE_ACC_END(intt);
    PROFILE_ACC_START(polyadd);
    k_poly_add_q(u, e1, u, N);
    PROFILE_ACC_END(polyadd);

    // v = t * r + e2 + encode(m)
    PROFILE_ACC_START(pointwise);
    k_poly_pointwise_q(t_hat, r_hat, v, N);
    PROFILE_ACC_END(pointwise);
    PROFILE_ACC_START(intt);
    k_ntt_inv(v, N, psi_inv_rev, n_inv);
    PROFILE_ACC_END(intt);
    PROFILE_ACC_START(polyadd);
    k_poly_add_q(v, e2, v, N);
    PROFILE_ACC_END(polyadd);

    PROFILE_ACC_START(message);
    encode_msg(msg, tmp);
    PROFILE_ACC_END(message);
    PROFILE_ACC_START(polyadd);
    k_poly_add_q(v, tmp, v, N);
    PROFILE_ACC_END(polyadd);

    PROFILE_ACC_START(compress);
    k_compress_q(u, cu, N, DU);
    k_compress_q(v, cv, N, DV);
    PROFILE_ACC_END(compress);
}

// --- Decryption -------------------------------------------------------------
// Returns the number of wrong bits; 0 is the only acceptable answer.
static int decrypt(void) {
    PROFILE_ACC_START(compress);
    k_decompress_q(cu, u, N, DU);
    k_decompress_q(cv, v, N, DV);
    PROFILE_ACC_END(compress);

    PROFILE_ACC_START(ntt);
    k_ntt_fwd(u, N, psi_rev);
    PROFILE_ACC_END(ntt);

    PROFILE_ACC_START(pointwise);
    k_poly_pointwise_q(u, s_hat, tmp, N);
    PROFILE_ACC_END(pointwise);

    PROFILE_ACC_START(intt);
    k_ntt_inv(tmp, N, psi_inv_rev, n_inv);
    PROFILE_ACC_END(intt);

    PROFILE_ACC_START(polyadd);
    k_poly_sub_q(v, tmp, tmp, N);
    PROFILE_ACC_END(polyadd);

    PROFILE_ACC_START(message);
    decode_msg(tmp, dec);
    PROFILE_ACC_END(message);

    int bad = 0;
    for (int i = 0; i < N; i++)
        if (dec[i] != msg[i]) bad++;
    return bad;
}

int main(void) {
    k_ntt_tables(N, psi_rev, psi_inv_rev, &n_inv);

    uint32_t sum = 0;
    int errors = 0;

    for (int round = 0; round < NROUND; round++) {
        uint32_t st = 0x5EEDu + (uint32_t)round;

        for (int i = 0; i < N; i++)          // a fresh message every round
            msg[i] = (int32_t)(k_rand(&st) & 1u);

        PROFILE_ACC_START(total);
        keygen(&st);
        encrypt(&st);
        int bad = decrypt();
        PROFILE_ACC_END(total);

        errors += bad;
        sum += (k_checksum_i32(cu, N) ^ (k_checksum_i32(cv, N) << 1)) *
               (uint32_t)(round + 1);
        sum += (uint32_t)bad * 0x10000u;
    }

    PROFILE_PRINTF("pqcrypto n=%d q=%d eta=%d du=%d dv=%d, %d round(s)\n",
           N, K_NTT_Q, ETA, DU, DV, NROUND);
    // The ciphertext is (N*DU + N*DV) bits against N bits of plaintext; that
    // expansion, and the noise budget that forces it, is the whole cost of
    // lattice cryptography.
    PROFILE_PRINTF("ciphertext %d bytes for %d message bits, %d bit error(s)\n",
           (N * DU + N * DV) / 8, N, errors);
    PROFILE_PRINTF("[profile] %-12s %10s %10s %6s %6s\n",
           "kernel", "cycles", "instr", "calls", "share");
    PROFILE_ACC_REPORT_OF(total,     PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(ntt,       PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(intt,      PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(pointwise, PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(polyadd,   PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(sample,    PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(compress,  PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(message,   PROFILE_ACC_CYCLES(total));

    if (errors) {
        printf("pqcrypto: %d bit error(s) FAIL\n", errors);
        return 1;
    }
    return check_result("pqcrypto", sum, GOLDEN);
}
