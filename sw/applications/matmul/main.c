// Lab 0 example app: integer matrix multiply (naive GEMM).
// Hot kernel = the triple loop. Good accelerator target (MAC array).

#include <stdint.h>
#include <stdio.h>
#include "profile.h"

#define N 16

static int32_t a[N][N];
static int32_t b[N][N];
static int32_t c[N][N];

static void init(void) {
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            a[i][j] = (i * 7 + j) & 0xF;
            b[i][j] = (i + j * 3) & 0xF;
        }
}

// The kernel students profile and (later) accelerate.
static void matmul(void) {
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            int32_t acc = 0;
            for (int k = 0; k < N; k++)
                acc += a[i][k] * b[k][j];
            c[i][j] = acc;
        }
}

static uint32_t checksum(void) {
    uint32_t s = 0;
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++)
            s += (uint32_t)c[i][j];
    return s;
}

int main(void) {
    init();
    PROFILE_START(matmul);
    matmul();
    PROFILE_END(matmul);
    printf("matmul %dx%d checksum=0x%08x\n", N, N, checksum());
    return 0;
}
