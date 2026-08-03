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

# Parse arguments.
# NET_MODE: 0=off, 1=on. Default ON — the PPC amiga-bridge speaks
# TCP-over-bsdsocket by default, which needs a NIC in the guest.
# The old serial-passthrough path still works but is slower and the
# bridge's TCP mode is where new work happens.
INSTALL_MODE=0
GDB_MODE=0
GDB_PORT=${GDB_PORT:-1234}
NET_MODE=1
while [[ $# -gt 0 ]]; do
    case $1 in
        --install) INSTALL_MODE=1; shift ;;
        --serial) SERIAL_PORT="$2"; shift 2 ;;
        --gdb)     GDB_MODE=1; shift ;;
        --gdb-port) GDB_PORT="$2"; GDB_MODE=1; shift 2 ;;
        --net)     NET_MODE=1; shift ;;
        --no-net)  NET_MODE=0; shift ;;
        --help|-h)
            cat <<EOH
Usage: $0 [--install] [--serial PORT] [--gdb [--gdb-port N]] [--net|--no-net]

  --install         Boot from CDROM for OS installation
  --serial PORT     Serial TCP port for bridge (default: 2346)
  --gdb             Enable QEMU's built-in GDB stub for CPU-level
                    debugging (registers, memory, breakpoints on any
                    address). Attach with scripts/gdb-os4.sh
  --gdb-port N      Port for GDB stub (default: 1234)
  --net             Enable QEMU user-mode NAT networking (rtl8139)
                    plus a hostfwd on host:2347 -> guest:2345 so
                    devbench can reach the OS4 amiga-bridge in TCP
                    mode. This is the default; only present here
                    for symmetry with --no-net.
  --no-net          Force networking off. The bridge cannot use its
                    default TCP transport without this; launch the
                    bridge with 'SERIAL' arg to fall back through
                    the -serial tcp:: passthrough on :2346.

Networking is ON by default (the PPC bridge's TCP transport needs
a NIC in the guest). Pass --no-net only when you deliberately
want the serial-only path.

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

# No interactive ask — the PPC bridge default (TCP) needs network,
# so this script's job is to always bring one up. Use --no-net only
# for the rare case where you actually don't want it (headless
# smoke test, deliberate serial-only debugging, port 2347 conflict).

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
#
# zoom-to-fit is ON by default so the QEMU window is resizable and
# stays roughly usable at any size. Cost: on macOS Retina the input
# path (USB tablet, absolute coords) doesn't get scaled the same way
# the framebuffer does, so when the window is not exactly at the
# guest's native resolution the mouse pointer can drift by up to the
# scale factor — click lands ~20-30px away from where you aimed.
#
# If that bites you, opt into precise-mouse mode:
#     DISPLAY_ARG=cocoa,zoom-to-fit=off,show-cursor=on ./scripts/start-qemu-os4.sh
# The window becomes fixed at the guest framebuffer size (small but
# tablet coords match exactly).
#
# Long-term fix on the guest: change OS4's ScreenMode (Prefs → Screen)
# to a bigger resolution (1024×768 or 1280×1024). Bigger framebuffer =
# bigger window at 1:1, plus mouse still accurate.
case "$(uname -s)" in
    Darwin) DISPLAY_ARG="${DISPLAY_ARG:-cocoa,zoom-to-fit=on,show-cursor=on}" ;;
    Linux)  DISPLAY_ARG="${DISPLAY_ARG:-gtk,zoom-to-fit=on,show-cursor=on}"   ;;
    *)      DISPLAY_ARG="${DISPLAY_ARG:-sdl}" ;;
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
# HMP monitor on TCP 2348 — used to peek guest-physical RAM via `xp`
# during virtnet DMA debugging. Never wait for a connection so QEMU
# always boots.
QEMU_CMD+=( -monitor "tcp:127.0.0.1:2348,server,nowait" )
read -ra _gdb_arr   <<<"$GDB_ARGS";  QEMU_CMD+=( "${_gdb_arr[@]}" )
# Networking off by default (base OS4 has no TCP/IP configured). --net
# adds a user-mode NAT e1000 NIC which OS4 sees as an e1000 device
# handled by Roadshow. Guest sees DNS/NAT via QEMU's built-in stack;
# no host-side setup needed.
if [ "$NET_MODE" -eq 1 ]; then
    # Single NIC: Intel 82540EM (e1000) on subnet 192.168.100.0/24, wired
    # to Bill Borsari's virte1000.device (the known-working e1000 driver;
    # see the bill_e1000_driver memory). Guest DHCPs .15 from QEMU; SLIRP
    # gateway is .2 and doubles as a host proxy.
    #   host 17777 -> guest UDP  (latency echo)
    #   host 17778 -> guest TCP  (bandwidth listener)
    #
    # The rtl8139 that used to sit on n0 (with a hostfwd for the TCP-mode
    # amiga-bridge) is gone: bridge now runs in SERIAL mode via the
    # `-serial tcp::2346,server` passthrough (see S:User-Startup on the
    # guest — "Run >NIL: DH1:amiga-bridge SERIAL"), so no NIC is needed
    # for devbench control. Additional NICs used for other driver work
    # (rtl8139re, virtnet) are DISABLED and can be re-enabled by uncommenting:
    # QEMU_CMD+=( -netdev "user,id=n0,hostfwd=tcp::2347-:2345" \
    #             -device rtl8139,netdev=n0 )
    # QEMU_CMD+=( -netdev "user,id=n2,net=192.168.101.0/24,hostfwd=udp::17877-192.168.101.15:17877,hostfwd=tcp::17878-192.168.101.15:17878" \
    #             -device virtio-net-pci,netdev=n2 )
    # QEMU_CMD+=( -netdev "user,id=n3,net=192.168.102.0/24,hostfwd=udp::17977-192.168.102.15:17977,hostfwd=tcp::17978-192.168.102.15:17978" \
    #             -device rtl8139,netdev=n3,mac=52:54:00:12:34:59 )
    QEMU_CMD+=( -netdev "user,id=n1,net=192.168.100.0/24,hostfwd=udp::17777-192.168.100.15:17777,hostfwd=tcp::17778-192.168.100.15:17778" \
                -device e1000-82540em,netdev=n1 )
    QEMU_CMD+=( -object "filter-dump,id=n1-dump,netdev=n1,file=/tmp/qemu-n1.pcap" )
    # QEMU_CMD+=( -object "filter-dump,id=n2-dump,netdev=n2,file=/tmp/qemu-n2.pcap" )
    # Temporary trace hack for virtnet debugging — capture virtio_pci
    # writes/reads too so we can see queue-setup event ordering.
    # virtio-net trace directives removed with n2. Restore alongside n2 if needed.
    NET_STATUS="single NIC: e1000-82540em (virte1000; 17777/17778); pcap /tmp/qemu-n1.pcap (bridge on SERIAL passthrough :2346)"
else
    QEMU_CMD+=( -nic none )
    NET_STATUS="disabled (-nic none)"
fi
# Mouse mode. Two options:
#
#   USB_TABLET=1  (opt-in)  — attach usb-tablet HID device for
#     absolute-position coords. When OS4's USB stack recognises it,
#     you don't need to grab the mouse; hover-and-click works. When
#     OS4's USB stack DOESN'T recognise it (which we've observed
#     mid-session after crash-reboots), clicks get lost entirely and
#     the mouse becomes unusable. Also on Retina + zoom-to-fit=on
#     the coordinate mapping drifts.
#
#   default (no flag)  — no tablet device. QEMU's Cocoa layer falls
#     back to classic click-to-grab: click in the window and the
#     mouse gets captured; press Ctrl-Opt-G (or Ctrl-Alt-G on some
#     kbds) to release it back to macOS. Works with any guest, no
#     driver assumptions.
#
# We default to click-to-grab because that mode has never failed on
# us; the tablet is off by default until OS4 has a persistent USB
# input setup that survives reboots.
if [ "${USB_TABLET:-0}" = "1" ]; then
    QEMU_CMD+=( -usb -device usb-tablet )
    MOUSE_STATUS="absolute (usb-tablet)"
else
    MOUSE_STATUS="click-to-grab (Ctrl-Opt-G to release)"
fi
QEMU_CMD+=( -display "$DISPLAY_ARG" -name "AmigaOS 4.1 - DevBench" )

echo "=== Starting QEMU sam460ex ==="
echo "  Machine:    sam460ex (PowerPC 460EX)"
echo "  RAM:        1024 MB"
echo "  System HDD: $HDD_SYSTEM"
echo "  Dev HDD:    $HDD_DEV"
echo "  Serial:     TCP port $SERIAL_PORT"
echo "  Network:    $NET_STATUS"
echo "  Mouse:      $MOUSE_STATUS"
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
