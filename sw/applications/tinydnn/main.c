// Lab 0 app: a COMPLETE convolutional network, end to end.
//
//   16x16 int8 image
//     -> conv 1->4, 3x3, ReLU      (14x14)
//     -> maxpool 2x2               ( 7x7)
//     -> conv 4->8, 3x3, ReLU      ( 5x5)
//     -> maxpool 2x2               ( 2x2)
//     -> flatten (32) -> FC 32->16, ReLU
//     -> FC 16->NCLASS -> argmax
//
// The shape of LeNet-5 (LeCun et al., 1998), shrunk to run in RTL simulation
// and quantized to int8 weights / int32 accumulators (Jacob et al., CVPR 2018).
// Weights are pseudo-random from a fixed seed, so this classifies nothing
// meaningful -- but every layer, buffer and requantization step is real, the
// arithmetic is bit-exact and identical on host, Verilator, QuestaSim and FPGA,
// and the cost profile is the cost profile of a real quantized CNN.
//
// Why this app exists: the single-kernel benchmarks (matmul, tinyconv, fir)
// tell you what one kernel costs. This one tells you what accelerating that
// kernel is worth, because it reports the per-kernel share of a whole
// inference. Amdahl's law is the entire lesson of Lab 0.
//
// It is also the app to run on the FPGA: at these sizes a full inference is
// slow in RTL simulation but instant on the board, so you can run many
// inferences and measure a throughput, not just one latency.

#include <stdint.h>
#include <stdio.h>
#include "profile.h"
#include "kernels.h"

#define IMG   16                    // input image is IMG x IMG, 1 channel
#define C1     4                    // conv1 output channels
#define C2     8                    // conv2 output channels
#define KS     3                    // 3x3 kernels throughout
#define H1    (IMG - KS + 1)        // 14  conv1 output
#define P1    (H1 / 2)              //  7  pool1 output
#define H2    (P1 - KS + 1)         //  5  conv2 output
#define P2    (H2 / 2)              //  2  pool2 output
#define FLAT  (C2 * P2 * P2)        // 32  flattened features
#define FC1    16                   // hidden width
#define NCLASS 10                   // output classes

#ifndef NINFER
#define NINFER 1                    // inferences per run (raise it on the FPGA)
#endif

// Requantization shifts: int32 accumulator -> int8 activation. Picked so the
// activations use the int8 range without saturating everything to +/-127.
#define SH_CONV1 4
#define SH_CONV2 3
#define SH_FC1   4

#ifndef GOLDEN
#define GOLDEN 0xffffff90u
#endif

// --- Parameters (pseudo-random, fixed seed => reproducible everywhere) ------
static int8_t  w1[C1 * 1 * KS * KS], w2[C2 * C1 * KS * KS];
static int8_t  wf1[FC1 * FLAT],      wf2[NCLASS * FC1];
static int32_t b1[C1], b2[C2], bf1[FC1], bf2[NCLASS];

// --- Activations ------------------------------------------------------------
static int8_t  img[1 * IMG * IMG];
static int32_t acc1[C1 * H1 * H1], pool1[C1 * P1 * P1];
static int8_t  a1[C1 * P1 * P1];
static int32_t acc2[C2 * H2 * H2], pool2[C2 * P2 * P2];
static int8_t  a2[FLAT];
static int32_t accf1[FC1];
static int8_t  af1[FC1];
static int32_t logits[NCLASS];

// One accumulating counter per kernel class, plus one around the whole
// inference. The per-kernel counters nest inside `total`, so their percentages
// tell you what fraction of the network each accelerator candidate covers.
PROFILE_ACC_DECL(total);
PROFILE_ACC_DECL(conv);
PROFILE_ACC_DECL(pool);
PROFILE_ACC_DECL(linear);
PROFILE_ACC_DECL(requant);

static void init_params(void) {
    uint32_t s = 0xC0FFEEu;
    k_fill_i8(w1,  C1 * KS * KS,      4, &s);
    k_fill_i8(w2,  C2 * C1 * KS * KS, 4, &s);
    k_fill_i8(wf1, FC1 * FLAT,        4, &s);
    k_fill_i8(wf2, NCLASS * FC1,      4, &s);
    for (int i = 0; i < C1;     i++) b1[i]  = (int32_t)(k_rand(&s) % 64) - 32;
    for (int i = 0; i < C2;     i++) b2[i]  = (int32_t)(k_rand(&s) % 64) - 32;
    for (int i = 0; i < FC1;    i++) bf1[i] = (int32_t)(k_rand(&s) % 64) - 32;
    for (int i = 0; i < NCLASS; i++) bf2[i] = (int32_t)(k_rand(&s) % 64) - 32;
}

// A different image per inference, so NINFER > 1 is not the same work repeated
// out of the same cache lines.
static void init_image(int n) {
    uint32_t s = 0x1234u + (uint32_t)n;
    k_fill_i8(img, IMG * IMG, 64, &s);
}

static int infer(void) {
    PROFILE_ACC_START(total);

    PROFILE_ACC_START(conv);
    k_conv2d_relu_i8(img, w1, b1, acc1, 1, C1, IMG, IMG, KS);
    PROFILE_ACC_END(conv);

    PROFILE_ACC_START(pool);
    k_maxpool2x2_i32(acc1, pool1, C1, H1, H1);
    PROFILE_ACC_END(pool);

    PROFILE_ACC_START(requant);
    k_requant_i8(pool1, a1, C1 * P1 * P1, SH_CONV1);
    PROFILE_ACC_END(requant);

    PROFILE_ACC_START(conv);
    k_conv2d_relu_i8(a1, w2, b2, acc2, C1, C2, P1, P1, KS);
    PROFILE_ACC_END(conv);

    PROFILE_ACC_START(pool);
    k_maxpool2x2_i32(acc2, pool2, C2, H2, H2);
    PROFILE_ACC_END(pool);

    PROFILE_ACC_START(requant);
    k_requant_i8(pool2, a2, FLAT, SH_CONV2);   // flatten is just a reinterpret
    PROFILE_ACC_END(requant);

    PROFILE_ACC_START(linear);
    k_linear_i8(a2, wf1, bf1, accf1, 1, FLAT, FC1);
    PROFILE_ACC_END(linear);

    k_relu_i32(accf1, FC1);

    PROFILE_ACC_START(requant);
    k_requant_i8(accf1, af1, FC1, SH_FC1);
    PROFILE_ACC_END(requant);

    PROFILE_ACC_START(linear);
    k_linear_i8(af1, wf2, bf2, logits, 1, FC1, NCLASS);
    PROFILE_ACC_END(linear);

    PROFILE_ACC_END(total);
    return k_argmax_i32(logits, NCLASS);
}

int main(void) {
    init_params();

    uint32_t sum = 0;
    for (int n = 0; n < NINFER; n++) {
        init_image(n);
        int cls = infer();
        sum += (uint32_t)(cls + 1) * (uint32_t)(n + 1);
        sum += k_checksum_i32(logits, NCLASS);
    }

    printf("tinydnn %dx%d -> %d classes, %d inference(s)\n",
           IMG, IMG, NCLASS, NINFER);
    printf("[profile] %-12s %10s %10s %6s %6s\n",
           "kernel", "cycles", "instr", "calls", "share");
    PROFILE_ACC_REPORT_OF(total,   PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(conv,    PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(pool,    PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(linear,  PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(requant, PROFILE_ACC_CYCLES(total));

    return check_result("tinydnn", sum, GOLDEN);
}
