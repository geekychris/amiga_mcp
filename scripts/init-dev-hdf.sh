#!/bin/bash
#
# Initialize the OS4 dev HDF from macOS — no Media Toolbox required.
#
# 1. Install RDB (Rigid Disk Block) so AmigaOS recognises the drive
# 2. Add a single partition covering the whole disk (DOS3, FFS-Int-Dircache)
# 3. Format the partition and label it as `DevDrive:`
#
# After this, you can copy files in via:
#   scripts/deploy-os4.sh <local-file> [target-name]
# On the OS4 side the drive mounts as `DevDrive:` (or DH1: depending on
# device order).
#
# Usage:
#   ./scripts/init-dev-hdf.sh                  # act on default HDF
#   ./scripts/init-dev-hdf.sh /path/to/my.hdf  # override
#
# WARNING: This destroys any existing partition table on the HDF.

set -e

HDF="${1:-$HOME/AmigaOS4/amigaos4-dev.hdf}"
VOL_LABEL="${VOL_LABEL:-DevDrive}"

if [ ! -f "$HDF" ]; then
    echo "ERROR: HDF not found: $HDF"
    exit 1
fi

if ! command -v rdbtool >/dev/null || ! command -v xdftool >/dev/null; then
    echo "ERROR: amitools missing. Run: scripts/install-amitools.sh"
    exit 1
fi

echo "=== Initialising dev HDF ==="
echo "  HDF:    $HDF"
echo "  Label:  $VOL_LABEL"
echo "  Size:   $(du -h "$HDF" | cut -f1) sparse ($(stat -f%z "$HDF") bytes real)"
echo ""

# Detect existing RDB — abort unless -f was given so we don't nuke a drive
# the user has already put files on.
if rdbtool -r "$HDF" info >/dev/null 2>&1; then
    if [ "$FORCE" != "1" ]; then
        echo "ERROR: HDF already has an RDB. Refusing to nuke without FORCE=1."
        echo "  Current layout:"
        rdbtool -r "$HDF" list 2>&1 | sed 's/^/    /'
        exit 1
    fi
    echo "  RDB already present — FORCE=1 set, overwriting."
fi

echo "→ install RDB"
rdbtool "$HDF" init

echo "→ add whole-disk DOS3 partition (device name DH1)"
# `add` with no size takes the full remaining space. Default DOS type is DOS3
# (FFS International + Directory Cache) which OS4 mounts natively. Name the
# partition DH1 explicitly — the rdbtool default (DH0) collides with the
# system HDF's partition name, so OS4 mounts the system drive and treats the
# dev drive as an unavailable "please insert DevDrive:" reference. Suppress
# the trailing traceback that comes from an implicit `free` on a full disk.
rdbtool -p DH "$HDF" add name=DH1 2>/dev/null || true

echo "→ format partition as '$VOL_LABEL'"
xdftool "$HDF" open part=0 + format "$VOL_LABEL"

echo ""
echo "=== Done ==="
rdbtool -r "$HDF" list
echo ""
xdftool -r "$HDF" open part=0 + list
