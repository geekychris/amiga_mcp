#!/bin/bash
#
# Install amitools (Python) which provides `rdbtool` and `xdftool` — used
# to read/write AmigaOS hardfiles (HDF) directly from macOS without
# booting the emulator.
#
# amitools ships with pip; nothing else required.

set -e

echo "=== Installing amitools ==="
if command -v xdftool >/dev/null && command -v rdbtool >/dev/null; then
    echo "  Already installed:"
    echo "    xdftool: $(command -v xdftool)"
    echo "    rdbtool: $(command -v rdbtool)"
    exit 0
fi

pip3 install amitools

echo ""
echo "=== Verification ==="
xdftool --help 2>&1 | head -1
rdbtool --help 2>&1 | head -1
echo ""
echo "Both installed. Use them via:"
echo "  scripts/init-dev-hdf.sh   — one-time: initialize the dev HDF"
echo "  scripts/deploy-os4.sh     — copy a file into the dev HDF"
