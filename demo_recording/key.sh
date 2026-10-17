#!/bin/bash
# key.sh <rawkey-hex> [hold-seconds]  — press (and hold) an Amiga key via devbench key injection
D="$(cd "$(dirname "$0")" && pwd)"
"$D/mcp.py" amiga_input_key "{\"rawkey\":\"$1\",\"direction\":\"down\"}" >/dev/null
sleep "${2:-0.1}"
"$D/mcp.py" amiga_input_key "{\"rawkey\":\"$1\",\"direction\":\"up\"}"
