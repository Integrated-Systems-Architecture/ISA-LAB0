# Lab 0 — Profiling & Accelerator Proposal

**Goal:** run the provided C applications on X-HEEP, profile them three different
ways, then pick ONE application and propose a hardware accelerator for it.

This lab is a gate: your group cannot start Lab 1 until the proposal is signed
off. There is (almost) no HDL here — it is about *understanding where the time
goes* before you build anything.

---

## 0. Setup (once)

You get X-HEEP as a **vendored** dependency. You will not commit the X-HEEP
source; you pull it with the vendoring tool. Know what each command does — you
will use `vendor`, `fusesoc` and `reggen` again in later labs.

```bash
make vendor           # python3 util/vendor.py x-heep.vendor.hjson  -> ./x-heep
make mcu-gen          # generate the MCU from config.py (fusesoc)
make verilator-build  # build the Verilator model
```

X-HEEP is configured by `config.py` (Python config: CPU, memory banks,
peripherals). You are free to change it — `make mcu-gen` regenerates the SoC
from whatever it says.

Read `x-heep.vendor.hjson` and `make help`. In your report, explain in 2–3
sentences **what `make vendor` actually did** (what it fetched, where, and why a
snapshot instead of a git submodule).

## 1. Run the apps

Three applications live in `sw/applications/`. Each is a **complete
application** assembled from several kernels, not one kernel in a wrapper:

- `rxchain`    — a digital receiver front-end: CORDIC NCO → mixer → FIR ×2 →
  decimate → magnitude → detector
- `tinydnn`    — a quantized CNN: conv → pool → conv → pool → FC → FC → argmax
- `tinyformer` — a 2-layer transformer encoder: embedding, multi-head
  attention, feed-forward, layer norm, classifier head

```bash
make verilator-run-app PROJECT=rxchain   # = make app + make verilator-run
# ... and the same for tinydnn and tinyformer
```

`verilator-run` on its own re-runs whatever binary was compiled last, so it
will happily print another app's numbers. Use `verilator-run-app` (or `make app
PROJECT=<app>` first) and check the app's own name in the output line.

Every app is **self-checking**: it prints `PASS` or `FAIL` against a golden
checksum. If an app prints `FAIL`, stop and fix the build before you record any
number from it.

There is deliberately no single-kernel benchmark in the set. Each app reports
**cycles per kernel and each kernel's share of the whole run**, and choosing
which kernel deserves hardware is the work you are being asked to do. A kernel
that dominates one app may be irrelevant in another.

All three apps are built from ONE shared kernel library,
`sw/external/kernels.h`. `k_fir_i16` is the same function whether `rxchain`
calls it or you call it yourself — which is the point: the accelerator you
build in Lab 1 replaces one of those functions and therefore speeds up every
app that calls it.

Before touching X-HEEP at all, run them on your own machine:

```bash
make host-check     # builds every app with cc and checks every result
```

The kernels are integer-only, so the host result is bit-identical to the
RISC-V one. Use it whenever you change an app; it takes seconds instead of
minutes. It reports no cycle counts (your laptop has no `mcycle`) — that is
what the target runs are for.

## 2. Profile three ways

For **each** app, use all three methods and note what each one tells you:

1. **Cycle-count printf** — the `PROFILE_ACC_*` macros (already wired): one
   counter per kernel, summed over the whole run and printed at the end as a
   table with each kernel's **share of the total**. Those percentages are the
   most important numbers in this lab — see §3 before you interpret them.
2. **RV_PROFILE flamegraph** — after a run (which produced the `.fst` waveform),
   run `make profile`. This feeds the waveform to the `rv_profile` tool and
   writes `x-heep/util/profile/flamegraph.svg`. Open it and identify the
   function that dominates. (`RV_PROFILE` defaults to the tool from the
   `core-v-mini-mcu` conda env; override it to point elsewhere if needed.)
3. **Waveforms** — `make verilator-run PROJECT=<app>` then `make verilator-waves`
   (gtkwave). Waveforms are a **debugging aid only** here: use them to
   sanity-check that the kernel is where you think it is (e.g. bus/memory
   activity during the loop). Nothing to submit from this step.

## 3. Amdahl's law: what a kernel is actually worth

Copy all three per-kernel tables into the report. Then answer, with numbers:

- Which kernel dominates each app? Is it the one you expected before running
  anything?
- If you made that kernel **infinitely fast** (zero cycles), what is the
  speedup of the whole application? That is Amdahl's ceiling, and no
  accelerator you build can beat it.
- Which kernels are cheap despite looking like work (`decimate` in `rxchain`),
  and which are expensive despite looking trivial (`detect`, `requant`)? Why?
- Does any single kernel carry weight in **more than one** app? An accelerator
  that does is worth more than one that does not.

Sizes are compile-time constants you can override, e.g.
`make app PROJECT=rxchain CFLAGS=-DNSAMP=1024`. Changing a size changes the
golden checksum; `make host-check` prints the new value to put in the app.

## 4. Choose one app + propose an accelerator

Pick the kernel that is the best hardware target and write the proposal in
`REPORT.md`. It must be a kernel that carries real weight in at least one app
— that app is the one you will run end to end in Lab 3, on the FPGA if you get
that far. Decide and justify:

- **Which kernel** you will accelerate (whole app or one function).
- **Which interface**: `CV-XIF` (instruction-extension co-processor) or
  `OBI + register file` (memory-mapped peripheral). Justify the choice from the
  kernel's shape (short fixed-latency op vs block/streaming).
- **Expected speedup** and where it comes from.
- A rough **datapath sketch** (block diagram is fine).
- Which of the three apps your accelerator would speed up, and by how much in
  each (use the measured per-kernel shares).

**Before you write any RTL, read the CORDIC example in
`cookbook/code/cordic/`.** The NCO stage of `rxchain` is bit-exact with it: the
same rotator exists there as VHDL, as SystemVerilog, as a Python golden model
and as six testbenches that check one against the other. That is the structure
Lab 1 asks you to reproduce for your own kernel — golden model first, RTL that
matches it bit for bit, one self-checking testbench that runs on both
simulators.

## 5. Optional: run a complete application on the FPGA

The end goal of the lab series is a whole network running on real hardware with
your accelerator in the SoC. Nothing stops you from doing the *software* half
of that now:

```bash
make vivado-fpga FPGA_BOARD=pynq-z2           # build the bitstream
make vivado-fpga-pgm FPGA_BOARD=pynq-z2       # program the board
make run-fpga-flash-load PROJECT=tinyformer   # flash the app and run it
```

On the board a run costs microseconds instead of simulated minutes, so you can
raise `NINFER` / `NBLOCK` and report a throughput. Not required for Lab 0 — but if
you do it, put the numbers in the report, and you will have the baseline your
Lab 3 speedup is measured against.

---

## Deliverable

Fill in `REPORT.md` completely. Commit it. Do **not** commit the vendored
`x-heep/` directory (it is in `.gitignore`).

## How this is checked

- CI: every app builds, runs on the Verilator model and prints `PASS`.
- Manual: the report has real numbers (not placeholders), all three profiling
  methods are covered, the Amdahl analysis of §3 uses the per-kernel shares,
  and the proposal's interface choice is justified.
