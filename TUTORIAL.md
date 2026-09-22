# Lab 0 — Tutorial: `rxchain` end to end

A guided run of one application, `rxchain`, with the output you should get at
every step. Do this once, in order, before you start [ASSIGNMENT.md](ASSIGNMENT.md).

Prerequisite: [SETUP.md](SETUP.md) finished. Every command below runs from the
`lab0` directory, with the conda env active and `RISCV_XHEEP` set:

```bash
conda activate x-heep
export RISCV_XHEEP=$HOME/tools/riscv
```

`make help` lists every target.

---

## 1. What `rxchain` is

A complete digital receiver front-end, integer only, in
`sw/applications/rxchain/main.c`:

```
int16 samples (wanted tone + interferer)
  -> nco         cos/sin of a running phase, computed by CORDIC
  -> mixer       x*cos, x*sin  ->  I/Q at baseband
  -> fir x2      anti-alias low-pass on I and on Q
  -> decimate /4 throw away what the filter made redundant
  -> magnitude   sqrt(I^2+Q^2) by integer square root
  -> detect      count and accumulate samples above a threshold
```

Six kernels, six different shapes: a shift-add iteration, a multiply per
sample, a long MAC chain, pure addressing, a square root, a compare-and-count.
They all live in `sw/external/kernels.h`, which **all four applications share** —
`k_fir_i16` is the same function whether `rxchain` calls it or `tinydnn` does.
That is the point: the accelerator you build in Lab 1 replaces one of those
functions, and speeds up every app that calls it.

The app is **self-checking**: it prints `PASS` or `FAIL` against a golden
checksum, so the same source tells you it is correct on your laptop and on
Verilator (and, in Lab 3, on the FPGA).

---

## 2. Run it on your laptop first (5 seconds)

```bash
make host-check
```

```
pqcrypto: checksum=0x0001fbba PASS
rxchain: checksum=0x3d02c350 PASS
tinydnn: checksum=0xffffff90 PASS
tinyformer: checksum=0x0000016a PASS
```

The kernels are integer-only, so the host result is **bit-identical** to the
RISC-V one. Use this every time you change an app: seconds instead of minutes.
It prints no cycle counts — your laptop has no `mcycle` — and that is what the
simulated runs are for.

---

## 3. Build the SoC and the simulator (once)

```bash
make vendor            # only if ./x-heep is missing
make mcu-gen           # generate the MCU described by config.py
make verilator-build   # compile the RTL into a C++ simulator (~5 min)
```

`config.py` is the SoC: CPU (`cv32e20`), memory banks, peripherals. You may
change it — `make mcu-gen` regenerates everything from it. Do not change it in
Lab 0 without saying so in the report; your numbers must be comparable with the
other groups'.

You only repeat these two steps after editing `config.py` or re-vendoring.

---

## 4. Run the application

```bash
make verilator-run-app PROJECT=rxchain
```

Tail of the output (default build: kernels inlined, profiling `printf`s on):

```
Simulation finished after 994702 clock cycles
Program Finished with value 0
rxchain n=256 taps=24 dec=4 -> 64 out, 1 block(s)
energy: rf=132237  baseband I=34962  Q=64
[profile] kernel           cycles      instr  calls  share
[profile] total            559316     218171      1   100%
[profile] nco               98880      76768      1    17%
[profile] mixer             22600       5650      1     4%
[profile] fir              423858     128493      1    75%
[profile] decimate           1412        652      1     0%
[profile] magnitude         11774       6158      1     2%
[profile] detect              647        388      1     0%
rxchain: checksum=0x3d02c350 PASS
```

Read it carefully, because this table is the lab:

- **`total` is not the simulation length.** The sim ran 994702 cycles; the app
  itself is 559316. The difference is startup, `printf` over the UART and exit.
- **`fir` is 75%** of the application. Even a perfect FIR accelerator can only
  make this app `1/(1-0.75) = 4×` faster. That is Amdahl's ceiling.
- **`decimate` is 0%.** It *looks* like work — it walks the whole array — but it
  only copies one sample in four. Intuition about which kernel is expensive is
  usually wrong; that is why you measure.
- **`calls` is 1** because `NBLOCK=1`. Raise it and everything scales except the
  fixed startup cost.

> `make verilator-run` (without `-app`) re-runs **whatever binary was compiled
> last**. It will happily print another application's numbers. Always use
> `verilator-run-app PROJECT=<app>`, and check the app's own header line.

---

## 5. Profile method 2: the flamegraph

`make profile` feeds the waveform of the last run to `rv_profile`, which maps
program counters to functions and writes `flamegraph.svg`.

One catch: the kernels in `kernels.h` are `static inline`, so the compiler
inlines them into `main` and the flamegraph shows one box, `main`, at 99.98%.
To see per-kernel functions you must **turn inlining off for the profiling
build**:

```bash
make verilator-run-app PROJECT=rxchain COMPILER_FLAGS="-fno-inline -DPROFILE_QUIET"
make profile
open flamegraph.svg          # macOS; Linux/WSL: xdg-open, or just open it in a browser
```

`-DPROFILE_QUIET` compiles the profiling `printf`s out. They are expensive:
the `-fno-inline` `rxchain` build takes about **1.42 M simulated cycles** loud
and **1.08 M** quiet, so formatting the report and pushing it through the UART
costs roughly **340 000 cycles** — a quarter of the simulation, and half as much
as the 722 482 cycles the kernels themselves take. (Careful: those are two
different meters. The 1.42 M / 1.08 M are simulation lengths, the number in
`Simulation finished after N clock cycles`, and they move by a few hundred
cycles from host to host; 722 482 is `[profile] total`, the profiled region
only, and that one is exact — see §4.) On the flamegraph the report is one enormous
`printf` tower next to the code you care about; quiet, the graph shows kernels
only. The `PASS` line still prints, so you can still tell the run was correct.

Read the cycle table from a normal (loud) run, then re-run quiet for the
picture.

**Open it in a browser, not in an image viewer.** `flamegraph.svg` carries its
own JavaScript and is interactive: click a box to zoom into that subtree,
"Reset Zoom" (top left) to come back, `Ctrl-F` to search and highlight every
frame matching a pattern, hover for the exact cycle count. Preview, `eog` and
friends render it as a flat picture and you lose all of that.

On the server there is no browser: copy the file to your own machine first.

```bash
scp <your-user>@isaserver:~/<repo>/flamegraph.svg .
```

Run the same build **without** `-DPROFILE_QUIET` once (`COMPILER_FLAGS="-fno-inline"`)
and the cycle table comes back, with different numbers:

```
[profile] total            722482     349799      1   100%
[profile] nco              256257     208230      1    35%
[profile] fir              429216     128505      1    59%
```

`nco` went from 17% to 35% — not because the hardware changed, but because
inlining was what made the CORDIC cheap. **Quote the shares from the normal
(inlined) build in your report, and use the flamegraph only to attribute cycles
to functions.** Knowing why the two disagree is part of the assignment (Q4.1).

---

## 6. Profile method 3: waveforms

```bash
make verilator-run-app PROJECT=rxchain
make verilator-waves
```

GTKWave opens the `.fst` of the last run. Waveforms are a **debugging aid** in
this lab, not a deliverable: use them to check that the kernel is where you
think it is (instruction fetches, memory traffic during the loop). You will
need them for real in Labs 1–3.

---

## 7. Change the size of the problem

Every app has compile-time knobs:

| App | Shape | Repetition |
|-----|-------|------------|
| `rxchain` | `NSAMP` (256), `TAPS` (24), `DEC` (4) | `NBLOCK` (1) |
| `tinydnn` | fixed network | `NINFER` (1) |
| `tinyformer` | fixed 2-layer encoder | `NINFER` (1) |
| `pqcrypto` | `N` (256, ring degree) | `NROUND` (1) |

The defaults are small so an RTL simulation finishes in about a minute. Lab 0
runs entirely on Verilator: keep the sizes small, and raise a knob only to see
what it does to the per-kernel shares. (In Lab 3 the same apps run on an FPGA,
where a large size costs microseconds instead of simulated minutes.)

**Any size change changes the golden checksum**, so a run at a new size prints
`FAIL` until you supply the new value. Get it from the host build, which takes
seconds, and pass it to the target build:

```bash
make host-check HOST_CFLAGS=-DNBLOCK=10
# rxchain: checksum=0x???????? expected=0x3d02c350 FAIL   <- the new checksum is the one it got

make verilator-run-app PROJECT=rxchain \
     COMPILER_FLAGS="-DNBLOCK=10 -DGOLDEN=0x????????"
```

Same flags on both sides, always: the host and the target agree bit for bit
only if they compute the same thing.

---

## 8. Cheat sheet

```bash
make help                                     # all targets
make host-check [HOST_CFLAGS=-DNBLOCK=10]     # laptop build + self-check, prints checksums
make mcu-gen                                  # regenerate the SoC from config.py
make verilator-build                          # build the simulator (after mcu-gen)
make verilator-run-app PROJECT=<app>          # compile <app> + simulate it
make app PROJECT=<app> COMPILER_FLAGS="..."   # compile only
make profile                                  # flamegraph.svg from the last run
make verilator-waves                          # GTKWave on the last run
```

Apps: `rxchain`, `tinydnn`, `tinyformer`, `pqcrypto`.

Now do [ASSIGNMENT.md](ASSIGNMENT.md).
