#!/bin/bash
#
# Pull / build the Docker images used to cross-compile for both
# Amiga targets, plus the debugger image.
#
# Idempotent — safe to re-run. Images with matching tags are reused.
#
#   amigadev/crosstools:m68k-amigaos             — classic 68k gcc (any host arch)
#   walkero/amigagccondocker:os4-gcc11            — PPC OS4 gcc (multi-arch: arm64/amd64)
#   amiga-devbench-gdb:latest                    — gdb-multiarch (built locally)
#
# Docker picks the right PPC image for your host from the multi-arch
# manifest, so no per-arch tag needed. We also opportunistically re-pull
# the PPC image if the local copy is >1 week old — walkero pushes
# updates occasionally.

set -e

echo "=== Amiga cross-compiler toolchain install ==="
echo ""

if ! docker version --format '{{.Server.Version}}' >/dev/null 2>&1; then
    echo "ERROR: Docker daemon isn't reachable."
    echo "       Start Rancher/Docker Desktop and retry."
    exit 1
fi

pull_if_missing() {
    local img="$1"
    if docker image inspect "$img" >/dev/null 2>&1; then
        echo "  [ok]      $img (already present)"
    else
        echo "  [pull]    $img"
        docker pull "$img" 2>&1 | tail -1
    fi
}

# --- 68k classic ---
echo "--- Classic 68k (AmigaOS 3.x) ---"
pull_if_missing amigadev/crosstools:m68k-amigaos

# --- PPC OS4 ---
echo ""
echo "--- PowerPC (AmigaOS 4.1) ---"
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=lib/ppc-image.sh
. "$PROJECT_DIR/scripts/lib/ppc-image.sh"
pull_if_missing "$PPC_IMAGE"
ppc_image_refresh_if_stale

# --- gdb-multiarch for debugging PPC via QEMU GDB stub ---
echo ""
echo "--- Debugger (gdb-multiarch) ---"
GDB_IMG="amiga-devbench-gdb:latest"
if docker image inspect "$GDB_IMG" >/dev/null 2>&1; then
    echo "  [ok]      $GDB_IMG (already built)"
else
    echo "  [build]   $GDB_IMG (from docker/Dockerfile.gdb, ~1 min)"
    PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
    docker build -t "$GDB_IMG" -f "$PROJECT_DIR/docker/Dockerfile.gdb" \
                 "$PROJECT_DIR/docker" 2>&1 | tail -2
fi

echo ""
echo "=== Verification ==="
echo -n "  m68k gcc:  "
docker run --rm amigadev/crosstools:m68k-amigaos m68k-amigaos-gcc --version 2>&1 | head -1
echo -n "  ppc gcc:   "
docker run --rm "$PPC_IMAGE" \
    sh -c 'export PATH=/opt/ppc-amigaos/bin:$PATH && ppc-amigaos-gcc --version' 2>&1 | head -1
echo -n "  gdb:       "
docker run --rm "$GDB_IMG" gdb-multiarch --version 2>&1 | head -1

echo ""
echo "Ready. Build any example for either arch:"
echo "  make -C examples/hello_world                              # 68k (default)"
echo "  scripts/build-example-ppc.sh hello_world                  # PPC"
echo "  scripts/build-bridge-ppc.sh                               # PPC bridge"
