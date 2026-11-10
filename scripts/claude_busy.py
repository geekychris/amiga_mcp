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

The banner is tools/claude_busy (build: scripts/build-claude-busy.sh). On an
AmigaOS 4 guest that has it installed as DH1:bbtools/claude_busy, that copy is
run; otherwise it is pushed to RAM: when the Amiga doesn't have it yet. (A
devbench on another machine reads amiga_push_file's local path on ITS machine,
so remote guests need the DH1: copy to show the banner after a reboot.) Its window is never made
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
ON_DISK = "DH1:bbtools/claude_busy"     # installed on an OS4 guest's dev disk (remote guests)
_PROG = {}                               # url -> the copy banner_on ran


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


def _missing(listing):
    listing = listing.lower()
    return "not found" in listing or "no information" in listing or not listing.strip()


def _on_disk(m, url):
    """Is the banner installed on the guest's disk? Only asked of an OS4 guest, where DH1:
    always exists (a 68k Amiga may have no DH1:, and naming a missing volume opens a
    requester)."""
    if ":3001" in url:
        return False
    out = m.tool("amiga_dos_command", command=f"List {ON_DISK}", timeout=15)
    return "claude_busy" in out and not _missing(out)


def banner_on(url, message=""):
    """Show the banner (the guest's DH1: copy, or pushed to RAM: first if needed). Never raises."""
    try:
        m = _MCP(url)
        if _on_disk(m, url):
            _PROG[url] = ON_DISK
            m.tool("amiga_launch", command=f"{ON_DISK} {_quote(message)}" if message else ON_DISK)
            return True
        _PROG[url] = ON_AMIGA
        listing = m.tool("amiga_dos_command", command=f"List {ON_AMIGA}", timeout=15)
        if _missing(listing):
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
        m = _MCP(url)
        prog = _PROG.get(url) or (ON_DISK if _on_disk(m, url) else ON_AMIGA)
        m.tool("amiga_dos_command", command=f"{prog} OFF", timeout=15)   # any copy finds the banner's port
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
