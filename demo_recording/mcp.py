#!/usr/bin/env python3
"""mcp.py <tool> [json-args] — call an amiga-devbench MCP tool over streamable HTTP.

The session's amiga-dev MCP connection points at :3000 (OS4), so for the 68k devbench
(:3001) we speak JSON-RPC directly. Every call is logged to logs/mcp_calls.jsonl and
marked on the timeline so the edit can find each devbench interaction.
  mcp.py --list            list tool names
"""
import json, sys, time, urllib.request, os, datetime
URL = os.environ.get("DEVBENCH_MCP", "http://localhost:3001/mcp")
D = os.path.dirname(os.path.abspath(__file__))
SESSF = os.path.join(D, ".mcp_session")

def post(body, sid=None):
    req = urllib.request.Request(URL, json.dumps(body).encode(), {
        "Content-Type": "application/json", "Accept": "application/json, text/event-stream",
        **({"mcp-session-id": sid} if sid else {})})
    with urllib.request.urlopen(req, timeout=180) as r:
        raw = r.read().decode(); sid2 = r.headers.get("mcp-session-id")
    msgs = [json.loads(l[5:]) for l in raw.splitlines() if l.startswith("data:")] or ([json.loads(raw)] if raw.strip() else [])
    return msgs, sid2

def session():
    if os.path.exists(SESSF):
        return open(SESSF).read().strip()
    _, sid = post({"jsonrpc": "2.0", "id": 0, "method": "initialize", "params": {
        "protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "demo", "version": "1"}}})
    post({"jsonrpc": "2.0", "method": "notifications/initialized"}, sid)
    if sid:                                   # stateless servers send no session id
        open(SESSF, "w").write(sid)
    return sid

def call(method, params):
    for attempt in range(2):
        sid = session()
        try:
            msgs, _ = post({"jsonrpc": "2.0", "id": 1, "method": method, "params": params}, sid)
            return msgs
        except urllib.error.HTTPError as e:
            if attempt == 0 and e.code in (400, 404):
                os.remove(SESSF); continue
            raise

if sys.argv[1] == "--list":
    m = call("tools/list", {})
    print("\n".join(sorted(t["name"] for t in m[-1]["result"]["tools"]))); sys.exit()
tool = sys.argv[1]; args = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
t0 = time.time()
m = call("tools/call", {"name": tool, "arguments": args})
res = m[-1].get("result", m[-1])
text = "\n".join(c.get("text", "") for c in res.get("content", []) if c.get("type") == "text") if isinstance(res, dict) else str(res)
rec = {"t": round(t0, 3), "iso": datetime.datetime.fromtimestamp(t0).isoformat(timespec="milliseconds"),
       "dur": round(time.time() - t0, 2), "tool": tool, "args": args, "result": text[:4000]}
open(os.path.join(D, "logs", "mcp_calls.jsonl"), "a").write(json.dumps(rec) + "\n")
open(os.path.join(D, "timeline.jsonl"), "a").write(json.dumps({"t": rec["t"], "iso": rec["iso"], "kind": "mcp", "label": tool, "note": json.dumps(args)}) + "\n")
print(text)
is_err = isinstance(res, dict) and (res.get("isError") or "error" in m[-1])
sys.exit(1 if is_err else 0)
