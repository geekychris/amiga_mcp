#!/bin/bash
# showcase.sh - a short scripted flight for the highlight reel, played through
# devbench key injection and scene hooks while rec.sh records the screen.
D="$(cd "$(dirname "$0")" && pwd)"; M="$D/mcp.py"; K="$D/key.sh"; mark() { "$D/mark.sh" showcase "$@"; }
hold() { "$M" amiga_input_key "{\"rawkey\":\"$1\",\"direction\":\"$2\"}" >/dev/null; }
hook() { "$M" amiga_call_hook "{\"client\":\"terra_strike\",\"hook\":\"$1\",\"args\":\"$2\"}"; }
"$M" amiga_launch '{"command":"Dropbox:Dev/terra_strike"}'; sleep 6
mark "title flyover"; sleep 2
mark "Return: start";            "$K" 44 >/dev/null; sleep 1.5
mark "lift off";                 "$K" 4C 1.4 >/dev/null; sleep 0.5
mark "throttle + bank";          hold 20 down; "$K" 4E 1.2 >/dev/null; sleep 0.5; hold 20 up
mark "cruise + fire";            hold 40 down; sleep 3; "$K" 4F 0.8 >/dev/null; sleep 2; hold 40 up
mark "scene hook: tanks";        hook scene tanks; sleep 1; hold 40 down; sleep 2.5; hold 40 up; sleep 1.5
mark "scene hook: sea";          hook scene sea; sleep 1; hold 40 down; sleep 2.5; hold 40 up; sleep 1.5
mark "scene hook: landing";      hook scene landing; sleep 0.8; "$K" 4D 3.5 >/dev/null; sleep 2.5
mark "landed + refuelling";      hook status; sleep 2
mark "lift off again";           "$K" 4C 1.0 >/dev/null
mark "clear hook: level cleared"; hook clear; sleep 6
"$M" amiga_get_var '{"name":"level"}'
mark "stop client";              "$M" amiga_stop_client '{"name":"terra_strike"}'; sleep 1.5
