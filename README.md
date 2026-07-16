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
make vendor                        # pull X-HEEP into ./x-heep
make mcu-gen                       # generate MCU from config.py
make verilator-build               # build the Verilator model
make app PROJECT=matmul            # compile the app (out-of-tree)
make verilator-run PROJECT=matmul  # run it; prints kernel cycle count
make profile                       # RV_PROFILE flamegraph from the last run
make help
```

Target names mirror X-HEEP's own so you learn the real flow; each just forwards
to the vendored X-HEEP with the right `PROJECT`, `SOURCE` and config.

Then do `TODO.md` and fill in `REPORT.md`.

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
sw/external/profile.h    shared cycle/instret profiling macros
sw/{device,linker,build} symlinks into x-heep/sw (made by `make vendor`)
TODO.md / REPORT.md      the activity guide and the report you fill in
x-heep/                  vendored X-HEEP (gitignored, created by `make vendor`)
```

The layout follows the standard X-HEEP-based project (like `gr-heep`): apps and
shared headers live here; `make vendor` symlinks X-HEEP's `device`/`linker`/`build`
into `sw/` so an out-of-tree build finds the generated device headers.
