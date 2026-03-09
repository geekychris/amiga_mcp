"""Amiga simulator - TCP server that pretends to be an Amiga running a bouncing ball app."""

from __future__ import annotations

import asyncio
import logging
import math
import random

logger = logging.getLogger(__name__)


class AmigaSimulator:
    """TCP server on a given port that fakes an Amiga with a bouncing ball demo."""

    def __init__(self, port: int = 1234) -> None:
        self._port = port
        self._server: asyncio.Server | None = None

        # Simulated state
        self._tick = 0
        self._ball_x = 160
        self._ball_y = 100
        self._ball_dx = 3
        self._ball_dy = 2
        self._frame_count = 0
        self._free_chip = 512000
        self._free_fast = 1048576
        self._running = True

        # Variable registry
        self._vars: dict[str, dict] = {
            "ball_x": {"type": "i32", "get": lambda: str(self._ball_x),
                       "set": lambda v: setattr(self, "_ball_x", int(v))},
            "ball_y": {"type": "i32", "get": lambda: str(self._ball_y),
                       "set": lambda v: setattr(self, "_ball_y", int(v))},
            "ball_dx": {"type": "i32", "get": lambda: str(self._ball_dx),
                        "set": lambda v: setattr(self, "_ball_dx", int(v))},
            "ball_dy": {"type": "i32", "get": lambda: str(self._ball_dy),
                        "set": lambda v: setattr(self, "_ball_dy", int(v))},
            "frame_count": {"type": "u32", "get": lambda: str(self._frame_count)},
            "free_chip": {"type": "u32", "get": lambda: str(self._free_chip)},
            "free_fast": {"type": "u32", "get": lambda: str(self._free_fast)},
        }

    def _get_memory(self, addr: int, size: int) -> str:
        """Generate fake memory contents."""
        return "".join(f"{(addr + i) & 0xFF:02X}" for i in range(size))

    async def start(self) -> None:
        self._server = await asyncio.start_server(
            self._handle_client, "127.0.0.1", self._port,
        )
        logger.info("Amiga Simulator listening on TCP port %d", self._port)
        logger.info("Simulating: Bouncing Ball demo")
        logger.info("Variables: %s", ", ".join(self._vars.keys()))

    async def stop(self) -> None:
        self._running = False
        if self._server:
            self._server.close()
            await self._server.wait_closed()
            self._server = None

    async def _handle_client(
        self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter,
    ) -> None:
        addr = writer.get_extra_info("peername")
        logger.info("MCP host connected from %s", addr)

        def send_line(line: str) -> None:
            try:
                writer.write((line + "\n").encode("latin-1"))
            except Exception:
                pass

        # Send startup messages
        send_line(f"LOG|I|{self._tick}|Debug session started")
        self._tick += 1
        send_line(f"HB|{self._tick}|{self._free_chip}|{self._free_fast}")
        send_line(f"LOG|I|{self._tick}|Bouncing Ball demo starting")
        self._tick += 1
        send_line(f"LOG|I|{self._tick}|Window opened: inner 312x178")
        self._tick += 1

        sim_task = asyncio.ensure_future(self._simulation_loop(send_line))
        line_buf = ""

        try:
            while True:
                data = await reader.read(4096)
                if not data:
                    break
                line_buf += data.decode("latin-1", errors="replace")
                lines = line_buf.split("\n")
                line_buf = lines.pop()

                for line in lines:
                    trimmed = line.strip()
                    if not trimmed:
                        continue
                    logger.debug("  <- %s", trimmed)
                    self._handle_command(send_line, trimmed)
                    await writer.drain()
        except (asyncio.CancelledError, ConnectionError):
            pass
        finally:
            sim_task.cancel()
            try:
                await sim_task
            except asyncio.CancelledError:
                pass
            writer.close()
            try:
                await writer.wait_closed()
            except Exception:
                pass
            logger.info("MCP host disconnected")

    async def _simulation_loop(self, send_line) -> None:
        """Run the bouncing ball simulation at ~20fps."""
        try:
            while self._running:
                await asyncio.sleep(0.05)  # ~20fps

                # Update ball
                self._ball_x += self._ball_dx
                self._ball_y += self._ball_dy

                if self._ball_x <= 5 or self._ball_x >= 307:
                    self._ball_dx = -self._ball_dx
                    self._ball_x += self._ball_dx
                    send_line(f"LOG|D|{self._tick}|Bounce X at {self._ball_x}")

                if self._ball_y <= 5 or self._ball_y >= 173:
                    self._ball_dy = -self._ball_dy
                    self._ball_y += self._ball_dy
                    send_line(f"LOG|D|{self._tick}|Bounce Y at {self._ball_y}")

                self._frame_count += 1
                self._tick += 1

                # Vary memory slightly
                self._free_chip += random.randint(-100, 100)
                self._free_fast += random.randint(-200, 200)

                # Heartbeat every 60 frames
                if self._frame_count % 60 == 0:
                    send_line(f"HB|{self._tick}|{self._free_chip}|{self._free_fast}")

                # Periodic info logs
                if self._frame_count % 120 == 0:
                    send_line(
                        f"LOG|I|{self._tick}|frame={self._frame_count} "
                        f"ball({self._ball_x},{self._ball_y}) "
                        f"vel({self._ball_dx},{self._ball_dy})"
                    )

                # Occasional warnings
                if self._frame_count % 500 == 0 and self._free_chip < 480000:
                    send_line(
                        f"LOG|W|{self._tick}|Chip memory getting low: {self._free_chip} bytes"
                    )
        except asyncio.CancelledError:
            pass

    def _handle_command(self, send_line, line: str) -> None:
        parts = line.split("|")
        cmd = parts[0]

        if cmd == "PING":
            send_line(f"HB|{self._tick}|{self._free_chip}|{self._free_fast}")

        elif cmd == "GETVAR" and len(parts) >= 2:
            name = parts[1]
            v = self._vars.get(name)
            if v:
                send_line(f"VAR|{name}|{v['type']}|{v['get']()}")
            else:
                send_line(f"VAR|{name}|err|not_found")

        elif cmd == "SETVAR" and len(parts) >= 3:
            name = parts[1]
            value = parts[2]
            v = self._vars.get(name)
            if v and "set" in v:
                v["set"](value)
                send_line(f"VAR|{name}|{v['type']}|{v['get']()}")
                send_line(f"LOG|I|{self._tick}|Host set {name} = {value}")

        elif cmd == "INSPECT" and len(parts) >= 3:
            addr = int(parts[1], 16)
            size = min(int(parts[2]), 4096)
            offset = 0
            while offset < size:
                chunk = min(256, size - offset)
                hex_data = self._get_memory(addr + offset, chunk)
                send_line(f"MEM|{(addr + offset):08x}|{chunk}|{hex_data}")
                offset += chunk

        elif cmd == "EXEC" and len(parts) >= 3:
            cmd_id = parts[1]
            expression = "|".join(parts[2:])
            if expression == "reset":
                self._ball_x = 160
                self._ball_y = 100
                self._ball_dx = 3
                self._ball_dy = 2
                send_line(f"CMD|{cmd_id}|ok|Ball position reset")
                send_line(f"LOG|I|{self._tick}|Ball position reset by host")
            elif expression == "status":
                send_line(
                    f"CMD|{cmd_id}|ok|ball({self._ball_x},{self._ball_y}) "
                    f"vel({self._ball_dx},{self._ball_dy}) frame={self._frame_count}"
                )
            else:
                send_line(f"CMD|{cmd_id}|ok|Executed: {expression}")

        elif cmd == "LISTCLIENTS":
            send_line("CLIENTS|1|bouncing_ball")

        elif cmd == "LISTTASKS":
            import json
            tasks = [
                {"name": "bouncing_ball", "priority": 0, "state": "running", "stackSize": 4096},
                {"name": "input.device", "priority": 20, "state": "waiting", "stackSize": 4096},
                {"name": "trackdisk.device", "priority": 5, "state": "waiting", "stackSize": 4096},
            ]
            send_line(f"TASKS|{len(tasks)}|{json.dumps(tasks)}")

        elif cmd == "LISTLIBS":
            import json
            libs = [
                {"name": "exec.library", "version": 40, "revision": 10},
                {"name": "dos.library", "version": 40, "revision": 3},
                {"name": "intuition.library", "version": 40, "revision": 5},
                {"name": "graphics.library", "version": 40, "revision": 2},
            ]
            send_line(f"LIBS|{len(libs)}|{json.dumps(libs)}")

        elif cmd == "LISTDIR" and len(parts) >= 2:
            import json
            path = parts[1]
            entries = [
                {"name": "c", "type": "dir", "size": 0, "date": "2024-01-15", "prot": "----rwed", "path": f"{path}c/"},
                {"name": "libs", "type": "dir", "size": 0, "date": "2024-01-15", "prot": "----rwed", "path": f"{path}libs/"},
                {"name": "startup-sequence", "type": "file", "size": 512, "date": "2024-01-15", "prot": "----rwed"},
            ]
            send_line(f"DIR|{path}|{len(entries)}|{json.dumps(entries)}")

        elif cmd == "READFILE" and len(parts) >= 4:
            path = parts[1]
            offset = int(parts[2])
            size = min(int(parts[3]), 4096)
            hex_data = self._get_memory(offset, size)
            send_line(f"FILE|{path}|{size}|{offset}|{hex_data}")

        elif cmd == "LAUNCH" and len(parts) >= 3:
            cmd_id = parts[1]
            command = "|".join(parts[2:])
            send_line(f"PROC|{cmd_id}|started|Launched: {command}")
            send_line(f"LOG|I|{self._tick}|Launched process: {command}")

        elif cmd == "DOSCOMMAND" and len(parts) >= 3:
            cmd_id = parts[1]
            command = "|".join(parts[2:])
            send_line(f"CMD|{cmd_id}|ok|DOS: {command}")
