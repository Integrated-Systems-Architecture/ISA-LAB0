// Lab 0 app: a COMPLETE digital receiver front-end, end to end.
//
//   int16 samples (two tones, one wanted, one interferer)
//     -> NCO           cos/sin of a running phase, by CORDIC
//     -> mixer         x * cos, x * sin  ->  I/Q at baseband
//     -> FIR x2        anti-alias low-pass on I and on Q
//     -> decimate /4   throw away what the filter made redundant
//     -> magnitude     sqrt(I^2 + Q^2), by integer square root
//     -> detector      count and accumulate the samples over a threshold
//
// The textbook receive chain (see e.g. Lyons, "Understanding Digital Signal
// Processing"), integer only, fixed point throughout: Q1.14 for the NCO
// output, Q15 coefficients, int16 samples, int32 accumulators.
//
// Six kernels, six very different shapes: a shift-add iteration (CORDIC), a
// multiply per sample (mixer), a long MAC chain (FIR), pure addressing
// (decimation), a square root, and a compare-and-count. They cost wildly
// different amounts, and the per-kernel table this app prints is how you find
// out which -- that decision is the point of the app. The obvious answer is
// not always right: decimation looks like work and costs nothing, the
// detector looks trivial and touches every sample.
//
// The CORDIC stage is also THE REFERENCE EXAMPLE for the whole lab series:
// `k_cordic_rot` is bit-exact with `cookbook/code/cordic/`, where the same
// rotator exists as VHDL, as SystemVerilog, as a Python golden model and as
// six testbenches that check one against the other. Read that before Lab 1.

#include <stdint.h>
#include <stdio.h>
#include "profile.h"
#include "kernels.h"

#ifndef NSAMP
#define NSAMP 256                 // input samples
#endif
#ifndef TAPS
#define TAPS  24                  // anti-alias filter length
#endif
#ifndef DEC
#define DEC   4                   // decimation factor
#endif
#ifndef NBLOCK
#define NBLOCK 1                  // blocks per run (raise it on the FPGA)
#endif

#define NOUT   (((NSAMP) + (DEC) - 1) / (DEC))
#define QSHIFT 15                 // FIR coefficients are Q15
#define MIXSH  14                 // NCO output is Q1.14
#define PHASE_STEP 3217           // phase increment per sample, Q2.13 (~0.39 rad)
#define AMPL       6000           // amplitude of the wanted tone
#define THRESHOLD  1200           // detector threshold on the magnitude

#ifndef GOLDEN
#define GOLDEN 0x3d02c350u
#endif

static int16_t x[NSAMP];          // input samples
static int16_t phase[NSAMP];      // NCO phase, Q2.13, wrapped to [-pi, pi]
static int16_t nco_c[NSAMP], nco_s[NSAMP];
static int16_t mix_i[NSAMP], mix_q[NSAMP];
static int16_t flt_i[NSAMP], flt_q[NSAMP];
static int16_t dec_i[NOUT],  dec_q[NOUT];
static int16_t mag[NOUT];
static int16_t h[TAPS];
static int16_t car_c[NSAMP], car_s[NSAMP];   // carrier used to build the input

PROFILE_ACC_DECL(total);
PROFILE_ACC_DECL(nco);
PROFILE_ACC_DECL(mixer);
PROFILE_ACC_DECL(fir);
PROFILE_ACC_DECL(decimate);
PROFILE_ACC_DECL(magnitude);
PROFILE_ACC_DECL(detect);

// The received signal: a tone at the carrier frequency (the one we want, and
// the one the NCO is tuned to), an interferer near Nyquist that the chain must
// reject, and a little noise. The carrier is generated with the same CORDIC
// the receiver uses, so the wanted tone lands exactly at DC after mixing.
static void init_input(int block) {
    uint32_t s = 0xA5A5u + (uint32_t)block;
    for (int n = 0; n < NSAMP; n++) {
        int32_t wanted = ((int32_t)car_c[n] * AMPL) >> K_CORDIC_QF;
        int32_t interf = (((n * 4) % 8) - 4) * 1400;
        int32_t noise  = (int32_t)(k_rand(&s) % 401) - 200;
        x[n] = (int16_t)(wanted + interf + noise);
    }
}

// Free-running phase accumulator, wrapped into the CORDIC's input range.
// |theta| <= pi is a hard requirement of the rotator, exactly as in the RTL.
static void init_phase(void) {
    int32_t p = 0;
    for (int n = 0; n < NSAMP; n++) {
        phase[n] = (int16_t)p;
        p += PHASE_STEP;
        if (p >  K_CORDIC_PI_EXT) p -= 2 * K_CORDIC_PI_EXT;
        if (p < -K_CORDIC_PI_EXT) p += 2 * K_CORDIC_PI_EXT;
    }
}

// Triangular-windowed moving average: a crude but real low-pass, normalized so
// the Q15 coefficients sum to about 1.0 (unit DC gain).
static void init_coeffs(void) {
    int32_t w[TAPS];
    int32_t sum = 0;
    for (int t = 0; t < TAPS; t++) {
        int32_t d = t - (TAPS / 2);
        if (d < 0) d = -d;
        w[t] = (TAPS / 2) - d + 1;
        sum += w[t];
    }
    for (int t = 0; t < TAPS; t++)
        h[t] = (int16_t)((w[t] << QSHIFT) / sum);
}

// Mean square of a signal, skipping the filter's startup transient.
static uint32_t energy(const int16_t *v, int n, int skip) {
    uint32_t acc = 0;
    for (int i = skip; i < n; i++) {
        int32_t s = v[i] >> 4;   // scale down so the sum cannot overflow
        acc += (uint32_t)(s * s);
    }
    return acc / (uint32_t)(n - skip);
}

// One block through the whole chain. Returns the detector's output.
static uint32_t receive(void) {
    PROFILE_ACC_START(total);

    PROFILE_ACC_START(nco);
    k_cordic_sincos(phase, NSAMP, nco_c, nco_s);
    PROFILE_ACC_END(nco);

    PROFILE_ACC_START(mixer);
    k_mix_i16(x, nco_c, mix_i, NSAMP, MIXSH);
    k_mix_i16(x, nco_s, mix_q, NSAMP, MIXSH);
    PROFILE_ACC_END(mixer);

    PROFILE_ACC_START(fir);
    k_fir_i16(mix_i, NSAMP, h, TAPS, flt_i, QSHIFT);
    k_fir_i16(mix_q, NSAMP, h, TAPS, flt_q, QSHIFT);
    PROFILE_ACC_END(fir);

    PROFILE_ACC_START(decimate);
    int nout = k_decimate_i16(flt_i, NSAMP, dec_i, DEC);
    k_decimate_i16(flt_q, NSAMP, dec_q, DEC);
    PROFILE_ACC_END(decimate);

    PROFILE_ACC_START(magnitude);
    k_mag_i16(dec_i, dec_q, mag, nout);
    PROFILE_ACC_END(magnitude);

    PROFILE_ACC_START(detect);
    uint32_t hits = 0, acc = 0;
    for (int i = 0; i < nout; i++)
        if (mag[i] > THRESHOLD) { hits++; acc += (uint32_t)mag[i]; }
    PROFILE_ACC_END(detect);

    PROFILE_ACC_END(total);
    return (hits << 24) ^ acc;
}

int main(void) {
    init_phase();
    init_coeffs();
    k_cordic_sincos(phase, NSAMP, car_c, car_s);  // carrier for the test signal

    uint32_t sum = 0;
    for (int b = 0; b < NBLOCK; b++) {
        init_input(b);
        sum += receive() * (uint32_t)(b + 1);
    }

    printf("rxchain n=%d taps=%d dec=%d -> %d out, %d block(s)\n",
           NSAMP, TAPS, DEC, NOUT, NBLOCK);
    // After mixing, the wanted tone sits at DC on I and the interferer has
    // been filtered out, so filtered-I energy is dominated by a steady level
    // near (AMPL/2)^2 while Q collapses towards zero.
    printf("energy: rf=%u  baseband I=%u  Q=%u\n",
           (unsigned)energy(x, NSAMP, 0),
           (unsigned)energy(flt_i, NSAMP, TAPS),
           (unsigned)energy(flt_q, NSAMP, TAPS));
    printf("[profile] %-12s %10s %10s %6s %6s\n",
           "kernel", "cycles", "instr", "calls", "share");
    PROFILE_ACC_REPORT_OF(total,     PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(nco,       PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(mixer,     PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(fir,       PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(decimate,  PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(magnitude, PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(detect,    PROFILE_ACC_CYCLES(total));

    return check_result("rxchain", sum, GOLDEN);
}
