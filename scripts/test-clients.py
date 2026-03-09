#!/usr/bin/env python3
"""Focused test for client registration flow.

Tests: RUN hello_world → PING (check cc) → LISTCLIENTS (check names)
Avoids blocking commands (DIR, LAUNCH, INSPECT) that can hang the bridge.
"""

import json
import sys
import time
import urllib.request
import urllib.error

BASE = "http://localhost:3000"

def api(method, path, body=None, timeout=10):
    url = BASE + path
    headers = {"Content-Type": "application/json"} if body else {}
    data = json.dumps(body).encode() if body else None
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    t0 = time.monotonic()
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read().decode()
            elapsed = (time.monotonic() - t0) * 1000
            return json.loads(raw), elapsed
    except Exception as e:
        elapsed = (time.monotonic() - t0) * 1000
        return {"error": str(e)}, elapsed


def main():
    print("=" * 60)
    print("Client Registration Flow Test")
    print("=" * 60)

    # 1. Check health
    d, ms = api("GET", "/health")
    if d.get("status") != "ok":
        print(f"ABORT: Server not running: {d}")
        return 1
    print(f"[OK] Server healthy ({ms:.0f}ms)")

    # 2. Check connection
    d, ms = api("GET", "/api/status")
    if not d.get("connected"):
        print(f"ABORT: Not connected: {d}")
        return 1
    print(f"[OK] Connected ({ms:.0f}ms)")

    # 3. PING first to confirm bridge is alive
    d, ms = api("POST", "/api/ping")
    if "error" in d:
        print(f"ABORT: Bridge not responding: {d}")
        return 1
    pong = d.get("pong", {})
    cc = pong.get("clientCount", -1)
    print(f"[OK] PING -> cc={cc}, chip={pong.get('freeChip')}, fast={pong.get('freeFast')} ({ms:.0f}ms)")

    # 4. LISTCLIENTS before launching hello_world
    d, ms = api("GET", "/api/clients")
    clients_before = d.get("clients", [])
    print(f"[OK] LISTCLIENTS (before): {clients_before} ({ms:.0f}ms)")

    # 5. Check recent logs for the CDBG debug dump
    d, ms = api("GET", "/api/logs?count=10")
    for log in d.get("logs", []):
        msg = log.get("message", "")
        if "CDBG" in msg:
            print(f"     CDBG: {msg}")

    # 6. Launch hello_world
    print(f"\n--- Launching hello_world ---")
    d, ms = api("POST", "/api/run", {"command": "Dropbox:Dev/hello_world"})
    status = d.get("status", "")
    output = d.get("output", "")
    if status == "OK":
        print(f"[OK] RUN hello_world: {output} ({ms:.0f}ms)")
    else:
        print(f"[??] RUN hello_world: status={status} output={output} ({ms:.0f}ms)")
        if status == "timeout":
            # Try with different path
            print("     Retrying with AK2:Dev/hello_world...")
            d, ms = api("POST", "/api/run", {"command": "AK2:Dev/hello_world"})
            status = d.get("status", "")
            output = d.get("output", "")
            print(f"     Result: status={status} output={output} ({ms:.0f}ms)")

    # 7. Wait for hello_world to register
    print(f"\n--- Waiting for client registration ---")
    for wait in range(1, 6):
        time.sleep(2)
        d, ms = api("POST", "/api/ping")
        pong = d.get("pong", {})
        cc = pong.get("clientCount", -1)
        print(f"  [{wait}] PING: cc={cc} ({ms:.0f}ms)")
        if cc > 0:
            break

    # 8. Now the key test: LISTCLIENTS
    print(f"\n--- Client List Test ---")
    for attempt in range(3):
        # PING to check cc
        d, ms = api("POST", "/api/ping")
        pong = d.get("pong", {})
        cc_ping = pong.get("clientCount", -1)

        time.sleep(0.5)

        # LISTCLIENTS
        d, ms = api("GET", "/api/clients")
        clients = d.get("clients", [])

        # Check CDBG from logs
        d2, _ = api("GET", "/api/logs?count=5")
        cdbg = ""
        for log in d2.get("logs", []):
            msg = log.get("message", "")
            if "CDBG" in msg:
                cdbg = msg
                break

        match = "MATCH" if (cc_ping > 0) == (len(clients) > 0) else "MISMATCH"
        print(f"  [{attempt+1}] PING cc={cc_ping}, CLIENTS={clients} [{match}] ({ms:.0f}ms)")
        if cdbg:
            print(f"       CDBG: {cdbg}")

        time.sleep(1)

    # 9. Summary
    print(f"\n{'=' * 60}")
    d, ms = api("POST", "/api/ping")
    pong = d.get("pong", {})
    d2, _ = api("GET", "/api/clients")
    clients = d2.get("clients", [])
    cc = pong.get("clientCount", -1)

    if cc > 0 and len(clients) > 0:
        print(f"SUCCESS: PING cc={cc}, CLIENTS={clients}")
    elif cc > 0 and len(clients) == 0:
        print(f"BUG CONFIRMED: PING cc={cc} but CLIENTS empty")
    elif cc == 0 and len(clients) == 0:
        print(f"NO CLIENTS: hello_world may not have started")
    else:
        print(f"UNEXPECTED: cc={cc}, clients={clients}")
    print("=" * 60)

    return 0


if __name__ == "__main__":
    sys.exit(main())
