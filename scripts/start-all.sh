#!/bin/bash
# Start the full Amiga debug environment:
#   1. amiga-devbench (Python MCP server + web UI)
#   2. FS-UAE emulator
#
# Usage: ./scripts/start-all.sh [--kill]
#   --kill  Stop all processes and exit
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
PIDFILE_DIR="/tmp/amiga-dev"
mkdir -p "$PIDFILE_DIR"

FSUAE_PID="$PIDFILE_DIR/fsuae.pid"
DEVBENCH_PID="$PIDFILE_DIR/devbench.pid"
DEVBENCH_LOG="$PIDFILE_DIR/devbench.log"

FSUAE_CONFIG="$HOME/Documents/FS-UAE/Configurations/AmiKit-Debug.fs-uae"

kill_if_running() {
    local pidfile="$1"
    local name="$2"
    if [ -f "$pidfile" ]; then
        local pid=$(cat "$pidfile")
        if kill -0 "$pid" 2>/dev/null; then
            echo "  Stopping $name (PID $pid)"
            kill "$pid" 2>/dev/null
            sleep 1
            kill -0 "$pid" 2>/dev/null && kill -9 "$pid" 2>/dev/null
        fi
        rm -f "$pidfile"
    fi
    # Also kill by name as fallback
    case "$name" in
        fs-uae)    pkill -f "fs-uae" 2>/dev/null || true ;;
        devbench)  pkill -f "amiga_devbench" 2>/dev/null || true
                   lsof -ti :3000 2>/dev/null | xargs kill 2>/dev/null || true ;;
    esac
}

stop_all() {
    echo "--- Stopping all ---"
    kill_if_running "$DEVBENCH_PID" "devbench"
    kill_if_running "$FSUAE_PID" "fs-uae"
    rm -f /tmp/amiga-serial
    echo "  Done."
}

if [ "${1:-}" = "--kill" ]; then
    stop_all
    exit 0
fi

# Stop anything already running
stop_all
sleep 1

echo ""
echo "=== Starting Amiga Debug Environment ==="
echo ""

# 1. amiga-devbench (Python MCP server + web UI)
echo "--- Starting amiga-devbench ---"
cd "$PROJECT_ROOT"
python3 -m amiga_devbench > "$DEVBENCH_LOG" 2>&1 &
echo $! > "$DEVBENCH_PID"
# Wait for HTTP port to be ready
for i in 1 2 3 4 5 6 7 8 9 10; do
    if nc -z 127.0.0.1 3000 2>/dev/null; then
        break
    fi
    sleep 1
done
if ! nc -z 127.0.0.1 3000 2>/dev/null; then
    echo "  ERROR: devbench failed to start. Log:"
    tail -20 "$DEVBENCH_LOG"
    stop_all
    exit 1
fi
echo "  devbench running (PID $(cat "$DEVBENCH_PID"))"
echo "  Web UI: http://localhost:3000"

# 2. FS-UAE
echo ""
echo "--- Starting FS-UAE ---"
if [ ! -f "$FSUAE_CONFIG" ]; then
    echo "  WARNING: Config not found: $FSUAE_CONFIG"
    echo "  Skipping emulator start. Launch it manually."
else
    fs-uae "$FSUAE_CONFIG" &
    echo $! > "$FSUAE_PID"
    sleep 2
    if ! kill -0 $(cat "$FSUAE_PID") 2>/dev/null; then
        echo "  WARNING: FS-UAE failed to start"
    else
        echo "  FS-UAE running (PID $(cat "$FSUAE_PID"))"
    fi
fi

echo ""
echo "=== All services running ==="
echo ""
echo "  Devbench log: $DEVBENCH_LOG"
echo "  Web UI:       http://localhost:3000"
echo ""
echo "To stop everything:"
echo "  ./scripts/start-all.sh --kill"
echo ""
