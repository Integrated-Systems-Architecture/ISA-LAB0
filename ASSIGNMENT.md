# Lab 0 — Assignment

**Groups of 3. Deliverable: this file, filled in, committed to your repository.**

Goal: run four complete C applications on X-HEEP, measure where their cycles
go, and propose one hardware accelerator that you will build in Lab 1.

This lab is a gate — you cannot start Lab 1 until the proposal is signed off.
There is no HDL here. The work is *understanding where the time goes* before
you build anything.

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

**Q1.2** `x-heep/` is in `.gitignore`. What would a grader have to run to
reproduce your exact SoC from your repository alone?

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
Raise the repetition knob and check the profile shares stay the same:

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

## Step 8 — Choose one application and propose an accelerator

Pick the kernel you will build in Lab 1. It must carry real weight in at least
one application — that application is what you will run end to end in Lab 3.

| | |
|---|---|
| Chosen application | _____ |
| Kernel to accelerate | _____ (function in `sw/external/kernels.h`) |
| Measured share of that app | ___% |
| Amdahl ceiling | _____× |
| Realistic target speedup | _____× |

**Q8.1 Interface.** Choose one and justify it from the *shape* of the kernel
(short fixed-latency operation on registers vs. block/streaming work on memory):

☐ CV-XIF (instruction-extension co-processor)  ☐ OBI + register file (memory-mapped peripheral)

> _____

**Q8.2 Datapath sketch.** Block diagram or description: datapath, word widths,
how many operations per cycle, how data gets in and out.

> _____

**Q8.3 Where the speedup comes from.** Per-element cycle cost in software
(count the instructions in the C kernel) versus your hardware, times the number
of elements.

> _____

**Q8.4 Reuse.** Which of the four applications would this accelerator speed up,
and by how much in each? Use your measured shares.

> _____

**Q8.5 Risks.** What could make the real speedup much smaller than Q8.3
(memory bandwidth, data movement over the interface, setup cost per call)?

> _____

Before writing any RTL in Lab 1, read the CORDIC case study: *Example
Cookbook*, ch. 10, with its code in `books/examplecookbook/code/cordic/`. The
`nco` kernel of `rxchain` is bit-exact with it. That is the structure Lab 1
asks of you: one golden model, RTL that matches it bit for bit, one
self-checking testbench that runs on more than one simulator.

---

## Step 9 — Optional: run on the FPGA

```bash
make vivado-fpga FPGA_BOARD=pynq-z2
make vivado-fpga-pgm FPGA_BOARD=pynq-z2
make run-fpga-flash-load PROJECT=tinyformer
```

On the board a run costs microseconds instead of simulated minutes, so raise
`NINFER` / `NBLOCK` / `NROUND` and measure a throughput. Not required — but the
number you get here is the baseline your Lab 3 speedup is measured against.

> _optional: board, sizes used, cycles or wall-clock per run, comparison with
> the Verilator number_

---

## How this is graded

| | |
|---|---|
| All four apps run and print `PASS` | required |
| Steps 3, 4, 6 filled with your own numbers | 40% |
| Analysis in Q3.1, Q6.1–Q6.3, Q7.1 | 30% |
| Proposal in Step 8, interface justified from the kernel's shape | 30% |
| Step 9 | bonus |

Commit **this file** and `figures/`. Do **not** commit `x-heep/` or `build/`.
