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

if ! command -v rdbtool >/dev/null || ! command -v xdftool >/dev/null; then
    echo "ERROR: amitools missing. Run: scripts/install-amitools.sh"
    exit 1
fi

# rdbtool below runs `create` unconditionally so a missing HDF is fine —
# it produces the sparse container itself. Still create the parent
# directory ourselves; rdbtool won't.
mkdir -p "$(dirname "$HDF")"

echo "=== Initialising dev HDF ==="
echo "  HDF:    $HDF"
echo "  Label:  $VOL_LABEL"
# wc -c is portable across macOS + Linux; `stat -f%z` is BSD-only,
# `stat -c%s` is GNU-only. Strip wc's leading whitespace.
if [ -f "$HDF" ]; then
    echo "  Size:   $(du -h "$HDF" | cut -f1) sparse ($(wc -c < "$HDF" | tr -d ' ') bytes real)"
else
    echo "  Size:   (will be created)"
fi
echo ""

# Detect existing RDB — abort unless -f was given so we don't nuke a drive
# the user has already put files on. Skips the check when the HDF doesn't
# exist yet (rdbtool `create` below will make it).
if [ -f "$HDF" ] && rdbtool -r "$HDF" info >/dev/null 2>&1; then
    if [ "$FORCE" != "1" ]; then
        echo "ERROR: HDF already has an RDB. Refusing to nuke without FORCE=1."
        echo "  Current layout:"
        rdbtool -r "$HDF" list 2>&1 | sed 's/^/    /'
        exit 1
    fi
    echo "  RDB already present — FORCE=1 set, overwriting."
fi

echo "→ recreate HDF with realistic CHS geometry + install RDB"
# amitools' auto-geometry (heads=1, secs=32) makes OS4's Media Toolbox
# refuse the disk with "serious errors" and its sii3112 SATA driver skips
# the RDB scan entirely. Force classic PC-style CHS (16 heads × 63 sectors)
# so OS4 trusts the layout. 1040 cyls × 16 × 63 × 512 = exactly 512 MB.
#
# Note: chs= must be set at CREATE time, not at open — subsequent opens on
# an existing file re-detect geometry from size and pick the same silly
# default. So we delete the HDF (safe: called only after FORCE=1 gate) and
# create afresh with the right geometry embedded. Skipping the rm when the
# HDF doesn't exist yet (fresh install path) — rdbtool create makes it.
if [ "$FORCE" = "1" ] && [ -f "$HDF" ]; then rm -f "$HDF"; fi
# Suppress stderr (rdbtool's traceback from the trailing implicit-free
# walk on a fully-partitioned disk) but NOT the exit code — if create/
# init/add actually fail, we want the script to stop before format tries
# to run against a half-baked disk.
rdbtool -f "$HDF" create size=512Mi chs=1040,16,63 + init + add name=DH1 2>/dev/null

echo "→ format partition as '$VOL_LABEL'"
xdftool "$HDF" open part=0 + format "$VOL_LABEL"

echo ""
echo "=== Done ==="
rdbtool -r "$HDF" list
echo ""
xdftool -r "$HDF" open part=0 + list
