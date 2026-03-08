#!/bin/bash
# Build Amiga projects using Docker cross-compiler.
#
# Usage:
#   ./scripts/build.sh              # Build everything (lib + all examples)
#   ./scripts/build.sh lib          # Build only the debug library
#   ./scripts/build.sh examples     # Build only examples (requires lib)
#   ./scripts/build.sh clean        # Clean all build artifacts
#   ./scripts/build.sh hello_world  # Build a single example by name
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
DOCKER_IMAGE="amigadev/crosstools:m68k-amigaos"

# All available example projects
EXAMPLES="hello_world bouncing_ball system_monitor"

echo "=== Amiga Cross-Build ==="

# Check Docker is running
if ! docker info >/dev/null 2>&1; then
    echo "ERROR: Docker is not running. Start Docker Desktop and try again."
    exit 1
fi

# Pull image if not present
if ! docker image inspect "$DOCKER_IMAGE" >/dev/null 2>&1; then
    echo "Pulling $DOCKER_IMAGE (first time only)..."
    docker pull "$DOCKER_IMAGE"
fi

docker_make() {
    docker run --rm \
        -v "$PROJECT_ROOT:/work" \
        -w /work \
        "$DOCKER_IMAGE" \
        make -C "$1" ${2:-}
}

TARGET="${1:-all}"

case "$TARGET" in
    all)
        echo "Building: debug library + all examples"
        docker_make amiga-debug-lib
        for ex in $EXAMPLES; do
            docker_make "examples/$ex"
        done
        ;;
    lib)
        echo "Building: debug library"
        docker_make amiga-debug-lib
        ;;
    examples)
        echo "Building: all examples"
        for ex in $EXAMPLES; do
            docker_make "examples/$ex"
        done
        ;;
    clean)
        echo "Cleaning all build artifacts"
        docker_make amiga-debug-lib clean
        for ex in $EXAMPLES; do
            docker_make "examples/$ex" clean
        done
        ;;
    *)
        # Check if it's a known example name
        for ex in $EXAMPLES; do
            if [ "$TARGET" = "$ex" ]; then
                echo "Building: examples/$ex"
                docker_make "examples/$ex"
                echo "=== Build complete ==="
                exit 0
            fi
        done
        echo "Unknown target: $TARGET"
        echo "Usage: $0 [all|lib|examples|clean|$EXAMPLES]"
        exit 1
        ;;
esac

# Show results
echo ""
echo "=== Build Results ==="
for ex in $EXAMPLES; do
    BIN="$PROJECT_ROOT/examples/$ex/$ex"
    if [ -f "$BIN" ]; then
        SIZE=$(ls -lh "$BIN" | awk '{print $5}')
        echo "  ✓ $ex ($SIZE)"
    fi
done
LIB="$PROJECT_ROOT/amiga-debug-lib/libdebug.a"
if [ -f "$LIB" ]; then
    SIZE=$(ls -lh "$LIB" | awk '{print $5}')
    echo "  ✓ libdebug.a ($SIZE)"
fi
echo "=== Done ==="
