#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""Take an emulator for a test: the emulator lock plus a "CLAUDE IS TESTING"
banner on the Amiga's screen, so the person at the machine knows to leave it.

In a test script:

    sys.path.insert(0, "<amiga_mcp>/scripts")
    from claude_busy import claude_testing
    with claude_testing("http://localhost:3010/mcp", "Boing Twist tests"):
        ...                                  # drive the emulator

From a shell (the lock is only held while the command runs, so prefer the
context manager; on/off just show and take down the banner):

    scripts/claude_busy.py on  [--url URL] [message]
    scripts/claude_busy.py off [--url URL]
    scripts/claude_busy.py run [--url URL] [--message M] -- <command...>

The banner is tools/claude_busy (build: scripts/build-claude-busy.sh). It is
pushed to RAM: when the Amiga doesn't have it yet. Its window is never made
active, so keys a test injects still reach the game. URL ports: 3010/3000
is the AmigaOS 4 guest (claude_busy_os4), 3001 classic 68k (claude_busy).
"""
import argparse
import contextlib
import fcntl
import json
import os
import subprocess
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(ROOT, "tools", "claude_busy")
LOCK = "/tmp/amiga-emu.lock"
ON_AMIGA = "RAM:claude_busy"


class _MCP:
    def __init__(self, url):
        self.url, self.sid = url, None

    def _post(self, body):
        hdr = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
        if self.sid:
            hdr["mcp-session-id"] = self.sid
        req = urllib.request.Request(self.url, json.dumps(body).encode(), hdr)
        with urllib.request.urlopen(req, timeout=120) as r:
            raw = r.read().decode()
            self.sid = r.headers.get("mcp-session-id") or self.sid
        msgs = [json.loads(l[5:]) for l in raw.splitlines() if l.startswith("data:")]
        return msgs or ([json.loads(raw)] if raw.strip() else [])

    def tool(self, name, **args):
        if self.sid is None:
            self._post({"jsonrpc": "2.0", "id": 0, "method": "initialize", "params": {
                "protocolVersion": "2025-03-26", "capabilities": {},
                "clientInfo": {"name": "claude-busy", "version": "1"}}})
            self._post({"jsonrpc": "2.0", "method": "notifications/initialized"})
        res = self._post({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                          "params": {"name": name, "arguments": args}})[-1].get("result", {})
        return "\n".join(c.get("text", "") for c in res.get("content", []) if c.get("type") == "text")


def _binary(url):
    name = "claude_busy" if ":3001" in url else "claude_busy_os4"
    return os.path.join(TOOL, name)


def _quote(s):
    return '"' + s.replace('"', "'") + '"'


def banner_on(url, message=""):
    """Show the banner (pushing the program to RAM: first if needed). Never raises."""
    try:
        m = _MCP(url)
        listing = m.tool("amiga_dos_command", command=f"List {ON_AMIGA}", timeout=15).lower()
        if "not found" in listing or "no information" in listing:
            local = _binary(url)
            if not os.path.exists(local):
                subprocess.run([os.path.join(ROOT, "scripts", "build-claude-busy.sh")],
                               check=True, capture_output=True)
            m.tool("amiga_push_file", local_path=local, amiga_path=ON_AMIGA)
            m.tool("amiga_protect", path=ON_AMIGA, bits="00000000")
        m.tool("amiga_launch", command=f"{ON_AMIGA} {_quote(message)}" if message else ON_AMIGA)
        return True
    except Exception as e:                          # a banner must never break a test
        print(f"claude_busy: banner not shown: {e}", file=sys.stderr)
        return False


def banner_off(url):
    try:
        _MCP(url).tool("amiga_dos_command", command=f"{ON_AMIGA} OFF", timeout=15)
    except Exception as e:
        print(f"claude_busy: banner not taken down: {e}", file=sys.stderr)


@contextlib.contextmanager
def claude_testing(url="http://localhost:3010/mcp", message=""):
    """Hold the emulator lock and show the banner for the duration."""
    lock = open(LOCK, "w")
    fcntl.flock(lock, fcntl.LOCK_EX)
    try:
        banner_on(url, message)
        try:
            yield
        finally:
            banner_off(url)
    finally:
        fcntl.flock(lock, fcntl.LOCK_UN)
        lock.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("action", choices=["on", "off", "run"])
    ap.add_argument("--url", default="http://localhost:3010/mcp")
    ap.add_argument("--message", default="")
    ap.add_argument("words", nargs="*", help="on: the message")
    argv = sys.argv[1:]
    rest = argv[argv.index("--") + 1:] if "--" in argv else []
    o = ap.parse_intermixed_args(argv[:argv.index("--")] if "--" in argv else argv)
    if o.action == "on":
        sys.exit(0 if banner_on(o.url, o.message or " ".join(o.words)) else 1)
    if o.action == "off":
        banner_off(o.url)
        return
    if not rest:
        ap.error("run needs a command after --")
    with claude_testing(o.url, o.message):
        sys.exit(subprocess.call(rest))


if __name__ == "__main__":
    main()
