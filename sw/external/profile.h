#ifndef LAB_PROFILE_H
#define LAB_PROFILE_H

// Minimal on-target profiling: read the RISC-V cycle / instret CSRs and print
// the delta around a code region. This is the "printf the cycles" method from
// Lab 0. The other two methods (RV_PROFILE flamegraph, waveform inspection) use
// X-HEEP's own tooling and need no code here.

#include <stdint.h>
#include <stdio.h>

// Return type is register-width (unsigned long == 32-bit on the RV32 ilp32
// target) so the csrr operand matches the register and no width warning fires.
static inline uint32_t read_mcycle(void) {
    unsigned long c;
    __asm__ volatile("csrr %0, mcycle" : "=r"(c));
    return (uint32_t)c;
}

static inline uint32_t read_minstret(void) {
    unsigned long c;
    __asm__ volatile("csrr %0, minstret" : "=r"(c));
    return (uint32_t)c;
}

// ponytail: 32-bit deltas. mcycle is 64-bit; on RV32 we ignore mcycleh, so a
// region longer than ~2^32 cycles wraps. Lab kernels are far shorter. If you
// ever profile something that long, read {mcycleh,mcycle} as a 64-bit pair.
#define PROFILE_START(tag)                                                     \
    uint32_t _prof_c0_##tag = read_mcycle();                                   \
    uint32_t _prof_i0_##tag = read_minstret()

#define PROFILE_END(tag)                                                       \
    do {                                                                       \
        uint32_t _c = read_mcycle() - _prof_c0_##tag;                          \
        uint32_t _i = read_minstret() - _prof_i0_##tag;                        \
        printf("[profile] " #tag ": %u cycles, %u instr\n",                    \
               (unsigned)_c, (unsigned)_i);                                    \
    } while (0)

#endif // LAB_PROFILE_H
