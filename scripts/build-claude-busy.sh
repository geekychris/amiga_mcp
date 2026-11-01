#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
# Build tools/claude_busy (the "Claude is testing" banner) for AmigaOS 4 and 68k.
set -e
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
. "$PROJECT_DIR/scripts/lib/ppc-image.sh"
docker run --rm -v "$PROJECT_DIR:/work" -w /work/tools/claude_busy "$PPC_IMAGE" \
    sh -c 'export PATH=/opt/ppc-amigaos/bin:/opt/adtools/bin:$PATH; make ARCH=ppc &&
           test "$(ppc-amigaos-objdump -d claude_busy_os4 | grep -c dlmzb)" = 0'
docker run --rm -v "$PROJECT_DIR:/work" -w /work/tools/claude_busy amigadev/crosstools:m68k-amigaos make ARCH=m68k
