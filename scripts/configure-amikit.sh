#!/bin/bash
# Configure AmiKit's WinUAE to enable serial-over-TCP for debug communication.
#
# What this does:
#   - Adds serial_port=TCP://0.0.0.0:1234 to the WinUAE config
#   - Creates a backup of the original config
#   - Sets up the Dropbox shared folder for deploying binaries
#
# IMPORTANT: AmiKit must be restarted after running this script.
#
# Usage:
#   ./scripts/configure-amikit.sh
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

AMIKIT_BASE="/Applications/AmiKit.app/Contents/SharedSupport/prefix/drive_c/AmiKit"
UAE_CONFIG_DIR="$AMIKIT_BASE/RabbitHole/WinUAE/Configurations"
DROPBOX_DIR="$AMIKIT_BASE/Dropbox"
SERIAL_PORT="${AMIGA_SERIAL_PORT:-1234}"

# Configs to patch - all variants
UAE_CONFIGS=(
    "AmiKit.uae"
    "AmiKit-fullwindow.uae"
    "AmiKit-fullscreen.uae"
    "AmiKit-windowed.uae"
)

echo "=== AmiKit Serial Debug Configuration ==="
echo ""

# Verify AmiKit exists
if [ ! -d "$AMIKIT_BASE" ]; then
    echo "ERROR: AmiKit not found at $AMIKIT_BASE"
    echo "Is AmiKit installed in /Applications?"
    exit 1
fi

echo "AmiKit found at: $AMIKIT_BASE"
echo "Serial TCP port: $SERIAL_PORT"
echo ""

# Patch each config file
echo "--- Configuring WinUAE serial port ---"
for config in "${UAE_CONFIGS[@]}"; do
    CONFIG_PATH="$UAE_CONFIG_DIR/$config"
    if [ ! -f "$CONFIG_PATH" ]; then
        echo "  Skipping $config (not found)"
        continue
    fi

    # Create backup if one doesn't exist
    BACKUP="$CONFIG_PATH.bak"
    if [ ! -f "$BACKUP" ]; then
        cp "$CONFIG_PATH" "$BACKUP"
        echo "  Backup: $config -> $config.bak"
    fi

    # Check if serial_port is already configured
    if grep -q "^serial_port=" "$CONFIG_PATH"; then
        CURRENT=$(grep "^serial_port=" "$CONFIG_PATH" | head -1)
        if echo "$CURRENT" | grep -q "TCP://"; then
            echo "  ✓ $config (already configured: $CURRENT)"
            continue
        fi
        # Replace existing serial_port line
        sed -i.tmp "s|^serial_port=.*|serial_port=TCP://0.0.0.0:$SERIAL_PORT|" "$CONFIG_PATH"
        rm -f "$CONFIG_PATH.tmp"
    else
        # Add serial_port after serial_direct line
        sed -i.tmp "/^serial_direct=/a\\
serial_port=TCP://0.0.0.0:$SERIAL_PORT" "$CONFIG_PATH"
        rm -f "$CONFIG_PATH.tmp"
    fi

    echo "  ✓ $config (serial_port=TCP://0.0.0.0:$SERIAL_PORT)"
done

# Set up deploy directory
echo ""
echo "--- Setting up deploy folder ---"

DEV_DIR="$DROPBOX_DIR/Dev"
mkdir -p "$DEV_DIR"
echo "  ✓ Created $DEV_DIR"
echo "     (accessible on Amiga as AK2:Dev/)"

# Deploy any already-built binaries
EXAMPLES="hello_world bouncing_ball system_monitor"
DEPLOYED=0
for ex in $EXAMPLES; do
    BIN="$PROJECT_ROOT/examples/$ex/$ex"
    if [ -f "$BIN" ]; then
        cp "$BIN" "$DEV_DIR/"
        DEPLOYED=$((DEPLOYED + 1))
        echo "  ✓ Deployed $ex"
    fi
done

echo ""
echo "=== Configuration Complete ==="
echo ""
echo "Next steps:"
echo "  1. Quit AmiKit completely (Amiga menu -> Shutdown, then close the window)"
echo "  2. Restart AmiKit"
echo "  3. On the Amiga, open a Shell and run:"
echo "       AK2:Dev/system_monitor"
echo "  4. On the Mac, start the MCP server:"
echo "       ./scripts/start-mcp.sh"
echo "     The MCP server will connect to TCP port $SERIAL_PORT"
echo ""
echo "How it works:"
echo "  - WinUAE listens on TCP port $SERIAL_PORT (serial.device → TCP)"
echo "  - Amiga programs use serial.device as normal"
echo "  - MCP server connects to localhost:$SERIAL_PORT"
echo "  - Your binaries are at AK2:Dev/ on the Amiga"
echo ""
if [ "$DEPLOYED" -eq 0 ]; then
    echo "  No binaries deployed yet. Run ./scripts/build.sh first, then:"
    echo "    ./scripts/deploy.sh"
fi
