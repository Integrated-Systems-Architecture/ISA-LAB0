// Lab 0 app: a COMPLETE transformer encoder, end to end.
//
//   token ids -> embedding + positional encoding
//   for each of NLAYER layers:
//       Q,K,V projections            (3 linear layers)
//       multi-head attention         (Q.K^T, softmax, weights.V, per head)
//       output projection + residual + layer norm
//       feed-forward: DM -> DFF -> DM, ReLU, + residual + layer norm
//   mean-pool over the sequence -> classifier head -> argmax
//
// The encoder block of Vaswani et al., "Attention Is All You Need"
// (NeurIPS 2017), shrunk to run in RTL simulation and made integer-only in the
// style of I-BERT (Kim et al., ICML 2021): int8 weights and activations, int32
// accumulators, a shift-based softmax (cf. Softermax, Stevens et al., DAC 2021)
// and an integer layer norm. The weights are pseudo-random from a fixed seed,
// so it predicts nothing meaningful -- but every tensor, projection and
// normalization is real, the arithmetic is bit-exact and reproduces identically
// on host, Verilator and FPGA.
//
// Why this app exists: the attention app profiles one attention block in
// isolation. Here the same three kernels sit inside a whole network, next to
// the projections and the feed-forward layers -- and the per-kernel report at
// the end shows that in a real transformer the plain matmuls, not the exotic
// softmax, are where the cycles go. That is the number your Lab 3 speedup will
// be measured against.
//
// This is the heaviest app in the set. In RTL simulation one forward pass takes
// a while; on the FPGA it is immediate, which is the point of running the full
// network on hardware rather than a single kernel in a testbench.

#include <stdint.h>
#include <stdio.h>
#include "profile.h"
#include "kernels.h"

#define VOCAB   32                  // token vocabulary
#define SEQ      8                  // sequence length
#define DM      16                  // model dimension
#define HEADS    2                  // attention heads
#define DH      (DM / HEADS)        // per-head dimension
#define DFF     32                  // feed-forward hidden width
#define NLAYER   2                  // encoder layers
#define NCLASS   4                  // classifier outputs

#ifndef NINFER
#define NINFER  1                   // forward passes per run (raise in Lab 3)
#endif

// Fixed-point rescale shifts: int32 accumulator -> int8 activation.
#define SH_QKV    5                 // after a DM-wide projection
#define SH_SCORE  7                 // stands in for the 1/sqrt(d_k) scaling
#define SH_CTX    8                 // undoes the Q8 softmax weights
#define SH_NORM   0                 // layer norm already outputs int8 range
#define SH_FF     5                 // after the DFF-wide hidden layer
#define NORM_GAIN 64                // layer-norm output scale

#ifndef GOLDEN
#define GOLDEN 0x0000016au
#endif

typedef struct {
    int8_t  wq[DM * DM], wk[DM * DM], wv[DM * DM], wo[DM * DM];
    int8_t  w1[DFF * DM], w2[DM * DFF];
    int32_t bq[DM], bk[DM], bv[DM], bo[DM], b1[DFF], b2[DM];
} layer_t;

static layer_t layers[NLAYER];
static int8_t  embed[VOCAB * DM];
static int8_t  pos[SEQ * DM];
static int8_t  wcls[NCLASS * DM];
static int32_t bcls[NCLASS];

// Activations
static int32_t tokens[SEQ];
static int8_t  x[SEQ * DM];                 // the residual stream, int8
static int8_t  xres[SEQ * DM];              // copy kept for the residual add
static int32_t q32[SEQ * DM], k32[SEQ * DM], v32[SEQ * DM];
static int8_t  q8[SEQ * DM],  k8[SEQ * DM],  v8[SEQ * DM];
static int8_t  qh[SEQ * DH],  kh[SEQ * DH],  vh[SEQ * DH];
static int32_t scores[SEQ * SEQ];
static int32_t ctx[SEQ * DH];
static int8_t  ctxcat[SEQ * DM];            // heads concatenated back to DM
static int32_t proj[SEQ * DM];
static int32_t ff1[SEQ * DFF];
static int8_t  ff1_8[SEQ * DFF];
static int32_t ff2[SEQ * DM];
static int32_t pooled[DM];
static int8_t  pooled8[DM];
static int32_t logits[NCLASS];

// Per-kernel-class counters. `total` wraps the whole forward pass, so every
// other line's percentage is its share of one complete inference.
PROFILE_ACC_DECL(total);
PROFILE_ACC_DECL(linear);     // Q/K/V, output projection, FFN, classifier
PROFILE_ACC_DECL(qk);         // Q . K^T
PROFILE_ACC_DECL(softmax);
PROFILE_ACC_DECL(av);         // weights . V
PROFILE_ACC_DECL(norm);       // layer norm (mean, variance, isqrt, divide)
PROFILE_ACC_DECL(requant);

static void init_params(void) {
    uint32_t s = 0xBEEF01u;
    k_fill_i8(embed, VOCAB * DM, 32, &s);
    k_fill_i8(pos,   SEQ * DM,    8, &s);
    for (int l = 0; l < NLAYER; l++) {
        layer_t *L = &layers[l];
        k_fill_i8(L->wq, DM * DM,  4, &s);
        k_fill_i8(L->wk, DM * DM,  4, &s);
        k_fill_i8(L->wv, DM * DM,  4, &s);
        k_fill_i8(L->wo, DM * DM,  4, &s);
        k_fill_i8(L->w1, DFF * DM, 4, &s);
        k_fill_i8(L->w2, DM * DFF, 4, &s);
        for (int i = 0; i < DM;  i++) {
            L->bq[i] = (int32_t)(k_rand(&s) % 32) - 16;
            L->bk[i] = (int32_t)(k_rand(&s) % 32) - 16;
            L->bv[i] = (int32_t)(k_rand(&s) % 32) - 16;
            L->bo[i] = (int32_t)(k_rand(&s) % 32) - 16;
            L->b2[i] = (int32_t)(k_rand(&s) % 32) - 16;
        }
        for (int i = 0; i < DFF; i++)
            L->b1[i] = (int32_t)(k_rand(&s) % 32) - 16;
    }
    k_fill_i8(wcls, NCLASS * DM, 4, &s);
    for (int i = 0; i < NCLASS; i++) bcls[i] = (int32_t)(k_rand(&s) % 32) - 16;
}

// Embedding lookup + additive positional encoding, clamped back to int8.
static void embed_tokens(void) {
    for (int t = 0; t < SEQ; t++)
        for (int d = 0; d < DM; d++) {
            int32_t v = (int32_t)embed[tokens[t] * DM + d] +
                        (int32_t)pos[t * DM + d];
            if (v >  127) v =  127;
            if (v < -128) v = -128;
            x[t * DM + d] = (int8_t)v;
        }
}

// acc += residual, then row-wise layer norm, then back to int8 in `dst`.
// The residual stream is int8 and the accumulator is int32, so the residual is
// shifted up to put both on a comparable scale; the layer norm renormalizes
// straight afterwards, so this constant only sets how much weight the residual
// keeps relative to the sub-layer output.
static void add_norm(int32_t *acc, const int8_t *res, int8_t *dst, int width) {
    for (int t = 0; t < SEQ; t++) {
        for (int d = 0; d < width; d++)
            acc[t * width + d] += (int32_t)res[t * width + d] << SH_QKV;

        PROFILE_ACC_START(norm);
        k_layernorm_i32(&acc[t * width], width, NORM_GAIN);
        PROFILE_ACC_END(norm);
    }
    PROFILE_ACC_START(requant);
    k_requant_i8(acc, dst, SEQ * width, SH_NORM);
    PROFILE_ACC_END(requant);
}

static void encoder_layer(const layer_t *L) {
    // --- self-attention ------------------------------------------------
    PROFILE_ACC_START(linear);
    k_linear_i8(x, L->wq, L->bq, q32, SEQ, DM, DM);
    k_linear_i8(x, L->wk, L->bk, k32, SEQ, DM, DM);
    k_linear_i8(x, L->wv, L->bv, v32, SEQ, DM, DM);
    PROFILE_ACC_END(linear);

    PROFILE_ACC_START(requant);
    k_requant_i8(q32, q8, SEQ * DM, SH_QKV);
    k_requant_i8(k32, k8, SEQ * DM, SH_QKV);
    k_requant_i8(v32, v8, SEQ * DM, SH_QKV);
    PROFILE_ACC_END(requant);

    for (int h = 0; h < HEADS; h++) {
        // Gather this head's slice: head h owns columns [h*DH, h*DH+DH) of the
        // DM-wide projections, which are not contiguous in memory.
        for (int t = 0; t < SEQ; t++)
            for (int d = 0; d < DH; d++) {
                qh[t * DH + d] = q8[t * DM + h * DH + d];
                kh[t * DH + d] = k8[t * DM + h * DH + d];
                vh[t * DH + d] = v8[t * DM + h * DH + d];
            }

        PROFILE_ACC_START(qk);
        k_matmul_i8_bt(qh, kh, scores, SEQ, DH, SEQ);   // scores = Qh . Kh^T
        PROFILE_ACC_END(qk);

        k_shift_i32(scores, SEQ * SEQ, SH_SCORE);       // the 1/sqrt(d_k) scale

        PROFILE_ACC_START(softmax);
        k_softmax_q8(scores, SEQ, SEQ);
        PROFILE_ACC_END(softmax);

        PROFILE_ACC_START(av);
        k_matmul_q8_i8(scores, vh, ctx, SEQ, SEQ, DH, SH_CTX);
        PROFILE_ACC_END(av);

        // Scatter the head back into the concatenated DM-wide context.
        for (int t = 0; t < SEQ; t++)
            for (int d = 0; d < DH; d++) {
                int32_t v = ctx[t * DH + d];
                if (v >  127) v =  127;
                if (v < -128) v = -128;
                ctxcat[t * DM + h * DH + d] = (int8_t)v;
            }
    }

    for (int i = 0; i < SEQ * DM; i++) xres[i] = x[i];   // keep the residual

    PROFILE_ACC_START(linear);
    k_linear_i8(ctxcat, L->wo, L->bo, proj, SEQ, DM, DM);
    PROFILE_ACC_END(linear);

    add_norm(proj, xres, x, DM);

    // --- feed-forward ---------------------------------------------------
    for (int i = 0; i < SEQ * DM; i++) xres[i] = x[i];

    PROFILE_ACC_START(linear);
    k_linear_i8(x, L->w1, L->b1, ff1, SEQ, DM, DFF);
    PROFILE_ACC_END(linear);

    k_relu_i32(ff1, SEQ * DFF);

    PROFILE_ACC_START(requant);
    k_requant_i8(ff1, ff1_8, SEQ * DFF, SH_FF);
    PROFILE_ACC_END(requant);

    PROFILE_ACC_START(linear);
    k_linear_i8(ff1_8, L->w2, L->b2, ff2, SEQ, DFF, DM);
    PROFILE_ACC_END(linear);

    add_norm(ff2, xres, x, DM);
}

static int forward(void) {
    PROFILE_ACC_START(total);

    embed_tokens();
    for (int l = 0; l < NLAYER; l++)
        encoder_layer(&layers[l]);

    // Mean-pool over the sequence, then the classifier head.
    for (int d = 0; d < DM; d++) {
        int32_t s = 0;
        for (int t = 0; t < SEQ; t++) s += (int32_t)x[t * DM + d];
        pooled[d] = s / SEQ;
    }
    PROFILE_ACC_START(requant);
    k_requant_i8(pooled, pooled8, DM, 0);
    PROFILE_ACC_END(requant);

    PROFILE_ACC_START(linear);
    k_linear_i8(pooled8, wcls, bcls, logits, 1, DM, NCLASS);
    PROFILE_ACC_END(linear);

    PROFILE_ACC_END(total);
    return k_argmax_i32(logits, NCLASS);
}

int main(void) {
    init_params();

    uint32_t sum = 0;
    for (int n = 0; n < NINFER; n++) {
        uint32_t s = 0x51EEDu + (uint32_t)n;
        for (int t = 0; t < SEQ; t++)
            tokens[t] = (int32_t)(k_rand(&s) % VOCAB);

        int cls = forward();
        sum += (uint32_t)(cls + 1) * (uint32_t)(n + 1);
        sum += k_checksum_i32(logits, NCLASS);
    }

    printf("tinyformer seq=%d dm=%d heads=%d dff=%d layers=%d, %d pass(es)\n",
           SEQ, DM, HEADS, DFF, NLAYER, NINFER);
    printf("[profile] %-12s %10s %10s %6s %6s\n",
           "kernel", "cycles", "instr", "calls", "share");
    PROFILE_ACC_REPORT_OF(total,   PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(linear,  PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(qk,      PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(softmax, PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(av,      PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(norm,    PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(requant, PROFILE_ACC_CYCLES(total));

    return check_result("tinyformer", sum, GOLDEN);
}
