# ISA labs - environment for X-HEEP on isaserver.
#
#   source /oss-tools/init.sh
#
# Sets up: conda env `x-heep` (python, fusesoc, rv_profile), Verilator, GTKWave
# and the RISC-V compiler. Run it in every new shell, or add the line above to
# your ~/.bashrc. Nothing is installed in your home: everything is read from
# this shared, read-only directory. Your builds stay in your own repository.

# --- locate this file, however it was sourced ---------------------------------
# Everything below is relative to it, so the tree can be moved without edits.
if [ -n "$BASH_SOURCE" ]; then
    _isa_self="${BASH_SOURCE[0]}"
elif [ -n "$ZSH_VERSION" ]; then
    _isa_self="${(%):-%x}"
else
    _isa_self="$0"
fi
ISA_TOOLS="$(cd "$(dirname "$_isa_self")" && pwd)"
export ISA_TOOLS
unset _isa_self

if [ ! -x "$ISA_TOOLS/conda/bin/conda" ]; then
    echo "ISA: $ISA_TOOLS does not look like an installed toolchain" >&2
    return 1
fi

# --- python environment (conda) ----------------------------------------------
__conda_setup="$("$ISA_TOOLS/conda/bin/conda" shell.bash hook 2>/dev/null)"
if [ $? -eq 0 ]; then
    eval "$__conda_setup"
else
    . "$ISA_TOOLS/conda/etc/profile.d/conda.sh"
fi
unset __conda_setup
conda activate x-heep || { echo "ISA: cannot activate the x-heep conda env" >&2; return 1; }

# --- keep conda's host compiler out of the way --------------------------------
# The env carries a modern g++ (Verilator needs one, and its generated Makefile
# refers to it by absolute path). But conda also EXPORTS CC/CXX/GCC/OBJCOPY/...
# pointing at that x86 toolchain, and X-HEEP's makefiles pick them up with `?=`
# — the boot ROM then gets assembled by the host assembler and every RISC-V
# instruction is reported as "no such instruction". Drop those exports; nothing
# in the labs needs them.
unset ADDR2LINE AR AS BUILD CC CFLAGS CPP CPPFLAGS CXX CXXFLAGS \
      DEBUG_CFLAGS DEBUG_CXXFLAGS GCC GXX HOST LD LDFLAGS NM OBJCOPY \
      OBJDUMP RANLIB READELF STRIP

# --- host headers for the Verilator model -------------------------------------
# The model is compiled with conda's g++, whose sysroot is conda's own: it never
# looks in /usr/include. Verilator's FST tracing pulls in <zlib.h>, and the
# unset above removed the CPPFLAGS that normally carry -I$CONDA_PREFIX/include,
# so the build dies with "fatal error: zlib.h: No such file or directory".
# USER_CPPFLAGS / USER_LDFLAGS are Verilator's own variables (verilated.mk), so
# they reach the model build and nothing else — in particular not the RISC-V
# app build, which is exactly why the generic CPPFLAGS must stay unset.
#
# /usr/lib64 is there for -lelf: X-HEEP's testbench loads the ELF itself and
# links libelf unconditionally, conda has no libelf package in this env, and
# the system one (elfutils-libelf-devel) is outside conda's sysroot so the
# cross-ld never finds it. Safe to mix: conda's sysroot is CentOS 7 glibc 2.17,
# the same glibc these libraries were built against. Conda's -L comes first, so
# anything conda does ship (libz, libstdc++) still wins.
export USER_CPPFLAGS="-I$CONDA_PREFIX/include"
export USER_LDFLAGS="-L$CONDA_PREFIX/lib -Wl,-rpath,$CONDA_PREFIX/lib -L/usr/lib64"

# --- RISC-V compiler ----------------------------------------------------------
# X-HEEP derives the compiler prefix from $RISCV_XHEEP/bin/*gcc, so pointing
# this at the toolchain root is all that is needed.
export RISCV_XHEEP="$ISA_TOOLS/riscv"
export PATH="$RISCV_XHEEP/bin:$PATH"

# --- simulation ---------------------------------------------------------------
# Verilator is built from source (X-HEEP parses `verilator --version` and needs
# the "rev vX.Y" string that only an upstream build prints). GTKWave comes from
# the conda env.
export VERILATOR_ROOT="$ISA_TOOLS/verilator/5.040/share/verilator"
export PATH="$ISA_TOOLS/verilator/5.040/bin:$PATH"

# --- RTL formatting -----------------------------------------------------------
# `mcu-gen` runs `verible-verilog-format` on the generated RTL.
export PATH="$ISA_TOOLS/verible/bin:$PATH"

echo "ISA labs environment ready:"
echo "  python    $(python --version 2>&1 | awk '{print $2}')"
echo "  verilator $(verilator --version 2>/dev/null | awk '{print $2}')"
_isa_rvgcc="$(ls "$RISCV_XHEEP"/bin/*elf-gcc 2>/dev/null | head -1)"
echo "  riscv gcc $("$_isa_rvgcc" -dumpversion 2>/dev/null)   ($(basename "${_isa_rvgcc:-none}"))"
unset _isa_rvgcc
echo "  fusesoc   $(fusesoc --version 2>/dev/null)"
echo "  verible   $(verible-verilog-format --version 2>/dev/null | head -1 | awk '{print $2}')"
