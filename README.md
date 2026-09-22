# Lab 0 — Profiling and first accelerator ideas

MSc lab, groups of 3. Over four activities you profile C applications running on
the **X-HEEP** RISC-V microcontroller, pick one, and build a custom hardware
accelerator for it:

| | | |
|---|---|---|
| **Lab 0** | profile the provided apps and work out which kernel is worth accelerating | *this repository* |
| Lab 1 | implement the accelerator standalone (RTL + testbench), simulate and synthesize | |
| Lab 2 | optimize it (retiming, pipelining, folding, arithmetic) and compare PPA with Lab 1 | |
| Lab 3 | integrate it into X-HEEP over OBI+REG, write the C driver, measure the real speedup | |

## Start here

1. **[SETUP.md](SETUP.md)** — the toolchain. On the **ISA server** it is already
   installed: source the environment script and you are done (first section of
   SETUP.md). On your own machine (macOS, Ubuntu/Linux, WSL2) budget about an
   hour.
2. **[TUTORIAL.md](TUTORIAL.md)** — a guided run of one application,
   `rxchain`, with the output you should see at every step.
3. **[ASSIGNMENT.md](ASSIGNMENT.md)** — what you must do and hand in. Fill it
   in and commit it; that file *is* the report. Its last step asks for your
   first ideas about an accelerator, not a design: the accelerator is chosen
   together at the start of Lab 1.

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
`PASS` or `FAIL`, so the same source tells you it is correct on your laptop and
on Verilator — and, in Lab 3, on the FPGA.

## Tools you must understand (not just invoke)

The Makefile wraps them, but you will be asked to explain what they do:

- **vendor** (`util/vendor.py` + `x-heep.vendor.hjson`) — pulls a pinned X-HEEP
  snapshot into `./x-heep`, reproducibly.
- **FuseSoC** — builds the SoC and the simulation model from `.core` files.
- **reggen** — generates the register interface (CSRs) and the C HAL from an
  hjson description; you need it in Lab 3 for the OBI+REG path.

## Reference material

The course cookbooks live in their own repository,
<https://github.com/Integrated-Systems-Architecture/ISA-BOOKS> (PDFs included;
in the course repository they are the `books/` submodule — `git submodule
update --init books`). They cover exactly these tools:

| Question | Where |
|----------|-------|
| What is FuseSoC doing? What is a `.core` file? | *FuseSoC Cookbook*, ch. 1 |
| What did `make vendor` do, and why not a submodule? | *FuseSoC Cookbook*, ch. 2 |
| How is this Makefile put together? | *FuseSoC Cookbook*, ch. 3 |
| `reggen` / register interfaces (Lab 3) | *FuseSoC Cookbook*, ch. 4 |
| Verilator, waveforms, self-checking testbenches | *Simulation Cookbook*, ch. 8 |
| Python-driven verification, cocotb | *Simulation Cookbook*, ch. 9 |
| The CORDIC case study — golden model, VHDL, SystemVerilog, six testbenches | *Example Cookbook*, ch. 10, code in `examplecookbook/code/cordic/` |
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

## Papers: how others built it

Before you commit to a kernel in Step 8 of the assignment, look at how others
accelerated it: what they put in hardware, what they left in software, and what
it cost. Skim the abstracts and figures; you do not need to read them all.

Links go to IEEE Xplore: full text from the Politecnico network or the VPN,
and most have an author copy or arXiv version if you search the title. The
master list, with a note on what to take from each paper, is
[`READING.md` in ISA-BOOKS](https://github.com/Integrated-Systems-Architecture/ISA-BOOKS/blob/main/READING.md) (`books/READING.md` in the course
repository).

**Why a memory-mapped accelerator with its own data port** — read one of these first

- [An Analysis of Accelerator Coupling in Heterogeneous Architectures](https://ieeexplore.ieee.org/document/7167228) — Cota, …, Carloni, DAC 2015. *in-pipeline vs bus-attached accelerators, and when each wins.*
- [Agile SoC Development with Open ESP](https://ieeexplore.ieee.org/document/9256819) — Mantovani, …, Carloni, ICCAD 2020. *registers + DMA + interrupt socket around an accelerator.*
- [X-HEEP: An Open-Source, Configurable and Extendible RISC-V Platform for TinyAI Applications](https://ieeexplore.ieee.org/document/11130281) — Machetti, Schiavone, …, Atienza, ISVLSI 2025. *the platform you integrate into.*
- [An IoT Endpoint SoC for Secure and Energy-Efficient Near-Sensor Analytics (Fulmine)](https://ieeexplore.ieee.org/document/7927716) — Conti, …, Rossi, Benini, TCAS-I 2017. *conv and crypto engines driven by memory-mapped registers.*
- [Scalable and RISC-V Programmable Near-Memory Computing Architectures for Edge Nodes](https://ieeexplore.ieee.org/document/10964076) — Caon, …, Masera, Martina, Atienza, TETC 2025. *accelerators on the X-HEEP bus as OBI slaves.*
- [X-TRELA: An Open-Source Streaming Elastic CGRA With ASIC Implementation for the Edge](https://ieeexplore.ieee.org/document/11577129) — Vázquez, Miranda, Rodríguez, Otero (UPM), IEEE Access 2026. *a CGRA on X-HEEP, taped out, open source.*

**rxchain** — CORDIC, FIR, decimation, isqrt

- [50 Years of CORDIC: Algorithms, Architectures, and Applications](https://ieeexplore.ieee.org/document/5089431) — Meher et al., TCAS-I 2009. *iterative vs unrolled vs pipelined, scale factor.*
- [The CORDIC Trigonometric Computing Technique](https://ieeexplore.ieee.org/document/5222693) — Volder, IRE Trans. Electronic Computers 1959. *the original.*
- [Uniformly Distributed CORDIC](https://ieeexplore.ieee.org/document/10972359) — Garrido, Medina, Paz, López-Vallejo (UPM), TCAS-I 2025. *a recent rotator design.*
- [Methods of Mapping from Phase to Sine Amplitude in Direct Digital Synthesis](https://ieeexplore.ieee.org/document/585137) — Vankka, IEEE Trans. UFFC 1997. *NCO alternatives to CORDIC.*
- [An Economical Class of Digital Filters for Decimation and Interpolation](https://ieeexplore.ieee.org/document/1163535) — Hogenauer, TASSP 1981. *CIC: decimation without multipliers.*
- [Applications of Distributed Arithmetic to Digital Signal Processing: A Tutorial Review](https://ieeexplore.ieee.org/document/29648) — White, IEEE ASSP Magazine 1989. *FIR with lookup tables instead of multipliers.*
- [Subexpression Sharing in Filters Using Canonic Signed Digit Multipliers](https://ieeexplore.ieee.org/document/539000) — Hartley, TCAS-II 1996. *fixed coefficients as shared shifts and adds.*
- [Use of Minimum-Adder Multiplier Blocks in FIR Digital Filters](https://ieeexplore.ieee.org/document/466647) — Dempster, Macleod, TCAS-II 1995. *multiple-constant multiplication.*
- [A New Non-Restoring Square Root Algorithm and Its VLSI Implementations](https://ieeexplore.ieee.org/document/563604) — Li, Chu, ICCD 1996. *isqrt, iterative and pipelined.*

**tinydnn** — convolution and linear layers

- [Why Systolic Architectures?](https://ieeexplore.ieee.org/document/1653825) — Kung, IEEE Computer 1982. *operand reuse, the founding argument.*
- [Efficient Processing of Deep Neural Networks: A Tutorial and Survey](https://ieeexplore.ieee.org/document/8114708) — Sze, Chen, Yang, Emer, Proc. IEEE 2017. *loop nests and dataflows.*
- [Eyeriss: A Spatial Architecture for Energy-Efficient Dataflow for CNNs](https://ieeexplore.ieee.org/document/7551407) — Chen, Emer, Sze, ISCA 2016. *dataflow comparison.*
- [Eyeriss: An Energy-Efficient Reconfigurable Accelerator for Deep CNNs](https://ieeexplore.ieee.org/document/7738524) — Chen, Krishna, Emer, Sze, JSSC 2017. *the chip.*
- [Vega: A Ten-Core SoC for IoT Endnodes With DNN Acceleration and Cognitive Wake-Up](https://ieeexplore.ieee.org/document/9560136) — Rossi, …, Benini, JSSC 2022. *a conv engine next to the cores, in silicon.*
- [Marsellus: A Heterogeneous RISC-V AI-IoT End-Node SoC With 2–8 b DNN Acceleration](https://ieeexplore.ieee.org/document/10269153) — Conti, …, Benini, JSSC 2024. *bit-width as a design parameter.*
- [Self-Reconfigurable Evolvable Hardware System for Adaptive Image Processing](https://ieeexplore.ieee.org/document/6494560) — Salvador, Otero, Mora, de la Torre, Riesgo (UPM), Sekanina, TC 2013. *a systolic PE array for 2-D windows.*

**tinyformer** — matmul, softmax, attention (the matmuls are ~80% of the run: start there)

- [ITA: An Energy-Efficient Attention and Softmax Accelerator for Quantized Transformers](https://ieeexplore.ieee.org/document/10244348) — Islamoglu, …, Benini, ISLPED 2023. *int8 attention, integer softmax.*
- [Toward Attention-Based TinyML: A Heterogeneous Accelerated Architecture and Automated Deployment Flow](https://ieeexplore.ieee.org/document/10833747) — Wiese, …, Conti, Benini, IEEE D&T 2025. *ITA next to RISC-V cores, whole networks.*
- [Softermax: Hardware/Software Co-Design of an Efficient Softmax for Transformers](https://ieeexplore.ieee.org/document/9586134) — Stevens et al., DAC 2021. *softmax for integer hardware.*
- [A^3: Accelerating Attention Mechanisms in Neural Networks with Approximation](https://ieeexplore.ieee.org/document/9065498) — Ham et al., HPCA 2020. *attention as a pipeline.*
- [SpAtten: Efficient Sparse Attention Architecture with Cascade Token and Head Pruning](https://ieeexplore.ieee.org/document/9407232) — Wang, Zhang, Han, HPCA 2021. *skipping unimportant work.*
- [RedMulE: A Compact FP16 Matrix-Multiplication Accelerator](https://ieeexplore.ieee.org/document/9774759) — Tortorella, …, Conti, DATE 2022. *matmul engine organization (FP16).*
- [A Flexible Template for Edge Generative AI With High-Accuracy Accelerated Softmax and GELU](https://ieeexplore.ieee.org/document/10971415) — Belano, …, Conti, Benini, JETCAS 2025. *softmax/GELU next to a matmul engine (BF16).*

**pqcrypto** — NTT and modular arithmetic (an NTT is an FFT modulo q: the FFT papers apply)

- [An Extensive Study of Flexible Design Methods for the Number Theoretic Transform](https://ieeexplore.ieee.org/document/9171507) — Mert et al., TC 2022. *the NTT hardware tutorial.*
- [High-Speed Polynomial Multiplication Architecture for Ring-LWE and SHE Cryptosystems](https://ieeexplore.ieee.org/document/6918547) — Chen, …, Roy, Verbauwhede, TCAS-I 2015. *pipelined NTT/INTT.*
- [KaLi: A Crystal for Post-Quantum Security Using Kyber and Dilithium](https://ieeexplore.ieee.org/document/9946370) — Aikata, …, Pagliarini, Roy, TCAS-I 2023. *one NTT datapath for Kyber and Dilithium.*
- [High-Speed NTT-based Polynomial Multiplication Accelerator for Post-Quantum Cryptography](https://ieeexplore.ieee.org/document/9603378) — Bisheh-Niasar, Azarderakhsh, Mozaffari-Kermani, ARITH 2021. *Kyber NTT, several butterfly cores.*
- [An Energy-Efficient Configurable Lattice Cryptography Processor for the Quantum-Secure IoT](https://ieeexplore.ieee.org/document/8662528) — Banerjee, Pathak, Chandrakasan, ISSCC 2019. *low-power lattice crypto in silicon.*
- [A New Approach to Pipeline FFT Processor](https://ieeexplore.ieee.org/document/508145) — He, Torkelson, IPPS 1996. *single-delay-feedback pipeline.*
- [Pipelined Radix-2^k Feedforward FFT Architectures](https://ieeexplore.ieee.org/document/6118316) — Garrido, Grajal, Sánchez (UPM), Gustafsson, TVLSI 2013. *parallel feedforward pipelines.*
- [Analyzing and Comparing Montgomery Multiplication Algorithms](https://ieeexplore.ieee.org/document/502403) — Koç, Acar, Kaliski, IEEE Micro 1996. *Montgomery reduction.*

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

## What you hand in

1. Commit `ASSIGNMENT.md`, filled in, and the flamegraphs under `figures/`.
   Never commit `x-heep/`, `build/` or `flamegraph.svg` — they are generated,
   and they are in `.gitignore`.
2. **Publish a release of your repository.** The release is the submission: we
   read the code and the report at that tag, so anything committed afterwards
   does not count.

```bash
git add ASSIGNMENT.md figures/
git commit -m "Lab 0: profiling results and accelerator candidates"
git push
gh release create lab0-final --title "Lab 0" --notes "Profiling results and accelerator candidates"
```

Without the `gh` CLI, do the same from the repository page on GitHub:
*Releases → Draft a new release → tag* `lab0-final` *→ Publish release*.

One release per group, from the group repository. Late fixes mean a new release
— tell us, because we take the latest one before the deadline.

See the *Git Cookbook*, ch. 3, for tags, releases and the group workflow.
