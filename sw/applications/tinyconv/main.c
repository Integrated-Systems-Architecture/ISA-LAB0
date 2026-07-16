// Lab 0 example app: single Conv2D + ReLU layer (int8 in/weights, int32
// accumulate), the core building block of a CNN layer such as LeNet-5
// [LeCun et al., 1998] run in integer-only arithmetic as in quantized
// inference [Jacob et al., CVPR 2018]. Hot kernel = the 6-deep MAC loop.
// Good accelerator target: same MAC-array idea as matmul, but with a
// sliding window instead of full reuse -- classic systolic-array /
// line-buffer territory.

#include <stdint.h>
#include <stdio.h>
#include "profile.h"

#define CIN  2   // input channels
#define COUT 4   // output channels (filters)
#define K    3   // kernel size (KxK)
#define IH   10  // input height
#define IW   10  // input width
#define OH   (IH - K + 1)  // output height (valid conv, no padding)
#define OW   (IW - K + 1)  // output width

static int8_t  in[CIN][IH][IW];
static int8_t  weight[COUT][CIN][K][K];
static int32_t bias[COUT];
static int32_t out[COUT][OH][OW];

static void init(void) {
    for (int c = 0; c < CIN; c++)
        for (int y = 0; y < IH; y++)
            for (int x = 0; x < IW; x++)
                in[c][y][x] = (int8_t)(((y * 3 + x * 5 + c) % 17) - 8);

    for (int f = 0; f < COUT; f++) {
        bias[f] = (f * 3) - 4;
        for (int c = 0; c < CIN; c++)
            for (int kh = 0; kh < K; kh++)
                for (int kw = 0; kw < K; kw++)
                    weight[f][c][kh][kw] =
                        (int8_t)(((f * 5 + c * 2 + kh * 3 + kw) % 9) - 4);
    }
}

// The kernel students profile and (later) accelerate.
static void conv2d_relu(void) {
    for (int f = 0; f < COUT; f++)
        for (int oy = 0; oy < OH; oy++)
            for (int ox = 0; ox < OW; ox++) {
                int32_t acc = bias[f];
                for (int c = 0; c < CIN; c++)
                    for (int kh = 0; kh < K; kh++)
                        for (int kw = 0; kw < K; kw++)
                            acc += (int32_t)in[c][oy + kh][ox + kw] *
                                   (int32_t)weight[f][c][kh][kw];
                out[f][oy][ox] = acc > 0 ? acc : 0;  // ReLU
            }
}

static uint32_t checksum(void) {
    uint32_t s = 0;
    for (int f = 0; f < COUT; f++)
        for (int oy = 0; oy < OH; oy++)
            for (int ox = 0; ox < OW; ox++)
                s += (uint32_t)out[f][oy][ox];
    return s;
}

int main(void) {
    init();
    PROFILE_START(conv2d_relu);
    conv2d_relu();
    PROFILE_END(conv2d_relu);
    printf("tinyconv %dx%d->%dx%d x%d filters checksum=0x%08x\n",
           IH, IW, OH, OW, COUT, checksum());
    return 0;
}
