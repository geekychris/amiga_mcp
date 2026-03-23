#!/bin/bash
# One-time setup: pull Docker image, install devbench, build everything.
#
# Usage:
#   ./scripts/setup.sh
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
DOCKER_IMAGE="amigadev/crosstools:m68k-amigaos"

echo "=== Amiga DevBench Development Environment Setup ==="
echo ""

# 1. Check prerequisites
echo "--- Checking prerequisites ---"

if ! command -v docker >/dev/null 2>&1; then
    echo "ERROR: Docker not found. Install Docker Desktop first."
    echo "  macOS:   brew install --cask docker"
    echo "  Linux:   sudo apt-get install docker.io"
    echo "  Windows: https://www.docker.com/products/docker-desktop/"
    exit 1
fi
echo "  OK Docker installed"

if ! docker info >/dev/null 2>&1; then
    echo "ERROR: Docker is not running. Start Docker Desktop."
    exit 1
fi
echo "  OK Docker running"

if ! command -v python3 >/dev/null 2>&1; then
    echo "ERROR: Python 3 not found. Install Python 3.10+."
    echo "  macOS:   brew install python@3.12"
    echo "  Linux:   sudo apt-get install python3 python3-pip"
    echo "  Windows: https://www.python.org/downloads/"
    exit 1
fi
PY_VER=$(python3 --version)
echo "  OK $PY_VER"

# 2. Pull Docker cross-compiler image
echo ""
echo "--- Docker cross-compiler ---"
if docker image inspect "$DOCKER_IMAGE" >/dev/null 2>&1; then
    echo "  OK Image already pulled: $DOCKER_IMAGE"
else
    echo "  Pulling $DOCKER_IMAGE..."
    docker pull "$DOCKER_IMAGE"
    echo "  OK Image pulled"
fi

# 3. Install amiga-devbench (Python MCP server + web UI)
echo ""
echo "--- Installing amiga-devbench ---"
cd "$PROJECT_ROOT"
pip install -e amiga-devbench
echo "  OK amiga-devbench installed"

# 4. Build Amiga code
echo ""
echo "--- Building Amiga bridge and examples ---"
cd "$PROJECT_ROOT"
make bridge
echo "  OK Bridge built"

echo ""
echo "=== Setup Complete ==="
echo ""
echo "Next steps:"
echo "  1. Start the devbench:      make start"
echo "     (or: python3 -m amiga_devbench)"
echo "  2. Open web dashboard:      http://localhost:3000"
echo "  3. Build examples:          make examples"
echo "  4. Deploy to emulator:      cp examples/hello_world/hello_world <shared-folder>/"
echo ""
echo "To add to Claude Code, put this in your MCP config:"
echo '  {'
echo '    "mcpServers": {'
echo '      "amiga-dev": {'
echo '        "type": "streamable-http",'
echo '        "url": "http://localhost:3000/mcp"'
echo '      }'
echo '    }'
echo '  }'
