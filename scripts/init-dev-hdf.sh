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

echo "→ recreate HDF with realistic CHS geometry + install RDB"
# amitools' auto-geometry (heads=1, secs=32) makes OS4's Media Toolbox
# refuse the disk with "serious errors" and its sii3112 SATA driver skips
# the RDB scan entirely. Force classic PC-style CHS (16 heads × 63 sectors)
# so OS4 trusts the layout. 1040 cyls × 16 × 63 × 512 = exactly 512 MB.
#
# Note: chs= must be set at CREATE time, not at open — subsequent opens on
# an existing file re-detect geometry from size and pick the same silly
# default. So we delete the HDF (safe: called only after FORCE=1 gate) and
# create afresh with the right geometry embedded.
if [ "$FORCE" = "1" ]; then rm -f "$HDF"; fi
rdbtool -f "$HDF" create size=512Mi chs=1040,16,63 + init + add name=DH1 2>/dev/null || true

echo "→ format partition as '$VOL_LABEL'"
xdftool "$HDF" open part=0 + format "$VOL_LABEL"

echo ""
echo "=== Done ==="
rdbtool -r "$HDF" list
echo ""
xdftool -r "$HDF" open part=0 + list
