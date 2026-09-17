# Lab 0 — Assignment

**Groups of 3. Deliverable: this file, filled in, committed to your repository.**

Goal: run four complete C applications on X-HEEP, measure where their cycles
go, and start thinking about which kernel deserves a hardware accelerator.

There is no HDL here, and no design to commit to yet: after one week of
lectures nobody is expected to have a finished architecture. The work is
*understanding where the time goes* before you build anything. Step 8 collects
your first ideas; we discuss them with you, and the accelerator is decided
together at the start of Lab 1.

Before you start: finish [SETUP.md](SETUP.md), then walk through
[TUTORIAL.md](TUTORIAL.md) once with `rxchain`.

Fill in every `_____`. Placeholders left in the file are treated as missing
work. Numbers must come from your own runs.

---

## Group

| | |
|---|---|
| Group number | _____ |
| Members | _____, _____, _____ |
| Toolchain | RISC-V GCC `_____`, Verilator `_____`, OS `_____` |
| X-HEEP revision | `_____` (from `x-heep.lock.hjson`) |

---

## Step 1 — Vendoring (understand your own build)

Run `make vendor`, then read `x-heep.vendor.hjson` and `x-heep.lock.hjson`.

**Q1.1** What did `make vendor` fetch, where did it put it, and why is it a
pinned snapshot instead of a git submodule? (2–3 sentences.)

> _____

**Q1.2** `x-heep/` is in `.gitignore`. Starting from your repository alone,
which commands reproduce your exact SoC, and which file fixes the revision?

> _____

Reference: *FuseSoC Cookbook*, ch. 2 (vendoring), ch. 1 (FuseSoC), ch. 3 (makefiles).

---

## Step 2 — Run all four applications

Build once, then run each app:

```bash
make mcu-gen
make verilator-build
make verilator-run-app PROJECT=rxchain      # then tinydnn, tinyformer, pqcrypto
```

Every app self-checks. **If an app prints `FAIL`, stop and fix the build before
recording any number from it.**

**Q2.1** Paste the last four lines of each run (the `checksum=... PASS` line and
the application's own header line).

```
_____
```

---

## Step 3 — Method 1: cycle counters

Each app prints a per-kernel table: cycles, instructions, calls, and the
kernel's **share of the whole application**. Copy the numbers you measured.

Default sizes. Do not change them for this step.

### rxchain (NSAMP=256, TAPS=24, DEC=4, NBLOCK=1)

| kernel | cycles | instr | share |
|--------|--------|-------|-------|
| total | | | 100% |
| nco | | | |
| mixer | | | |
| fir | | | |
| decimate | | | |
| magnitude | | | |
| detect | | | |

### tinydnn (NINFER=1)

| kernel | cycles | instr | share |
|--------|--------|-------|-------|
| total | | | 100% |
| conv | | | |
| pool | | | |
| linear | | | |
| requant | | | |

### tinyformer (NINFER=1)

| kernel | cycles | instr | share |
|--------|--------|-------|-------|
| total | | | 100% |
| linear | | | |
| qk | | | |
| softmax | | | |
| av | | | |
| norm | | | |
| requant | | | |

### pqcrypto (N=256, NROUND=1)

| kernel | cycles | instr | share |
|--------|--------|-------|-------|
| total | | | 100% |
| ntt | | | |
| intt | | | |
| pointwise | | | |
| polyadd | | | |
| sample | | | |
| compress | | | |
| message | | | |

**Q3.1** Cycles per instruction (`total cycles / total instr`) for each app.
Which app stalls the most, and why? (Think memory access pattern.)

> _____

---

## Step 4 — Method 2: flamegraph

The flamegraph attributes cycles to **functions**, not to the regions you
wrapped by hand. Build the profiling run with inlining off, otherwise every
kernel disappears into `main` (see TUTORIAL.md §5):

```bash
make verilator-run-app PROJECT=<app> COMPILER_FLAGS=-fno-inline
make profile
mkdir -p figures && cp flamegraph.svg figures/<app>.svg
```

**Commit one flamegraph per application** under `figures/` and link them here:

| App | Flamegraph | Dominant function | Agrees with Step 3? |
|-----|------------|-------------------|---------------------|
| rxchain | `figures/rxchain.svg` | _____ | _____ |
| tinydnn | `figures/tinydnn.svg` | _____ | _____ |
| tinyformer | `figures/tinyformer.svg` | _____ | _____ |
| pqcrypto | `figures/pqcrypto.svg` | _____ | _____ |

**Q4.1** The `-fno-inline` run reports different cycle counts than the run in
Step 3. Give both totals for one app and explain which one you would quote as
"the" cost of the application, and why.

> _____

---

## Step 5 — Method 3: waveforms (debug only)

```bash
make verilator-run-app PROJECT=<app>
make verilator-waves
```

Nothing to submit. Use it to convince yourself the kernel is where you think it
is: find the loop in the waveform (instruction fetches, memory traffic) and
check it lines up with the counter table.

**Q5.1** One sentence: what did the waveform show you that the two profilers
did not?

> _____

Reference: *Simulation Cookbook*, ch. 8 (Verilator).

---

## Step 6 — Amdahl's law

For each app, take the dominant kernel from Step 3 and compute the ceiling:

`speedup_max = 1 / (1 - share)`

| App | Dominant kernel | Share | Ceiling if that kernel cost 0 cycles |
|-----|-----------------|-------|--------------------------------------|
| rxchain | _____ | ___% | _____× |
| tinydnn | _____ | ___% | _____× |
| tinyformer | _____ | ___% | _____× |
| pqcrypto | _____ | ___% | _____× |

**Q6.1** Was the dominant kernel the one you expected before running anything?
Which kernel looked like work and cost nothing? Which looked trivial and cost a
lot? Explain both with the operation counts, not with intuition.

> _____

**Q6.2** Does any single kernel carry weight in **more than one** application?
An accelerator that serves two apps is worth more than one that serves one.
(Look at `sw/external/kernels.h`: all four apps call the same functions.)

> _____

**Q6.3** Take your dominant kernel and assume the accelerator makes it **10×
faster**, not infinitely fast. What whole-application speedup do you get?
Compare it with the ceiling above.

> _____

---

## Step 7 — Scaling: small in simulation, large on hardware

RTL simulation is ~1000× slower than the real SoC, so the defaults are small.
In Lab 3 you will run these apps on an FPGA and can raise the sizes freely; here
everything runs on Verilator, so raise the repetition knob just enough to check
that the profile shares stay the same:

```bash
make host-check HOST_CFLAGS=-DNBLOCK=10          # prints the new golden checksum
make verilator-run-app PROJECT=rxchain \
     COMPILER_FLAGS="-DNBLOCK=10 -DGOLDEN=0x<checksum from host-check>"
```

**Q7.1** Report total cycles for one app at its default size and at 10×. Are the
per-kernel **shares** the same? What does that tell you about fixed costs
(setup, `printf`) versus the steady-state work an accelerator would attack?

> _____

---

## Step 8 — Start thinking: which kernel would you accelerate?

First ideas, not a design. You are **not** committing to anything here — you
have one week of lectures behind you, and Lab 1 starts by discussing what you
wrote in this step. Wrong-but-argued beats vague-but-safe.

**Q8.1 Candidates.** Two kernels you would consider, best first. They must
carry real weight in at least one application — your numbers from Step 3 and
Step 6 decide that, not intuition.

| | Application | Kernel (function in `kernels.h`) | Share | Amdahl ceiling |
|---|---|---|---|---|
| 1st choice | _____ | _____ | ___% | _____× |
| 2nd choice | _____ | _____ | ___% | _____× |

**Q8.2 Why this one?** What in the C code makes it a good target for hardware?
Look for: a regular loop with a fixed trip count, independent operations that
could run in the same cycle, integer/fixed-point arithmetic, few data-dependent
branches, data that arrives in a predictable order.

> _____

**Q8.3 Why not the other one?** One or two sentences on what makes your second
choice harder or less rewarding.

> _____

**Q8.4 Reuse.** Which of the four applications would your first choice speed
up, and by roughly how much in each? Use the shares you measured.

> _____

**Q8.5 How would data reach it?** No RTL, no block diagram — just say which of
these two feels right and why, from the *shape* of the kernel (one short
operation on a few registers vs. a block of data in memory):

☐ CV-XIF — an extra instruction the CPU issues, result back in a register
☐ OBI + register file — a peripheral you write data to, start, and read back

> _____ (you may change your mind in Lab 1; say what you are unsure about)

**Q8.6 Open questions.** What do you still need to know before you could design
this? List them — they are the agenda of the Lab 1 kickoff.

> _____

### Before Lab 1

Read the CORDIC case study: *Example Cookbook*, ch. 10, with its code in
`examplecookbook/code/cordic/` (see the reference list in
[README.md](README.md#reference-material)). The `nco` kernel of `rxchain` is
bit-exact with it. That is the structure Lab 1 asks of you: one golden model,
RTL that matches it bit for bit, one self-checking testbench.

---

## What you hand in

- **This file**, every `_____` filled with your own measurements.
- **`figures/`** with one flamegraph per application.
- A **release** of your repository, tagged `lab0-final`, published after those
  commits (see [README.md](README.md#what-you-hand-in)). The release is the
  submission.

Nothing is checked automatically. We read the file and discuss it with your
group: be ready to reproduce any number in it on your machine, and to argue
the candidates you listed in Step 8.

Do **not** commit `x-heep/`, `build/` or `flamegraph.svg` — they are generated,
and they are in `.gitignore`.
