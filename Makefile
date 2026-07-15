DOCKER_IMAGE = amigadev/crosstools:m68k-amigaos
DOCKER_RUN = docker run --rm -v $(PWD):/work -w /work $(DOCKER_IMAGE)

# Discover example projects at build time. Each subdir of examples/ with a
# Makefile is treated as an example. examples/ is a git submodule pointing at
# github.com/geekychris/amiga_games — run `git submodule update --init` after
# cloning if the directory is empty.
EXAMPLES := $(patsubst examples/%/Makefile,%,$(wildcard examples/*/Makefile))

.PHONY: all examples bridge clean start setup host-tests $(EXAMPLES) $(addsuffix .clean,$(EXAMPLES))

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

setup:
	pip install -e amiga-devbench

start:
	python3 -m amiga_devbench

# Host-buildable unit tests for pure-C bridge logic (no Docker / Amiga cross-
# compiler needed). See amiga-bridge/host/README.md.
host-tests:
	$(MAKE) -C amiga-bridge/host test
