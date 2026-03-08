#!/bin/bash
# End-to-end test: starts simulator + MCP server, runs all tools, verifies output.
#
# Usage:
#   ./scripts/test-flow.sh
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
MCP_DIR="$PROJECT_ROOT/mcp-server"

ACCEPT="Accept: application/json, text/event-stream"
PASS=0
FAIL=0

cleanup() {
    kill "$SIM_PID" "$MCP_PID" 2>/dev/null
    wait "$SIM_PID" "$MCP_PID" 2>/dev/null || true
}
trap cleanup EXIT

# Extract tool result text from SSE response
extract_text() {
    python3 -c "
import sys, json
for line in sys.stdin:
    line = line.strip()
    if line.startswith('data: '):
        d = json.loads(line[6:])
        r = d.get('result', {})
        content = r.get('content', [])
        if content:
            print(content[0].get('text', ''))
            break
" 2>/dev/null
}

check() {
    local label="$1"
    local output="$2"
    local expect="$3"

    if echo "$output" | grep -q "$expect"; then
        echo "  ✓ $label"
        PASS=$((PASS + 1))
    else
        echo "  ✗ $label (expected '$expect', got: $output)"
        FAIL=$((FAIL + 1))
    fi
}

mcp_call() {
    local id="$1"
    local tool="$2"
    local args="$3"
    curl -s -N -X POST http://localhost:3000/mcp \
        -H "Content-Type: application/json" \
        -H "$ACCEPT" \
        -H "mcp-session-id: $SESSION_ID" \
        -d "{\"jsonrpc\":\"2.0\",\"id\":$id,\"method\":\"tools/call\",\"params\":{\"name\":\"$tool\",\"arguments\":$args}}" \
        --max-time 10 2>/dev/null | extract_text
}

echo "=== Amiga MCP End-to-End Test ==="
echo ""

# Start simulator
echo "--- Starting simulator ---"
cd "$MCP_DIR"
npx tsx src/simulator.ts > /tmp/amiga_sim.log 2>&1 &
SIM_PID=$!
sleep 1

if ! kill -0 "$SIM_PID" 2>/dev/null; then
    echo "ERROR: Simulator failed to start"
    cat /tmp/amiga_sim.log
    exit 1
fi
echo "  ✓ Simulator running (PID $SIM_PID)"

# Start MCP server
echo "--- Starting MCP server ---"
AMIGA_PROJECT_ROOT="$PROJECT_ROOT" npx tsx src/index.ts > /tmp/amiga_mcp.log 2>&1 &
MCP_PID=$!
sleep 2

if ! kill -0 "$MCP_PID" 2>/dev/null; then
    echo "ERROR: MCP server failed to start"
    cat /tmp/amiga_mcp.log
    exit 1
fi
echo "  ✓ MCP server running (PID $MCP_PID)"

# Health check
echo ""
echo "--- Health check ---"
HEALTH=$(curl -s http://localhost:3000/health 2>/dev/null)
check "Health endpoint" "$HEALTH" '"status":"ok"'

# Initialize session
echo ""
echo "--- MCP session ---"
HEADERS=$(curl -s -D - -X POST http://localhost:3000/mcp \
    -H "Content-Type: application/json" \
    -H "$ACCEPT" \
    -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0.0"}}}' \
    -o /tmp/mcp_init 2>&1)
SESSION_ID=$(echo "$HEADERS" | grep -i 'mcp-session-id' | awk '{print $2}' | tr -d '\r\n')
check "Session created" "$SESSION_ID" "[a-f0-9]"

# Send initialized notification
curl -s -X POST http://localhost:3000/mcp \
    -H "Content-Type: application/json" \
    -H "$ACCEPT" \
    -H "mcp-session-id: $SESSION_ID" \
    -d '{"jsonrpc":"2.0","method":"notifications/initialized"}' > /dev/null

# Connect
echo ""
echo "--- Tool tests ---"
OUT=$(mcp_call 2 "amiga_connect" '{}')
check "amiga_connect" "$OUT" "Connected"
sleep 1

# Ping
OUT=$(mcp_call 3 "amiga_ping" '{}')
check "amiga_ping" "$OUT" "Amiga alive"

# Get variable
OUT=$(mcp_call 4 "amiga_get_var" '{"name":"ball_x"}')
check "amiga_get_var" "$OUT" "ball_x"

# Set variable
OUT=$(mcp_call 5 "amiga_set_var" '{"name":"ball_dx","value":"7"}')
check "amiga_set_var" "$OUT" "ball_dx = 7"

# Get logs
OUT=$(mcp_call 6 "amiga_log" '{"count":20}')
check "amiga_log" "$OUT" "Debug session started"

# Exec command
OUT=$(mcp_call 7 "amiga_exec" '{"command":"status"}')
check "amiga_exec" "$OUT" "ball("

# Inspect memory
OUT=$(mcp_call 8 "amiga_inspect_memory" '{"address":"00000100","size":16}')
check "amiga_inspect_memory" "$OUT" "00000100"

# Disconnect
OUT=$(mcp_call 9 "amiga_disconnect" '{}')
check "amiga_disconnect" "$OUT" "Disconnected"

# Summary
echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
