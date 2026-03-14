DOCKER_IMAGE = amigadev/crosstools:m68k-amigaos
DOCKER_RUN = docker run --rm -v $(PWD):/work -w /work $(DOCKER_IMAGE)

.PHONY: all lib examples bridge clean mcp-server start setup

all: lib examples bridge

lib:
	$(DOCKER_RUN) make -C amiga-debug-lib

examples: lib
	$(DOCKER_RUN) make -C examples/hello_world
	$(DOCKER_RUN) make -C examples/bouncing_ball
	$(DOCKER_RUN) make -C examples/system_monitor
	$(DOCKER_RUN) make -C examples/plasma
	$(DOCKER_RUN) make -C examples/sfx_player
	$(DOCKER_RUN) make -C examples/game_of_life
	$(DOCKER_RUN) make -C examples/memory_monitor
	$(DOCKER_RUN) make -C examples/disk_benchmark
	$(DOCKER_RUN) make -C examples/shell_proxy
	$(DOCKER_RUN) make -C examples/foo
	$(DOCKER_RUN) make -C examples/symbol_demo
	$(DOCKER_RUN) make -C examples/arexx_test
	$(DOCKER_RUN) make -C examples/test_example
	$(DOCKER_RUN) make -C examples/test_new_features
	$(DOCKER_RUN) make -C examples/boing_ball
	$(DOCKER_RUN) make -C examples/starfield

bridge:
	$(DOCKER_RUN) make -C amiga-bridge

clean:
	$(DOCKER_RUN) make -C amiga-debug-lib clean
	$(DOCKER_RUN) make -C examples/hello_world clean
	$(DOCKER_RUN) make -C examples/bouncing_ball clean
	$(DOCKER_RUN) make -C examples/system_monitor clean
	$(DOCKER_RUN) make -C examples/plasma clean
	$(DOCKER_RUN) make -C examples/sfx_player clean
	$(DOCKER_RUN) make -C examples/game_of_life clean
	$(DOCKER_RUN) make -C examples/memory_monitor clean
	$(DOCKER_RUN) make -C examples/disk_benchmark clean
	$(DOCKER_RUN) make -C examples/shell_proxy clean
	$(DOCKER_RUN) make -C examples/foo clean
	$(DOCKER_RUN) make -C examples/symbol_demo clean
	$(DOCKER_RUN) make -C examples/arexx_test clean
	$(DOCKER_RUN) make -C examples/test_example clean
	$(DOCKER_RUN) make -C examples/test_new_features clean
	$(DOCKER_RUN) make -C examples/boing_ball clean
	$(DOCKER_RUN) make -C examples/starfield clean
	$(DOCKER_RUN) make -C amiga-bridge clean

mcp-server:
	cd mcp-server && npm install && npm run build

setup:
	pip install -e amiga-devbench

start:
	python3 -m amiga_devbench
