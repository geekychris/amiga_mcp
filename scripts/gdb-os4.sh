#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins

#
# Attach ppc-amigaos-gdb (from the walkero Docker image) to the QEMU
# sam460ex GDB stub. Prerequisite: start QEMU with --gdb (that opens
# the stub on TCP 1234, matching this script's default).
#
# Usage:
#   ./scripts/gdb-os4.sh                       # bare CPU-level debug
#   ./scripts/gdb-os4.sh path/to/binary.elf    # load symbols for `binary`
#   ./scripts/gdb-os4.sh amiga-bridge/amiga-bridge
#
# Env:
#   GDB_HOST    hostname of QEMU GDB stub (default: host.docker.internal
#               on macOS Docker, works transparently to reach the host)
#   GDB_PORT    port of QEMU GDB stub (default: 1234)
#   GDB_IMAGE   debugger image (default: amiga-devbench-gdb:latest, built
#               locally from docker/Dockerfile.gdb — gdb-multiarch on
#               ubuntu:24.04; walkero's ppc-amigaos image doesn't ship gdb).
#
# The GDB stub debugs the entire emulated CPU, not a specific OS4
# process. Useful commands once connected:
#   (gdb) target remote        — usually already done via .gdbinit
#   (gdb) info registers       — dump PPC register file
#   (gdb) x/16i \$pc            — disassemble around program counter
#   (gdb) x/16x \$r3            — inspect memory at r3
#   (gdb) bt                   — backtrace (works if symbols are loaded)
#   (gdb) c                    — continue
#   (gdb) Ctrl-C               — halt CPU
# For catching a specific process's crash, load its ELF with add-symbol-file
# at the load address OS4 reported (see AllocMem / crash guru output).

set -e

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
GDB_HOST="${GDB_HOST:-host.docker.internal}"
GDB_PORT="${GDB_PORT:-1234}"
GDB_IMAGE="${GDB_IMAGE:-amiga-devbench-gdb:latest}"

ELF_ARG="${1:-}"
GDB_ARGS=()

# If a binary path is given, mount + pass to gdb for symbol loading.
if [ -n "$ELF_ARG" ]; then
    if [ ! -f "$ELF_ARG" ]; then
        echo "ERROR: ELF not found: $ELF_ARG"
        exit 1
    fi
    # Normalise to an absolute path so relative invocations like
    # `examples/hello_world/hello_world` (documented in the header) pass
    # the containment check below. Use python for portable realpath —
    # macOS `readlink -f` is GNU-only, `realpath` isn't stock on macOS.
    ELF_ABS="$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$ELF_ARG")"
    # Enforce containment on the normalised path.
    if [[ "$ELF_ABS" != "$PROJECT_DIR"/* ]]; then
        echo "ERROR: ELF must live under project dir: $PROJECT_DIR"
        echo "       Got: $ELF_ABS"
        exit 1
    fi
    REL="${ELF_ABS#$PROJECT_DIR/}"
    GDB_ARGS=("/work/$REL")
fi

# Native Linux Docker can't resolve host.docker.internal by default
# (macOS + Windows Docker Desktop map it automatically). Add --add-host
# so `target remote host.docker.internal:1234` works on Linux too.
# GDB_HOST override wins over both defaults.
DOCKER_HOST_ARGS=()
if [ "$(uname -s)" = "Linux" ]; then
    DOCKER_HOST_ARGS+=( --add-host=host.docker.internal:host-gateway )
fi

if ! docker version --format '{{.Server.Version}}' >/dev/null 2>&1; then
    echo "ERROR: Docker daemon isn't reachable. Start Rancher/Docker Desktop."
    exit 1
fi

if ! docker image inspect "$GDB_IMAGE" >/dev/null 2>&1; then
    echo "GDB image not found. Building it (one-time, ~2 min)..."
    docker build -t "$GDB_IMAGE" -f "$PROJECT_DIR/docker/Dockerfile.gdb" "$PROJECT_DIR/docker"
fi

echo "=== Attaching gdb-multiarch to QEMU sam460ex ==="
echo "  Target:  $GDB_HOST:$GDB_PORT"
echo "  Image:   $GDB_IMAGE"
[ -n "$ELF_ARG" ] && echo "  Symbols: $ELF_ARG"
echo ""
echo "Type 'help' for GDB commands. Ctrl-D or 'quit' to exit."
echo ""

INIT_CMDS="set confirm off
set pagination off
set architecture powerpc:common
set endian big
target remote $GDB_HOST:$GDB_PORT
echo Connected. Use Ctrl-C to halt CPU; 'c' to continue.\n"

docker run --rm -it \
    "${DOCKER_HOST_ARGS[@]}" \
    -v "$PROJECT_DIR:/work" \
    -w /work \
    "$GDB_IMAGE" \
    sh -c "printf '%b' '$INIT_CMDS' > /tmp/gdbinit && \
           gdb-multiarch -q -x /tmp/gdbinit ${GDB_ARGS[*]}"
