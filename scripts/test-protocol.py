#!/usr/bin/env python3
"""Protocol layer reliability test.

Sends commands to the Amiga bridge via devbench REST API
and reports exactly what works and what doesn't.
"""

import json
import sys
import time
import urllib.request
import urllib.error

BASE = "http://localhost:3000"

def api(method, path, body=None):
    """Make an API call, return (status, data_dict, elapsed_ms)."""
    url = BASE + path
    headers = {"Content-Type": "application/json"} if body else {}
    data = json.dumps(body).encode() if body else None
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    t0 = time.monotonic()
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            raw = resp.read().decode()
            elapsed = (time.monotonic() - t0) * 1000
            return resp.status, json.loads(raw), elapsed
    except urllib.error.HTTPError as e:
        elapsed = (time.monotonic() - t0) * 1000
        return e.code, {}, elapsed
    except Exception as e:
        elapsed = (time.monotonic() - t0) * 1000
        return 0, {"error": str(e)}, elapsed


def test(name, method, path, body=None, check=None, delay=2.0):
    """Run a single test and print result."""
    status, data, ms = api(method, path, body)
    ok = True
    detail = ""

    if check:
        ok, detail = check(data)
    elif "error" in data and "timeout" in str(data.get("error", "")).lower():
        ok = False
        detail = data["error"]

    tag = "PASS" if ok else "FAIL"
    print(f"  [{tag}] {name} ({ms:.0f}ms)")
    if not ok or detail:
        print(f"         Response: {json.dumps(data, indent=None)[:200]}")
        if detail:
            print(f"         Detail: {detail}")
    time.sleep(delay)
    return ok


def main():
    print("=" * 60)
    print("Protocol Layer Test Suite")
    print("=" * 60)
    passed = 0
    failed = 0

    # 1. Health check
    print("\n--- Basic Connectivity ---")
    if test("Health check", "GET", "/health",
            check=lambda d: (d.get("status") == "ok", "")):
        passed += 1
    else:
        failed += 1
        print("  ABORT: Server not running")
        return

    # 2. Connection status
    if test("Connection status", "GET", "/api/status",
            check=lambda d: (d.get("connected") == True, f"connected={d.get('connected')}")):
        passed += 1
    else:
        failed += 1

    # 3. LISTCLIENTS FIRST (before PING) - test ordering
    print("\n--- Short Messages ---")
    if test("LISTCLIENTS (before PING)", "GET", "/api/clients",
            check=lambda d: ("error" not in d and isinstance(d.get("clients"), list),
                            f"got: {d}")):
        passed += 1
    else:
        failed += 1

    # 4. PING - short response
    def check_ping(d):
        if "error" in d:
            return False, d["error"]
        pong = d.get("pong", {})
        if not pong:
            return False, "no pong data"
        chip = pong.get("freeChip", 0)
        fast = pong.get("freeFast", 0)
        issues = []
        if chip < 100:
            issues.append(f"freeChip={chip} (suspiciously low)")
        if fast > 500_000_000:
            issues.append(f"freeFast={fast} (suspiciously high)")
        return len(issues) == 0, "; ".join(issues) if issues else ""

    if test("PING/PONG", "POST", "/api/ping", check=check_ping):
        passed += 1
    else:
        failed += 1

    # 5. PING reliability (5x)
    ping_ok = 0
    for i in range(5):
        s, d, ms = api("POST", "/api/ping")
        if "pong" in d:
            ping_ok += 1
        time.sleep(1)
    reliable = ping_ok == 5
    tag = "PASS" if reliable else "FAIL"
    print(f"  [{tag}] PING reliability: {ping_ok}/5")
    if reliable:
        passed += 1
    else:
        failed += 1

    # 6. LISTCLIENTS (after PING) - compare with before
    if test("LISTCLIENTS (after PING)", "GET", "/api/clients",
            check=lambda d: ("error" not in d and isinstance(d.get("clients"), list),
                            f"got: {d}")):
        passed += 1
    else:
        failed += 1

    # 7. TASKS
    if test("LISTTASKS", "GET", "/api/tasks",
            check=lambda d: ("error" not in d and isinstance(d.get("tasks"), list),
                            f"got: {d}")):
        passed += 1
    else:
        failed += 1

    # 7. VOLUMES - medium length response
    print("\n--- Medium Messages ---")
    def check_volumes(d):
        vols = d.get("volumes", [])
        if not vols:
            return False, f"empty volumes (got: {d})"
        if len(vols) < 2:
            return False, f"only {len(vols)} volumes: {vols}"
        return True, f"{len(vols)} volumes: {vols}"

    if test("LISTVOLUMES", "GET", "/api/volumes", check=check_volumes):
        passed += 1
    else:
        failed += 1

    # 8. VOLUMES reliability (3x)
    vol_ok = 0
    for i in range(3):
        s, d, ms = api("GET", "/api/volumes")
        vols = d.get("volumes", [])
        if len(vols) >= 2:
            vol_ok += 1
        time.sleep(2)
    reliable = vol_ok == 3
    tag = "PASS" if reliable else "FAIL"
    print(f"  [{tag}] VOLUMES reliability: {vol_ok}/3")
    if reliable:
        passed += 1
    else:
        failed += 1

    # 9. DIR listing
    print("\n--- Long Messages ---")
    def check_dir(d):
        entries = d.get("entries", [])
        if not entries:
            return False, f"empty dir listing (got: {d})"
        return True, f"{len(entries)} entries"

    if test("DIR SYS:", "GET", "/api/dir?path=SYS:", check=check_dir):
        passed += 1
    else:
        failed += 1

    # 10. LAUNCH (DOS command execution)
    def check_launch(d):
        if d.get("status") == "timeout":
            return False, "timeout"
        output = d.get("output", "")
        if not output:
            return False, f"empty output (got: {d})"
        return True, f"output: {output[:80]}..."

    if test("LAUNCH 'dir SYS:'", "POST", "/api/launch",
            body={"command": "dir SYS:"},
            check=check_launch, delay=3):
        passed += 1
    else:
        failed += 1

    # 11. Memory inspection
    def check_mem(d):
        if "error" in d:
            return False, d["error"]
        dump = d.get("dump", "")
        if not dump:
            return False, "empty dump"
        # Check if it's all zeros (likely emulator issue)
        if dump.replace("0", "").replace(" ", "").replace("\n", "").replace(".", "").replace("f", "").replace("e", "").replace("d", "").replace("c", "").replace("b", "").replace("a", "") == "":
            return True, "WARNING: all zeros (emulator may not expose this memory)"
        return True, ""

    if test("MEMORY 0xF80000 (ROM)", "GET", "/api/memory?address=F80000&size=16",
            check=check_mem):
        passed += 1
    else:
        failed += 1

    # 12. Heartbeat check - wait and see if we get one
    print("\n--- Async Messages ---")
    s, d, ms = api("GET", "/api/status")
    hb = d.get("lastHeartbeat")
    if hb:
        tag = "PASS"
        print(f"  [{tag}] Heartbeat received: chip={hb.get('freeChip')}, fast={hb.get('freeFast')}")
        passed += 1
    else:
        print(f"  [WARN] No heartbeat yet (may need to wait longer)")

    # Summary
    print("\n" + "=" * 60)
    total = passed + failed
    print(f"Results: {passed}/{total} passed, {failed}/{total} failed")
    if failed > 0:
        print("PROTOCOL LAYER HAS ISSUES")
    else:
        print("ALL TESTS PASSED")
    print("=" * 60)

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
