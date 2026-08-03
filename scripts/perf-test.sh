#!/usr/bin/env bash
# perf-test.sh — end-to-end TCP throughput measurement across the guest
# NIC we're developing/testing.
#
# Runs Bill Borsari's pyperf.py (see scripts/pyperf.py, sourced from
# github.com/geekychris/amiga-e1000-driver) as a listener on the host
# and a client inside the OS4 guest, then reports Mbit/sec.
#
# Preconditions:
#   * QEMU sam460ex booted with -device e1000-82540em on subnet
#     192.168.100.0/24 (the default from start-qemu-os4.sh --net)
#   * amiga-bridge daemon running inside the guest — SERIAL mode works
#     when the network stack is being debugged; TCP mode works too
#   * python3 installed on host (macOS or Linux)
#   * python-os4 (Python 3.12+) installed on the OS4 guest at DH1:
#   * pyperf.py already deployed to DH1: (see scripts/deploy-os4.sh
#     or use scripts/pyperf.py directly with xdftool)
#
# Usage:
#   ./scripts/perf-test.sh                 # default 5s test
#   ./scripts/perf-test.sh 30              # 30 second test
#
# Env:
#   PORT     TCP port used both sides (default: 17999 — a port QEMU
#            does NOT hostfwd, otherwise the QEMU listener steals it
#            before pyperf can bind)
#   TARGET   IP address inside the guest that reaches the host. Default
#            192.168.100.2 = SLIRP gateway for the e1000 netdev. If you
#            change the QEMU -netdev subnet, override this.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
DURATION="${1:-5}"
PORT="${PORT:-17999}"
TARGET="${TARGET:-192.168.100.2}"
API="http://localhost:3000"

if ! command -v python3 >/dev/null; then
    echo "perf-test: python3 not on PATH" >&2
    exit 2
fi
if ! curl -fs "$API/api/status" >/dev/null; then
    echo "perf-test: devbench REST not reachable at $API" >&2
    exit 3
fi

# Server on host, foreground bind check, then background.
pkill -f "pyperf\.py.*--server.*--port $PORT" 2>/dev/null || true
sleep 1
python3 "$HERE/pyperf.py" --server --port "$PORT" --bind 0.0.0.0 \
    > /tmp/pyperf-server.log 2>&1 &
SERVER_PID=$!
sleep 2
if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "perf-test: pyperf server failed to start:"
    cat /tmp/pyperf-server.log
    exit 4
fi
echo "server listening on 0.0.0.0:$PORT (pid $SERVER_PID)"

trap 'kill "$SERVER_PID" 2>/dev/null || true' EXIT

# Client on guest via devbench. Redirect output to RAM: then read back.
STAMP=$(date +%s)
GUEST_OUT="RAM:pt_${STAMP}.txt"

echo "→ guest: pyperf.py --client --host $TARGET --port $PORT --time $DURATION"
curl -sf --max-time "$((DURATION + 30))" -X POST "$API/api/launch" \
    -H 'Content-Type: application/json' \
    -d "$(printf '{"command":"DH1:python-os4 DH1:pyperf.py --client --host %s --port %s --time %s >%s"}' \
        "$TARGET" "$PORT" "$DURATION" "$GUEST_OUT")" > /dev/null

# Give the test a chance to flush its output file before we read.
sleep 3

echo ""
echo "=== guest client stdout ==="
curl -sf --max-time 10 "$API/api/file?path=$GUEST_OUT&offset=0&size=2048" 2>&1 \
    | python3 -c "
import sys, json, binascii
d = json.load(sys.stdin)
h = d.get('hexData')
if h:
    print(binascii.unhexlify(h.split('...')[0]).decode('latin-1', errors='replace'))
else:
    print(d.get('content', d))
"

echo ""
echo "=== host server log (last 10 lines) ==="
tail -10 /tmp/pyperf-server.log
