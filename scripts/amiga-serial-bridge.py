#!/usr/bin/env python3
"""Amiga serial bridge: creates PTY for FS-UAE and bridges to TCP.

Start this BEFORE FS-UAE. It:
1. Creates a PTY at /tmp/amiga-serial for FS-UAE
2. Listens on TCP port 1234 for MCP/client connections
3. Bridges data bidirectionally between PTY and TCP

Usage: ./amiga-serial-bridge.py [--port 1234] [--pty /tmp/amiga-serial]
"""
import os
import sys
import select
import socket
import signal
import argparse
import tty
import termios

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=1234)
    parser.add_argument("--pty", default="/tmp/amiga-serial")
    args = parser.parse_args()

    # Create PTY
    master_fd, slave_fd = os.openpty()
    slave_name = os.ttyname(slave_fd)

    # Configure PTY: raw mode, no echo, matching socat's raw,echo=0
    # Must set on both master and slave before FS-UAE opens it
    for fd in (master_fd, slave_fd):
        attrs = termios.tcgetattr(fd)
        # Input: no special processing
        attrs[0] = 0  # iflag
        # Output: no special processing
        attrs[1] = 0  # oflag
        # Control: 8N1, enable receiver
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        # Local: nothing (no echo, no canonical, no signals)
        attrs[3] = 0  # lflag
        # Special chars
        attrs[6][termios.VMIN] = 1
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, attrs)

    # Symlink slave to known path
    try:
        os.unlink(args.pty)
    except FileNotFoundError:
        pass
    os.symlink(slave_name, args.pty)

    print(f"PTY: {args.pty} -> {slave_name}")
    print(f"TCP: listening on port {args.port}")
    print(f"Start FS-UAE now. Ctrl+C to stop.")
    sys.stdout.flush()

    # Listen on TCP
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", args.port))
    srv.listen(1)
    srv.setblocking(False)

    tcp_conn = None
    running = True

    def cleanup(*a):
        nonlocal running
        running = False

    signal.signal(signal.SIGINT, cleanup)
    signal.signal(signal.SIGTERM, cleanup)

    try:
        while running:
            # Build select list
            rlist = [master_fd, srv]
            if tcp_conn:
                rlist.append(tcp_conn)

            try:
                readable, _, _ = select.select(rlist, [], [], 0.5)
            except (OSError, ValueError):
                break

            for r in readable:
                if r == srv:
                    # Accept new TCP connection (drop old one)
                    conn, addr = srv.accept()
                    if tcp_conn:
                        print(f"Replacing connection with {addr}")
                        tcp_conn.close()
                    else:
                        print(f"Client connected: {addr}")
                    tcp_conn = conn
                    tcp_conn.setblocking(False)
                    sys.stdout.flush()

                elif r == master_fd:
                    # Data from Amiga (PTY) -> forward to TCP
                    try:
                        data = os.read(master_fd, 4096)
                        if data:
                            print(f"PTY->TCP: {data!r}", flush=True)
                            if tcp_conn:
                                try:
                                    tcp_conn.sendall(data)
                                except (BrokenPipeError, OSError):
                                    print("TCP client disconnected", flush=True)
                                    tcp_conn.close()
                                    tcp_conn = None
                    except OSError as e:
                        print(f"PTY read error: {e}", flush=True)

                elif r == tcp_conn:
                    # Data from TCP -> forward to Amiga (PTY)
                    try:
                        data = tcp_conn.recv(4096)
                        if data:
                            print(f"TCP->PTY: {data!r}", flush=True)
                            os.write(master_fd, data)
                        else:
                            print("TCP client disconnected", flush=True)
                            tcp_conn.close()
                            tcp_conn = None
                    except (ConnectionResetError, OSError):
                        print("TCP client disconnected", flush=True)
                        tcp_conn.close()
                        tcp_conn = None

    finally:
        if tcp_conn:
            tcp_conn.close()
        srv.close()
        os.close(master_fd)
        os.close(slave_fd)
        try:
            os.unlink(args.pty)
        except:
            pass
        print("\nCleaned up.")

if __name__ == "__main__":
    main()
