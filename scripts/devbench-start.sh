#!/bin/bash
# Start the full Amiga development environment:
#   1. amiga-devbench (PTY + web UI + MCP)
#   2. FS-UAE emulator
#
# Usage:
#   ./scripts/devbench-start.sh           # Start everything
#   ./scripts/devbench-start.sh --sim     # Start with simulator (no FS-UAE)
#   ./scripts/devbench-start.sh --stop    # Stop everything
#   ./scripts/devbench-start.sh --restart # Restart FS-UAE only (devbench stays)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
PIDFILE_DIR="/tmp/amiga-dev"
mkdir -p "$PIDFILE_DIR"

DEVBENCH_PID="$PIDFILE_DIR/devbench.pid"
FSUAE_PID="$PIDFILE_DIR/fsuae.pid"
DEVBENCH_LOG="$PIDFILE_DIR/devbench.log"

FSUAE_CONFIG="$HOME/Documents/FS-UAE/Configurations/AmiKit-Debug.fs-uae"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

status_ok()   { echo -e "  ${GREEN}✓${NC} $1"; }
status_fail() { echo -e "  ${RED}✗${NC} $1"; }
status_info() { echo -e "  ${YELLOW}→${NC} $1"; }

is_running() {
    local pidfile="$1"
    if [ -f "$pidfile" ]; then
        local pid=$(cat "$pidfile")
        if kill -0 "$pid" 2>/dev/null; then
            return 0
        fi
    fi
    return 1
}

stop_process() {
    local pidfile="$1"
    local name="$2"
    if is_running "$pidfile"; then
        local pid=$(cat "$pidfile")
        kill "$pid" 2>/dev/null
        sleep 1
        if kill -0 "$pid" 2>/dev/null; then
            kill -9 "$pid" 2>/dev/null
        fi
        status_ok "Stopped $name (PID $pid)"
    fi
    rm -f "$pidfile"
}

stop_all() {
    echo ""
    echo "Stopping all services..."
    stop_process "$FSUAE_PID" "FS-UAE"
    pkill -f "fs-uae.*AmiKit-Debug" 2>/dev/null || true
    stop_process "$DEVBENCH_PID" "amiga-devbench"
    pkill -f "amiga_devbench" 2>/dev/null || true
    lsof -ti :3000 2>/dev/null | xargs kill 2>/dev/null || true
    rm -f /tmp/amiga-serial
    status_ok "Done"
    echo ""
}

start_devbench() {
    local extra_args="$1"

    if is_running "$DEVBENCH_PID"; then
        status_ok "amiga-devbench already running (PID $(cat "$DEVBENCH_PID"))"
        return 0
    fi

    # Kill anything on port 3000
    lsof -ti :3000 2>/dev/null | xargs kill 2>/dev/null || true
    sleep 1

    cd "$PROJECT_ROOT"

    # Auto-install amiga-devbench if not already installed
    if ! python3 -c "import amiga_devbench" 2>/dev/null; then
        status_info "Installing amiga-devbench package..."
        pip install -e amiga-devbench > "$DEVBENCH_LOG" 2>&1
        if [ $? -ne 0 ]; then
            status_fail "Failed to install amiga-devbench (see $DEVBENCH_LOG)"
            return 1
        fi
        status_ok "amiga-devbench installed"
    fi

    python3 -m amiga_devbench --log-level DEBUG $extra_args > "$DEVBENCH_LOG" 2>&1 &
    echo $! > "$DEVBENCH_PID"

    # Wait for HTTP to be ready
    for i in $(seq 1 15); do
        if curl -s http://localhost:3000/health > /dev/null 2>&1; then
            status_ok "amiga-devbench running (PID $(cat "$DEVBENCH_PID"))"
            return 0
        fi
        sleep 1
    done

    status_fail "amiga-devbench failed to start"
    echo "  Log: $DEVBENCH_LOG"
    tail -20 "$DEVBENCH_LOG" 2>/dev/null
    return 1
}

start_fsuae() {
    if ! [ -f "$FSUAE_CONFIG" ]; then
        status_fail "FS-UAE config not found: $FSUAE_CONFIG"
        return 1
    fi

    # Kill existing FS-UAE
    pkill -f "fs-uae.*AmiKit-Debug" 2>/dev/null || true
    sleep 1

    fs-uae "$FSUAE_CONFIG" &
    echo $! > "$FSUAE_PID"
    sleep 3

    if is_running "$FSUAE_PID"; then
        status_ok "FS-UAE running (PID $(cat "$FSUAE_PID"))"
    else
        status_fail "FS-UAE failed to start"
        return 1
    fi
}

show_status() {
    echo ""
    echo "=== Amiga DevBench Status ==="
    echo ""
    if is_running "$DEVBENCH_PID"; then
        status_ok "amiga-devbench: running (PID $(cat "$DEVBENCH_PID"))"
    else
        status_fail "amiga-devbench: not running"
    fi
    if is_running "$FSUAE_PID"; then
        status_ok "FS-UAE: running (PID $(cat "$FSUAE_PID"))"
    elif pgrep -f "fs-uae" > /dev/null 2>&1; then
        status_info "FS-UAE: running (not managed)"
    else
        status_fail "FS-UAE: not running"
    fi
    if [ -e /tmp/amiga-serial ]; then
        status_ok "PTY: /tmp/amiga-serial exists"
    else
        status_fail "PTY: /tmp/amiga-serial missing"
    fi
    echo ""
    echo "  Web UI:   http://localhost:3000/"
    echo "  MCP:      http://localhost:3000/mcp"
    echo "  Log:      $DEVBENCH_LOG"
    echo ""
}

# --- Main ---

case "${1:-}" in
    --stop)
        stop_all
        exit 0
        ;;
    --restart)
        echo ""
        echo "=== Restarting FS-UAE (devbench stays running) ==="
        echo ""
        stop_process "$FSUAE_PID" "FS-UAE"
        pkill -f "fs-uae.*AmiKit-Debug" 2>/dev/null || true
        sleep 1
        # Make sure devbench is running
        if ! is_running "$DEVBENCH_PID"; then
            status_info "amiga-devbench not running, starting it..."
            start_devbench
        fi
        start_fsuae
        show_status
        echo "Next: On Amiga shell, run:"
        echo "  AK2:Dev/amiga-bridge"
        echo "  AK2:Dev/hello_world"
        echo ""
        exit 0
        ;;
    --status)
        show_status
        exit 0
        ;;
    --sim)
        echo ""
        echo "=== Starting Amiga DevBench (Simulator Mode) ==="
        echo ""
        stop_all
        start_devbench "--simulator"
        show_status
        exit 0
        ;;
    "")
        echo ""
        echo "=== Starting Amiga Development Environment ==="
        echo ""
        stop_all
        sleep 1
        start_devbench
        start_fsuae
        show_status
        echo "Next: On Amiga shell, run:"
        echo "  AK2:Dev/amiga-bridge"
        echo "  AK2:Dev/hello_world"
        echo ""
        exit 0
        ;;
    *)
        echo "Usage: $0 [--stop|--restart|--status|--sim]"
        echo ""
        echo "  (no args)   Start devbench + FS-UAE"
        echo "  --sim       Start devbench with built-in simulator (no FS-UAE)"
        echo "  --restart   Restart FS-UAE only (devbench stays running)"
        echo "  --stop      Stop everything"
        echo "  --status    Show status"
        exit 1
        ;;
esac
