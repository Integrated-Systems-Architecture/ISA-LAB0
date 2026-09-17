# Lab 0 — Setup

Everything you need to build the X-HEEP SoC, compile the lab applications for
RISC-V and simulate them. Do this **once per machine**, before the tutorial.

Budget about an hour, most of it unattended downloads and compiles.

Supported hosts: **Ubuntu/Debian Linux**, **Windows + WSL2 (Ubuntu)**, **macOS
(Apple Silicon or Intel)**. One machine per group is enough to get started, but
everyone should end up with a working install.

---

## 0. What you are installing, and why

| Tool | Version | Used for |
|------|---------|----------|
| Python + conda env | 3.12 | `fusesoc` (SoC build), `mcu-gen`, `rv_profile` (flamegraph) |
| RISC-V GCC | CORE-V toolchain (`riscv32-corev-elf-`) | compiling the lab apps for the CPU |
| Verilator | **5.040** | the RTL simulator you run the apps on |
| GTKWave | any recent | looking at waveforms |
| host C compiler | gcc or clang, already on your machine | `make host-check` (seconds instead of minutes) |

Two environment variables must be set in every shell you work in:

```bash
export RISCV_XHEEP=$HOME/tools/riscv          # where the RISC-V toolchain is installed
export PATH=$HOME/tools/verilator/5.040/bin:$PATH
conda activate x-heep                         # the Python env you create in step 2
```

Put the two `export` lines in `~/.bashrc` (Linux/WSL) or `~/.zshrc` (macOS) so
you do not have to remember them.

---

## 1. OS packages

### Ubuntu / Debian / WSL2

```bash
sudo apt update
sudo apt install -y git make cmake autoconf automake autotools-dev curl \
  build-essential g++ ccache bison flex gawk texinfo gperf libtool patchutils bc \
  zlib1g-dev libexpat-dev libmpc-dev libmpfr-dev libgmp-dev ninja-build \
  libglib2.0-dev help2man perl libfl-dev libelf-dev numactl gtkwave
```

**WSL2 only:** work inside the Linux filesystem (`~/...`), **not** under
`/mnt/c/...`. Builds on `/mnt/c` are 5–10× slower and symlinks (which this
project uses in `sw/`) behave differently. To see GTKWave windows you need
WSLg (Windows 11, default) or an X server on Windows 10.

### macOS

```bash
# Xcode command line tools + Homebrew packages
xcode-select --install
brew install git make cmake autoconf automake gawk gnu-sed coreutils \
             perl help2man ccache
```

GTKWave on macOS is easiest from the **OSS CAD Suite** (you will also want it
in Lab 2 for Yosys/OpenROAD): download the latest release for your architecture
from <https://github.com/YosysHQ/oss-cad-suite-build/releases>, unpack it into
`~/tools/oss-cad-suite`, and add `~/tools/oss-cad-suite/bin` to your `PATH`.

---

## 2. Python environment

Install [Miniconda](https://www.anaconda.com/docs/getting-started/miniconda/install)
if you do not have conda. Then, **from the `lab0` directory of your repository**:

```bash
conda create -y -n x-heep python=3.12
conda activate x-heep
pip install hjson                          # needed by util/vendor.py, which runs next
make vendor                                # pulls X-HEEP into ./x-heep (a snapshot, no .git)
export CMAKE_POLICY_VERSION_MINIMUM=3.5    # see note below
pip install -r requirements.txt
```

`requirements.txt` installs `fusesoc` and `edalize` (which build the SoC), the
`mako`/`hjson`/`black` tooling `mcu-gen` needs, and `rv_profile` (the flamegraph
profiler). It is X-HEEP's own list minus `yamlfmt`, which nothing uses and which
no longer builds on modern Python.

> **`CMAKE_POLICY_VERSION_MINIMUM=3.5`**: `rv_profile` depends on `pylibfst`,
> whose build script still declares compatibility with CMake 3.0. CMake 4
> refuses that unless you set this variable. Without it you get
> `Compatibility with CMake < 3.5 has been removed` and no profiler.

> X-HEEP is **not** committed to your repository. `make vendor` downloads the
> exact revision pinned in `x-heep.vendor.hjson` into `./x-heep`, which is in
> `.gitignore`. Re-run `make vendor` if you ever delete it.

Check:

```bash
fusesoc --version && rv_profile --help | head -1
```

Activate this environment (`conda activate x-heep`) in **every** shell you use
for the labs.

## 3. RISC-V toolchain

X-HEEP's default compiler is the **CORE-V GCC** toolchain from Embecosm
(prefix `riscv32-corev-elf-`). Prebuilt binaries exist for Linux and macOS —
use them, do not build GCC from source.

1. Open <https://embecosm.com/downloads/tool-chain-downloads/#corev> and
   download the **CORE-V RISC-V GCC** package for your OS
   (Ubuntu/WSL: the Linux x86_64 tarball; macOS: the macOS tarball matching
   Intel or Apple Silicon).
2. Unpack it and install it under `~/tools/riscv`:

```bash
mkdir -p ~/tools/riscv
tar -xf corev-openhw-gcc-*.tar.gz --strip-components=1 -C ~/tools/riscv
export RISCV_XHEEP=$HOME/tools/riscv
```

3. Check:

```bash
$RISCV_XHEEP/bin/riscv32-corev-elf-gcc --version
```

X-HEEP derives the compiler prefix from `$RISCV_XHEEP/bin/*gcc`, so as long as
that binary is there nothing else needs configuring. On macOS the first run may
be blocked by Gatekeeper; allow it in *System Settings → Privacy & Security*.

**Alternative:** the plain `riscv32-unknown-elf-` toolchain also works
(`make app COMPILER_PREFIX=riscv32-unknown-`), but then you lose the CORE-V
extensions you may want in Lab 3.

---

## 4. Verilator 5.040

Distribution packages are usually too old (Ubuntu 22.04 ships 4.x). Build the
version X-HEEP is tested with:

```bash
export VERILATOR_VERSION=5.040
git clone https://github.com/verilator/verilator.git ~/src/verilator
cd ~/src/verilator
git checkout v$VERILATOR_VERSION
autoconf
./configure --prefix=$HOME/tools/verilator/$VERILATOR_VERSION
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
make install
export PATH=$HOME/tools/verilator/$VERILATOR_VERSION/bin:$PATH
```

Check:

```bash
verilator --version      # expect: Verilator 5.040
```

Use a compiler that is at least g++ 13 (Linux/WSL) or the Apple clang that
comes with the command line tools (macOS). `brew install verilator` may give
you a different 5.x release; it usually works, but the course only supports
5.040.

---

## 5. Verify the whole flow

From `lab0`, with the conda env active and `RISCV_XHEEP` set:

```bash
make host-check                            # ~5 s   builds+runs all 4 apps on your laptop
make mcu-gen                               # ~30 s  generates the SoC from config.py
make verilator-build                       # ~5 min builds the simulation model
make verilator-run-app PROJECT=rxchain     # ~1 min compiles rxchain and simulates it
make profile                               # ~2 min flamegraph.svg from the last run
```

The setup is correct when `make host-check` prints four `PASS` lines and the
simulation ends with:

```
rxchain: checksum=0x3d02c350 PASS
```

Now go to [TUTORIAL.md](TUTORIAL.md).

---

## 6. When it breaks

| Symptom | Cause / fix |
|---------|-------------|
| `ModuleNotFoundError: No module named 'hjson'` | conda env not activated, or step 2 not done: `conda activate x-heep && pip install hjson` |
| `Compatibility with CMake < 3.5 has been removed` while installing | `export CMAKE_POLICY_VERSION_MINIMUM=3.5` and re-run the `pip install` |
| `Failed building wheel for ruamel.yaml` | you installed `x-heep/util/python-requirements.txt` instead of this project's `requirements.txt` |
| `WARNING: RISCV_XHEEP not set in environment ... Using default: ~/.riscv` | `export RISCV_XHEEP=$HOME/tools/riscv` |
| `riscv32-corev-elf-gcc: not found` | toolchain not unpacked into `$RISCV_XHEEP`, or the tarball had an extra top directory — check `ls $RISCV_XHEEP/bin` |
| `make: *** No rule to make target 'verilator-build'` | `make vendor` was not run (`x-heep/external.mk` does not exist yet) |
| fusesoc/verilator errors mentioning `%Error: ... unsupported` | wrong Verilator version — `verilator --version` must say 5.040 |
| `make profile` says `no .fst under x-heep/build` | run `make verilator-run-app PROJECT=<app>` first; the profiler reads that run's waveform |
| `rv_profile: command not found` | conda env not activated, or `pip install -r requirements.txt` not done |
| GTKWave opens nothing on WSL | no WSLg/X server; copy the `.fst` to Windows and open it with a Windows GTKWave build |
| Everything is slow under WSL | repository is on `/mnt/c`; move it to `~` |
| `realpath: illegal option -- -` (macOS) | you overrode `SOURCE`; leave the Makefile default (`../../sw/`) |

Stuck for more than 30 minutes on one error: ask, do not reinstall everything.
