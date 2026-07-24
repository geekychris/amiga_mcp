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
GDB_MODE=0
GDB_PORT=${GDB_PORT:-1234}
NET_MODE=0
while [[ $# -gt 0 ]]; do
    case $1 in
        --install) INSTALL_MODE=1; shift ;;
        --serial) SERIAL_PORT="$2"; shift 2 ;;
        --gdb)     GDB_MODE=1; shift ;;
        --gdb-port) GDB_PORT="$2"; GDB_MODE=1; shift 2 ;;
        --net)     NET_MODE=1; shift ;;
        --help|-h)
            cat <<EOH
Usage: $0 [--install] [--serial PORT] [--gdb [--gdb-port N]] [--net]

  --install         Boot from CDROM for OS installation
  --serial PORT     Serial TCP port for bridge (default: 2346)
  --gdb             Enable QEMU's built-in GDB stub for CPU-level
                    debugging (registers, memory, breakpoints on any
                    address). Attach with scripts/gdb-os4.sh
  --gdb-port N      Port for GDB stub (default: 1234)
  --net             Enable QEMU user-mode NAT networking (e1000
                    driver). Needed for AmiUpdate / web browsing on
                    the OS4 side. Off by default because base
                    installs may not have TCP/IP configured yet.

Environment variables:
  OS4_DIR           Directory containing OS4 files (default: ~/AmigaOS4)
  SERIAL_PORT       Serial TCP port (default: 2346)
  GDB_PORT          GDB stub port (default: 1234, needs --gdb to activate)
EOH
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

# GDB stub for CPU-level debugging (registers, memory, breakpoints).
# Not started (nowait) so QEMU still boots without a GDB attached; when
# scripts/gdb-os4.sh connects, it lands wherever the CPU is at that moment.
if [ $GDB_MODE -eq 1 ]; then
    GDB_ARGS="-gdb tcp::${GDB_PORT},server,nowait"
    echo "  GDB stub:   TCP port ${GDB_PORT} (attach with scripts/gdb-os4.sh)"
else
    GDB_ARGS=""
fi

# Display backend — cocoa on macOS, gtk (or sdl fallback) on Linux.
# Both support zoom-to-fit; on Linux the window is resizable natively.
case "$(uname -s)" in
    Darwin) DISPLAY_ARG="cocoa,zoom-to-fit=on,show-cursor=on" ;;
    Linux)  DISPLAY_ARG="gtk,zoom-to-fit=on,show-cursor=on"   ;;
    *)      DISPLAY_ARG="sdl" ;;
esac

# 1 GB — 512 MB left AmigaOS 4.1 wedged mid-boot on the CD. sam460ex
# supports up to 2 GB. Networking is disabled (rtl8139 isn't a
# sam460ex-supported NIC and QEMU warned it wouldn't be created).
#
# Build the QEMU invocation as a Bash array so paths with spaces / quotes
# survive without eval-quoting hazards. The BIOS_ARG / DRIVE_ARGS /
# BOOT_ARGS / GDB_ARGS shell-string variables are expanded once via a
# helper `read -ra` split — each is a small, controlled list.
QEMU_CMD=( "$QEMU" -machine sam460ex -m 1024 )
read -ra _bios_arr  <<<"$BIOS_ARG";  QEMU_CMD+=( "${_bios_arr[@]}" )
read -ra _drive_arr <<<"$DRIVE_ARGS";QEMU_CMD+=( "${_drive_arr[@]}" )
read -ra _boot_arr  <<<"$BOOT_ARGS"; QEMU_CMD+=( "${_boot_arr[@]}" )
QEMU_CMD+=( -serial "tcp::${SERIAL_PORT},server,nowait" )
read -ra _gdb_arr   <<<"$GDB_ARGS";  QEMU_CMD+=( "${_gdb_arr[@]}" )
# Networking off by default (base OS4 has no TCP/IP configured). --net
# adds a user-mode NAT e1000 NIC which OS4 sees as an e1000 device
# handled by Roadshow. Guest sees DNS/NAT via QEMU's built-in stack;
# no host-side setup needed.
if [ "$NET_MODE" -eq 1 ]; then
    # RTL8139 chosen because base OS4.1 FE ships rtl8139.device out
    # of the box. e1000 needs a driver install (not present in base).
    QEMU_CMD+=( -nic user,model=rtl8139 )
    NET_STATUS="user-mode NAT (rtl8139)"
else
    QEMU_CMD+=( -nic none )
    NET_STATUS="disabled (-nic none)"
fi
QEMU_CMD+=( -display "$DISPLAY_ARG" -name "AmigaOS 4.1 - DevBench" )

echo "=== Starting QEMU sam460ex ==="
echo "  Machine:    sam460ex (PowerPC 460EX)"
echo "  RAM:        1024 MB"
echo "  System HDD: $HDD_SYSTEM"
echo "  Dev HDD:    $HDD_DEV"
echo "  Serial:     TCP port $SERIAL_PORT"
echo "  Network:    $NET_STATUS"
echo "  Display:    $DISPLAY_ARG"
echo ""
echo "DevBench connection:"
echo "  python3 -m amiga_devbench --serial-host 127.0.0.1 --serial-port $SERIAL_PORT"
echo ""

# Launch in background so we can post-launch-resize the window on macOS.
"${QEMU_CMD[@]}" &
QEMU_PID=$!
if [ "$(uname -s)" = "Darwin" ] && command -v osascript >/dev/null; then
    # QEMU Cocoa can't set initial window size — poll for the window
    # then resize/reposition. Fails silently if it never opens (headless
    # or QEMU exits early).
    (
        for i in 1 2 3 4 5; do
            sleep 1
            osascript -e 'tell application "System Events" to tell process "qemu-system-ppc" to set size of first window to {960, 720}' 2>/dev/null && break
        done
        osascript -e 'tell application "System Events" to tell process "qemu-system-ppc" to set position of first window to {200, 80}' 2>/dev/null
    ) &
fi
wait "$QEMU_PID"
