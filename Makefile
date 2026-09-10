# Top-level Makefile for the X-HEEP accelerator labs.
#
# It defines a few of its own targets (vendor, mcu-gen, help) and then includes
# X-HEEP's `external.mk`. That include is the supported way to drive X-HEEP from
# an out-of-tree project: any target NOT defined here (app, verilator-build,
# verilator-run, profile, waves, clean, ...) is forwarded to the vendored X-HEEP
# with our SOURCE (so it builds our out-of-tree apps) and HEEP_EXTERNAL_ROOT.
#
# Layout follows the standard X-HEEP-based project (see gr-heep): our sw/ holds
# applications/ and external/, plus symlinks device/ linker/ build/ back into the
# vendored X-HEEP sw. Building with SOURCE pointed at our sw/ then finds both our
# apps AND X-HEEP's generated device headers (x-heep.h) through those symlinks.
#
# IMPORTANT: `include external.mk` MUST stay at the very bottom -- its catch-all
# rule swallows every target defined after it.

ROOT_DIR        := $(realpath .)
export HEEP_DIR ?= $(ROOT_DIR)/x-heep
XHEEP_CFG       ?= $(ROOT_DIR)/config.py  # your SoC config (edit config.py)
export PROJECT  ?= rxchain                # app under sw/applications/<PROJECT>

# SOURCE = path from x-heep/sw to OUR sw dir, so X-HEEP builds our out-of-tree
# apps and (via the device/linker/build symlinks in our sw) finds its own device
# headers. We set it explicitly because external.mk's default uses GNU `realpath
# --relative-to`, which is absent on macOS. Fixed layout => always ../../sw/.
export SOURCE   ?= ../../sw/

# `make profile` feeds the RTL-sim waveform to the rv_profile tool to build a
# flamegraph. RV_PROFILE is the PATH to that tool (not the app); it ships in the
# core-v-mini-mcu conda env, so the bare name works. Exported so the forwarded
# X-HEEP `profile` target sees it. Needs a prior `verilator-run` (for the .fst).
export RV_PROFILE ?= rv_profile

.PHONY: help vendor sw-links mcu-gen host-check

help:
	@echo "X-HEEP accelerator labs -- Lab 0 (profiling)"
	@echo ""
	@echo "  make vendor                        pull X-HEEP into ./x-heep + set up sw symlinks"
	@echo "  make mcu-gen                       generate the MCU from config.py (fusesoc)"
	@echo "  make verilator-build               build the Verilator model      [-> X-HEEP]"
	@echo "  make app PROJECT=<app>             compile <app> out-of-tree       [-> X-HEEP]"
	@echo "  make verilator-run-app PROJECT=<app>   compile + run <app>         [-> X-HEEP]"
	@echo "  make verilator-run PROJECT=<app>   run the LAST-BUILT app           [-> X-HEEP]"
	@echo "  make questasim-run-app PROJECT=<app>   build + run on QuestaSim    [-> X-HEEP]"
	@echo "  make profile                       flamegraph from last run's .fst  [-> X-HEEP]"
	@echo "  make host-check                    build+run every app on the HOST, check results"
	@echo "  make verilator-waves               open last waveform (gtkwave)    [-> X-HEEP]"
	@echo ""
	@echo "  [-> X-HEEP] = forwarded to the vendored X-HEEP via external.mk"
	@echo "  typical: make vendor && make mcu-gen && make verilator-build \\"
	@echo "           && make verilator-run-app PROJECT=rxchain"
	@echo "  apps: $(notdir $(wildcard sw/applications/*))"

# --- Host check -------------------------------------------------------------
# Build and run every app with the HOST compiler (no X-HEEP, no simulator) and
# check its self-test. The apps are plain integer C, so the host result is
# bit-identical to the RISC-V one: use this to check a change in seconds
# instead of minutes, and to get the golden checksum after changing a size.
HOST_CC   ?= cc
HOST_APPS := $(notdir $(wildcard sw/applications/*))
HOST_DIR  := build/host

host-check:
	@mkdir -p $(HOST_DIR); rc=0; \
	for a in $(HOST_APPS); do \
	  $(HOST_CC) -O2 -Wall -Isw/external -o $(HOST_DIR)/$$a sw/applications/$$a/main.c || exit 1; \
	  $(HOST_DIR)/$$a | grep -E 'PASS|FAIL' || rc=1; \
	done; exit $$rc

# --- Vendoring --------------------------------------------------------------
# python3 util/vendor.py x-heep.vendor.hjson  ->  snapshot of X-HEEP into ./x-heep
vendor:
	python3 util/vendor.py x-heep.vendor.hjson
	@$(MAKE) sw-links

# Symlink X-HEEP's device/linker/build into our sw/ so an out-of-tree build finds
# the generated device headers. Idempotent; re-run after every vendor.
sw-links:
	@ln -sfn ../x-heep/sw/device sw/device
	@ln -sfn ../x-heep/sw/linker sw/linker
	@ln -sfn ../x-heep/sw/build  sw/build
	@echo "sw symlinks ready (device, linker, build -> x-heep)"

# --- MCU generation ---------------------------------------------------------
# Defined here (not just forwarded) so we can inject our own config.py.
mcu-gen:
	$(MAKE) -C $(HEEP_DIR) mcu-gen PYTHON_X_HEEP_CFG=$(XHEEP_CFG) HEEP_EXTERNAL_ROOT=$(ROOT_DIR)

# --- Forward everything else to X-HEEP --------------------------------------
# Only once X-HEEP is vendored (before that, external.mk does not exist yet and
# `make vendor` / `make help` still work).
XHEEP_MAKE = $(HEEP_DIR)/external.mk
ifneq ("$(wildcard $(XHEEP_MAKE))","")
include $(XHEEP_MAKE)
endif
