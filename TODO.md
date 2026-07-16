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

Provided apps live in `sw/applications/`:

- `matmul` — 16×16 integer matrix multiply
- `crc32`  — CRC-32 over a 256-byte buffer

```bash
make app PROJECT=matmul && make verilator-run PROJECT=matmul
make app PROJECT=crc32  && make verilator-run PROJECT=crc32
```

Each app already prints a cycle/instruction count around its hot kernel (see
`sw/external/profile.h`). Record those numbers.

## 2. Profile three ways

For **each** app, use all three methods and note what each one tells you:

1. **Cycle-count printf** — the `PROFILE_START/END` macros (already wired).
   Report cycles and instructions for the kernel.
2. **RV_PROFILE flamegraph** — after a run (which produced the `.fst` waveform),
   run `make profile`. This feeds the waveform to the `rv_profile` tool and
   writes `x-heep/util/profile/flamegraph.svg`. Open it and identify the
   function that dominates. (`RV_PROFILE` defaults to the tool from the
   `core-v-mini-mcu` conda env; override it to point elsewhere if needed.)
3. **Waveforms** — `make verilator-run PROJECT=<app>` then `make verilator-waves`
   (gtkwave). Waveforms are a **debugging aid only** here: use them to
   sanity-check that the kernel is where you think it is (e.g. bus/memory
   activity during the loop). Nothing to submit from this step.

## 3. Choose one app + propose an accelerator

Pick the app whose hot kernel is the best hardware target and write the proposal
in `REPORT.md`. Decide and justify:

- **Which kernel** you will accelerate (whole app or one function).
- **Which interface**: `CV-XIF` (instruction-extension co-processor) or
  `OBI + register file` (memory-mapped peripheral). Justify the choice from the
  kernel's shape (short fixed-latency op vs block/streaming).
- **Expected speedup** and where it comes from.
- A rough **datapath sketch** (block diagram is fine).

---

## Deliverable

Fill in `REPORT.md` completely. Commit it. Do **not** commit the vendored
`x-heep/` directory (it is in `.gitignore`).

## How this is checked

- CI: both apps build and run on the Verilator model and print their profiling
  line.
- Manual: the report has real numbers (not placeholders), all three profiling
  methods are covered, and the proposal's interface choice is justified.
