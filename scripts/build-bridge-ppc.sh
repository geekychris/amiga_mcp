#!/bin/bash
#
# Cross-compile amiga-bridge for AmigaOS 4.1 PPC via Docker.
#
# Uses the walkero/amigagccondocker:os4-* image which ships
# ppc-amigaos-gcc + newlib + os4 SDK. The default image tag is
# arm64 (Apple Silicon); override with $PPC_IMAGE for other hosts.
#
# Usage:
#   ./scripts/build-bridge-ppc.sh          # normal build
#   ./scripts/build-bridge-ppc.sh clean    # clean + rebuild
#   PPC_IMAGE=... ./scripts/build-bridge-ppc.sh   # override image tag

set -e

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BRIDGE_DIR="$PROJECT_DIR/amiga-bridge"
PPC_IMAGE="${PPC_IMAGE:-walkero/amigagccondocker:os4-gcc11-arm64}"

if ! command -v docker >/dev/null; then
    echo "ERROR: docker not found in PATH."
    exit 1
fi

if ! docker version --format '{{.Server.Version}}' >/dev/null 2>&1; then
    echo "ERROR: Docker daemon isn't reachable."
    echo "       Start Docker Desktop or Rancher Desktop and retry."
    exit 1
fi

echo "=== amiga-bridge PPC build ==="
echo "  Image:   $PPC_IMAGE"
echo "  Bridge:  $BRIDGE_DIR"
echo "  Target:  AmigaOS 4.1 (sam460ex, PowerPC 460EX)"
echo ""

# Pull if missing; skip re-pull if already present to keep iteration fast.
if ! docker image inspect "$PPC_IMAGE" >/dev/null 2>&1; then
    echo "Pulling $PPC_IMAGE (first-time, ~500MB)..."
    docker pull "$PPC_IMAGE"
fi

# The container's PATH doesn't include the PPC compiler by default;
# some images expose it at /opt/... — export the common paths so `make`
# finds ppc-amigaos-gcc. Adjust if your image lives elsewhere.
PATH_EXPORT='export PATH=/opt/ppc-amigaos/bin:/opt/adtools/bin:$PATH'

TARGET="${1:-all}"

docker run --rm \
    -v "$PROJECT_DIR:/work" \
    -w /work/amiga-bridge \
    "$PPC_IMAGE" \
    sh -c "$PATH_EXPORT && make ARCH=ppc $TARGET 2>&1"

echo ""
echo "=== Done ==="
ls -la "$BRIDGE_DIR"/amiga-bridge "$BRIDGE_DIR"/libbridge.a 2>/dev/null || true
