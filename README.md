# X-HEEP Accelerator Labs

MSc lab, groups of 3. You profile C applications on **X-HEEP**, pick one, and
build a custom hardware accelerator for it across four activities:

- **Lab 0** — profile the provided apps, choose one, propose an accelerator. *(this repo, ready)*
- **Lab 1** — implement the accelerator standalone (RTL + testbench), simulate and synthesize.
- **Lab 2** — optimize it (retiming, pipelining, folding, arithmetic opt) and compare PPA vs Lab 1.
- **Lab 3** — integrate it into X-HEEP over CV-XIF or OBI+REG, write the C driver, measure real speedup.

Each lab has a `labN/TODO.md` telling you exactly what to do and how it is
checked.

## Prerequisites

- RISC-V GCC toolchain (`riscv32-unknown-elf-*`), Verilator, gtkwave, Python 3
  with the X-HEEP requirements. QuestaSim optional for the commercial path.
- Simplest: use the X-HEEP Docker image (see the vendored `x-heep/README.md`
  after `make vendor`).

## Quick start (Lab 0)

```bash
make host-check                    # build+run every app on your laptop (seconds)
make vendor                        # pull X-HEEP into ./x-heep
make mcu-gen                       # generate MCU from config.py
make verilator-build               # build the Verilator model
make verilator-run-app PROJECT=rxchain  # compile the app and run it
make profile                       # RV_PROFILE flamegraph from the last run
make help
```

Every app self-checks: it compares its result against a golden checksum and
prints `PASS` or `FAIL`, so the same binary tells you it is correct on your
host, on Verilator, on QuestaSim and on the FPGA.

Target names mirror X-HEEP's own so you learn the real flow; each just forwards
to the vendored X-HEEP with the right `PROJECT`, `SOURCE` and config.

Then do `TODO.md` and fill in `REPORT.md`.

## The applications

Three of them. Each is a **complete application**, not a kernel in a wrapper,
and each is assembled from the shared kernel library `sw/external/kernels.h`:

| App | What it is | Kernels in it |
|-----|------------|---------------|
| `rxchain`    | a digital receiver front-end | CORDIC NCO, mixer, FIR ×2, decimate, magnitude, detector |
| `tinydnn`    | a quantized CNN inference | conv2d+ReLU ×2, maxpool ×2, linear ×2, requant |
| `tinyformer` | a 2-layer transformer encoder | linear ×9, Q·Kᵀ, softmax, weights·V, layer norm, requant |

There are no single-kernel benchmarks on purpose. Each app prints **cycles per
kernel and its share of the whole run**, and *deciding which kernel deserves
hardware is the work of Lab 0*. The obvious answer is often wrong: in
`rxchain` decimation looks like work and costs nothing; in `tinyformer` the
softmax looks exotic and is 1% while the plain matmuls are 80%. Whatever share
you measure is Amdahl's ceiling on the speedup you can report in Lab 3.

Because the apps share the library, one accelerator can serve more than one of
them — `k_fir_i16`, `k_cordic_rot`, `k_matmul_i8_bt` and `k_conv2d_relu_i8` are
each called by real work in at least one app.

Sizes are compile-time overridable, e.g. `make app PROJECT=rxchain
CFLAGS=-DNSAMP=1024` or `-DNBLOCK=100` / `-DNINFER=100` to run many
blocks/inferences. Change a size and the golden checksum changes: `make
host-check` prints the new one.

## The reference example: CORDIC

The CORDIC stage of `rxchain` (`k_cordic_rot`, the NCO) is bit-exact with the
worked example of the SystemVerilog cookbook,
`cookbook/code/cordic/`: the same rotator written in **VHDL**
(`cordic_rot.vhd`) and **SystemVerilog** (`cordic_rot.sv`), verified against
one Python golden model (`cordic_model.py`) by six different testbenches, with
the vectors in `vectors/cordic_vectors.txt`.

It uses the same constants, the same guard bits and the same truncation, so
the C, the VHDL and the SystemVerilog produce identical integers.

Lab 0 only asks you to pick an application; the bit-exactness is Lab 1's
problem, and that is where it is checked on every simulation (`make bitexact`
there pins Lab 1's golden model to the cookbook, and the testbench pins the
RTL to that model).

Read that example before Lab 1. It is the shape your own work should have:
one golden model, RTL that matches it bit for bit, and a self-checking
testbench that runs on more than one simulator.

## Running the full networks on the FPGA

A full `tinyformer` pass is slow in RTL simulation and instant on hardware, so
the end goal is to run the complete applications on an FPGA:

```bash
make vivado-fpga FPGA_BOARD=pynq-z2      # build the bitstream   [-> X-HEEP]
make vivado-fpga-pgm FPGA_BOARD=pynq-z2  # program the board     [-> X-HEEP]
make run-fpga-flash-load PROJECT=tinyformer   # flash + run the app
```

On the board you can raise `NINFER` / `NBLOCK` and measure a throughput over
many runs instead of one simulated latency — and in Lab 3 the same app runs
with your accelerator in the SoC, so the before/after numbers are directly
comparable.

## Tools you must understand (not just invoke)

The Makefiles wrap them, but the guides expose the raw commands. You will be
expected to explain what these do:

- **vendor** (`util/vendor.py` + `*.vendor.hjson`) — pulls X-HEEP / IPs into the
  tree reproducibly.
- **FuseSoC** — builds the SoC and the simulation model from `.core` files.
- **reggen** — generates the register interface (CSRs) and C HAL from an hjson
  description (used in Lab 3 for the OBI+REG path).

## Layout

```
Makefile                 top-level wrapper; includes x-heep/external.mk
util/vendor.py           the vendoring tool (committed so you can run it)
x-heep.vendor.hjson      what to vendor and at which revision
config.py                X-HEEP SoC configuration (CPU, memory, peripherals)
sw/applications/<app>/   provided C apps (built out-of-tree via SOURCE=../../sw/)
sw/external/kernels.h    shared integer kernel library, used by every app
sw/external/profile.h    cycle/instret profiling macros + PASS/FAIL self-check
sw/{device,linker,build} symlinks into x-heep/sw (made by `make vendor`)
TODO.md / REPORT.md      the activity guide and the report you fill in
x-heep/                  vendored X-HEEP (gitignored, created by `make vendor`)
```

The layout follows the standard X-HEEP-based project (like `gr-heep`): apps and
shared headers live here; `make vendor` symlinks X-HEEP's `device`/`linker`/`build`
into `sw/` so an out-of-tree build finds the generated device headers.
