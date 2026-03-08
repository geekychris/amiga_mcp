#!/usr/bin/env python3
"""Direct serial bridge to Amiga via PTY.

Usage:
    amiga-serial.py listen [--timeout SEC]    Listen for messages
    amiga-serial.py ping                      Send PING, wait for heartbeat
    amiga-serial.py send CMD                  Send raw command
    amiga-serial.py getvar NAME               Get variable value
    amiga-serial.py setvar NAME VALUE         Set variable value
    amiga-serial.py exec EXPR                 Execute expression
    amiga-serial.py inspect ADDR SIZE         Inspect memory

Requires socat running:
    socat pty,raw,echo=0,link=/tmp/amiga-serial -,raw,echo=0 &
    (or use --pty PATH to specify a different PTY path)
"""
import sys
import os
import select
import time
import argparse

DEFAULT_PTY = "/tmp/amiga-serial"

def open_pty(path):
    """Open the PTY slave for read/write."""
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    return fd

def read_lines(fd, timeout=3.0):
    """Read available lines from PTY within timeout."""
    buf = b""
    lines = []
    deadline = time.time() + timeout
    while time.time() < deadline:
        remaining = deadline - time.time()
        if remaining <= 0:
            break
        r, _, _ = select.select([fd], [], [], min(remaining, 0.1))
        if r:
            try:
                chunk = os.read(fd, 4096)
                if chunk:
                    buf += chunk
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        lines.append(line.decode("latin-1").strip())
            except OSError:
                break
    return lines

def send_cmd(fd, cmd):
    """Send a command string to the Amiga."""
    os.write(fd, (cmd + "\n").encode("latin-1"))

def cmd_listen(fd, timeout):
    """Listen for messages and print them."""
    lines = read_lines(fd, timeout)
    if not lines:
        print("No messages received.")
    for line in lines:
        print(line)

def cmd_ping(fd):
    """Send PING and wait for heartbeat response."""
    send_cmd(fd, "PING")
    lines = read_lines(fd, timeout=3.0)
    for line in lines:
        if line.startswith("HB|"):
            parts = line.split("|")
            if len(parts) >= 4:
                print(f"Amiga alive! tick={parts[1]} chip_free={parts[2]} fast_free={parts[3]}")
                return
        print(line)
    if not any(l.startswith("HB|") for l in lines):
        print("No heartbeat response (timeout)")

def cmd_getvar(fd, name):
    """Get a variable value."""
    send_cmd(fd, f"GETVAR|{name}")
    lines = read_lines(fd, timeout=3.0)
    for line in lines:
        print(line)

def cmd_setvar(fd, name, value):
    """Set a variable value."""
    send_cmd(fd, f"SETVAR|{name}|{value}")
    lines = read_lines(fd, timeout=3.0)
    for line in lines:
        print(line)

def cmd_exec(fd, expr, cmd_id=1):
    """Execute expression on Amiga."""
    send_cmd(fd, f"EXEC|{cmd_id}|{expr}")
    lines = read_lines(fd, timeout=3.0)
    for line in lines:
        print(line)

def cmd_inspect(fd, addr, size):
    """Inspect memory at address."""
    send_cmd(fd, f"INSPECT|{addr}|{size}")
    lines = read_lines(fd, timeout=3.0)
    for line in lines:
        print(line)

def cmd_send(fd, raw):
    """Send raw command and print responses."""
    send_cmd(fd, raw)
    lines = read_lines(fd, timeout=3.0)
    for line in lines:
        print(line)

def main():
    parser = argparse.ArgumentParser(description="Amiga serial debug tool")
    parser.add_argument("--pty", default=DEFAULT_PTY, help="PTY path")
    sub = parser.add_subparsers(dest="command")

    p_listen = sub.add_parser("listen", help="Listen for messages")
    p_listen.add_argument("--timeout", type=float, default=5.0)

    sub.add_parser("ping", help="Ping the Amiga")

    p_send = sub.add_parser("send", help="Send raw command")
    p_send.add_argument("cmd", help="Raw command string")

    p_getvar = sub.add_parser("getvar", help="Get variable")
    p_getvar.add_argument("name")

    p_setvar = sub.add_parser("setvar", help="Set variable")
    p_setvar.add_argument("name")
    p_setvar.add_argument("value")

    p_exec = sub.add_parser("exec", help="Execute expression")
    p_exec.add_argument("expr")

    p_inspect = sub.add_parser("inspect", help="Inspect memory")
    p_inspect.add_argument("addr", help="Hex address")
    p_inspect.add_argument("size", help="Bytes to read")

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        sys.exit(1)

    if not os.path.exists(args.pty):
        print(f"Error: PTY {args.pty} not found. Is socat running?")
        sys.exit(1)

    fd = open_pty(args.pty)
    try:
        if args.command == "listen":
            cmd_listen(fd, args.timeout)
        elif args.command == "ping":
            cmd_ping(fd)
        elif args.command == "send":
            cmd_send(fd, args.cmd)
        elif args.command == "getvar":
            cmd_getvar(fd, args.name)
        elif args.command == "setvar":
            cmd_setvar(fd, args.name, args.value)
        elif args.command == "exec":
            cmd_exec(fd, args.expr)
        elif args.command == "inspect":
            cmd_inspect(fd, args.addr, args.size)
    finally:
        os.close(fd)

if __name__ == "__main__":
    main()
