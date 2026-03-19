#!/bin/bash
#
# Set up AmigaOS 4.1 development environment
#
# This script:
# 1. Verifies QEMU PPC is installed
# 2. Creates HDD images
# 3. Pulls the PPC cross-compiler Docker image
# 4. Verifies the toolchain works
# 5. Creates devbench config for PPC

set -e

OS4_DIR="${OS4_DIR:-$HOME/AmigaOS4}"
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

echo "=== AmigaOS 4.1 PPC Development Setup ==="
echo "  Project: $PROJECT_DIR"
echo "  OS4 dir: $OS4_DIR"
echo ""

# Step 1: Check QEMU
echo "--- Step 1: QEMU PPC ---"
if command -v qemu-system-ppc &>/dev/null; then
    echo "  QEMU PPC: $(qemu-system-ppc --version | head -1)"
    # Verify sam460ex machine
    if qemu-system-ppc -machine help 2>/dev/null | grep -q sam460ex; then
        echo "  sam460ex machine: available"
    else
        echo "  WARNING: sam460ex machine not available in this QEMU build"
    fi
else
    echo "  QEMU PPC: NOT FOUND"
    echo "  Install with: brew install qemu"
    echo "  Or build from source with --target-list=ppc-softmmu"
    exit 1
fi
echo ""

# Step 2: Create directories and HDD images
echo "--- Step 2: Directories and HDD images ---"
mkdir -p "$OS4_DIR"

if [ ! -f "$OS4_DIR/amigaos4-system.hdf" ]; then
    echo "  Creating system HDD (2GB)..."
    qemu-img create -f raw "$OS4_DIR/amigaos4-system.hdf" 2G
else
    echo "  System HDD: exists ($(du -h "$OS4_DIR/amigaos4-system.hdf" | cut -f1))"
fi

if [ ! -f "$OS4_DIR/amigaos4-dev.hdf" ]; then
    echo "  Creating dev HDD (512MB)..."
    qemu-img create -f raw "$OS4_DIR/amigaos4-dev.hdf" 512M
else
    echo "  Dev HDD: exists ($(du -h "$OS4_DIR/amigaos4-dev.hdf" | cut -f1))"
fi
echo ""

# Step 3: Check for OS4 files
echo "--- Step 3: AmigaOS 4.1 files ---"
MISSING=0
if [ -f "$OS4_DIR/AmigaOS4.1-FE.iso" ]; then
    echo "  Install ISO: found"
else
    echo "  Install ISO: NOT FOUND"
    echo "    Download from: https://www.hyperion-entertainment.com/"
    echo "    Place at: $OS4_DIR/AmigaOS4.1-FE.iso"
    MISSING=1
fi

if [ -f "$OS4_DIR/u-boot-sam460ex.bin" ]; then
    echo "  U-Boot firmware: found"
else
    echo "  U-Boot firmware: NOT FOUND (QEMU may have built-in support)"
    echo "    If needed, obtain from A-EON or build from source"
fi
echo ""

# Step 4: PPC Cross-Compiler
echo "--- Step 4: PPC Cross-Compiler ---"

# Check native first
if command -v ppc-amigaos-gcc &>/dev/null; then
    echo "  Native ppc-amigaos-gcc: $(ppc-amigaos-gcc --version 2>&1 | head -1)"
else
    echo "  Native ppc-amigaos-gcc: not installed"
fi

# Check Docker
if command -v docker &>/dev/null; then
    echo "  Docker: available"

    # Try to pull the PPC image
    PPC_IMAGE="walkero/amigagccondocker:os4-gcc11-arm64"
    echo "  Pulling PPC Docker image: $PPC_IMAGE"
    if docker pull "$PPC_IMAGE" 2>/dev/null; then
        echo "  PPC Docker image: ready"
        # Verify compiler
        echo "  Testing ppc-amigaos-gcc..."
        GCC_VER=$(docker run --rm "$PPC_IMAGE" ppc-amigaos-gcc --version 2>&1 | head -1)
        if [ -n "$GCC_VER" ]; then
            echo "  Compiler: $GCC_VER"
        else
            echo "  WARNING: Compiler not found in image, trying alternate..."
            # Try alternate image
            PPC_IMAGE="amigadev/crosstools:ppc-amigaos"
            docker pull "$PPC_IMAGE" 2>/dev/null || true
        fi
    else
        echo "  WARNING: Could not pull Docker image"
        echo "  Docker daemon may not be running"
    fi
else
    echo "  Docker: NOT FOUND"
    echo "  Install Docker Desktop or Rancher Desktop for cross-compilation"
fi
echo ""

# Step 5: Create devbench config
echo "--- Step 5: DevBench PPC config ---"
PPC_CONFIG="$PROJECT_DIR/devbench-ppc.toml"
if [ ! -f "$PPC_CONFIG" ]; then
    cat > "$PPC_CONFIG" << 'TOML'
# Amiga DevBench Configuration — AmigaOS 4.1 PPC
# Copy to devbench.toml or use: python3 -m amiga_devbench --config devbench-ppc.toml

[serial]
mode = "tcp"
host = "127.0.0.1"
port = 2346
pty_path = "/tmp/amiga-serial-ppc"

[emulator]
# QEMU sam460ex
binary = "/opt/homebrew/bin/qemu-system-ppc"
config = "scripts/start-qemu-os4.sh"
auto_start = false  # Manual start recommended for QEMU

[server]
port = 3000
log_level = "INFO"

[build]
arch = "ppc"
docker_image = "walkero/amigagccondocker:os4-gcc11-arm64"

[paths]
# Adjust deploy_dir when OS4 dev HDD mount point is known
deploy_dir = ""

[bridge]
crash_handler_auto_enable = true
TOML
    echo "  Created: $PPC_CONFIG"
else
    echo "  Config exists: $PPC_CONFIG"
fi
echo ""

# Summary
echo "=== Setup Summary ==="
echo ""
echo "QEMU:       ready"
echo "HDD images: ready"
if [ $MISSING -eq 1 ]; then
    echo "OS4 files:  INCOMPLETE (see above)"
else
    echo "OS4 files:  ready"
fi
echo "Compiler:   check Docker status above"
echo ""
echo "Next steps:"
echo "  1. Obtain AmigaOS 4.1 FE ISO if not already done"
echo "  2. Run: ./scripts/start-qemu-os4.sh --install"
echo "  3. Install OS to the system HDD"
echo "  4. Run: ./scripts/start-qemu-os4.sh"
echo "  5. Configure serial.device on AmigaOS 4.1"
echo "  6. Build bridge: make bridge ARCH=ppc"
echo "  7. Deploy and run on OS4"
echo ""
