#!/bin/bash
# Deploy built Amiga binaries to AmiKit's shared folder.
#
# Binaries are copied to the Dropbox/Dev/ directory, which is
# accessible on the Amiga as AK2:Dev/
#
# Usage:
#   ./scripts/deploy.sh                          # Deploy all built examples
#   ./scripts/deploy.sh hello_world              # Deploy specific example
#   AMIGA_DEPLOY_DIR=/path/to/share ./scripts/deploy.sh  # Custom deploy path
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

EXAMPLES="hello_world bouncing_ball system_monitor"

# Auto-detect AmiKit Dropbox folder
AMIKIT_DROPBOX="/Applications/AmiKit.app/Contents/SharedSupport/prefix/drive_c/AmiKit/Dropbox/Dev"

if [ -n "$AMIGA_DEPLOY_DIR" ]; then
    DEPLOY_DIR="$AMIGA_DEPLOY_DIR"
elif [ -d "/Applications/AmiKit.app" ]; then
    DEPLOY_DIR="$AMIKIT_DROPBOX"
    mkdir -p "$DEPLOY_DIR"
elif [ -d "$HOME/FS-UAE/Hard Drives/Work" ]; then
    DEPLOY_DIR="$HOME/FS-UAE/Hard Drives/Work"
else
    echo "ERROR: Cannot find emulator shared folder."
    echo ""
    echo "AmiKit not found at /Applications/AmiKit.app"
    echo "Set AMIGA_DEPLOY_DIR to your emulator's shared folder:"
    echo "  export AMIGA_DEPLOY_DIR=/path/to/shared/folder"
    exit 1
fi

echo "=== Deploying to $DEPLOY_DIR ==="
echo "    (Amiga path: AK2:Dev/)"
echo ""

TARGET="${1:-}"
DEPLOYED=0

deploy_binary() {
    local name="$1"
    local bin="$PROJECT_ROOT/examples/$name/$name"
    if [ -f "$bin" ]; then
        cp "$bin" "$DEPLOY_DIR/"
        local size
        size=$(ls -lh "$bin" | awk '{print $5}')
        echo "  ✓ $name ($size)"
        DEPLOYED=$((DEPLOYED + 1))
    else
        echo "  ✗ $name (not built - run ./scripts/build.sh first)"
    fi
}

if [ -n "$TARGET" ]; then
    found=0
    for ex in $EXAMPLES; do
        if [ "$TARGET" = "$ex" ]; then
            deploy_binary "$ex"
            found=1
            break
        fi
    done
    if [ "$found" -eq 0 ]; then
        echo "Unknown example: $TARGET"
        echo "Available: $EXAMPLES"
        exit 1
    fi
else
    for ex in $EXAMPLES; do
        deploy_binary "$ex"
    done
fi

echo ""
echo "=== $DEPLOYED binary(ies) deployed ==="
echo ""
echo "On the Amiga, open a Shell and run:"
for ex in $EXAMPLES; do
    if [ -f "$DEPLOY_DIR/$ex" ]; then
        echo "  AK2:Dev/$ex"
    fi
done
