#!/bin/bash
# Start the full Amiga debug environment:
#   1. MCP server (includes built-in PTY for FS-UAE serial)
#   2. FS-UAE emulator
#
# The MCP server now creates the PTY directly - no separate bridge needed.
#
# Usage: ./scripts/start-all.sh [--kill]
#   --kill  Stop all processes and exit
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
PIDFILE_DIR="/tmp/amiga-dev"
mkdir -p "$PIDFILE_DIR"

FSUAE_PID="$PIDFILE_DIR/fsuae.pid"
MCP_PID="$PIDFILE_DIR/mcp.pid"
MCP_LOG="$PIDFILE_DIR/mcp.log"

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
        fs-uae) pkill -f "fs-uae" 2>/dev/null || true ;;
        mcp)    pkill -f "tsx src/index.ts" 2>/dev/null || true
                lsof -ti :3000 2>/dev/null | xargs kill 2>/dev/null || true ;;
    esac
}

stop_all() {
    echo "--- Stopping all ---"
    kill_if_running "$MCP_PID" "mcp"
    kill_if_running "$FSUAE_PID" "fs-uae"
    # Clean up PTY symlink (MCP server should do this, but just in case)
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

# 1. MCP server (creates PTY automatically)
echo "--- Starting MCP server (with built-in PTY) ---"
cd "$PROJECT_ROOT/mcp-server"
export AMIGA_PROJECT_ROOT="$PROJECT_ROOT"
# PTY mode is auto-detected (no AMIGA_SERIAL_HOST set)
npm run dev > "$MCP_LOG" 2>&1 &
echo $! > "$MCP_PID"
# Wait for HTTP port to be ready
for i in 1 2 3 4 5 6 7 8 9 10; do
    if nc -z 127.0.0.1 3000 2>/dev/null; then
        break
    fi
    sleep 1
done
if ! nc -z 127.0.0.1 3000 2>/dev/null; then
    echo "  ERROR: MCP server failed to start. Log:"
    tail -20 "$MCP_LOG"
    stop_all
    exit 1
fi
echo "  MCP server running (PID $(cat "$MCP_PID"))"
echo "  PTY: /tmp/amiga-serial (auto-created by MCP server)"
cd "$PROJECT_ROOT"

# 2. FS-UAE
echo ""
echo "--- Starting FS-UAE ---"
if [ ! -f "$FSUAE_CONFIG" ]; then
    echo "  ERROR: Config not found: $FSUAE_CONFIG"
    exit 1
fi
fs-uae "$FSUAE_CONFIG" &
echo $! > "$FSUAE_PID"
sleep 2
if ! kill -0 $(cat "$FSUAE_PID") 2>/dev/null; then
    echo "  ERROR: FS-UAE failed to start"
    stop_all
    exit 1
fi
echo "  FS-UAE running (PID $(cat "$FSUAE_PID"))"

echo ""
echo "=== All services running ==="
echo ""
echo "  MCP log:     $MCP_LOG"
echo ""
echo "Next steps:"
echo "  1. In FS-UAE, open Shell and run: DH2:Dev/hello_world"
echo "  2. In Claude, do /mcp to reconnect, then use amiga tools"
echo ""
echo "To stop everything:"
echo "  ./scripts/start-all.sh --kill"
echo ""
