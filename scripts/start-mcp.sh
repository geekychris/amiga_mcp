#!/bin/bash
# Start the MCP server. Optionally also start the Amiga simulator.
#
# By default, the server starts in PTY mode (creates /tmp/amiga-serial).
# Set AMIGA_SERIAL_HOST to use TCP mode instead (for WinUAE).
#
# Usage:
#   ./scripts/start-mcp.sh              # Start MCP server (PTY mode)
#   ./scripts/start-mcp.sh --tcp        # Start MCP server (TCP mode)
#   ./scripts/start-mcp.sh --simulator  # Start simulator + MCP server (TCP mode)
#   ./scripts/start-mcp.sh --sim        # Same as above (shorthand)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
MCP_DIR="$PROJECT_ROOT/mcp-server"

export AMIGA_PROJECT_ROOT="$PROJECT_ROOT"

cd "$MCP_DIR"

# Check dependencies
if [ ! -d "node_modules" ]; then
    echo "Installing dependencies..."
    npm install
fi

if [ "$1" = "--simulator" ] || [ "$1" = "--sim" ]; then
    # Simulator uses TCP, so force TCP mode
    export AMIGA_SERIAL_HOST="127.0.0.1"
    echo "Starting Amiga simulator on TCP port ${AMIGA_SERIAL_PORT:-1234}..."
    npx tsx src/simulator.ts &
    SIM_PID=$!
    trap "kill $SIM_PID 2>/dev/null" EXIT
    sleep 1
    echo ""
elif [ "$1" = "--tcp" ]; then
    export AMIGA_SERIAL_HOST="${AMIGA_SERIAL_HOST:-127.0.0.1}"
    echo "Starting MCP server in TCP mode (${AMIGA_SERIAL_HOST}:${AMIGA_SERIAL_PORT:-1234})"
fi

echo "Starting MCP server on http://localhost:${MCP_PORT:-3000}/mcp"
if [ -z "$AMIGA_SERIAL_HOST" ]; then
    echo "Mode: PTY (will create ${AMIGA_PTY_PATH:-/tmp/amiga-serial} for FS-UAE)"
else
    echo "Mode: TCP (connecting to ${AMIGA_SERIAL_HOST}:${AMIGA_SERIAL_PORT:-1234})"
fi
exec npx tsx src/index.ts
