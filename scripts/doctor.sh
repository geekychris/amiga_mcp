#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
#
# doctor.sh — check that everything Amiga DevBench needs is in place, and say
# how to fix what isn't. Changes nothing.
#
#   scripts/doctor.sh            # both targets
#   scripts/doctor.sh 68k        # classic Amiga (FS-UAE, AmigaOS 3.x)
#   scripts/doctor.sh os4        # AmigaOS 4.1 (QEMU sam460ex)
#
# Exit status: 0 if everything required for the chosen target(s) is there,
# 1 otherwise. Warnings (optional things) don't fail it.

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TARGET="${1:-all}"
case "$TARGET" in all|68k|os4) ;; *) echo "Usage: $0 [all|68k|os4]"; exit 2 ;; esac

if [ -t 1 ]; then G=$'\e[32m'; R=$'\e[31m'; Y=$'\e[33m'; B=$'\e[1m'; N=$'\e[0m'; else G= R= Y= B= N=; fi
FAILS=0; WARNS=0
ok()   { printf "  ${G}✓${N} %s\n" "$1"; }
bad()  { printf "  ${R}✗${N} %s\n" "$1"; [ -n "$2" ] && printf "      → %s\n" "$2"; FAILS=$((FAILS + 1)); }
warn() { printf "  ${Y}!${N} %s\n" "$1"; [ -n "$2" ] && printf "      → %s\n" "$2"; WARNS=$((WARNS + 1)); }
section() { printf "\n${B}%s${N}\n" "$1"; }
have() { command -v "$1" >/dev/null 2>&1; }
OS="$(uname -s)"
pkg() { if [ "$OS" = "Darwin" ]; then echo "brew install $1"; else echo "sudo apt-get install $2"; fi; }

# a setting from devbench.toml for a profile, via devbench's own loader
cfgval() {
    python3 - "$1" "$2" 2>/dev/null <<'PY'
import sys
from amiga_devbench.config import load_config, apply_profile
c = load_config()
apply_profile(c, sys.argv[1])
c.resolve_paths()
v = getattr(c, sys.argv[2], "")
print(v if v is not None else "")
PY
}
port_free() { ! (lsof -iTCP:"$1" -sTCP:LISTEN >/dev/null 2>&1); }
port_owner() { lsof -iTCP:"$1" -sTCP:LISTEN -Fc 2>/dev/null | sed -n 's/^c//p' | head -1; }

# ---------------------------------------------------------------- common
section "Common"
if have python3; then
    PV=$(python3 -c 'import sys; print("%d.%d" % sys.version_info[:2])')
    if python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)'; then ok "Python $PV"
    else bad "Python $PV is too old (need 3.10+)" "$(pkg python@3.12 python3)"; fi
else bad "Python 3 not found" "$(pkg python@3.12 python3)"; fi

if python3 -c 'import amiga_devbench' 2>/dev/null; then ok "amiga-devbench installed (python3 -m amiga_devbench)"
else bad "amiga-devbench not installed" "scripts/setup.sh  (or: pip install -e amiga-devbench)"; fi

if have docker; then
    if docker info >/dev/null 2>&1; then ok "Docker running"
    else bad "Docker installed but not running" "start Docker Desktop (macOS: open -a Docker)"; fi
else bad "Docker not found" "$( [ "$OS" = Darwin ] && echo 'brew install --cask docker' || echo 'sudo apt-get install docker.io')"; fi

if [ -f "$ROOT/examples/README.md" ]; then ok "examples submodule checked out"
else bad "examples/ submodule missing" "git submodule update --init"; fi

if [ -f "$ROOT/devbench.toml" ]; then ok "devbench.toml"
else bad "devbench.toml missing" "copy it from the repo (git checkout devbench.toml)"; fi

docker_img() {   # image, label, required
    if docker image inspect "$1" >/dev/null 2>&1; then ok "$2 ($1)"
    else bad "$2 not pulled ($1)" "scripts/install-toolchains.sh"; fi
}

# ---------------------------------------------------------------- 68k
if [ "$TARGET" = all ] || [ "$TARGET" = 68k ]; then
    section "Classic Amiga · 68k (FS-UAE, AmigaOS 3.x) — devbench on :3001"
    have docker && docker info >/dev/null 2>&1 && docker_img amigadev/crosstools:m68k-amigaos "m68k cross-compiler"

    BIN=$(cfgval local-fsuae emulator_binary)
    FS=""
    if [ "$BIN" = auto ] || [ -z "$BIN" ]; then
        for c in "$HOME/.amiga-devbench/fs-uae" /tmp/fsuae-src/fs-uae "$(command -v fs-uae)"; do
            [ -n "$c" ] && [ -x "$c" ] && { FS="$c"; break; }
        done
    else FS="$BIN"; fi
    if [ -n "$FS" ] && [ -x "$FS" ]; then
        if grep -q "fs-uae-rpc v1" "$FS" 2>/dev/null; then ok "FS-UAE: $FS (patched debugger build)"
        else ok "FS-UAE: $FS"; warn "stock FS-UAE: the debugger tools (breakpoints, stepping) need the patched build" "see README: 'How devbench picks the fs-uae binary'"; fi
    else bad "FS-UAE not found" "$( [ "$OS" = Darwin ] && echo 'brew install fs-uae' || echo 'sudo apt-get install fs-uae')"; fi

    CONF=$(cfgval local-fsuae emulator_config)
    if [ -n "$CONF" ] && [ -f "$CONF" ]; then
        ok "FS-UAE config: $CONF"
        KICK=$(sed -n 's/^kickstart_file *= *//p' "$CONF" | head -1)
        if [ -n "$KICK" ] && [ -f "$KICK" ]; then ok "Kickstart ROM: $KICK"
        else bad "Kickstart ROM not found (${KICK:-kickstart_file not set})" "copy your Kickstart 3.x ROM (e.g. from Amiga Forever) and set kickstart_file in $CONF"; fi
        HD=$(sed -n 's/^hard_drive_0 *= *//p' "$CONF" | head -1)
        if [ -n "$HD" ] && [ -e "$HD" ]; then ok "System disk: $HD"
        else bad "AmigaOS 3.x system disk not found (${HD:-hard_drive_0 not set})" "install AmigaOS 3.x / AmiKit and set hard_drive_0 in $CONF (see /fsuae-setup)"; fi
        SP=$(sed -n 's/^serial_port *= *//p' "$CONF" | head -1)
        WANT=$(cfgval local-fsuae serial_port)
        if echo "$SP" | grep -q ":$WANT\$"; then ok "Serial on TCP :$WANT (matches the local-fsuae profile)"
        else bad "FS-UAE serial_port is '${SP:-unset}', devbench expects TCP :$WANT" "set serial_port = tcp://0.0.0.0:$WANT in $CONF"; fi
    else bad "FS-UAE config not found (${CONF:-unset})" "copy AmiKit-Debug.fs-uae from the repo root and set [emulator] config in devbench.toml"; fi

    DEP=$(cfgval local-fsuae deploy_dir)
    if [ -n "$DEP" ] && [ -d "$DEP" ]; then
        ok "Deploy folder: $DEP"
        if [ -f "$DEP/amiga-bridge" ]; then ok "Bridge daemon deployed ($DEP/amiga-bridge; run from the Amiga's startup)"
        else bad "amiga-bridge not in the deploy folder" "make bridge && cp amiga-bridge/amiga-bridge \"$DEP/\""; fi
    else bad "Deploy folder not found (${DEP:-unset})" "set [paths] deploy_dir in devbench.toml to the folder the emulator mounts as DH2:"; fi

    if [ -f "$ROOT/amiga-bridge/libbridge.a" ]; then ok "libbridge.a built"
    else bad "libbridge.a not built" "make bridge"; fi

    for p in 3001 "$WANT"; do
        [ -z "$p" ] && continue
        if port_free "$p"; then ok "Port $p free"
        else o=$(port_owner "$p"); case "$o" in fs-uae*|Python*|python*) ok "Port $p in use by $o (already running)";; *) warn "Port $p is used by '$o'" "free it, or change the port";; esac; fi
    done
fi

# ---------------------------------------------------------------- OS4
if [ "$TARGET" = all ] || [ "$TARGET" = os4 ]; then
    section "AmigaOS 4.1 · PowerPC (QEMU sam460ex) — devbench on :3000"
    have docker && docker info >/dev/null 2>&1 && docker_img walkero/amigagccondocker:os4-gcc11 "PPC cross-compiler"

    Q=/opt/homebrew/bin/qemu-system-ppc; [ -x "$Q" ] || Q=$(command -v qemu-system-ppc)
    if [ -n "$Q" ] && [ -x "$Q" ]; then
        if "$Q" -machine help 2>/dev/null | grep -q sam460ex; then ok "QEMU: $Q ($("$Q" --version | head -1 | sed 's/.*version //'))"
        else bad "QEMU has no sam460ex machine" "$(pkg qemu qemu-system-ppc)"; fi
    else bad "qemu-system-ppc not found" "$(pkg qemu qemu-system-ppc)"; fi

    if have xdftool; then ok "amitools (xdftool) for writing the OS4 disk image"
    else bad "amitools not installed" "scripts/install-amitools.sh  (or: pip install amitools)"; fi
    if have lha; then ok "lha"; else warn "lha not installed (only needed to install OS4 from the ISO)" "$(pkg lha lhasa)"; fi

    D="${OS4_DIR:-$HOME/AmigaOS4}"
    if [ -f "$D/amigaos4-system.hdf" ]; then
        if rdbtool "$D/amigaos4-system.hdf" info 2>/dev/null | grep -q "Partition"; then ok "OS4 system disk: $D/amigaos4-system.hdf"
        else bad "OS4 system disk exists but has no partitions (OS4 not installed yet)" "scripts/start-qemu-os4.sh --install  (needs the AmigaOS 4.1 FE ISO; see docs/amigaos4-setup.md)"; fi
    else bad "OS4 system disk not found ($D/amigaos4-system.hdf)" "AmigaOS 4.1 Final Edition (commercial, Hyperion) — follow docs/amigaos4-setup.md"; fi
    if [ -f "$D/amigaos4-dev.hdf" ]; then
        ok "OS4 dev disk: $D/amigaos4-dev.hdf"
        if xdftool -r "$D/amigaos4-dev.hdf" open part=0 + list 2>/dev/null | grep -q "^  amiga-bridge "; then ok "Bridge daemon on the dev disk (DH1:amiga-bridge)"
        else bad "amiga-bridge not on the OS4 dev disk" "scripts/build-bridge-ppc.sh && scripts/deploy-os4.sh amiga-bridge/amiga-bridge amiga-bridge"; fi
    else bad "OS4 dev disk not found ($D/amigaos4-dev.hdf)" "scripts/init-dev-hdf.sh"; fi

    if [ -f "$ROOT/third_party/mesa-os4/out/lib/libOSMesa.a" ]; then ok "Software OpenGL for OS4 (third_party/mesa-os4)"
    else warn "OSMesa not built (only planet_chomp and rolling_steel need it)" "third_party/mesa-os4/build.sh"; fi

    for p in 3000 2346 2347 2348; do
        if port_free "$p"; then ok "Port $p free"
        else o=$(port_owner "$p"); case "$o" in qemu*|Python*|python*) ok "Port $p in use by $o (already running)";; *) warn "Port $p is used by '$o'" "free it, or change the port";; esac; fi
    done
fi

echo
if [ "$FAILS" -eq 0 ]; then
    printf "${G}${B}All set${N}%s. Start with: scripts/start.sh %s\n" "$( [ "$WARNS" -gt 0 ] && echo " ($WARNS warning(s))")" "$TARGET"
    exit 0
fi
printf "${R}${B}%d problem(s)${N}%s — fix the ✗ items above (scripts/setup.sh does the automatic ones).\n" "$FAILS" "$( [ "$WARNS" -gt 0 ] && echo ", $WARNS warning(s)")"
exit 1
