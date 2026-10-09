#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
#
# setup.sh — install everything Amiga DevBench needs that can be installed
# automatically, build the pieces, then run scripts/doctor.sh to show what's
# left (the commercial parts: Kickstart ROM, AmigaOS installs).
#
#   scripts/setup.sh            # both targets
#   scripts/setup.sh 68k        # classic Amiga (FS-UAE, AmigaOS 3.x)
#   scripts/setup.sh os4        # AmigaOS 4.1 (QEMU sam460ex)
#
# Safe to re-run: every step skips what's already there.
# macOS (Homebrew) and Debian/Ubuntu (apt). Windows: scripts/install.ps1.

set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TARGET="${1:-all}"
case "$TARGET" in all|68k|os4) ;; *) echo "Usage: $0 [all|68k|os4]"; exit 2 ;; esac
want() { [ "$TARGET" = all ] || [ "$TARGET" = "$1" ]; }
cd "$ROOT"

if [ -t 1 ]; then B=$'\e[1m'; N=$'\e[0m'; else B= N=; fi
step() { printf "\n${B}== %s${N}\n" "$1"; }
have() { command -v "$1" >/dev/null 2>&1; }
OS="$(uname -s)"

# one package, from Homebrew or apt (brew-name apt-name)
pkg_install() {
    if [ "$OS" = Darwin ]; then
        have brew || { echo "Homebrew is needed: https://brew.sh"; exit 1; }
        brew list "$1" >/dev/null 2>&1 || brew install "$1"
    elif have apt-get; then
        dpkg -s "$2" >/dev/null 2>&1 || sudo apt-get install -y "$2"
    else
        echo "Install '$2' with your package manager, then re-run."; return 1
    fi
}

step "Python 3.10+"
if ! have python3 || ! python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)'; then
    pkg_install python@3.12 python3
fi
python3 --version

step "Docker"
if ! have docker; then
    if [ "$OS" = Darwin ]; then brew install --cask docker; else pkg_install docker docker.io; fi
fi
if ! docker info >/dev/null 2>&1; then
    [ "$OS" = Darwin ] && open -a Docker 2>/dev/null || true
    echo "Waiting for Docker to start (start Docker Desktop if it doesn't)..."
    for _ in $(seq 1 60); do docker info >/dev/null 2>&1 && break; sleep 3; done
    docker info >/dev/null 2>&1 || { echo "Docker isn't running: start it and re-run this script."; exit 1; }
fi
echo "Docker running"

step "Repository (examples submodule)"
git submodule update --init

step "amiga-devbench (host server: web UI + MCP)"
python3 -m pip install -q -e amiga-devbench
python3 -c 'import amiga_devbench; print("amiga-devbench installed")'

step "Cross-compilers (Docker images)"
scripts/install-toolchains.sh

if want os4; then
    step "AmigaOS 4: QEMU, lha, amitools"
    have qemu-system-ppc || pkg_install qemu qemu-system-ppc
    # lha: only needed to install OS4 from its ISO (Homebrew has no lha formula)
    have lha || pkg_install lhasa lhasa || echo "lha not installed: get it from https://github.com/jca02266/lha if you need to install OS4"

    have xdftool || scripts/install-amitools.sh
    step "AmigaOS 4: software OpenGL (Mesa 7.8.2 for planet_chomp / rolling_steel)"
    [ -f third_party/mesa-os4/out/lib/libOSMesa.a ] || third_party/mesa-os4/build.sh
    step "AmigaOS 4: bridge daemon"
    # both arches build in amiga-bridge/src: clean first so no 68k objects linger
    scripts/build-bridge-ppc.sh clean >/dev/null
    scripts/build-bridge-ppc.sh
    D="${OS4_DIR:-$HOME/AmigaOS4}"
    if [ -f "$D/amigaos4-dev.hdf" ]; then
        if pgrep -f qemu-system-ppc >/dev/null; then
            echo "QEMU is running: not writing the dev disk. Stop OS4 and run:"
            echo "  scripts/deploy-os4.sh amiga-bridge/amiga-bridge amiga-bridge"
        else
            scripts/deploy-os4.sh --rm amiga-bridge >/dev/null 2>&1 || true
            scripts/deploy-os4.sh amiga-bridge/amiga-bridge amiga-bridge
        fi
    fi
fi

# (68k last, so amiga-bridge/libbridge.a ends up 68k, as the 68k examples expect)
if want 68k; then
    step "Classic 68k: FS-UAE"
    have fs-uae || [ -x "$HOME/.amiga-devbench/fs-uae" ] || pkg_install fs-uae fs-uae || true
    step "Classic 68k: bridge daemon + client library"
    # both arches build in amiga-bridge/src: clean first so no PPC objects linger
    docker run --rm -v "$ROOT:/work" -w /work amigadev/crosstools:m68k-amigaos make -C amiga-bridge clean all
    DEP=$(python3 - <<'PY'
from amiga_devbench.config import load_config, apply_profile
c = load_config(); apply_profile(c, "local-fsuae"); c.resolve_paths(); print(c.deploy_dir)
PY
)
    if [ -d "$DEP" ]; then
        cp amiga-bridge/amiga-bridge "$DEP/" && echo "amiga-bridge copied to $DEP (the Amiga's DH2:)"
    else
        echo "Deploy folder '$DEP' doesn't exist yet: set [paths] deploy_dir in devbench.toml"
    fi
fi


step "Checking the result"
scripts/doctor.sh "$TARGET" || true
