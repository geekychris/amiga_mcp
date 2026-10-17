#!/bin/bash
# key.sh <rawkey-hex> [hold-seconds]  — press (and hold) an Amiga key via devbench key injection
D="$(cd "$(dirname "$0")" && pwd)"
up() { "$D/mcp.py" amiga_input_key "{\"rawkey\":\"$1\",\"direction\":\"up\"}"; }
"$D/mcp.py" amiga_input_key "{\"rawkey\":\"$1\",\"direction\":\"down\"}" >/dev/null || exit 1
trap 'up "$1" >/dev/null; exit 130' INT TERM          # never leave the key held down
sleep "${2:-0.1}"
trap - INT TERM
up "$1"
