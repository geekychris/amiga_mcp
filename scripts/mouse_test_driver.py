#!/usr/bin/env python3
"""Ad-hoc driver to test mouse window-resize on a real Amiga via the bridge.

Connects to the bridge over TCP, runs a small sequence of primitives
(mouse move/click, screenshot, window list) in ONE connection, reusing
devbench's own protocol parser and screenshot renderer.

Usage:
  python3 mouse_test_driver.py HOST PORT "cmd; cmd; ..."

Primitives (semicolon separated):
  pin              INPUTMOVE -4000 -4000  (clamp pointer to top-left 0,0)
  pinbr            INPUTMOVE  4000  4000  (clamp pointer to bottom-right)
  move DX DY       relative pointer move
  down / up        left button down / up
  rdown / rup      right button down / up
  sleep MS         sleep milliseconds
  shot NAME        capture screen -> /tmp/NAME.png
  windows          LISTWINDOWS2 -> print parsed window list
  launch CMD...    LAUNCH a program (rest of token is the command)
  key HEX DIR      INPUTKEY raw key
"""
import os, sys, time, socket, threading, queue

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "amiga-devbench"))

from amiga_devbench import protocol
from amiga_devbench.screenshot import save_screenshot


class Bridge:
    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=10)
        self.sock.settimeout(0.2)
        self.buf = b""
        self.q = queue.Queue()
        self.running = True
        self.t = threading.Thread(target=self._reader, daemon=True)
        self.t.start()

    def _reader(self):
        while self.running:
            try:
                chunk = self.sock.recv(65536)
                if not chunk:
                    break
                self.buf += chunk
                while b"\n" in self.buf:
                    line, self.buf = self.buf.split(b"\n", 1)
                    s = line.decode("latin-1").strip()
                    if not s:
                        continue
                    msg = protocol.parse_message(s)
                    self.q.put((s, msg))
            except socket.timeout:
                continue
            except OSError:
                break

    def send(self, cmd):
        line = protocol.format_command(cmd)
        self.sock.sendall((line + "\n").encode("latin-1"))
        return line

    def drain(self):
        out = []
        try:
            while True:
                out.append(self.q.get_nowait())
        except queue.Empty:
            pass
        return out

    def collect(self, pred, timeout):
        """Collect parsed messages until pred(msg) True or timeout. Returns list."""
        got = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                raw, msg = self.q.get(timeout=0.2)
            except queue.Empty:
                continue
            got.append((raw, msg))
            if msg and pred(msg):
                break
        return got

    def close(self):
        self.running = False
        try:
            self.sock.close()
        except OSError:
            pass


def do_screenshot(br, name):
    br.send({"type": "SCREENSHOT"})
    scrinfo = None
    scrdata, scrrgb, scrrle = [], [], []
    deadline = time.time() + 60
    while time.time() < deadline:
        try:
            raw, msg = br.q.get(timeout=0.3)
        except queue.Empty:
            continue
        if not msg:
            continue
        t = msg.get("type")
        if t == "SCRINFO":
            scrinfo = msg
        elif t == "SCRRLE":
            scrrle.append(msg)
            if scrinfo and len(scrrle) >= scrinfo["height"]:
                break
        elif t == "SCRRGB":
            scrrgb.append(msg)
            if scrinfo and len(scrrgb) >= scrinfo["height"]:
                break
        elif t == "SCRDATA":
            scrdata.append(msg)
            if scrinfo:
                if msg.get("plane") == 255 and len(scrdata) >= scrinfo["height"]:
                    break
                if len(scrdata) >= scrinfo["height"] * scrinfo["depth"]:
                    break
        elif t == "ERR" and "SCREENSHOT" in str(msg.get("context", "")):
            print(f"  screenshot ERR: {msg.get('message')}")
            return
    if not scrinfo:
        print("  screenshot: no SCRINFO (timeout)")
        return
    path = f"/tmp/{name}.png"
    save_screenshot(scrinfo, scrdata,
                    save_path=path,
                    rgb_lines=scrrgb or None,
                    rle_lines=scrrle or None)
    print(f"  shot {name}: {scrinfo['width']}x{scrinfo['height']}x{scrinfo['depth']} -> {path}")


def do_windows(br):
    br.send({"type": "LISTWINDOWS2", "screen": ""})
    got = br.collect(lambda m: m.get("type") in ("WINDOWS", "ERR"), 5)
    for raw, msg in got:
        if msg and msg.get("type") == "WINDOWS":
            ws = msg["windows"]
            print(f"  windows on screen @{msg['screenAddr']} ({len(ws)}):")
            for w in ws:
                print(f"    '{w['title']}' pos=({w['left']},{w['top']}) "
                      f"size={w['width']}x{w['height']} @{w['addr']}")
            return
        if msg and msg.get("type") == "ERR":
            print(f"  windows ERR: {msg.get('message')}")
            return
    print("  windows: timeout")


def main():
    host, port, seq = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    br = Bridge(host, port)
    time.sleep(0.5)
    init = br.drain()
    print(f"connected to {host}:{port}; {len(init)} initial msgs "
          f"(e.g. {init[0][0] if init else '-'})")

    for cmd in [c.strip() for c in seq.split(";") if c.strip()]:
        parts = cmd.split()
        op = parts[0]
        if op == "pin":
            print(br.send({"type": "INPUTMOVE", "dx": -4000, "dy": -4000}))
        elif op == "pinbr":
            print(br.send({"type": "INPUTMOVE", "dx": 4000, "dy": 4000}))
        elif op == "move":
            print(br.send({"type": "INPUTMOVE", "dx": int(parts[1]), "dy": int(parts[2])}))
        elif op == "down":
            print(br.send({"type": "INPUTCLICK", "button": "left", "direction": "down"}))
        elif op == "up":
            print(br.send({"type": "INPUTCLICK", "button": "left", "direction": "up"}))
        elif op == "rdown":
            print(br.send({"type": "INPUTCLICK", "button": "right", "direction": "down"}))
        elif op == "rup":
            print(br.send({"type": "INPUTCLICK", "button": "right", "direction": "up"}))
        elif op == "key":
            print(br.send({"type": "INPUTKEY", "rawkey": parts[1], "direction": parts[2]}))
        elif op == "sleep":
            time.sleep(int(parts[1]) / 1000.0)
        elif op == "shot":
            do_screenshot(br, parts[1])
        elif op == "windows":
            do_windows(br)
        elif op == "raw":
            line = cmd[len("raw"):].strip()
            br.sock.sendall((line + "\n").encode("latin-1"))
            print(line)
        elif op == "launch":
            cmdline = cmd[len("launch"):].strip()
            print(br.send({"type": "LAUNCH", "id": "1", "command": cmdline}))
        else:
            print(f"  ?unknown op: {op}")
        # surface any OK/ERR acks
        for raw, msg in br.drain():
            if msg and msg.get("type") in ("OK", "ERR"):
                print(f"    <- {raw}")
        time.sleep(0.05)

    br.close()


if __name__ == "__main__":
    main()
