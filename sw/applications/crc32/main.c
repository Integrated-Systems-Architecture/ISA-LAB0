// Lab 0 example app: CRC-32 (IEEE, bit-serial) over a byte buffer.
// Hot kernel = the per-bit inner loop. Tiny accelerator target, good fit for
// an OBI + register-mapped peripheral.

#include <stdint.h>
#include <stdio.h>
#include "profile.h"

#define LEN 256

static uint8_t buf[LEN];

static void init(void) {
    for (int i = 0; i < LEN; i++)
        buf[i] = (uint8_t)(i * 31 + 7);
}

// The kernel students profile and (later) accelerate.
static uint32_t crc32(const uint8_t *data, int len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

int main(void) {
    init();
    uint32_t r;
    PROFILE_START(crc32);
    r = crc32(buf, LEN);
    PROFILE_END(crc32);
    printf("crc32 len=%d result=0x%08x\n", LEN, r);
    return 0;
}
