#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins

#
# Copy a file into the OS4 dev HDF from macOS.
#
# The HDF must first be initialised — run scripts/init-dev-hdf.sh once
# before any deploys. QEMU should ideally NOT be running while we write
# (raw HDF is shared state).
#
# Usage:
#   ./scripts/deploy-os4.sh <local-file> [target-name]
#   ./scripts/deploy-os4.sh --list
#   ./scripts/deploy-os4.sh --rm <target-name>
#
# Env:
#   OS4_DEV_HDF   HDF path (default: $HOME/AmigaOS4/amigaos4-dev.hdf)

set -e

HDF="${OS4_DEV_HDF:-$HOME/AmigaOS4/amigaos4-dev.hdf}"

if [ ! -f "$HDF" ]; then
    echo "ERROR: HDF not found: $HDF"
    exit 1
fi

if ! command -v xdftool >/dev/null; then
    echo "ERROR: xdftool missing. Run: scripts/install-amitools.sh"
    exit 1
fi

if pgrep -f "qemu-system-ppc.*$HDF" >/dev/null; then
    if [ "$FORCE" != "1" ]; then
        echo "ERROR: QEMU is currently attached to this HDF."
        echo "       Concurrent writes can corrupt the raw HDF because"
        echo "       QEMU + OS4 hold their own cached view of the disk."
        echo "       Shut down OS4 first (or set FORCE=1 to override —"
        echo "       'diskchange DH1:' after the write only refreshes OS4's"
        echo "       directory view, it does not make the write atomic)."
        exit 1
    fi
    echo "WARNING: QEMU attached — proceeding under FORCE=1."
    echo ""
fi

CMD="${1:-}"
case "$CMD" in
    --list|"")
        xdftool -r "$HDF" open part=0 + list
        ;;
    --rm)
        [ -n "$2" ] || { echo "Usage: $0 --rm <target-name>"; exit 1; }
        xdftool "$HDF" open part=0 + delete "$2"
        echo "Removed: $2"
        ;;
    --help|-h)
        sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
        ;;
    *)
        LOCAL="$CMD"
        TARGET="${2:-$(basename "$LOCAL")}"
        if [ ! -f "$LOCAL" ]; then
            echo "ERROR: local file not found: $LOCAL"
            exit 1
        fi
        SIZE=$(wc -c < "$LOCAL" | tr -d ' ')
        echo "→ $LOCAL ($SIZE bytes) → DevDrive:$TARGET"
        # -f overwrites an existing entry; without it xdftool complains.
        xdftool -f "$HDF" open part=0 + write "$LOCAL" "$TARGET"
        echo ""
        xdftool -r "$HDF" open part=0 + list "$TARGET"

        # If a running devbench is on port 3000, poke it to run
        # `diskchange DH1:` on the Amiga so OS4 re-reads the directory
        # and sees the newly-written file. Silent if devbench isn't up.
        if command -v curl >/dev/null && \
           curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:3000/api/status 2>/dev/null | grep -q 200; then
            echo "→ nudging OS4 (diskchange DH1:) via bridge..."
            curl -s -X POST http://127.0.0.1:3000/api/dos/exec \
                 -H 'Content-Type: application/json' \
                 -d '{"command":"diskchange DH1:","timeout":10}' \
                 >/dev/null 2>&1 || \
                 echo "  (bridge nudge failed — run 'diskchange DH1:' from OS4 shell)"
        fi
        ;;
esac
