#!/bin/bash
# showcase2.sh - stage 2 highlight: the new main screen, then into the game.
D="$(cd "$(dirname "$0")" && pwd)"; M="$D/mcp.py"; K="$D/key.sh"; mark() { "$D/mark.sh" showcase2 "$@"; }
hold() { "$M" amiga_input_key "{\"rawkey\":\"$1\",\"direction\":\"$2\"}" >/dev/null; }
hook() { "$M" amiga_call_hook "{\"client\":\"terra_strike\",\"hook\":\"$1\",\"args\":\"$2\"}" >/dev/null; }
mark "launch: main screen"; "$M" amiga_launch '{"command":"Dropbox:Dev/terra_strike"}' >/dev/null; sleep 9
mark "Return: palette switch into the game"; "$K" 44 >/dev/null; sleep 1.5
mark "lift off"; "$K" 4C 1.3 >/dev/null
mark "scene tanks + fire"; hook scene tanks; sleep 0.8; hold 40 down; sleep 2.5; hold 40 up; sleep 1.5
mark "Esc: back to the main screen"; "$K" 45 >/dev/null; sleep 6
mark "stop"; "$M" amiga_stop_client '{"name":"terra_strike"}' >/dev/null
