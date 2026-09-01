#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins

# Smoke-test the devbench <-> AmigaBridge connection.
#
# Polls http://localhost:${PORT}/api/status until `connected: true` and a
# heartbeat has been observed within the last HB_MAX_AGE_SEC seconds. Exits
# 0 on success, non-zero on timeout or protocol mismatch.
#
# Works for all three deployment topologies (local FS-UAE via PTY, real Amiga
# over TCP, remote AmiKit over TCP) — it doesn't care what's on the other end
# of the wire, only that the bridge is talking.
#
# Usage:
#   scripts/smoke-bridge.sh                    # defaults: port 3000, 30s timeout
#   PORT=3000 TIMEOUT=60 scripts/smoke-bridge.sh
set -euo pipefail

PORT="${PORT:-3000}"
TIMEOUT="${TIMEOUT:-30}"
HB_MAX_AGE_SEC="${HB_MAX_AGE_SEC:-15}"
URL="http://localhost:${PORT}/api/status"

echo "Polling ${URL} for up to ${TIMEOUT}s..."

deadline=$(( $(date +%s) + TIMEOUT ))
last_err=""
while [ "$(date +%s)" -lt "$deadline" ]; do
  if body="$(curl -fsS --max-time 3 "$URL" 2>/dev/null)"; then
    connected="$(printf '%s' "$body" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("connected", False))')"
    mode="$(printf '%s' "$body" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("mode", "?"))')"
    silent="$(printf '%s' "$body" | python3 -c 'import json,sys; v=json.load(sys.stdin).get("bridgeSilentSec"); print(v if v is not None else -1)')"
    if [ "$connected" = "True" ]; then
      # silent may be -1 (never seen HB) or a float
      silent_int="$(printf '%s' "$silent" | python3 -c 'import sys; print(int(float(sys.stdin.read())))')"
      if [ "$silent_int" -ge 0 ] && [ "$silent_int" -le "$HB_MAX_AGE_SEC" ]; then
        echo "OK: connected=true mode=$mode bridgeSilentSec=$silent"
        exit 0
      fi
      last_err="connected=true but no recent heartbeat (silent=${silent}s > ${HB_MAX_AGE_SEC}s)"
    else
      last_err="connected=false (mode=$mode)"
    fi
  else
    last_err="devbench /api/status unreachable at $URL"
  fi
  sleep 1
done

echo "FAIL: $last_err" >&2
exit 1
