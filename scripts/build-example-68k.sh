#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
#
# Cross-compile an example for classic AmigaOS (68k) via Docker, with
# ARCH=m68k (for examples whose Makefile builds more than one target).
#
# Usage: ./scripts/build-example-68k.sh <example-name> [make-target]
set -e
if [ $# -lt 1 ]; then
    echo "Usage: $0 <example-name> [make-target]"
    exit 1
fi
EXAMPLE="$1"
TARGET="${2:-all}"
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="${M68K_IMAGE:-amigadev/crosstools:m68k-amigaos}"
[ -d "$PROJECT_DIR/examples/$EXAMPLE" ] || { echo "ERROR: no example dir examples/$EXAMPLE"; exit 1; }
echo "=== $EXAMPLE (68k) ==="
docker run --rm -v "$PROJECT_DIR:/work" -w "/work/examples/$EXAMPLE" "$IMAGE" \
    sh -c "make ARCH=m68k $TARGET 2>&1"
