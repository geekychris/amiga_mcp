#!/usr/bin/env bash
# start-fsuae.sh — start FS-UAE with the AmiKit-Debug config that pairs
# with amiga-devbench on TCP :2345.
#
# This is DIFFERENT from start-qemu-os4.sh:
#   QEMU sam460ex runs AmigaOS 4.1 PPC (the target for virte1000/virtnet/rtl8139re)
#   FS-UAE runs classic-Amiga 68k AmigaOS 3.x (via AmiKit) — used for the
#   68k build/test half of the amiga_mcp toolchain.
#
# Usage:
#   ./scripts/start-fsuae.sh              # start (or restart) FS-UAE
#   ./scripts/start-fsuae.sh --stop       # kill only
#   ./scripts/start-fsuae.sh --status     # is it running?
#
# Prereqs (already true on chris' laptop, checked at startup):
#   - fs-uae in PATH (brew install fs-uae)
#   - Config at $FSUAE_CONFIG (default AmiKit-Debug)
#   - kick.rom present per config
#
# The config exposes serial to tcp://0.0.0.0:2345 so amiga-devbench can
# attach as a TCP client. If you also want to run the QEMU OS4 guest,
# start it FIRST (it uses tcp:2347) — FS-UAE and QEMU can coexist as
# each has its own serial-over-TCP port.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FSUAE_CONFIG="${FSUAE_CONFIG:-$HOME/Documents/FS-UAE/Configurations/AmiKit-Debug.fs-uae}"
PIDFILE_DIR="${TMPDIR:-/tmp}"
FSUAE_PID_FILE="$PIDFILE_DIR/fsuae.pid"
FSUAE_LOG="$PIDFILE_DIR/fsuae.log"

# Prefer /opt/homebrew/bin/fs-uae on arm64 macs; else PATH lookup.
FSUAE_BIN="${FSUAE_BIN:-$(command -v fs-uae || echo /opt/homebrew/bin/fs-uae)}"

is_running() {
    [ -f "$FSUAE_PID_FILE" ] || return 1
    local pid
    pid="$(cat "$FSUAE_PID_FILE")"
    [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null
}

cmd_stop() {
    if is_running; then
        local pid; pid="$(cat "$FSUAE_PID_FILE")"
        echo "Killing FS-UAE (PID $pid)"
        kill "$pid" 2>/dev/null || true
        sleep 1
        kill -9 "$pid" 2>/dev/null || true
        rm -f "$FSUAE_PID_FILE"
    fi
    # Also sweep for any orphaned FS-UAE with our config name (belt +
    # suspenders — a crashed pidfile shouldn't leave FS-UAE alive).
    pkill -f "fs-uae.*$(basename "$FSUAE_CONFIG")" 2>/dev/null || true
}

cmd_status() {
    if is_running; then
        local pid; pid="$(cat "$FSUAE_PID_FILE")"
        echo "FS-UAE running (PID $pid, config $FSUAE_CONFIG)"
        return 0
    fi
    echo "FS-UAE not running"
    return 1
}

cmd_start() {
    if [ ! -x "$FSUAE_BIN" ] && ! command -v fs-uae >/dev/null 2>&1; then
        echo "ERROR: fs-uae not found. brew install fs-uae." >&2
        exit 2
    fi
    if [ ! -f "$FSUAE_CONFIG" ]; then
        echo "ERROR: FS-UAE config not found: $FSUAE_CONFIG" >&2
        echo "  Set FSUAE_CONFIG env var to point at a different .fs-uae file." >&2
        exit 3
    fi

    # If already running, stop first so we get a clean boot.
    if is_running; then
        echo "FS-UAE already running — restarting"
        cmd_stop
    fi

    echo "Starting FS-UAE with config: $FSUAE_CONFIG"
    nohup "$FSUAE_BIN" "$FSUAE_CONFIG" > "$FSUAE_LOG" 2>&1 &
    local pid=$!
    echo "$pid" > "$FSUAE_PID_FILE"
    sleep 3

    if is_running; then
        echo "FS-UAE started (PID $pid, log $FSUAE_LOG)"
        echo ""
        echo "Serial exposed at tcp://127.0.0.1:2345"
        echo "  Attach amiga-devbench: python -m amiga_devbench --profile fsuae \\"
        echo "                                                   --serial-host 127.0.0.1 --serial-port 2345"
    else
        echo "ERROR: FS-UAE failed to start. Log:"
        tail -30 "$FSUAE_LOG" >&2
        rm -f "$FSUAE_PID_FILE"
        exit 4
    fi
}

case "${1:-start}" in
    --stop|stop)     cmd_stop ;;
    --status|status) cmd_status ;;
    --start|start|"") cmd_start ;;
    *)
        echo "Usage: $0 [start|stop|status]" >&2
        exit 1
        ;;
esac
