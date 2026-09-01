#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins

#
# amiga-devbench — one-shot installer for macOS and Linux.
#
# Clones the repo, installs the Python host server, pulls the m68k cross-compiler
# Docker image, builds the bridge daemon + examples, and starts the web UI on
# http://localhost:3000.
#
# Invoke as:
#
#   curl -fsSL https://raw.githubusercontent.com/geekychris/amiga_mcp/main/scripts/install.sh | bash
#
# Works on macOS and Linux, arm64 and x86_64. Windows users: see scripts/install.ps1.
#
# Environment knobs:
#   AMIGA_MCP_SRC           install dir       (default: $HOME/.amiga-devbench/src)
#   AMIGA_MCP_REF           git ref           (default: main)
#   AMIGA_MCP_REPO          git remote        (default: https://github.com/geekychris/amiga_mcp.git)
#   AMIGA_MCP_BUILD         1 to build examples via Docker  (default: 1)
#   AMIGA_MCP_START         1 to launch the web UI          (default: 1)
#   AMIGA_MCP_OPEN          1 to open browser               (default: 1)
#   AMIGA_MCP_AUTO_INSTALL  1 to brew/apt/dnf install missing deps (default: 0)
#   AMIGA_MCP_BUILD_PATCHED 1 to clone + build the patched fs-uae fork with
#                             HTTP debugger support, installed to
#                             ~/.amiga-devbench/fs-uae. Requires C++ toolchain
#                             + ~10 system libs (autoconf, libtool, sdl2, glib,
#                             libpng, libmpeg2, openal-soft, ...). When
#                             AMIGA_MCP_AUTO_INSTALL=1 is also set, attempts
#                             to install them. Mac + Linux only. (default: 0)
#
# What this installer does NOT install:
#   - FS-UAE (licensed Kickstart ROM required) — install separately, see README.
#   - The patched fs-uae fork by default — set AMIGA_MCP_BUILD_PATCHED=1 to opt in.
#
# After install, point [emulator] in devbench.toml at your fs-uae binary
# (or AmiKit) and run `make start` to begin development.

set -euo pipefail

APP_SRC="${AMIGA_MCP_SRC:-$HOME/.amiga-devbench/src}"
APP_REF="${AMIGA_MCP_REF:-main}"
APP_REPO="${AMIGA_MCP_REPO:-https://github.com/geekychris/amiga_mcp.git}"
APP_BUILD="${AMIGA_MCP_BUILD:-1}"
APP_START="${AMIGA_MCP_START:-1}"
APP_OPEN="${AMIGA_MCP_OPEN:-1}"
AUTO_INSTALL="${AMIGA_MCP_AUTO_INSTALL:-0}"
BUILD_PATCHED="${AMIGA_MCP_BUILD_PATCHED:-0}"

PATCHED_SRC="$HOME/.amiga-devbench/fsuae_remote_patch"
PATCHED_DST="$HOME/.amiga-devbench/fs-uae"
PATCHED_REPO="${AMIGA_MCP_PATCHED_REPO:-https://github.com/geekychris/fsuae_remote_patch.git}"

RUN_DIR="$HOME/.amiga-devbench/run"
LOG_DIR="$HOME/.amiga-devbench/logs"
mkdir -p "$RUN_DIR" "$LOG_DIR"

c_reset=$'\033[0m'; c_bold=$'\033[1m'
c_green=$'\033[32m'; c_yellow=$'\033[33m'; c_red=$'\033[31m'; c_blue=$'\033[34m'

step() { printf "%s==>%s %s%s%s\n" "$c_blue" "$c_reset" "$c_bold" "$*" "$c_reset"; }
ok()   { printf "%s ✓%s %s\n" "$c_green" "$c_reset" "$*"; }
warn() { printf "%s ! %s%s\n" "$c_yellow" "$c_reset" "$*"; }
die()  { printf "%s ✗ %s%s\n" "$c_red" "$c_reset" "$*" >&2; exit 1; }

# Non-interactive shells (curl | bash) inherit a minimal PATH.
for _d in /snap/bin /usr/local/bin /opt/homebrew/bin "$HOME/.local/bin"; do
  if [ -d "$_d" ]; then
    case ":$PATH:" in *:"$_d":*) ;; *) PATH="$_d:$PATH" ;; esac
  fi
done
export PATH

# ---- platform detection -------------------------------------------------
case "$(uname -s)" in
  Darwin) OS=mac ;;
  Linux)
    if [ -r /etc/os-release ]; then
      . /etc/os-release
      case "$ID" in
        ubuntu|debian|linuxmint|pop) OS=debian ;;
        fedora|rhel|centos|rocky|almalinux) OS=fedora ;;
        arch|manjaro) OS=arch ;;
        *) OS=linux ;;
      esac
    else OS=linux; fi ;;
  *) die "unsupported OS: $(uname -s) — see scripts/install.ps1 for Windows" ;;
esac

install_hint() {
  case "$OS:$1" in
    mac:git)     echo "brew install git" ;;
    mac:python)  echo "brew install python@3.12" ;;
    mac:docker)  echo "brew install --cask docker  (or Rancher Desktop / OrbStack)" ;;
    debian:git)    echo "sudo apt-get install -y git" ;;
    debian:python) echo "sudo apt-get install -y python3 python3-venv python3-pip" ;;
    debian:docker) echo "see https://docs.docker.com/engine/install/ubuntu/" ;;
    fedora:git)    echo "sudo dnf install -y git" ;;
    fedora:python) echo "sudo dnf install -y python3 python3-pip" ;;
    fedora:docker) echo "see https://docs.docker.com/engine/install/fedora/" ;;
    arch:git)      echo "sudo pacman -S --noconfirm git" ;;
    arch:python)   echo "sudo pacman -S --noconfirm python python-pip" ;;
    arch:docker)   echo "sudo pacman -S --noconfirm docker && sudo systemctl enable --now docker" ;;
    *)             echo "install $1 (no hint for $OS — see your package manager)" ;;
  esac
}

auto_install() {
  local pkg="$1"
  step "Auto-installing $pkg (AMIGA_MCP_AUTO_INSTALL=1)"
  case "$OS:$pkg" in
    mac:git)       brew install git ;;
    mac:python)    brew install python@3.12 ;;
    mac:docker)    die "Docker on macOS needs a GUI install — $(install_hint docker)" ;;
    debian:git)    sudo apt-get update -qq </dev/tty && sudo apt-get install -y git </dev/tty ;;
    debian:python) sudo apt-get update -qq </dev/tty && sudo apt-get install -y python3 python3-venv python3-pip </dev/tty ;;
    debian:docker) die "Docker on Debian/Ubuntu needs repo setup — $(install_hint docker)" ;;
    fedora:git)    sudo dnf install -y git </dev/tty ;;
    fedora:python) sudo dnf install -y python3 python3-pip </dev/tty ;;
    fedora:docker) die "Docker on Fedora needs repo setup — $(install_hint docker)" ;;
    arch:git)      sudo pacman -S --noconfirm git </dev/tty ;;
    arch:python)   sudo pacman -S --noconfirm python python-pip </dev/tty ;;
    arch:docker)   sudo pacman -S --noconfirm docker </dev/tty && sudo systemctl enable --now docker </dev/tty ;;
    *)             die "no auto-install recipe for $pkg on $OS — $(install_hint "$pkg")" ;;
  esac
}

require() {
  local bin="$1" pkg="$2"
  if command -v "$bin" >/dev/null 2>&1; then return; fi
  if [ "$AUTO_INSTALL" = "1" ]; then
    auto_install "$pkg"
    command -v "$bin" >/dev/null 2>&1 || die "after auto-install, '$bin' still missing — $(install_hint "$pkg")"
    return
  fi
  die "missing '$bin' on PATH — $(install_hint "$pkg")
     (or re-run with AMIGA_MCP_AUTO_INSTALL=1 to attempt automatic install)"
}

# Pick a python binary that satisfies >=3.10
PY=""
pick_python() {
  for cand in python3.13 python3.12 python3.11 python3.10 python3 python; do
    if command -v "$cand" >/dev/null 2>&1; then
      if "$cand" -c 'import sys; sys.exit(0 if sys.version_info[:2] >= (3,10) else 1)' 2>/dev/null; then
        PY="$cand"; return 0
      fi
    fi
  done
  return 1
}

# --- 1. prereqs ----------------------------------------------------------
step "Checking prerequisites ($OS)"
require git git
if ! pick_python; then
  if [ "$AUTO_INSTALL" = "1" ]; then
    auto_install python
    pick_python || die "no python>=3.10 after install — $(install_hint python)"
  else
    die "python>=3.10 not found — $(install_hint python)
     (or re-run with AMIGA_MCP_AUTO_INSTALL=1)"
  fi
fi
ok "python: $PY ($("$PY" -c 'import sys;print(".".join(map(str,sys.version_info[:3])))'))"

require docker docker
if ! docker info >/dev/null 2>&1; then
  warn "docker daemon is not reachable — examples will not build until you start Docker"
  APP_BUILD=0
else
  ok "docker daemon reachable"
fi

# pip (sometimes a separate package on Debian)
if ! "$PY" -m pip --version >/dev/null 2>&1; then
  die "pip not available for $PY — $(install_hint python)"
fi
ok "pip available"

# --- 2. source -----------------------------------------------------------
step "Fetching source -> $APP_SRC ($APP_REF)"
if [ -d "$APP_SRC/.git" ]; then
  git -C "$APP_SRC" fetch --quiet origin "$APP_REF"
  git -C "$APP_SRC" checkout --quiet "$APP_REF"
  git -C "$APP_SRC" pull --quiet --ff-only origin "$APP_REF" || warn "could not fast-forward — keeping current state"
else
  mkdir -p "$(dirname "$APP_SRC")"
  git clone --quiet --branch "$APP_REF" "$APP_REPO" "$APP_SRC"
fi
ok "source ready"

# --- 3. python host server ----------------------------------------------
step "Installing amiga-devbench (Python host)"
# Use --user when not in a venv to avoid PEP 668 / system-Python pain.
PIP_FLAGS="--quiet"
if [ -z "${VIRTUAL_ENV:-}" ]; then
  PIP_FLAGS="$PIP_FLAGS --user"
fi
# --break-system-packages is required on some distros for --user installs against
# a PEP 668 system Python. Only add it if pip refuses without it.
if ! "$PY" -m pip install $PIP_FLAGS -e "$APP_SRC/amiga-devbench" 2>"$LOG_DIR/pip.log"; then
  if grep -q "externally-managed-environment\|--break-system-packages" "$LOG_DIR/pip.log"; then
    warn "system Python is PEP-668 managed; retrying with --break-system-packages"
    "$PY" -m pip install $PIP_FLAGS --break-system-packages -e "$APP_SRC/amiga-devbench" \
      || die "pip install failed — see $LOG_DIR/pip.log"
  else
    cat "$LOG_DIR/pip.log" >&2
    die "pip install failed — see $LOG_DIR/pip.log"
  fi
fi
ok "amiga-devbench installed ($($PY -c 'import amiga_devbench, os; print(os.path.dirname(amiga_devbench.__file__))'))"

# --- 4. cross-compiler image + examples (optional) ----------------------
if [ "$APP_BUILD" = "1" ]; then
  step "Pulling m68k cross-compiler image (amigadev/crosstools:m68k-amigaos)"
  docker pull --quiet amigadev/crosstools:m68k-amigaos >/dev/null \
    || warn "image pull failed — you can retry later with: docker pull amigadev/crosstools:m68k-amigaos"

  step "Building amiga-bridge + examples (this takes a minute)"
  if (cd "$APP_SRC" && make all >"$LOG_DIR/build.log" 2>&1); then
    ok "build complete"
  else
    warn "build failed — see $LOG_DIR/build.log (you can fix and re-run 'make all' inside $APP_SRC)"
  fi
else
  step "Skipping example build (AMIGA_MCP_BUILD=0 or docker unavailable)"
fi

# --- 4b. Optional: build the patched fs-uae fork ---------------------
#
# This is a from-source C++ build of fs-uae itself (~10 minutes, ~10 sys deps).
# Most users don't need it — stock fs-uae works fine for development. Opt in
# only when you want devbench's HTTP-driven CPU debugger features
# (FS-UAE tab in the Web UI, amiga_fsuae_* MCP tools).
build_patched_fsuae() {
  step "Building patched fs-uae (AMIGA_MCP_BUILD_PATCHED=1)"

  # Required system deps for the patched fork. We only auto-install when the
  # user has already opted in via AUTO_INSTALL=1 — otherwise list what's
  # missing and skip with a clear hint.
  case "$OS" in
    mac)
      need_brew=()
      for pkg in autoconf automake libtool pkg-config gettext glib libpng libmpeg2 openal-soft sdl2; do
        brew list --formula "$pkg" >/dev/null 2>&1 || need_brew+=("$pkg")
      done
      if [ ${#need_brew[@]} -gt 0 ]; then
        if [ "$AUTO_INSTALL" = "1" ]; then
          brew install "${need_brew[@]}" || { warn "brew install failed — skipping patched build"; return 1; }
        else
          warn "patched build needs brew deps: ${need_brew[*]}"
          warn "re-run with AMIGA_MCP_AUTO_INSTALL=1 AMIGA_MCP_BUILD_PATCHED=1 — skipping"
          return 1
        fi
      fi
      ;;
    debian)
      need_apt=()
      for pkg in build-essential autoconf automake libtool pkg-config gettext libglib2.0-dev libpng-dev libmpeg2-4-dev libopenal-dev libsdl2-dev zlib1g-dev; do
        dpkg -s "$pkg" >/dev/null 2>&1 || need_apt+=("$pkg")
      done
      if [ ${#need_apt[@]} -gt 0 ]; then
        if [ "$AUTO_INSTALL" = "1" ]; then
          sudo apt-get update -qq </dev/tty && sudo apt-get install -y "${need_apt[@]}" </dev/tty || { warn "apt install failed — skipping"; return 1; }
        else
          warn "patched build needs apt deps: ${need_apt[*]}"
          warn "re-run with AMIGA_MCP_AUTO_INSTALL=1 AMIGA_MCP_BUILD_PATCHED=1 — skipping"
          return 1
        fi
      fi
      ;;
    *)
      warn "patched build deps are only auto-resolved on macOS + Debian/Ubuntu."
      warn "see https://github.com/geekychris/fsuae_remote_patch#build-dependencies for $OS — continuing anyway"
      ;;
  esac

  # Clone or update the fork
  if [ -d "$PATCHED_SRC/.git" ]; then
    git -C "$PATCHED_SRC" pull --quiet --ff-only || warn "could not fast-forward $PATCHED_SRC"
  else
    mkdir -p "$(dirname "$PATCHED_SRC")"
    git clone --quiet "$PATCHED_REPO" "$PATCHED_SRC" || { warn "clone failed — skipping"; return 1; }
  fi

  # Run build.sh; it clones fs-uae upstream into /tmp/fsuae-src, patches, builds.
  step "Compiling patched fs-uae (this can take 5-15 min)"
  if (cd "$PATCHED_SRC" && ./build.sh >"$LOG_DIR/patched-build.log" 2>&1); then
    ok "patched fs-uae built"
  else
    warn "patched build failed — see $LOG_DIR/patched-build.log"
    warn "amiga-devbench will fall back to stock fs-uae on PATH"
    return 1
  fi

  # build.sh emits binary at /tmp/fsuae-src/fs-uae. Copy to stable location so
  # devbench's auto-discovery (~/.amiga-devbench/fs-uae) picks it up across
  # reboots even if /tmp is wiped.
  if [ -x /tmp/fsuae-src/fs-uae ]; then
    cp /tmp/fsuae-src/fs-uae "$PATCHED_DST"
    chmod +x "$PATCHED_DST"
    ok "patched binary installed to $PATCHED_DST"
  else
    warn "expected /tmp/fsuae-src/fs-uae after build — not found"
    return 1
  fi
}

if [ "$BUILD_PATCHED" = "1" ]; then
  build_patched_fsuae || true   # never fail the whole install on this
fi

if [ "$APP_START" != "1" ]; then
  cat <<EOF

Install complete. To start:

  cd $APP_SRC
  python3 -m amiga_devbench

Then open http://localhost:3000 in your browser.
EOF
  exit 0
fi

# --- 5. launch web UI in background -------------------------------------
start_bg() {
  local name="$1" cwd="$2"; shift 2
  local pidfile="$RUN_DIR/$name.pid" logfile="$LOG_DIR/$name.log"
  if [ -f "$pidfile" ] && kill -0 "$(cat "$pidfile")" 2>/dev/null; then
    warn "$name already running (pid $(cat "$pidfile"))"; return
  fi
  ( cd "$cwd" && nohup "$@" >"$logfile" 2>&1 & echo $! >"$pidfile" )
  ok "$name pid $(cat "$pidfile") — log: $logfile"
}

step "Starting amiga-devbench on http://localhost:3000"
start_bg devbench "$APP_SRC" "$PY" -m amiga_devbench

# --- 6. health probe + summary ------------------------------------------
step "Waiting for web UI"
up=0
for i in $(seq 1 30); do
  if curl -fsS http://localhost:3000/health >/dev/null 2>&1; then
    ok "up"; up=1; break
  fi
  sleep 1
done
[ "$up" = "1" ] || warn "not responding after 30s — check $LOG_DIR/devbench.log"

cat <<EOF

${c_bold}amiga-devbench is up.${c_reset}

  Web UI    http://localhost:3000
  MCP       http://localhost:3000/mcp
  source    $APP_SRC
  logs      $LOG_DIR/
  pids      $RUN_DIR/

Stop with:    kill \$(cat $RUN_DIR/devbench.pid)
Restart:      $APP_SRC/scripts/devbench-start.sh

Next steps:
  1. Install FS-UAE (or AmiKit) and a Kickstart ROM — see README "FS-UAE Emulator Setup".
  2. Edit $APP_SRC/devbench.toml and point [emulator] at your fs-uae binary + config.
  3. (Optional) Build the patched fs-uae fork from https://github.com/geekychris/fsuae_remote_patch
     for HTTP-driven CPU-level debugging — devbench detects it automatically.
EOF

if [ "$APP_OPEN" = "1" ] && [ "$up" = "1" ]; then
  if command -v open >/dev/null 2>&1; then open http://localhost:3000 >/dev/null 2>&1 || true
  elif command -v xdg-open >/dev/null 2>&1; then xdg-open http://localhost:3000 >/dev/null 2>&1 || true
  fi
fi
