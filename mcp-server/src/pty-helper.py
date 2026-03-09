#!/usr/bin/env python3
"""PTY helper for Amiga MCP server.

Creates a PTY pair, symlinks the slave to a known path, and bridges
the master fd to stdin/stdout so the Node process can read/write directly.

Signals readiness by writing "PTY_READY:<path>" to stderr.
"""
import os
import sys
import select
import signal
import termios

def main():
    pty_path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/amiga-serial"

    master_fd, slave_fd = os.openpty()
    slave_name = os.ttyname(slave_fd)

    # Configure raw mode on both master and slave
    for fd in (master_fd, slave_fd):
        attrs = termios.tcgetattr(fd)
        attrs[0] = 0  # iflag: no input processing
        attrs[1] = 0  # oflag: no output processing
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL  # cflag: 8N1
        attrs[3] = 0  # lflag: no echo, no canonical, no signals
        attrs[6][termios.VMIN] = 1
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, attrs)

    # Symlink slave to known path
    try:
        os.unlink(pty_path)
    except FileNotFoundError:
        pass
    os.symlink(slave_name, pty_path)

    # Signal ready to parent process
    sys.stderr.write(f"PTY_READY:{pty_path}\n")
    sys.stderr.flush()

    stdin_fd = sys.stdin.fileno()
    stdout_fd = sys.stdout.fileno()

    running = True

    def stop(*_args):
        nonlocal running
        running = False

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)

    try:
        while running:
            try:
                readable, _, _ = select.select([master_fd, stdin_fd], [], [], 0.5)
            except (OSError, ValueError):
                break

            for fd in readable:
                if fd == master_fd:
                    # Data from FS-UAE (via PTY slave) -> forward to Node (stdout)
                    try:
                        data = os.read(master_fd, 4096)
                        if data:
                            os.write(stdout_fd, data)
                        else:
                            running = False
                    except OSError:
                        running = False

                elif fd == stdin_fd:
                    # Data from Node (stdin) -> forward to FS-UAE (via PTY master)
                    try:
                        data = os.read(stdin_fd, 4096)
                        if data:
                            os.write(master_fd, data)
                        else:
                            running = False
                    except OSError:
                        running = False

    finally:
        os.close(master_fd)
        os.close(slave_fd)
        try:
            os.unlink(pty_path)
        except OSError:
            pass
        sys.stderr.write("PTY_CLOSED\n")
        sys.stderr.flush()


if __name__ == "__main__":
    main()
