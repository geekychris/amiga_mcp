#!/bin/sh
# Reproducible build of static software OSMesa (Mesa 7.8.2, classic swrast)
# for AmigaOS 4.1 PPC / newlib, using the walkero cross toolchain in Docker.
#
#   ./build.sh            # build out/lib/libOSMesa.a + out/include/GL + test
#   ./build.sh clean      # remove objects and outputs
#   JOBS=8 ./build.sh     # parallelism (default: host CPU count)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

VER=7.8.2
TARBALL=MesaLib-$VER.tar.bz2
URL=https://archive.mesa3d.org/older-versions/7.x/$VER/$TARBALL
IMAGE=${IMAGE:-walkero/amigagccondocker:os4-gcc11}
JOBS=${JOBS:-$( (sysctl -n hw.ncpu || nproc) 2>/dev/null || echo 4)}

if [ "$1" = clean ]; then
    rm -rf obj out test/osmesa_test
    exit 0
fi

if [ ! -f "$TARBALL" ]; then
    echo ">> downloading $URL"
    curl -fL -o "$TARBALL.part" "$URL"
    mv "$TARBALL.part" "$TARBALL"
fi

if [ ! -d "Mesa-$VER" ]; then
    echo ">> extracting $TARBALL"
    tar xjf "$TARBALL"
    for p in patches/*.patch; do
        [ -f "$p" ] || continue
        echo ">> applying $p"
        patch -d "Mesa-$VER" -p1 < "$p"
    done
fi

echo ">> building with $IMAGE (-j$JOBS)"
docker run --rm -v "$HERE":/work -w /work "$IMAGE" sh -c \
    "export PATH=/opt/ppc-amigaos/bin:\$PATH && make -j$JOBS all && make test"

echo
ls -l out/lib/libOSMesa.a test/osmesa_test
