# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

DOCKER_IMAGE = amigadev/crosstools:m68k-amigaos
DOCKER_RUN = docker run --rm -v $(PWD):/work -w /work $(DOCKER_IMAGE)

# Discover example projects at build time. Each subdir of examples/ with a
# Makefile is treated as an example. examples/ is a git submodule pointing at
# github.com/geekychris/amiga_games — run `git submodule update --init` after
# cloning if the directory is empty.
EXAMPLES := $(patsubst examples/%/Makefile,%,$(wildcard examples/*/Makefile))

.PHONY: setup doctor start start-68k start-os4 stop status devbench all examples bridge clean host-tests $(EXAMPLES) $(addsuffix .clean,$(EXAMPLES))

all: bridge examples

bridge:
	$(DOCKER_RUN) make -C amiga-bridge all

examples: bridge $(EXAMPLES)

# Per-example build target: `make dot_chase`
$(EXAMPLES):
	$(DOCKER_RUN) make -C examples/$@

clean: $(addsuffix .clean,$(EXAMPLES))
	$(DOCKER_RUN) make -C amiga-bridge clean

# Per-example clean target: `make dot_chase.clean`
$(addsuffix .clean,$(EXAMPLES)):
	$(DOCKER_RUN) make -C examples/$(basename $@) clean

# Setup and startup: see SETUP.md
setup:            ## install everything (both targets), then check
	scripts/setup.sh

doctor:           ## check prerequisites (both targets)
	scripts/doctor.sh

start:            ## start both: OS4 devbench :3000 + QEMU, 68k devbench :3001 + FS-UAE
	scripts/start.sh all

start-68k:        ## classic Amiga only
	scripts/start.sh 68k

start-os4:        ## AmigaOS 4 only
	scripts/start.sh os4

stop:             ## stop the emulators and devbenches
	scripts/start.sh stop

status:           ## what's running
	scripts/start.sh status

devbench:         ## one devbench on the active profile (devbench.toml), :3000 (was `make start`)
	python3 -m amiga_devbench

# Host-buildable unit tests for pure-C bridge logic (no Docker / Amiga cross-
# compiler needed). See amiga-bridge/host/README.md.
host-tests:
	$(MAKE) -C amiga-bridge/host test
