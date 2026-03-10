DOCKER_IMAGE = amigadev/crosstools:m68k-amigaos
DOCKER_RUN = docker run --rm -v $(PWD):/work -w /work $(DOCKER_IMAGE)

.PHONY: all lib examples bridge clean mcp-server

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
	$(DOCKER_RUN) make -C amiga-bridge clean

mcp-server:
	cd mcp-server && npm install && npm run build
