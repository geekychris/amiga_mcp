#!/bin/bash
#
# Cross-compile an example app for AmigaOS 4.1 PPC via Docker.
#
# Usage:
#   ./scripts/build-example-ppc.sh <example-name> [clean]
#
# Requires the amiga-bridge PPC libbridge.a to already be built:
#   ./scripts/build-bridge-ppc.sh
#
# Env:
#   PPC_IMAGE — override the toolchain image
#               (default: walkero/amigagccondocker:os4-gcc11-arm64)

set -e

if [ $# -lt 1 ]; then
    echo "Usage: $0 <example-name> [make-target]"
    echo "  e.g.  $0 hello_world"
    echo "        $0 hello_world clean"
    exit 1
fi

EXAMPLE="$1"
TARGET="${2:-all}"
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
EX_DIR="$PROJECT_DIR/examples/$EXAMPLE"
# Host-arch-aware PPC image selection (see scripts/lib/ppc-image.sh).
# shellcheck source=lib/ppc-image.sh
. "$PROJECT_DIR/scripts/lib/ppc-image.sh"

if [ ! -d "$EX_DIR" ]; then
    echo "ERROR: no example dir at $EX_DIR"
    exit 1
fi

if ! docker version --format '{{.Server.Version}}' >/dev/null 2>&1; then
    echo "ERROR: Docker daemon isn't reachable."
    echo "       Start Docker Desktop or Rancher Desktop and retry."
    exit 1
fi

echo "=== $EXAMPLE (PPC) ==="

if ! docker image inspect "$PPC_IMAGE" >/dev/null 2>&1; then
    echo "Pulling $PPC_IMAGE (first-time)..."
    docker pull "$PPC_IMAGE"
fi

PATH_EXPORT='export PATH=/opt/ppc-amigaos/bin:/opt/adtools/bin:$PATH'

docker run --rm \
    -v "$PROJECT_DIR:/work" \
    -w "/work/examples/$EXAMPLE" \
    "$PPC_IMAGE" \
    sh -c "$PATH_EXPORT && make ARCH=ppc $TARGET 2>&1"

echo ""
ls -la "$EX_DIR"/"$EXAMPLE" 2>/dev/null || true
