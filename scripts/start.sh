#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
#
# start.sh — start (or stop) devbench and its emulator, and wait until the
# Amiga's bridge answers.
#
#   scripts/start.sh 68k        # classic Amiga: FS-UAE + devbench on :3001
#   scripts/start.sh os4        # AmigaOS 4.1:  QEMU   + devbench on :3000
#   scripts/start.sh all        # both (the default)
#   scripts/start.sh status     # what's running
#   scripts/start.sh stop [68k|os4|all]
#
# Each devbench runs the matching devbench.toml profile (local-fsuae,
# qemu-os4) and starts its emulator through its own API, so the web UI's
# Emulator card can stop and restart it afterwards. Logs go to
# ~/.amiga-devbench/logs/. Ports: PORT_68K (3001), PORT_OS4 (3000).

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT_68K="${PORT_68K:-3001}"
PORT_OS4="${PORT_OS4:-3000}"
LOGDIR="$HOME/.amiga-devbench/logs"
mkdir -p "$LOGDIR"

if [ -t 1 ]; then G=$'\e[32m'; R=$'\e[31m'; Y=$'\e[33m'; B=$'\e[1m'; N=$'\e[0m'; else G= R= Y= B= N=; fi

port_of()    { [ "$1" = 68k ] && echo "$PORT_68K" || echo "$PORT_OS4"; }
profile_of() { [ "$1" = 68k ] && echo local-fsuae || echo qemu-os4; }
name_of()    { [ "$1" = 68k ] && echo "Classic Amiga · 68k (FS-UAE)" || echo "AmigaOS 4.1 · PowerPC (QEMU)"; }
api()        { curl -s -m "${3:-10}" ${2:+-X POST} "http://localhost:$1$4" 2>/dev/null; }
devbench_up() { curl -s -m 3 "http://localhost:$1/api/status" >/dev/null 2>&1; }
bridge_up() {
    curl -s -m 3 "http://localhost:$1/api/status" 2>/dev/null | python3 -c '
import json, sys
d = json.load(sys.stdin)
s = d.get("bridgeSilentSec")
sys.exit(0 if d.get("connected") and s is not None and s < 10 else 1)' 2>/dev/null
}
emu_running() {
    curl -s -m 3 "http://localhost:$1/api/emulator/status" 2>/dev/null | python3 -c '
import json, sys; sys.exit(0 if json.load(sys.stdin).get("running") else 1)' 2>/dev/null
}

start_one() {
    local t=$1 port profile log
    port=$(port_of "$t"); profile=$(profile_of "$t"); log="$LOGDIR/devbench-$t.log"
    printf "\n${B}%s${N}\n" "$(name_of "$t")"
    if devbench_up "$port"; then
        echo "  devbench already running on :$port"
    else
        echo "  starting devbench (profile $profile) on :$port — log: $log"
        # exec: no shell stays behind holding this script's stdout open
        ( cd "$ROOT" || exit 1
          exec nohup python3 -m amiga_devbench --profile "$profile" --port "$port" --no-emulator \
              >"$log" 2>&1 </dev/null ) &
        for _ in $(seq 1 30); do devbench_up "$port" && break; sleep 1; done
        devbench_up "$port" || { printf "  ${R}devbench didn't come up${N}: see %s\n" "$log"; return 1; }
    fi
    if emu_running "$port"; then
        echo "  emulator already running"
    else
        echo "  starting the emulator"
        curl -s -m 60 -X POST "http://localhost:$port/api/emulator/start" >/dev/null
    fi
    printf "  waiting for the Amiga to boot and its bridge to answer"
    for _ in $(seq 1 60); do bridge_up "$port" && break; printf "."; sleep 3; done
    echo
    if bridge_up "$port"; then
        printf "  ${G}ready${N}: http://localhost:%s/   (MCP: http://localhost:%s/mcp)\n" "$port" "$port"
    else
        printf "  ${Y}devbench is up but the Amiga's bridge hasn't answered yet${N}\n"
        echo "  (still booting? Is amiga-bridge started from the Amiga's startup? scripts/doctor.sh $t)"
        printf "  web UI: http://localhost:%s/\n" "$port"
    fi
}

stop_one() {
    local t=$1 port pids
    port=$(port_of "$t")
    printf "${B}%s${N}\n" "$(name_of "$t")"
    if devbench_up "$port"; then
        if emu_running "$port"; then
            echo "  stopping the emulator"
            curl -s -m 30 -X POST "http://localhost:$port/api/emulator/stop" >/dev/null
        fi
        pids=$(lsof -tiTCP:"$port" -sTCP:LISTEN 2>/dev/null)
        [ -n "$pids" ] && kill $pids 2>/dev/null && echo "  devbench on :$port stopped"
    else
        echo "  devbench not running on :$port"
    fi
}

status_one() {
    local t=$1 port d e b
    port=$(port_of "$t")
    d="${R}down${N}"; e="${R}stopped${N}"; b="${R}not answering${N}"
    if devbench_up "$port"; then
        d="${G}up${N} http://localhost:$port/"
        emu_running "$port" && e="${G}running${N}"
        bridge_up "$port" && b="${G}answering${N}"
    else e="?"; b="?"; fi
    printf "${B}%-32s${N} devbench %s   emulator %s   bridge %s\n" "$(name_of "$t")" "$d" "$e" "$b"
}

targets() { case "$1" in 68k) echo 68k ;; os4) echo os4 ;; all|"") echo "os4 68k" ;; *) echo "bad" ;; esac; }

cmd="${1:-all}"
case "$cmd" in
    status) for t in os4 68k; do status_one "$t"; done ;;
    stop)   T=$(targets "${2:-all}"); [ "$T" = bad ] && { echo "Usage: $0 stop [68k|os4|all]"; exit 2; }
            for t in $T; do stop_one "$t"; done ;;
    68k|os4|all)
            python3 -c 'import amiga_devbench' 2>/dev/null || { echo "amiga-devbench isn't installed: run scripts/setup.sh first"; exit 1; }
            rc=0; for t in $(targets "$cmd"); do start_one "$t" || rc=1; done; exit $rc ;;
    -h|--help|help) sed -n '5,16p' "$0" | sed 's/^# \{0,1\}//' ;;
    *) echo "Usage: $0 [68k|os4|all|status|stop [68k|os4|all]]"; exit 2 ;;
esac
