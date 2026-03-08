#!/bin/bash
# One-time setup: pull Docker image, install MCP server dependencies, build everything.
#
# Usage:
#   ./scripts/setup.sh
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
DOCKER_IMAGE="amigadev/crosstools:m68k-amigaos"

echo "=== Amiga MCP Development Environment Setup ==="
echo ""

# 1. Check prerequisites
echo "--- Checking prerequisites ---"

if ! command -v docker >/dev/null 2>&1; then
    echo "ERROR: Docker not found. Install Docker Desktop first."
    exit 1
fi
echo "  ✓ Docker installed"

if ! docker info >/dev/null 2>&1; then
    echo "ERROR: Docker is not running. Start Docker Desktop."
    exit 1
fi
echo "  ✓ Docker running"

if ! command -v node >/dev/null 2>&1; then
    echo "ERROR: Node.js not found. Install Node.js 20+."
    exit 1
fi
NODE_VER=$(node -v)
echo "  ✓ Node.js $NODE_VER"

if ! command -v npm >/dev/null 2>&1; then
    echo "ERROR: npm not found."
    exit 1
fi
echo "  ✓ npm $(npm -v)"

# 2. Pull Docker cross-compiler image
echo ""
echo "--- Docker cross-compiler ---"
if docker image inspect "$DOCKER_IMAGE" >/dev/null 2>&1; then
    echo "  ✓ Image already pulled: $DOCKER_IMAGE"
else
    echo "  Pulling $DOCKER_IMAGE..."
    docker pull "$DOCKER_IMAGE"
    echo "  ✓ Image pulled"
fi

# 3. Install MCP server dependencies
echo ""
echo "--- MCP server ---"
cd "$PROJECT_ROOT/mcp-server"
npm install
npm run build
echo "  ✓ MCP server built"

# 4. Build Amiga code
echo ""
echo "--- Amiga libraries and examples ---"
cd "$PROJECT_ROOT"
"$SCRIPT_DIR/build.sh" all

echo ""
echo "=== Setup Complete ==="
echo ""
echo "Next steps:"
echo "  1. Start the simulator:    cd mcp-server && npm run simulator"
echo "  2. Start the MCP server:   cd mcp-server && npm run dev"
echo "  3. Or test the build:      ./scripts/build.sh"
echo "  4. Deploy to emulator:     AMIGA_DEPLOY_DIR=/path/to/share ./scripts/deploy.sh"
echo ""
echo "To add to Claude Code, put this in your MCP config:"
echo "  {"
echo "    \"mcpServers\": {"
echo "      \"amiga-dev\": {"
echo "        \"type\": \"streamable-http\","
echo "        \"url\": \"http://localhost:3000/mcp\""
echo "      }"
echo "    }"
echo "  }"
