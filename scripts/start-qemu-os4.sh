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

# Check for U-Boot firmware
if [ ! -f "$UBOOT" ]; then
    echo "Warning: U-Boot firmware not found at $UBOOT"
    echo "The sam460ex machine requires U-Boot firmware."
    echo "You may need to obtain it from A-EON or build it."
    echo ""
    echo "Trying without explicit firmware (QEMU may have built-in)..."
    BIOS_ARG=""
else
    BIOS_ARG="-bios $UBOOT"
fi

# Build QEMU command
QEMU_CMD="$QEMU \
    -machine sam460ex \
    -m 512 \
    $BIOS_ARG \
    -drive file=$HDD_SYSTEM,format=raw,if=ide,index=0 \
    -drive file=$HDD_DEV,format=raw,if=ide,index=1 \
    -serial tcp::${SERIAL_PORT},server,nowait \
    -net nic,model=rtl8139 \
    -net user \
    -display default \
    -name 'AmigaOS 4.1 - DevBench'"

# Add CDROM for install mode
if [ $INSTALL_MODE -eq 1 ]; then
    if [ ! -f "$CDROM" ]; then
        echo "Error: CDROM image not found at $CDROM"
        echo "Download AmigaOS 4.1 FE from hyperion-entertainment.com"
        exit 1
    fi
    QEMU_CMD="$QEMU_CMD -cdrom $CDROM -boot d"
    echo "=== INSTALL MODE ==="
    echo "Booting from CDROM: $CDROM"
fi

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

eval $QEMU_CMD
