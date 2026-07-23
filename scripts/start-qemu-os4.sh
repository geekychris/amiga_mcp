#!/bin/bash
#
# Start QEMU sam460ex with AmigaOS 4.1
#
# Serial port exposed as TCP for devbench connection.
# First run: boot from CDROM to install OS.
# Subsequent runs: boot from HDD.
#
# Prerequisites:
#   - QEMU with PPC support (brew install qemu or build from source)
#   - AmigaOS 4.1 FE ISO (purchase from hyperion-entertainment.com)
#   - U-Boot firmware for sam460ex
#   - Hard drive images created with qemu-img
#
# Usage:
#   ./start-qemu-os4.sh              # Normal boot from HDD
#   ./start-qemu-os4.sh --install    # Boot from CDROM for installation
#   ./start-qemu-os4.sh --serial 2346  # Custom serial port

set -e

# Paths — adjust these for your setup
QEMU=/opt/homebrew/bin/qemu-system-ppc
OS4_DIR="${OS4_DIR:-$HOME/AmigaOS4}"
HDD_SYSTEM="${OS4_DIR}/amigaos4-system.hdf"
HDD_DEV="${OS4_DIR}/amigaos4-dev.hdf"
CDROM="${OS4_DIR}/AmigaOS4.1-FE.iso"
UBOOT="${OS4_DIR}/u-boot-sam460ex.bin"
SERIAL_PORT="${SERIAL_PORT:-2346}"

# Parse arguments
INSTALL_MODE=0
while [[ $# -gt 0 ]]; do
    case $1 in
        --install) INSTALL_MODE=1; shift ;;
        --serial) SERIAL_PORT="$2"; shift 2 ;;
        --help|-h)
            echo "Usage: $0 [--install] [--serial PORT]"
            echo ""
            echo "  --install     Boot from CDROM for OS installation"
            echo "  --serial PORT Serial TCP port (default: 2346)"
            echo ""
            echo "Environment variables:"
            echo "  OS4_DIR       Directory containing OS4 files (default: ~/AmigaOS4)"
            echo "  SERIAL_PORT   Serial TCP port (default: 2346)"
            echo ""
            echo "Required files in OS4_DIR:"
            echo "  amigaos4-system.hdf  - System hard drive image"
            echo "  amigaos4-dev.hdf     - Development shared drive"
            echo "  u-boot-sam460ex.bin  - U-Boot firmware"
            echo "  AmigaOS4.1-FE.iso   - OS install CD (for --install)"
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# Check prerequisites
if [ ! -x "$QEMU" ]; then
    echo "Error: QEMU not found at $QEMU"
    echo "Install with: brew install qemu"
    exit 1
fi

# Create HDD images if they don't exist
if [ ! -f "$HDD_SYSTEM" ]; then
    echo "Creating system HDD image (2GB)..."
    qemu-img create -f raw "$HDD_SYSTEM" 2G
    echo "  Created: $HDD_SYSTEM"
    echo "  You'll need to install AmigaOS 4.1 with --install flag"
fi

if [ ! -f "$HDD_DEV" ]; then
    echo "Creating development HDD image (512MB)..."
    qemu-img create -f raw "$HDD_DEV" 512M
    echo "  Created: $HDD_DEV"
fi

# Check for U-Boot firmware. Recent QEMU ships u-boot-sam460.bin in
# its share dir and auto-loads it for sam460ex; only pass -bios if a
# custom firmware is provided in $OS4_DIR.
if [ -f "$UBOOT" ]; then
    BIOS_ARG="-bios $UBOOT"
    echo "Using custom U-Boot: $UBOOT"
else
    BIOS_ARG=""
fi

# The sam460ex machine has ONE IDE bus with two slots (bus 0 / units
# 0-1). QEMU's implicit -cdrom would place the CD on bus 1 which
# doesn't exist, so we have to explicitly attach drives via if=ide
# with slot indexes. Install mode uses slot 1 for the CD and drops
# the dev HDD until after install completes. Normal mode uses slot
# 0 for the system HDD and slot 1 for the dev HDD.
if [ $INSTALL_MODE -eq 1 ]; then
    if [ ! -f "$CDROM" ]; then
        echo "Error: CDROM image not found at $CDROM"
        echo "Download AmigaOS 4.1 FE from hyperion-entertainment.com"
        exit 1
    fi
    DRIVE_ARGS="-drive file=$HDD_SYSTEM,format=raw,if=ide,index=0 \
                -drive file=$CDROM,format=raw,if=ide,index=1,media=cdrom"
    BOOT_ARGS="-boot d"
    echo "=== INSTALL MODE ==="
    echo "Booting from CDROM: $CDROM"
else
    # sam460ex's sii3112 SATA controller exposes TWO IDE-like buses
    # (ide.0 and ide.1), each supporting exactly one device. QEMU
    # attaches an empty ide-cd on ide.1 by default. To land the dev
    # HDF on ide.1 we attach it explicitly with if=none + ide-hd,
    # which overrides the default CD-ROM. System HDD stays on ide.0
    # via the simpler if=ide,index=0 shorthand.
    DRIVE_ARGS="-drive file=$HDD_SYSTEM,format=raw,if=ide,index=0 \
                -drive file=$HDD_DEV,format=raw,if=none,id=devdrv \
                -device ide-hd,drive=devdrv,bus=ide.1,unit=0"
    BOOT_ARGS=""
fi

# 1 GB — 512 MB left AmigaOS 4.1 wedged mid-boot on the CD. sam460ex
# supports up to 2 GB. Networking is disabled (rtl8139 isn't a
# sam460ex-supported NIC and QEMU warned it wouldn't be created).
QEMU_CMD="$QEMU \
    -machine sam460ex \
    -m 1024 \
    $BIOS_ARG \
    $DRIVE_ARGS \
    $BOOT_ARGS \
    -serial tcp::${SERIAL_PORT},server,nowait \
    -nic none \
    -display cocoa,zoom-to-fit=on,show-cursor=on \
    -name 'AmigaOS 4.1 - DevBench'"

echo "=== Starting QEMU sam460ex ==="
echo "  Machine:    sam460ex (PowerPC 460EX)"
echo "  RAM:        512MB"
echo "  System HDD: $HDD_SYSTEM"
echo "  Dev HDD:    $HDD_DEV"
echo "  Serial:     TCP port $SERIAL_PORT"
echo "  Network:    User-mode (NAT)"
echo ""
echo "DevBench connection:"
echo "  python3 -m amiga_devbench --serial-host 127.0.0.1 --serial-port $SERIAL_PORT"
echo ""

# QEMU Cocoa doesn't have a way to set an initial window size, so
# launch in the background and use System Events to resize once the
# window shows up. Falls back silently if osascript isn't available.
eval "$QEMU_CMD &"
QEMU_PID=$!
if command -v osascript >/dev/null; then
    (
        for i in 1 2 3 4 5; do
            sleep 1
            osascript -e 'tell application "System Events" to tell process "qemu-system-ppc" to set size of first window to {960, 720}' 2>/dev/null && break
        done
        osascript -e 'tell application "System Events" to tell process "qemu-system-ppc" to set position of first window to {200, 80}' 2>/dev/null
    ) &
fi
wait "$QEMU_PID"
