# Lab 0 — Profiling and accelerator proposal

MSc lab, groups of 3. Over four activities you profile C applications running on
the **X-HEEP** RISC-V microcontroller, pick one, and build a custom hardware
accelerator for it:

| | | |
|---|---|---|
| **Lab 0** | profile the provided apps, choose one, propose an accelerator | *this repository* |
| Lab 1 | implement the accelerator standalone (RTL + testbench), simulate and synthesize | |
| Lab 2 | optimize it (retiming, pipelining, folding, arithmetic) and compare PPA with Lab 1 | |
| Lab 3 | integrate it into X-HEEP over CV-XIF or OBI+REG, write the C driver, measure the real speedup | |

## Start here

1. **[SETUP.md](SETUP.md)** — install the toolchain (macOS, Ubuntu/Linux, WSL2).
   Once per machine, about an hour.
2. **[TUTORIAL.md](TUTORIAL.md)** — a guided run of one application,
   `rxchain`, with the output you should see at every step.
3. **[ASSIGNMENT.md](ASSIGNMENT.md)** — what you must do and hand in. Fill it
   in and commit it; that file *is* the report.

Quick check that your machine is ready:

```bash
make host-check     # 5 s, no X-HEEP needed: builds and self-checks all four apps
make help           # every target
```

## The applications

Four of them, in `sw/applications/`. Each is a **complete application**, not a
kernel in a wrapper, and all four are assembled from the same kernel library,
`sw/external/kernels.h`:

| App | What it is | Kernels in it |
|-----|------------|---------------|
| `rxchain` | a digital receiver front-end | CORDIC NCO, mixer, FIR ×2, decimate, magnitude, detector |
| `tinydnn` | a quantized CNN inference | conv2d+ReLU ×2, maxpool ×2, linear ×2, requant |
| `tinyformer` | a 2-layer transformer encoder | linear ×9, Q·Kᵀ, softmax, weights·V, layer norm, requant |
| `pqcrypto` | a lattice public-key encryption round | NTT ×3, inverse NTT ×3, pointwise multiply, poly add, noise sampling, compress |

There are no single-kernel benchmarks on purpose. Each app prints **cycles per
kernel and each kernel's share of the whole run**, and deciding which kernel
deserves hardware is the work of Lab 0. The obvious answer is often wrong: in
`rxchain` decimation looks like work and costs nothing; in `tinyformer` the
softmax looks exotic and is a few percent while the plain matmuls dominate; in
`pqcrypto` the pointwise multiply that *is* the polynomial product is a few
percent and the two transforms that only make it possible are most of the run.
Whatever share you measure is Amdahl's ceiling on the speedup you can report in
Lab 3.

Because the apps share one library, a single accelerator can serve more than one
of them — `k_fir_i16`, `k_cordic_rot`, `k_matmul_i8_bt` and `k_conv2d_relu_i8`
are each called by real work in at least one app.

Every app self-checks: it compares its result with a golden checksum and prints
`PASS` or `FAIL`, so the same source tells you it is correct on your laptop, on
Verilator, on QuestaSim and on the FPGA.

## Tools you must understand (not just invoke)

The Makefile wraps them, but you will be asked to explain what they do:

- **vendor** (`util/vendor.py` + `x-heep.vendor.hjson`) — pulls a pinned X-HEEP
  snapshot into `./x-heep`, reproducibly.
- **FuseSoC** — builds the SoC and the simulation model from `.core` files.
- **reggen** — generates the register interface (CSRs) and the C HAL from an
  hjson description; you need it in Lab 3 for the OBI+REG path.

## Reference material

The course cookbooks (the `books/` directory of the course material, PDFs
included) cover exactly these tools:

| Question | Where |
|----------|-------|
| What is FuseSoC doing? What is a `.core` file? | *FuseSoC Cookbook*, ch. 1 |
| What did `make vendor` do, and why not a submodule? | *FuseSoC Cookbook*, ch. 2 |
| How is this Makefile put together? | *FuseSoC Cookbook*, ch. 3 |
| `reggen` / register interfaces (Lab 3) | *FuseSoC Cookbook*, ch. 4 |
| Verilator, waveforms, self-checking testbenches | *Simulation Cookbook*, ch. 8 |
| Python-driven verification, cocotb | *Simulation Cookbook*, ch. 9 |
| The CORDIC case study — golden model, VHDL, SystemVerilog, six testbenches | *Example Cookbook*, ch. 10, code in `books/examplecookbook/code/cordic/` |
| VHDL/SystemVerilog side by side, coding style | *Design Cookbook*, ch. 1–4 |
| Testbench structure, UVM (Labs 1–2) | *Verification Cookbook*, ch. 5–7 |
| git and GitHub workflow for the group | *Git Cookbook*, ch. 1–3 |

For X-HEEP itself, the vendored copy carries its own documentation in
`x-heep/docs/` (also at <https://x-heep.readthedocs.io>).

The `nco` kernel of `rxchain` is **bit-exact** with the CORDIC of the Example
Cookbook: same constants, same guard bits, same truncation, so the C, the VHDL
and the SystemVerilog produce identical integers. Lab 0 only asks you to choose
an application — the bit-exactness is Lab 1's problem — but read that chapter
before Lab 1. It is the shape your own work should have: one golden model, RTL
that matches it bit for bit, and a self-checking testbench that runs on more
than one simulator.

## Layout

```
SETUP.md TUTORIAL.md ASSIGNMENT.md   read them in that order
Makefile                 top-level wrapper; includes x-heep/external.mk
requirements.txt         Python packages (install after `make vendor`)
config.py                X-HEEP SoC configuration (CPU, memory, peripherals)
util/vendor.py           the vendoring tool (committed, so you can run it)
x-heep.vendor.hjson      what to vendor and at which revision (pinned)
x-heep.lock.hjson        the revision you actually got
sw/applications/<app>/   the four provided C apps
sw/external/kernels.h    the shared integer kernel library
sw/external/profile.h    cycle/instret counters + the PASS/FAIL self-check
sw/{device,linker,build} symlinks into x-heep/sw (created by `make vendor`)
x-heep/                  the vendored X-HEEP (gitignored, created by `make vendor`)
```

The layout follows the standard X-HEEP-based project (like `gr-heep`): apps and
shared headers live here; `make vendor` symlinks X-HEEP's `device`/`linker`/`build`
into `sw/` so an out-of-tree build finds the generated device headers. Target
names mirror X-HEEP's own, so you learn the real flow; each forwards to the
vendored X-HEEP with the right `PROJECT`, `SOURCE` and config.

## What you commit

`ASSIGNMENT.md`, filled in, and the flamegraphs under `figures/`. Never commit
`x-heep/`, `build/` or `flamegraph.svg` — they are generated, and they are in
`.gitignore`.
