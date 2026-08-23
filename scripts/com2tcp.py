#!/usr/bin/env python3
"""Expose a Windows COM port as a TCP socket (for devbench in WSL).

Usage: python com2tcp.py [COM11] [2345] [115200]
"""
import socket
import sys
import threading
import time

import serial

COM = sys.argv[1] if len(sys.argv) > 1 else "COM11"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 2345
BAUD = int(sys.argv[3]) if len(sys.argv) > 3 else 115200

ser = serial.Serial(COM, BAUD, timeout=0.05)
srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("0.0.0.0", PORT))
srv.listen(1)
print(f"{COM} @ {BAUD} <-> tcp 0.0.0.0:{PORT}", flush=True)

while True:
    conn, addr = srv.accept()
    conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    print(f"client {addr}", flush=True)
    alive = [True]

    def com_to_tcp():
        while alive[0]:
            data = ser.read(4096)
            if data:
                try:
                    conn.sendall(data)
                except OSError:
                    break
        alive[0] = False

    t = threading.Thread(target=com_to_tcp, daemon=True)
    t.start()
    try:
        while True:
            data = conn.recv(4096)
            if not data:
                break
            # ponytail: pace writes to ~3KB/s — Amiga's 1-byte serial read
            # loop overruns on full-rate 115200 bursts (no flow control)
            for i in range(0, len(data), 32):
                ser.write(data[i:i + 32])
                time.sleep(0.01)
    except OSError:
        pass
    alive[0] = False
    t.join()
    conn.close()
    print("client gone", flush=True)
