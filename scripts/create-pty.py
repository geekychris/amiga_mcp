#!/usr/bin/env python3
"""Create a PTY for FS-UAE serial and keep it alive.
Run this before starting FS-UAE. Keep it running.

Usage: ./create-pty.py [/tmp/amiga-serial]
"""
import os
import sys
import signal
import time

path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/amiga-serial"

master, slave = os.openpty()
slave_name = os.ttyname(slave)
print(f"PTY slave: {slave_name}")

# Create symlink
try:
    os.unlink(path)
except FileNotFoundError:
    pass
os.symlink(slave_name, path)
print(f"Symlink: {path} -> {slave_name}")
print(f"Start FS-UAE now. Press Ctrl+C to stop.")

# Keep master open so the PTY stays alive
signal.signal(signal.SIGINT, lambda *a: sys.exit(0))
try:
    while True:
        time.sleep(1)
except (KeyboardInterrupt, SystemExit):
    os.close(master)
    os.close(slave)
    try:
        os.unlink(path)
    except:
        pass
    print("\nCleaned up.")
