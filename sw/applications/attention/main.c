// Lab 0 example app: single-head scaled dot-product attention
// [Vaswani et al., "Attention Is All You Need", NeurIPS 2017], fixed-point,
// integer-only. Three sub-kernels, each profiled separately:
//   1. qk_matmul  -- scores = Q . K^T                 (matmul, like matmul/)
//   2. softmax    -- row-wise weight normalization    (division-heavy)
//   3. av_matmul  -- out = weights . V                (matmul again)
// The softmax uses a shift-based, integer-only exponential approximation in
// the spirit of hardware-oriented softmax designs such as Softermax
// [Stevens et al., DAC 2021] and integer-only transformer inference such as
// I-BERT [Kim et al., ICML 2021] -- simplified here for teaching, not a
// faithful reproduction of either.
//
// Splitting the profiling into three tags is the point of this app: with
// three numbers instead of one, you have to look at the data to decide which
// piece is worth building hardware for -- it is not always the matmuls.

#include <stdint.h>
#include <stdio.h>
#include "profile.h"

#define SEQ 8   // sequence length
#define DM  8   // model / head dimension

#define WEIGHT_ONE   256   // fixed-point "1.0" for softmax weights (Q8)
#define DECAY_SHIFT  2     // controls how fast the shift-based exp decays

static int8_t  Q[SEQ][DM];
static int8_t  K[SEQ][DM];
static int8_t  V[SEQ][DM];
static int32_t scores[SEQ][SEQ];
static int32_t weights[SEQ][SEQ];  // Q8 fixed-point softmax weights
static int32_t out[SEQ][DM];

static void init(void) {
    for (int i = 0; i < SEQ; i++)
        for (int d = 0; d < DM; d++) {
            Q[i][d] = (int8_t)(((i * 3 + d * 2) % 11) - 5);
            K[i][d] = (int8_t)(((i * 5 + d * 7) % 13) - 6);
            V[i][d] = (int8_t)(((i * 2 + d * 3) % 9)  - 4);
        }
}

// 1. scores[i][j] = Q[i] . K[j]   (same primitive as matmul/)
static void qk_matmul(void) {
    for (int i = 0; i < SEQ; i++)
        for (int j = 0; j < SEQ; j++) {
            int32_t acc = 0;
            for (int d = 0; d < DM; d++)
                acc += (int32_t)Q[i][d] * (int32_t)K[j][d];
            scores[i][j] = acc;  // scale by 1/sqrt(DM) folded into DECAY_SHIFT
        }
}

// 2. row-wise softmax, shift-based integer exponential approximation.
static void softmax(void) {
    for (int i = 0; i < SEQ; i++) {
        int32_t row_max = scores[i][0];
        for (int j = 1; j < SEQ; j++)
            if (scores[i][j] > row_max) row_max = scores[i][j];

        int32_t sum = 0;
        for (int j = 0; j < SEQ; j++) {
            int32_t diff = row_max - scores[i][j];        // >= 0
            int32_t shift_amt = diff >> DECAY_SHIFT;
            int32_t w = (shift_amt < 31) ? (WEIGHT_ONE >> shift_amt) : 0;
            weights[i][j] = w;
            sum += w;
        }
        if (sum == 0) sum = 1;  // guard
        for (int j = 0; j < SEQ; j++)
            weights[i][j] = (weights[i][j] * WEIGHT_ONE) / sum;  // integer div
    }
}

// 3. out[i] = sum_j weights[i][j] * V[j]   (matmul again)
static void av_matmul(void) {
    for (int i = 0; i < SEQ; i++)
        for (int d = 0; d < DM; d++) {
            int32_t acc = 0;
            for (int j = 0; j < SEQ; j++)
                acc += weights[i][j] * (int32_t)V[j][d];
            out[i][d] = acc / WEIGHT_ONE;  // undo Q8 scaling
        }
}

static uint32_t checksum(void) {
    uint32_t s = 0;
    for (int i = 0; i < SEQ; i++)
        for (int d = 0; d < DM; d++)
            s += (uint32_t)out[i][d];
    return s;
}

int main(void) {
    init();

    PROFILE_START(qk_matmul);
    qk_matmul();
    PROFILE_END(qk_matmul);

    PROFILE_START(softmax);
    softmax();
    PROFILE_END(softmax);

    PROFILE_START(av_matmul);
    av_matmul();
    PROFILE_END(av_matmul);

    printf("attention seq=%d dm=%d checksum=0x%08x\n", SEQ, DM, checksum());
    return 0;
}
