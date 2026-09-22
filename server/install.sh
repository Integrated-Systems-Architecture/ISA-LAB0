#!/bin/bash
# ISA labs - install the shared toolchain. Run as root on isaserver (CentOS 7).
#
#     mkdir -p /oss-tools
#     cp install.sh init.sh requirements.txt /oss-tools/
#     bash /oss-tools/install.sh 2>&1 | tee /oss-tools/install.log
#
# ROOT is the directory this script lives in, so the tree can be copied
# elsewhere and reinstalled without editing anything. Conda bakes its own
# absolute prefix into the env, so a finished install cannot be moved: copy the
# scripts to the final path and install there.
#
# Updating an installed tree: re-running this script skips every finished
# step, and it never re-copies init.sh -- after editing init.sh, copy it over
# by hand (as root):  cp init.sh /oss-tools/init.sh
#
# Idempotent: each step is skipped if its product is already in place.
# Re-run one step from scratch by deleting its directory (conda/, src/,
# verilator/, riscv/, verible/) first.

set -euo pipefail   # pipefail matters: the tarballs are streamed curl | tar

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
JOBS="$(nproc)"

VERILATOR_VERSION=5.040
VERIBLE_VERSION=v0.0-4023-gc1271a00
MINICONDA_URL=https://repo.anaconda.com/miniconda/Miniconda3-py311_23.11.0-2-Linux-x86_64.sh
# CORE-V GCC 14.1.0, CentOS 7 build (system glibc is 2.17: newer builds abort
# with "GLIBC_2.25 not found"). The prefix riscv32-corev-elf- is what X-HEEP
# expects.
COREV_URL="https://buildbot.embecosm.com/job/corev-gcc-centos7/48/artifact/corev-openhw-gcc-centos7-20240530.tar.gz"

echo "### installing into $ROOT"
mkdir -p "$ROOT/downloads"

# The finished tree is ~6 GB (conda 2.5 + CORE-V gcc 2 + verilator build 1).
# Checking now beats failing halfway with "curl: (23) Failed writing body".
FREE_KB="$(df -Pk "$ROOT" | awk 'NR==2 {print $4}')"
if [ "$FREE_KB" -lt 8000000 ]; then
    echo "### only $((FREE_KB/1024/1024)) GB free on $ROOT -- need ~8 GB" >&2
    exit 1
fi

# X-HEEP's verilator testbench links -lelf, and no conda package in the env
# provides it: it has to come from the distribution. Check now -- otherwise the
# first student build dies at the link step, 15 minutes in. init.sh puts
# /usr/lib64 on the model's link line (see the USER_LDFLAGS comment there).
if [ ! -e /usr/lib64/libelf.so ]; then
    echo "### missing libelf: yum install -y elfutils-libelf-devel" >&2
    exit 1
fi

# --- 1. miniconda + the x-heep env -------------------------------------------
# Verilator is NOT taken from conda: X-HEEP parses `verilator --version` and
# needs the "rev vX.Y" string only an upstream build prints. gtkwave, git,
# cmake and make come from conda because CentOS 7 ships versions that are too
# old for X-HEEP.
if [ ! -x "$ROOT/conda/bin/conda" ]; then
    echo "### 1. miniconda"
    curl -sSL -o "$ROOT/downloads/miniconda.sh" "$MINICONDA_URL"
    bash "$ROOT/downloads/miniconda.sh" -b -p "$ROOT/conda" -f
    "$ROOT/conda/bin/conda" config --set always_yes true
fi

if [ ! -x "$ROOT/conda/envs/x-heep/bin/python" ]; then
    echo "### 2. conda env x-heep"
    # zlib: Verilator's FST tracing includes <zlib.h> when the MODEL is compiled,
    # and conda's g++ does not see /usr/include. python pulls zlib in anyway,
    # but naming it here keeps that accident from becoming a broken install.
    "$ROOT/conda/bin/conda" create -n x-heep -c conda-forge \
        python=3.11 gtkwave git cmake make zlib
fi

source "$ROOT/conda/etc/profile.d/conda.sh"
conda activate x-heep

# --- 2. python packages -------------------------------------------------------
# CMAKE_POLICY_VERSION_MINIMUM: pylibfst (pulled in by rv_profile) still
# declares compatibility with CMake 3.0, which CMake >= 4 refuses.
if ! command -v fusesoc >/dev/null; then
    echo "### 3. pip packages"
    export CMAKE_POLICY_VERSION_MINIMUM=3.5
    pip install -r "$ROOT/requirements.txt"
fi

# --- 3. verilator from source -------------------------------------------------
# CentOS 7's system g++ is 4.8.5, far too old. Build with conda's gcc 13 and
# rpath its libstdc++, otherwise the binary needs a GLIBCXX the host lacks.
if [ ! -x "$ROOT/verilator/$VERILATOR_VERSION/bin/verilator" ]; then
    echo "### 4. verilator $VERILATOR_VERSION"
    conda install -y -c conda-forge gxx_linux-64=13 autoconf flex bison help2man perl
    export CC="$CONDA_PREFIX/bin/x86_64-conda-linux-gnu-gcc"
    export CXX="$CONDA_PREFIX/bin/x86_64-conda-linux-gnu-g++"
    export CPPFLAGS="-I$CONDA_PREFIX/include"
    export CXXFLAGS="-I$CONDA_PREFIX/include"
    export LDFLAGS="-L$CONDA_PREFIX/lib -Wl,-rpath,$CONDA_PREFIX/lib"
    mkdir -p "$ROOT/src"
    rm -rf "$ROOT/src/verilator"
    git clone -q --branch "v$VERILATOR_VERSION" --depth 1 \
        https://github.com/verilator/verilator.git "$ROOT/src/verilator"
    cd "$ROOT/src/verilator"
    autoconf
    ./configure --prefix="$ROOT/verilator/$VERILATOR_VERSION"
    make -j"$JOBS"
    make install
    unset CC CXX CPPFLAGS CXXFLAGS LDFLAGS
fi

# --- 4. RISC-V compiler -------------------------------------------------------
if [ ! -x "$ROOT/riscv/bin/riscv32-corev-elf-gcc" ]; then
    echo "### 5. CORE-V gcc"
    mkdir -p "$ROOT/riscv"
    # Streamed into tar: the tarball is ~1 GB and never needs to exist on disk.
    # `curl: (23) Failed writing body` here means the filesystem is full.
    curl -fsSL "$COREV_URL" | tar -xz --strip-components=1 -C "$ROOT/riscv"
fi

# --- 5. verible ---------------------------------------------------------------
# mcu-gen formats the RTL it generates and fails outright without it.
if [ ! -x "$ROOT/verible/bin/verible-verilog-format" ]; then
    echo "### 6. verible $VERIBLE_VERSION"
    mkdir -p "$ROOT/verible"
    curl -fsSL "https://github.com/chipsalliance/verible/releases/download/${VERIBLE_VERSION}/verible-${VERIBLE_VERSION}-linux-static-x86_64.tar.gz" \
        | tar -xz --strip-components=1 -C "$ROOT/verible"
fi

# --- 6. permissions -----------------------------------------------------------
# a+rX: read for all, execute only on directories and on files that already
# have it. src/ and downloads/ are build leftovers, nobody needs them.
echo "### 7. permissions"
rm -rf "$ROOT/src" "$ROOT/downloads"   # build leftovers, nobody needs them
chown -R root:root "$ROOT"
chmod -R a+rX "$ROOT"
# students must be able to traverse into ROOT
d="$ROOT"
while [ "$d" != "/" ]; do chmod o+x "$d"; d="$(dirname "$d")"; done

# --- 7. verify ----------------------------------------------------------------
# Run the real init.sh in a clean shell as an unprivileged user: this is the
# only check that proves a student can use the install.
echo "### 8. verify"
su -s /bin/bash nobody -c "source '$ROOT/init.sh' >/dev/null && \
    python --version && \
    verilator --version | grep -q 'rev v' && verilator --version && \
    riscv32-corev-elf-gcc --version | head -1 && \
    fusesoc --version && \
    rv_profile --help >/dev/null && echo 'rv_profile ok' && \
    verible-verilog-format --version | head -1 && \
    gtkwave --version 2>/dev/null | head -1"

echo
echo "### done. students run:  source $ROOT/init.sh"
