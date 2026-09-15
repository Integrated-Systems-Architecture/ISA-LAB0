#ifndef LAB_KERNELS_H
#define LAB_KERNELS_H

// Shared integer kernel library for the Lab 0 applications.
//
// Every application in sw/applications/ is built out of these kernels: the two
// signal-processing apps (fir, cordic) and the two end-to-end networks
// (tinydnn, tinyformer). No app is a single kernel dressed up as a program --
// each one is a workload with several kernels in it, and deciding which of
// them deserves hardware is the work of Lab 0.
//
// The accelerator you build in Labs 1-3 replaces ONE of these functions.
// Because the apps share the library, that one accelerator speeds up every app
// that calls it -- and its share of each app's cycles, which the apps print,
// is the ceiling on the speedup you can report in Lab 3.
//
// Rules of the library:
//   * integer only (int8 activations/weights, int32 accumulators) -- the
//     quantized-inference style of Jacob et al., CVPR 2018; no FPU on the
//     default X-HEEP configuration, and fixed point is what you would build in
//     hardware anyway;
//   * flat pointers and explicit index arithmetic, no hidden strides, so the
//     memory access pattern you must reproduce in RTL is visible in the C;
//   * header-only `static inline`: sw/external/ is the include directory the
//     X-HEEP build whitelists, and a header needs no CMake changes.
//
// Sizes are passed as arguments, never baked in, so the same kernel serves a
// 16x16 benchmark and a transformer layer.

#include <stdint.h>

// --- Dense linear algebra ---------------------------------------------------

// c[rows][cols] = a[rows][inner] * b[cols][inner]^T -- b is stored row-major
// but consumed transposed. int8 operands, int32 accumulation.
//
// The GEMM of the app set: attention needs Q * K^T, and a weight matrix stored
// [out][in] has the same shape, so every dense layer and every attention score
// goes through here. Both operands are walked contiguously, so the memory
// pattern is one stride per operand -- convenient for a streaming accelerator.
// An 8x8->16 bit MAC is roughly a quarter of the area of a 32x32 one, which is
// the whole reason quantized networks are attractive in hardware.
static inline void k_matmul_i8_bt(const int8_t *a, const int8_t *b, int32_t *c,
                                  int rows, int inner, int cols) {
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++) {
            int32_t acc = 0;
            for (int p = 0; p < inner; p++)
                acc += (int32_t)a[i * inner + p] * (int32_t)b[j * inner + p];
            c[i * cols + j] = acc;
        }
}

// c = (a * b) >> shift, with int32 (fixed-point) a and int8 b. Attention needs
// this for weights * V: the softmax weights are Q8 fixed point, the values are
// int8, and the shift undoes the Q8 scaling.
static inline void k_matmul_q8_i8(const int32_t *a, const int8_t *b, int32_t *c,
                                  int rows, int inner, int cols, int shift) {
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++) {
            int32_t acc = 0;
            for (int p = 0; p < inner; p++)
                acc += a[i * inner + p] * (int32_t)b[p * cols + j];
            c[i * cols + j] = acc >> shift;
        }
}

// Fully connected layer: y[rows][n_out] = x[rows][n_in] * w[n_out][n_in]^T +
// bias[n_out]. Weights are stored output-major (the usual layout), hence the
// transposed matmul.
static inline void k_linear_i8(const int8_t *x, const int8_t *w,
                               const int32_t *bias, int32_t *y,
                               int rows, int n_in, int n_out) {
    k_matmul_i8_bt(x, w, y, rows, n_in, n_out);
    if (bias)
        for (int i = 0; i < rows; i++)
            for (int o = 0; o < n_out; o++)
                y[i * n_out + o] += bias[o];
}

// --- Convolution ------------------------------------------------------------

// Valid (no padding, unit stride) 2D convolution + ReLU.
//   in     [n_cin][ih][iw]           int8
//   weight [n_cout][n_cin][ks][ks]   int8
//   bias   [n_cout]                  int32
//   out    [n_cout][oh][ow]          int32, oh = ih-ks+1, ow = iw-ks+1
// The sliding window is the point: unlike GEMM, neighbouring output pixels
// reuse most of their inputs, which is what line buffers exploit in hardware.
static inline void k_conv2d_relu_i8(const int8_t *in, const int8_t *weight,
                                    const int32_t *bias, int32_t *out,
                                    int n_cin, int n_cout, int ih, int iw,
                                    int ks) {
    const int oh = ih - ks + 1;
    const int ow = iw - ks + 1;
    for (int f = 0; f < n_cout; f++)
        for (int oy = 0; oy < oh; oy++)
            for (int ox = 0; ox < ow; ox++) {
                int32_t acc = bias ? bias[f] : 0;
                for (int c = 0; c < n_cin; c++)
                    for (int kh = 0; kh < ks; kh++)
                        for (int kw = 0; kw < ks; kw++)
                            acc += (int32_t)in[(c * ih + oy + kh) * iw + ox + kw] *
                                   (int32_t)weight[((f * n_cin + c) * ks + kh) * ks + kw];
                out[(f * oh + oy) * ow + ox] = acc > 0 ? acc : 0;  // ReLU
            }
}

// 2x2 max pooling, stride 2. Odd input dimensions drop the last row/column.
static inline void k_maxpool2x2_i32(const int32_t *in, int32_t *out,
                                    int chans, int ih, int iw) {
    const int oh = ih / 2;
    const int ow = iw / 2;
    for (int c = 0; c < chans; c++)
        for (int oy = 0; oy < oh; oy++)
            for (int ox = 0; ox < ow; ox++) {
                const int32_t *p = &in[(c * ih + 2 * oy) * iw + 2 * ox];
                int32_t m = p[0];
                if (p[1] > m) m = p[1];
                if (p[iw] > m) m = p[iw];
                if (p[iw + 1] > m) m = p[iw + 1];
                out[(c * oh + oy) * ow + ox] = m;
            }
}

// --- Filtering --------------------------------------------------------------

// Direct-form FIR: y[n] = (sum_t h[t] * x[n-t]) >> shift, saturated to int16.
// Samples before the start of the buffer are treated as zero. One MAC plus one
// shift of the delay line per tap -- the smallest interesting MAC kernel, and
// the reason a FIR accelerator is mostly a shift register and one multiplier.
static inline void k_fir_i16(const int16_t *x, int n, const int16_t *h,
                             int taps, int16_t *y, int shift) {
    for (int i = 0; i < n; i++) {
        int32_t acc = 0;
        for (int t = 0; t < taps; t++) {
            int32_t s = (i - t >= 0) ? (int32_t)x[i - t] : 0;
            acc += s * (int32_t)h[t];
        }
        acc >>= shift;
        if (acc > 32767) acc = 32767;
        if (acc < -32768) acc = -32768;
        y[i] = (int16_t)acc;
    }
}

// Integer square root (Newton on the bit length). Used by the magnitude stage
// and by the layer norm; also a good example of an operation that is a few
// gates in hardware and a loop in software.
static inline uint32_t k_isqrt(uint32_t v) {
    uint32_t r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else              { r >>= 1; }
        bit >>= 2;
    }
    return r;
}

// Complex mixer stage: y[n] = (x[n] * m[n]) >> shift, saturated to int16.
// Multiplying a real input by the two NCO outputs is what moves a band down to
// baseband; it is also the one place in the receive chain that needs a real
// multiplier per sample.
static inline void k_mix_i16(const int16_t *x, const int16_t *m, int16_t *y,
                             int n, int shift) {
    for (int i = 0; i < n; i++) {
        int32_t v = ((int32_t)x[i] * (int32_t)m[i]) >> shift;
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        y[i] = (int16_t)v;
    }
}

// Keep every `factor`-th sample. Valid only after the anti-alias filter has
// run: decimation itself is free (it is just addressing), which is exactly why
// filtering before it, not after, is what costs.
static inline int k_decimate_i16(const int16_t *x, int n, int16_t *y,
                                 int factor) {
    int o = 0;
    for (int i = 0; i < n; i += factor)
        y[o++] = x[i];
    return o;
}

// Magnitude of a complex sample pair: mag = isqrt(i^2 + q^2). The square root
// is the expensive part in software and a shift-add loop in hardware -- the
// same trade the CORDIC makes.
static inline void k_mag_i16(const int16_t *re, const int16_t *im, int16_t *mag,
                             int n) {
    for (int i = 0; i < n; i++) {
        int32_t r = re[i], m = im[i];
        uint32_t p = (uint32_t)(r * r) + (uint32_t)(m * m);
        mag[i] = (int16_t)k_isqrt(p);
    }
}

// --- Activations and normalization -----------------------------------------

// Arithmetic right shift of a whole vector, in place: the fixed-point rescale
// that sits between two layers (e.g. the 1/sqrt(d_k) of attention).
static inline void k_shift_i32(int32_t *v, int n, int shift) {
    for (int i = 0; i < n; i++)
        v[i] >>= shift;
}

static inline void k_relu_i32(int32_t *v, int n) {
    for (int i = 0; i < n; i++)
        if (v[i] < 0) v[i] = 0;
}

// Requantize int32 accumulators back to int8 activations: arithmetic shift
// right, then clamp. This is the "scale by a power of two" simplification of
// the multiplier-plus-shift requantization used in real quantized inference.
static inline void k_requant_i8(const int32_t *acc, int8_t *out, int n, int shift) {
    for (int i = 0; i < n; i++) {
        int32_t v = acc[i] >> shift;
        if (v > 127) v = 127;
        if (v < -128) v = -128;
        out[i] = (int8_t)v;
    }
}

#define K_SOFTMAX_ONE  256   // Q8 fixed-point 1.0 for softmax weights
#define K_SOFTMAX_DECAY 2    // how fast the shift-based exp() decays

// Row-wise softmax, integer only, in place: row[j] (a raw score) becomes a Q8
// weight summing to K_SOFTMAX_ONE. exp(-d) is approximated by 1 >> (d >> 2),
// in the spirit of hardware softmax designs such as Softermax (Stevens et al.,
// DAC 2021) and integer-only inference such as I-BERT (Kim et al., ICML 2021)
// -- simplified for teaching, not a faithful reproduction of either. The
// max-subtraction is the standard overflow guard and is kept.
static inline void k_softmax_q8_row(int32_t *row, int n) {
    int32_t row_max = row[0];
    for (int j = 1; j < n; j++)
        if (row[j] > row_max) row_max = row[j];

    int32_t sum = 0;
    for (int j = 0; j < n; j++) {
        int32_t shift_amt = (row_max - row[j]) >> K_SOFTMAX_DECAY;  // >= 0
        int32_t w = (shift_amt < 31) ? (K_SOFTMAX_ONE >> shift_amt) : 0;
        row[j] = w;
        sum += w;
    }
    if (sum == 0) sum = 1;  // guard: every weight underflowed to zero
    for (int j = 0; j < n; j++)
        row[j] = (row[j] * K_SOFTMAX_ONE) / sum;  // integer divide, no divider in HW
}

static inline void k_softmax_q8(int32_t *m, int rows, int cols) {
    for (int i = 0; i < rows; i++)
        k_softmax_q8_row(&m[i * cols], cols);
}

// Row-wise layer normalization, integer: subtract the mean, divide by the
// standard deviation, rescale to `gain`. Real LayerNorm also has learned
// per-channel scale and bias; those are folded away here to keep the kernel
// readable. Division and sqrt are the expensive parts -- worth noticing when
// you decide what to accelerate.
static inline void k_layernorm_i32(int32_t *row, int n, int32_t gain) {
    int32_t mean = 0;
    for (int i = 0; i < n; i++) mean += row[i];
    mean /= n;

    uint32_t var = 0;
    for (int i = 0; i < n; i++) {
        int32_t d = row[i] - mean;
        var += (uint32_t)(d * d);
    }
    var /= (uint32_t)n;
    int32_t sd = (int32_t)k_isqrt(var);
    if (sd == 0) sd = 1;

    for (int i = 0; i < n; i++)
        row[i] = ((row[i] - mean) * gain) / sd;
}

// --- CORDIC -----------------------------------------------------------------
//
// Rotation-mode CORDIC, bit-exact with the worked example of the cookbook
// (`books/examplecookbook/code/cordic/`): the same shift-add micro-rotation loop as
// `cordic_rot.sv` / `cordic_rot.vhd`, and the same arithmetic as the Python
// golden model `cordic_model.py`. Same constants, same guard bits, same
// truncation, so this C, that RTL and that model all produce identical
// integers -- which is why it is the reference example for these labs: you can
// hold the software, the VHDL and the SystemVerilog side by side.
//
// Number formats (external):
//   x, y   signed 16 bit, 14 fractional bits (Q1.14)
//   theta  signed 16 bit, 13 fractional bits (Q2.13), |theta| <= pi
// Internally the datapath gains GL bits at the bottom (precision) and GM bits
// at the top (headroom for the CORDIC processing gain K ~ 1.6468).
//
// Seed with x = K_CORDIC_X0_UNIT, y = 0 and the outputs are cos/sin directly:
// the seed is 1/K, so the processing gain cancels.

#define K_CORDIC_DW  16   // external x/y width
#define K_CORDIC_QF  14   // external x/y fractional bits
#define K_CORDIC_AW  16   // external angle width
#define K_CORDIC_QA  13   // external angle fractional bits
#define K_CORDIC_N   14   // micro-rotations (= RTL parameter ITER)
#define K_CORDIC_GL   2   // guard bits at the LSB side
#define K_CORDIC_GM   2   // guard bits at the MSB side

#define K_CORDIC_IW  (K_CORDIC_DW + K_CORDIC_GL + K_CORDIC_GM)  // 20
#define K_CORDIC_IA  (K_CORDIC_AW + K_CORDIC_GL)                // 18

#define K_CORDIC_HALF_PI  51472   // pi/2, internal angle format (Q2.15)
#define K_CORDIC_PI      102944   // pi,   internal angle format
#define K_CORDIC_PI_EXT   25736   // pi,   external angle format (Q2.13)
#define K_CORDIC_X0_UNIT   9949   // round(2^QF / K): the "unit circle" seed

// arctan(2^-i) in the internal angle format -- the same table the RTL builds
// at elaboration time and dumps into cordic_atan_rom.svh.
static const int32_t k_cordic_atan[K_CORDIC_N] = {
    25736, 15193, 8027, 4075, 2045, 1024, 512, 256, 128, 64, 32, 16, 8, 4
};

// Reinterpret the low `width` bits of v as two's complement -- what a hardware
// register of that width does when the adder overflows it.
static inline int32_t k_wrap_signed(int32_t v, int width) {
    uint32_t m = (uint32_t)v & ((1u << width) - 1u);
    uint32_t sign = 1u << (width - 1);
    return (int32_t)(m ^ sign) - (int32_t)sign;
}

// Clamp to the range of a signed `width`-bit number.
static inline int32_t k_sat_signed(int32_t v, int width) {
    int32_t hi = (int32_t)((1u << (width - 1)) - 1u);
    int32_t lo = -hi - 1;
    if (v > hi) return hi;
    if (v < lo) return lo;
    return v;
}

// Rotate (x0, y0) by theta. Returns K * (x cos t - y sin t), K * (x sin t +
// y cos t) in the external format. `>>` on a negative value is an arithmetic
// shift on every compiler used here (and required from C23), which is what the
// RTL and the Python model do too -- it rounds towards minus infinity, and
// that shared rounding is what makes the three agree bit for bit.
static inline void k_cordic_rot(int32_t x0, int32_t y0, int32_t theta,
                                int32_t *x_out, int32_t *y_out) {
    // 1. widen into the internal format
    int32_t x = (int32_t)((uint32_t)x0 << K_CORDIC_GL);
    int32_t y = (int32_t)((uint32_t)y0 << K_CORDIC_GL);
    int32_t z = (int32_t)((uint32_t)theta << K_CORDIC_GL);

    // 2. coarse rotation: the micro-rotation series converges only for
    //    |z| <= 1.7433 rad, so first take out a whole quadrant if needed.
    if (z > K_CORDIC_HALF_PI) {
        int32_t t = x; x = -y; y = t; z -= K_CORDIC_HALF_PI;
    } else if (z < -K_CORDIC_HALF_PI) {
        int32_t t = x; x = y; y = -t; z += K_CORDIC_HALF_PI;
    }

    // 3. the micro-rotation loop: one shift, one add, one table lookup per
    //    iteration -- no multiplier anywhere, which is the whole point.
    for (int i = 0; i < K_CORDIC_N; i++) {
        int32_t dx = y >> i;
        int32_t dy = x >> i;
        int32_t xn, yn, zn;
        if (z >= 0) {                       // rotate counter-clockwise
            xn = x - dx; yn = y + dy; zn = z - k_cordic_atan[i];
        } else {                            // rotate clockwise
            xn = x + dx; yn = y - dy; zn = z + k_cordic_atan[i];
        }
        x = k_wrap_signed(xn, K_CORDIC_IW);
        y = k_wrap_signed(yn, K_CORDIC_IW);
        z = k_wrap_signed(zn, K_CORDIC_IA);
    }

    // 4. narrow back down, with saturation
    *x_out = k_sat_signed(x >> K_CORDIC_GL, K_CORDIC_DW);
    *y_out = k_sat_signed(y >> K_CORDIC_GL, K_CORDIC_DW);
}

// cos/sin of a whole array of angles, Q1.14 out, Q2.13 in.
static inline void k_cordic_sincos(const int16_t *theta, int n,
                                   int16_t *cos_out, int16_t *sin_out) {
    for (int i = 0; i < n; i++) {
        int32_t c, s;
        k_cordic_rot(K_CORDIC_X0_UNIT, 0, theta[i], &c, &s);
        cos_out[i] = (int16_t)c;
        sin_out[i] = (int16_t)s;
    }
}

// --- Misc -------------------------------------------------------------------

static inline int k_argmax_i32(const int32_t *v, int n) {
    int best = 0;
    for (int i = 1; i < n; i++)
        if (v[i] > v[best]) best = i;
    return best;
}

// Additive checksum over an int32 array -- how every app reports its result in
// one number, so the same value can be compared across the C build, the
// Verilator run and the FPGA run.
static inline uint32_t k_checksum_i32(const int32_t *v, int n) {
    uint32_t s = 0;
    for (int i = 0; i < n; i++)
        s += (uint32_t)v[i];
    return s;
}

// Small deterministic pseudo-random generator (xorshift32) used to fill the
// input tensors and weights. Deterministic across host, RTL simulation and
// FPGA, so the golden checksums hold everywhere.
static inline uint32_t k_rand(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

// Fill an int8 array with values in [-range, range].
static inline void k_fill_i8(int8_t *v, int n, int range, uint32_t *state) {
    for (int i = 0; i < n; i++)
        v[i] = (int8_t)((int32_t)(k_rand(state) % (uint32_t)(2 * range + 1)) - range);
}

// --- Number-theoretic transform (NTT) ---------------------------------------
//
// The NTT is the FFT with the complex unit circle replaced by the integers mod
// a prime: same Cooley-Tukey butterfly, same O(n log n), same bit-reversed
// dataflow -- but every value is an exact integer, so there is no rounding and
// no wordlength analysis to do. That is why every lattice-based post-quantum
// scheme (Kyber, Dilithium, Falcon, NewHope) multiplies polynomials with it,
// and why an NTT butterfly is the single most published accelerator in that
// field.
//
// Ring: Z_q[x] / (x^n + 1) with q = 12289 (the NewHope/Falcon prime) and n a
// power of two. Because x^n + 1 is NEGACYCLIC, the transform uses psi, a
// primitive 2n-th root of unity, not just the n-th root the plain FFT uses:
// the psi-weighting folds the "wrap around with a minus sign" into the
// transform, so a pointwise product in the NTT domain is a negacyclic
// convolution back in the coefficient domain.
//
// Structure follows Longa & Naehrig (CANS 2016): forward = Cooley-Tukey,
// natural order in, bit-reversed order out; inverse = Gentleman-Sande,
// bit-reversed in, natural out. No bit-reversal permutation pass is ever
// needed, because the two orders cancel.
//
// Arithmetic note: q < 2^14, so a product of two reduced values is < 2^28 and
// fits an int32 -- one 14x14 multiplier plus one reduction is the whole
// datapath. Reduction here is a plain `%` (the compiler turns it into a
// multiply and a shift); real hardware uses Montgomery or Barrett, which is
// one of the optimizations Lab 2 is about.

#define K_NTT_Q 12289    // prime modulus, q - 1 = 2^12 * 3
#define K_NTT_G 11       // a primitive root mod q

static inline int32_t k_modq(int32_t x) {
    x %= K_NTT_Q;
    return x < 0 ? x + K_NTT_Q : x;
}

static inline int32_t k_mulmod_q(int32_t a, int32_t b) {
    return k_modq(a * b);   // a, b < 2^14 => a*b < 2^28, no overflow
}

static inline int32_t k_powmod_q(int32_t base, uint32_t e) {
    int32_t r = 1;
    base = k_modq(base);
    while (e) {
        if (e & 1u) r = k_mulmod_q(r, base);
        base = k_mulmod_q(base, base);
        e >>= 1;
    }
    return r;
}

// Reverse the low `bits` bits of i -- the butterfly index permutation, free in
// hardware (it is just wiring) and a loop in software.
static inline int k_bitrev(int i, int bits) {
    int r = 0;
    for (int b = 0; b < bits; b++)
        if (i & (1 << b)) r |= 1 << (bits - 1 - b);
    return r;
}

// Build the two twiddle tables and n^-1 for a transform of length n (a power of
// two, n <= 2048 for this q). Tables are stored in bit-reversed order, which is
// what lets both loops read them sequentially:
//   psi_rev[i]     = psi^brv(i)      psi = primitive 2n-th root of unity
//   psi_inv_rev[i] = psi^-brv(i)
// Called once per program, not per transform.
static inline void k_ntt_tables(int n, int32_t *psi_rev, int32_t *psi_inv_rev,
                                int32_t *n_inv) {
    int bits = 0;
    while ((1 << bits) < n) bits++;

    // psi = g^((q-1)/2n) has order exactly 2n, because g generates Z_q*.
    int32_t psi     = k_powmod_q(K_NTT_G, (uint32_t)((K_NTT_Q - 1) / (2 * n)));
    int32_t psi_inv = k_powmod_q(psi, (uint32_t)(2 * n - 1));   // psi^(2n-1) = psi^-1

    for (int i = 0; i < n; i++) {
        int e = k_bitrev(i, bits);
        psi_rev[i]     = k_powmod_q(psi,     (uint32_t)e);
        psi_inv_rev[i] = k_powmod_q(psi_inv, (uint32_t)e);
    }
    *n_inv = k_powmod_q(n, (uint32_t)(K_NTT_Q - 2));            // Fermat inverse
}

// Forward NTT, in place. Cooley-Tukey butterflies, natural order in,
// bit-reversed order out. n/2 * log2(n) butterflies, each one multiply-mod and
// two add-mods -- the exact shape you would pipeline in RTL.
static inline void k_ntt_fwd(int32_t *a, int n, const int32_t *psi_rev) {
    int t = n;
    for (int m = 1; m < n; m *= 2) {
        t /= 2;
        for (int i = 0; i < m; i++) {
            int32_t s = psi_rev[m + i];
            int j1 = 2 * i * t;
            for (int j = j1; j < j1 + t; j++) {
                int32_t u = a[j];
                int32_t v = k_mulmod_q(a[j + t], s);
                a[j]     = k_modq(u + v);
                a[j + t] = k_modq(u - v);
            }
        }
    }
}

// Inverse NTT, in place. Gentleman-Sande butterflies (the multiply is AFTER
// the add/sub, the mirror image of the forward pass), bit-reversed order in,
// natural order out, then the 1/n scaling.
static inline void k_ntt_inv(int32_t *a, int n, const int32_t *psi_inv_rev,
                             int32_t n_inv) {
    int t = 1;
    for (int m = n; m > 1; m /= 2) {
        int j1 = 0;
        int h = m / 2;
        for (int i = 0; i < h; i++) {
            int32_t s = psi_inv_rev[h + i];
            for (int j = j1; j < j1 + t; j++) {
                int32_t u = a[j];
                int32_t v = a[j + t];
                a[j]     = k_modq(u + v);
                a[j + t] = k_mulmod_q(u - v, s);
            }
            j1 += 2 * t;
        }
        t *= 2;
    }
    for (int j = 0; j < n; j++)
        a[j] = k_mulmod_q(a[j], n_inv);
}

// c = a . b, coefficient by coefficient, mod q. In the NTT domain this single
// linear pass IS the polynomial multiplication -- which is the whole reason to
// pay for the two transforms.
static inline void k_poly_pointwise_q(const int32_t *a, const int32_t *b,
                                      int32_t *c, int n) {
    for (int i = 0; i < n; i++)
        c[i] = k_mulmod_q(a[i], b[i]);
}

static inline void k_poly_add_q(const int32_t *a, const int32_t *b, int32_t *c,
                                int n) {
    for (int i = 0; i < n; i++)
        c[i] = k_modq(a[i] + b[i]);
}

static inline void k_poly_sub_q(const int32_t *a, const int32_t *b, int32_t *c,
                                int n) {
    for (int i = 0; i < n; i++)
        c[i] = k_modq(a[i] - b[i]);
}

// Uniformly random polynomial mod q, by rejection sampling. This is what a
// real scheme expands from a seed with SHAKE; here the xorshift generator
// stands in for the hash, so the result is reproducible on host, RTL and FPGA.
static inline void k_poly_uniform_q(int32_t *a, int n, uint32_t *state) {
    for (int i = 0; i < n; i++) {
        uint32_t v;
        do { v = k_rand(state) & 0x3FFFu; } while (v >= (uint32_t)K_NTT_Q);
        a[i] = (int32_t)v;
    }
}

// Centered binomial noise: each coefficient is (popcount(x) - popcount(y)) for
// two eta-bit halves, so it lands in [-eta, eta] with a binomial shape. The
// standard cheap sampler of Kyber/NewHope -- no Gaussian, no table.
static inline void k_poly_cbd_q(int32_t *a, int n, int eta, uint32_t *state) {
    for (int i = 0; i < n; i++) {
        uint32_t r = k_rand(state);
        int32_t s = 0;
        for (int b = 0; b < eta; b++)
            s += (int32_t)((r >> b) & 1u) - (int32_t)((r >> (eta + b)) & 1u);
        a[i] = k_modq(s);
    }
}

// Lossy compression to d bits per coefficient and back: round(2^d * x / q) and
// its inverse. This is how a ciphertext is shrunk; the rounding error it adds
// is part of the noise budget the decryption has to survive.
static inline void k_compress_q(const int32_t *in, int32_t *out, int n, int d) {
    const int32_t mask = (1 << d) - 1;
    for (int i = 0; i < n; i++)
        out[i] = (int32_t)(((((uint32_t)in[i] << d) + K_NTT_Q / 2u) /
                            (uint32_t)K_NTT_Q)) & mask;
}

static inline void k_decompress_q(const int32_t *in, int32_t *out, int n, int d) {
    for (int i = 0; i < n; i++)
        out[i] = (int32_t)(((uint32_t)in[i] * (uint32_t)K_NTT_Q +
                            (1u << (d - 1))) >> d);
}

#endif // LAB_KERNELS_H
