#ifndef LAB_PROFILE_H
#define LAB_PROFILE_H

// Minimal on-target profiling: read the RISC-V cycle / instret CSRs and print
// the delta around a code region. This is the "printf the cycles" method from
// Lab 0. The other two methods (RV_PROFILE flamegraph, waveform inspection) use
// X-HEEP's own tooling and need no code here.
//
// Two flavours:
//   PROFILE_START/END        one region, printed immediately (single-kernel apps)
//   PROFILE_ACC_*            a counter per kernel, summed over many calls and
//                            printed at the end (the full-network apps, where
//                            the same kernel runs dozens of times)
//
// The same source also builds and runs on your host machine (`make host-check`),
// where there are no RISC-V CSRs: the counters then read zero and the profile
// lines print zeros. Host builds are for checking the numeric result, not for
// timing.

#include <stdint.h>
#include <stdio.h>

#ifdef __riscv
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
#else
// Host build: no performance CSRs. Report zero rather than a wall-clock number
// that would be meaningless next to a cycle count.
static inline uint32_t read_mcycle(void)   { return 0; }
static inline uint32_t read_minstret(void) { return 0; }
#endif

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

// --- Accumulating counters --------------------------------------------------
//
// For an app that calls the same kernel many times (a network layer loop), one
// printf per call is noise. Declare a counter once at file scope, wrap every
// call, print the totals at the end:
//
//     PROFILE_ACC_DECL(matmul);
//     ... PROFILE_ACC_START(matmul); k_matmul_i8(...); PROFILE_ACC_END(matmul);
//     PROFILE_ACC_REPORT(matmul);
//
// Different counters nest, and the nesting double-counts on purpose: wrap the
// whole inference in one counter and each kernel in its own, and the per-kernel
// totals tell you directly what fraction of the app an accelerator would touch.

#define PROFILE_ACC_DECL(tag)                                                  \
    static uint32_t _pa_c_##tag, _pa_i_##tag, _pa_n_##tag,                     \
                    _pa_c0_##tag, _pa_i0_##tag

// The start values live in the counter's own file-scope variables, not in
// locals, so one tag can be started and stopped many times in the same
// function. The flip side: a tag must not be nested inside ITSELF (use a
// second tag for the outer region, as the apps do with `total`).
#define PROFILE_ACC_START(tag)                                                 \
    do {                                                                       \
        _pa_c0_##tag = read_mcycle();                                          \
        _pa_i0_##tag = read_minstret();                                        \
    } while (0)

#define PROFILE_ACC_END(tag)                                                   \
    do {                                                                       \
        _pa_c_##tag += read_mcycle() - _pa_c0_##tag;                           \
        _pa_i_##tag += read_minstret() - _pa_i0_##tag;                         \
        _pa_n_##tag += 1;                                                      \
    } while (0)

#define PROFILE_ACC_CYCLES(tag) (_pa_c_##tag)

// Percentage is of `total` cycles, integer, so it stays readable when the app
// runs on a board with no floating point printf.
#define PROFILE_ACC_REPORT_OF(tag, total)                                      \
    printf("[profile] %-12s %10u %10u %6u %5u%%\n",        \
           #tag, (unsigned)_pa_c_##tag, (unsigned)_pa_i_##tag,                 \
           (unsigned)_pa_n_##tag,                                              \
           (unsigned)((total) ? (uint32_t)(((uint64_t)_pa_c_##tag * 100u) /    \
                                           (uint32_t)(total)) : 0u))

#define PROFILE_ACC_REPORT(tag) PROFILE_ACC_REPORT_OF(tag, _pa_c_##tag)

// --- Self-check -------------------------------------------------------------
//
// Every app ends with a checksum compared against a golden constant, so a run
// on the Verilator model or on the FPGA says PASS/FAIL by itself
// instead of asking you to eyeball a hex number. Returns the process exit code.
static inline int check_result(const char *name, uint32_t got, uint32_t want) {
    if (got == want) {
        printf("%s: checksum=0x%08x PASS\n", name, (unsigned)got);
        return 0;
    }
    printf("%s: checksum=0x%08x expected=0x%08x FAIL\n", name, (unsigned)got,
           (unsigned)want);
    return 1;
}

#endif // LAB_PROFILE_H
