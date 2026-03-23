#!/bin/bash
# Start the amiga-devbench server. Optionally with the built-in simulator.
#
# Usage:
#   ./scripts/start-mcp.sh              # Start devbench (reads devbench.toml)
#   ./scripts/start-mcp.sh --simulator  # Start with built-in Amiga simulator
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_ROOT"

if [ "$1" = "--simulator" ] || [ "$1" = "--sim" ]; then
    echo "Starting amiga-devbench with simulator..."
    exec python3 -m amiga_devbench --simulator
else
    echo "Starting amiga-devbench..."
    echo "Web UI: http://localhost:3000"
    echo "MCP:    http://localhost:3000/mcp"
    exec python3 -m amiga_devbench
fi
