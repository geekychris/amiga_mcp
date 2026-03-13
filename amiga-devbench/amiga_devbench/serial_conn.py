"""Serial connection to Amiga emulator - PTY and TCP modes."""

from __future__ import annotations

import asyncio
import logging
import os
import sys
import time
from typing import Any

from .protocol import format_command, parse_message
from .state import AmigaState, EventBus

logger = logging.getLogger(__name__)

RECONNECT_INTERVAL = 5.0
PTY_RESTART_DELAY = 2.0
MAX_LOG_BUFFER = 1000


class SerialConnection:
    """Manages connection to Amiga via PTY or TCP."""

    def __init__(
        self,
        state: AmigaState,
        event_bus: EventBus,
        host: str = "127.0.0.1",
        port: int = 1234,
        pty_path: str = "/tmp/amiga-serial",
    ) -> None:
        self._state = state
        self._event_bus = event_bus
        self._host = host
        self._port = port
        self._pty_path = pty_path
        self._mode = "tcp"

        # TCP state
        self._reader: asyncio.StreamReader | None = None
        self._writer: asyncio.StreamWriter | None = None
        self._tcp_task: asyncio.Task | None = None

        # PTY state
        self._master_fd: int | None = None
        self._slave_fd: int | None = None

        # Common
        self._line_buf = ""
        self._connected = False
        self._auto_reconnect = True
        self._reconnect_task: asyncio.Task | None = None

        # Optional callbacks for persistent logging
        self.on_tx: Any = None  # called with (line: str)
        self.on_rx: Any = None  # called with (line: str)

    @property
    def connected(self) -> bool:
        return self._connected

    @property
    def mode(self) -> str:
        return self._mode

    @property
    def pty_device_path(self) -> str:
        return self._pty_path

    def set_target(self, host: str, port: int) -> None:
        self._host = host
        self._port = port
        self._mode = "tcp"

    def set_mode(self, mode: str, **kwargs: Any) -> None:
        self._mode = mode
        if "pty_path" in kwargs:
            self._pty_path = kwargs["pty_path"]
        if "host" in kwargs:
            self._host = kwargs["host"]
        if "port" in kwargs:
            self._port = kwargs["port"]

    async def connect(self) -> None:
        if self._mode == "pty":
            await self.connect_pty()
        else:
            await self.connect_tcp()

    async def connect_tcp(self) -> None:
        if self._connected:
            return
        self._auto_reconnect = True
        try:
            self._reader, self._writer = await asyncio.wait_for(
                asyncio.open_connection(self._host, self._port),
                timeout=10.0,
            )
            self._connected = True
            self._line_buf = ""
            self._state.connected = True
            self._state.connection_mode = "tcp"
            self._state.serial_connected_at = time.time()
            self._event_bus.publish("connected", {})
            logger.info("Connected to Amiga via TCP at %s:%d", self._host, self._port)
            # Start read loop
            self._tcp_task = asyncio.ensure_future(self._tcp_read_loop())
        except Exception as e:
            logger.error("TCP connection failed: %s", e)
            raise

    async def _tcp_read_loop(self) -> None:
        try:
            while self._connected and self._reader:
                data = await self._reader.read(4096)
                if not data:
                    break
                self._handle_data(data.decode("latin-1", errors="replace"))
        except (asyncio.CancelledError, ConnectionError):
            pass
        except Exception as e:
            logger.error("TCP read error: %s", e)
        finally:
            was_connected = self._connected
            self._connected = False
            self._state.connected = False
            if was_connected:
                self._event_bus.publish("disconnected", {})
                logger.info("TCP disconnected")
            if self._auto_reconnect:
                self._schedule_reconnect()

    async def connect_pty(self) -> None:
        if self._connected:
            return
        self._auto_reconnect = True

        if sys.platform == "darwin" or sys.platform.startswith("linux"):
            # Reuse existing PTY fds if still open (after a soft disconnect)
            if self._master_fd is not None:
                self._connected = True
                self._line_buf = ""
                self._state.connected = True
                self._state.connection_mode = "pty"
                self._state.serial_connected_at = time.time()
                self._event_bus.publish("connected", {})
                logger.info("PTY reconnected (reusing existing fd): %s", self._pty_path)
                loop = asyncio.get_event_loop()
                loop.add_reader(self._master_fd, self._pty_read_callback)
                return

            import termios

            master_fd, slave_fd = os.openpty()
            slave_name = os.ttyname(slave_fd)

            # Configure raw mode on both fds
            for fd in (master_fd, slave_fd):
                attrs = termios.tcgetattr(fd)
                attrs[0] = 0  # iflag - no input processing
                attrs[1] = 0  # oflag - no output processing
                attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL  # cflag
                attrs[3] = 0  # lflag - no echo, no canonical
                attrs[6][termios.VMIN] = 1
                attrs[6][termios.VTIME] = 0
                termios.tcsetattr(fd, termios.TCSANOW, attrs)

            # Create symlink
            try:
                os.unlink(self._pty_path)
            except FileNotFoundError:
                pass
            os.symlink(slave_name, self._pty_path)

            self._master_fd = master_fd
            self._slave_fd = slave_fd
            self._connected = True
            self._line_buf = ""
            self._state.connected = True
            self._state.connection_mode = "pty"
            self._state.serial_connected_at = time.time()
            self._event_bus.publish("connected", {})
            logger.info("PTY ready: %s -> %s", self._pty_path, slave_name)

            # Register reader on the event loop
            loop = asyncio.get_event_loop()
            loop.add_reader(master_fd, self._pty_read_callback)
        else:
            raise RuntimeError("PTY mode is only supported on macOS and Linux")

    def _pty_read_callback(self) -> None:
        """Called by the event loop when data is available on the PTY master fd."""
        if self._master_fd is None:
            return
        try:
            data = os.read(self._master_fd, 4096)
            if data:
                self._handle_data(data.decode("latin-1", errors="replace"))
        except OSError as e:
            logger.error("PTY read error: %s", e)
            was_connected = self._connected
            self._connected = False
            self._state.connected = False
            if self._master_fd is not None:
                try:
                    asyncio.get_event_loop().remove_reader(self._master_fd)
                except Exception:
                    pass
            if was_connected:
                self._event_bus.publish("disconnected", {})
            if self._auto_reconnect:
                self._schedule_reconnect()

    def disconnect(self) -> None:
        self._auto_reconnect = False
        if self._reconnect_task and not self._reconnect_task.done():
            self._reconnect_task.cancel()
            self._reconnect_task = None

        if self._mode == "tcp":
            if self._tcp_task and not self._tcp_task.done():
                self._tcp_task.cancel()
            if self._writer:
                try:
                    self._writer.close()
                except Exception:
                    pass
            self._reader = None
            self._writer = None
        else:
            # PTY mode - only pause reading, do NOT close fds or unlink symlink.
            # FS-UAE reads the symlink at startup and keeps the fd open, so
            # destroying the PTY would permanently break the connection.
            if self._master_fd is not None:
                try:
                    asyncio.get_event_loop().remove_reader(self._master_fd)
                except Exception:
                    pass

        self._connected = False
        self._state.connected = False
        self._state.serial_connected_at = None
        self._state.last_bridge_message_at = None
        self._state.last_heartbeat_at = None
        self._event_bus.publish("disconnected", {})

    def send(self, cmd: dict[str, Any]) -> None:
        if not self._connected:
            raise RuntimeError("Not connected to Amiga serial port")
        line = format_command(cmd)
        self.send_raw(line)

    def send_raw(self, line: str) -> None:
        if not self._connected:
            raise RuntimeError("Not connected to Amiga serial port")
        logger.debug("TX: %s", line[:120])
        if self.on_tx:
            try:
                self.on_tx(line)
            except Exception:
                pass
        data = (line + "\n").encode("latin-1")
        if self._mode == "tcp":
            if self._writer is None:
                raise RuntimeError("Not connected (TCP)")
            self._writer.write(data)
        else:
            if self._master_fd is None:
                raise RuntimeError("Not connected (PTY)")
            os.write(self._master_fd, data)

    def _handle_data(self, data: str) -> None:
        # Log raw data for debugging
        raw_repr = repr(data[:200]) if len(data) > 200 else repr(data)
        logger.debug("RX raw: %s", raw_repr)

        # Flush stale buffer if it's been sitting too long (incomplete line)
        # This prevents a truncated response from corrupting the next one
        if self._line_buf and hasattr(self, '_line_buf_time'):
            if time.monotonic() - self._line_buf_time > 2.0:
                logger.debug("Flushing stale line buffer: %s", repr(self._line_buf[:80]))
                self._line_buf = ""
        self._line_buf_time = time.monotonic()

        self._line_buf += data
        lines = self._line_buf.split("\n")
        self._line_buf = lines.pop()

        for line in lines:
            trimmed = line.strip()
            if not trimmed:
                continue
            # Strip non-printable characters (serial noise) instead of
            # dropping the entire line - valid protocol data may follow
            cleaned = "".join(c for c in trimmed if ord(c) >= 32 or c == '\t')
            if not cleaned:
                logger.debug("Skipping all-garbage line: %s", repr(trimmed[:80]))
                continue
            if cleaned != trimmed:
                logger.debug("Stripped noise from line: %s -> %s", repr(trimmed[:80]), repr(cleaned[:80]))
            logger.debug("RX line: %s", cleaned[:120])
            if self.on_rx:
                try:
                    self.on_rx(cleaned)
                except Exception:
                    pass
            msg = parse_message(cleaned)
            if msg is None:
                logger.debug("Unparseable line: %s", cleaned[:120])
                continue
            self._process_message(msg)

    def _process_message(self, msg: dict[str, Any]) -> None:
        msg_type = msg["type"]

        # Track that we received a message from the bridge
        self._state.last_bridge_message_at = time.time()

        if msg_type == "LOG":
            self._state.add_log(msg)
            self._event_bus.publish("log", msg)

        elif msg_type == "VAR":
            self._state.vars[msg["name"]] = msg
            self._event_bus.publish("var", msg)

        elif msg_type == "HB":
            self._state.last_heartbeat = msg
            self._state.last_heartbeat_at = time.time()
            self._event_bus.publish("heartbeat", msg)

        elif msg_type == "MEM":
            self._event_bus.publish("mem", msg)

        elif msg_type == "CMD":
            self._event_bus.publish("cmd", msg)

        elif msg_type == "CLOG":
            raw_msg = msg["message"]
            # Check for test harness messages embedded in CLOG
            if raw_msg.startswith("TEST_"):
                test_parsed = protocol.parse_message(raw_msg)
                if test_parsed:
                    test_parsed["client"] = msg.get("client", "")
                    test_parsed["timestamp"] = msg["timestamp"]
                    self._event_bus.publish("test", test_parsed)
                    # Also publish as log for visibility
                    ttype = test_parsed["type"]
                    if ttype == "TEST_PASS":
                        self._event_bus.publish("log", {
                            "type": "LOG", "level": "I", "tick": 0,
                            "message": f"[TEST] PASS: {test_parsed.get('testName', '?')}",
                            "timestamp": msg["timestamp"], "client": msg.get("client", ""),
                        })
                    elif ttype == "TEST_FAIL":
                        self._event_bus.publish("log", {
                            "type": "LOG", "level": "E", "tick": 0,
                            "message": f"[TEST] FAIL: {test_parsed.get('testName', '?')} at {test_parsed.get('file', '?')}:{test_parsed.get('line', 0)}",
                            "timestamp": msg["timestamp"], "client": msg.get("client", ""),
                        })
                    elif ttype == "TEST_END":
                        p = test_parsed.get("passed", 0)
                        f = test_parsed.get("failed", 0)
                        t = test_parsed.get("total", 0)
                        self._event_bus.publish("log", {
                            "type": "LOG", "level": "E" if f > 0 else "I", "tick": 0,
                            "message": f"[TEST] Suite '{test_parsed.get('suite', '?')}': {p}/{t} passed, {f} failed",
                            "timestamp": msg["timestamp"], "client": msg.get("client", ""),
                        })
                    elif ttype == "TEST_BEGIN":
                        self._event_bus.publish("log", {
                            "type": "LOG", "level": "I", "tick": 0,
                            "message": f"[TEST] Suite '{test_parsed.get('suite', '?')}' started",
                            "timestamp": msg["timestamp"], "client": msg.get("client", ""),
                        })
                    return  # Don't publish as regular CLOG

            # Store as regular log with client prefix
            log_entry = {
                "type": "LOG",
                "level": msg["level"],
                "tick": msg["tick"],
                "message": f"[{msg['client']}] {raw_msg}",
                "timestamp": msg["timestamp"],
                "client": msg["client"],
            }
            self._state.add_log(log_entry)
            self._event_bus.publish("log", log_entry)
            self._event_bus.publish("clog", msg)

        elif msg_type == "CVAR":
            var_entry = {
                "type": "VAR",
                "name": msg["name"],
                "varType": msg["varType"],
                "value": msg["value"],
                "client": msg["client"],
            }
            self._state.vars[msg["name"]] = var_entry
            self._event_bus.publish("var", var_entry)
            self._event_bus.publish("cvar", msg)

        elif msg_type == "READY":
            self._event_bus.publish("ready", msg)

        elif msg_type == "CLIENTS":
            logger.info(">>> CLIENTS msg received: %s", msg)
            self._state.clients = msg.get("names", [])
            self._event_bus.publish("clients", msg)

        elif msg_type == "TASKS":
            logger.info(">>> TASKS msg received: %s", msg)
            self._event_bus.publish("tasks", msg)

        elif msg_type == "LIBS":
            self._event_bus.publish("libs", msg)

        elif msg_type == "DIR":
            self._event_bus.publish("dir", msg)

        elif msg_type == "FILE":
            self._event_bus.publish("file", msg)

        elif msg_type == "FILEINFO":
            self._event_bus.publish("fileinfo", msg)

        elif msg_type == "PROC":
            self._event_bus.publish("proc", msg)

        elif msg_type == "VOLUMES":
            self._event_bus.publish("volumes", msg)

        elif msg_type == "PONG":
            self._event_bus.publish("pong", msg)

        elif msg_type == "HOOKS":
            self._event_bus.publish("hooks", msg)

        elif msg_type == "MEMREGS":
            self._event_bus.publish("memregs", msg)

        elif msg_type == "CINFO":
            self._event_bus.publish("cinfo", msg)

        elif msg_type == "DEVICES":
            self._event_bus.publish("devices", msg)

        elif msg_type == "OK":
            self._event_bus.publish("ok", msg)

        elif msg_type == "ERR":
            self._event_bus.publish("err", msg)

        elif msg_type == "SCRINFO":
            self._event_bus.publish("scrinfo", msg)

        elif msg_type == "SCRDATA":
            self._event_bus.publish("scrdata", msg)

        elif msg_type == "PALETTE":
            self._event_bus.publish("palette", msg)

        elif msg_type == "COPPER":
            self._event_bus.publish("copper", msg)

        elif msg_type == "SPRITE":
            self._event_bus.publish("sprite", msg)

        elif msg_type == "CRASH":
            self._state.last_crash = msg
            self._state.add_log({
                "type": "LOG", "level": "E", "tick": 0,
                "message": f"CRASH: {msg.get('alertName', '?')} (0x{msg.get('alertNum', '?')})",
                "timestamp": msg.get("timestamp", ""),
            })
            self._event_bus.publish("crash", msg)
            self._event_bus.publish("log", {
                "type": "LOG", "level": "E", "tick": 0,
                "message": f"CRASH: {msg.get('alertName', '?')} (0x{msg.get('alertNum', '?')})",
                "timestamp": msg.get("timestamp", ""),
            })

        elif msg_type == "RESOURCES":
            self._event_bus.publish("resources", msg)

        elif msg_type == "PERF":
            self._event_bus.publish("perf", msg)

        elif msg_type == "WINLIST":
            self._event_bus.publish("winlist", msg)

        elif msg_type == "MEMMAP":
            self._event_bus.publish("memmap", msg)

        elif msg_type == "STACKINFO":
            self._event_bus.publish("stackinfo", msg)

        elif msg_type == "CHIPREGS":
            self._event_bus.publish("chipregs", msg)

        elif msg_type == "REGS":
            self._event_bus.publish("regs", msg)

        elif msg_type == "SEARCH":
            self._event_bus.publish("search", msg)

        elif msg_type == "LIBINFO":
            self._event_bus.publish("libinfo", msg)

        elif msg_type == "DEVINFO":
            self._event_bus.publish("devinfo", msg)

        elif msg_type == "LIBFUNCS":
            self._event_bus.publish("libfuncs", msg)

        elif msg_type == "SNOOP":
            self._event_bus.publish("snoop", msg)

        elif msg_type == "SNOOPSTATE":
            self._event_bus.publish("snoopstate", msg)

        elif msg_type == "AUDIOCHANNELS":
            self._event_bus.publish("audiochannels", msg)

        elif msg_type == "AUDIOSAMPLE":
            self._event_bus.publish("audiosample", msg)

        elif msg_type == "SCREENS":
            self._event_bus.publish("screens", msg)

        elif msg_type == "WINDOWS":
            self._event_bus.publish("windows", msg)

        elif msg_type == "GADGETS":
            self._event_bus.publish("gadgets", msg)

        elif msg_type == "FONTS":
            self._event_bus.publish("fonts", msg)

        elif msg_type == "FONTINFO":
            self._event_bus.publish("fontinfo", msg)

        elif msg_type == "CHIPLOG":
            self._event_bus.publish("chiplog", msg)

        elif msg_type == "CHIPLOGCHANGE":
            self._event_bus.publish("chiplogchange", msg)

        elif msg_type == "POOLS":
            self._event_bus.publish("pools", msg)

        elif msg_type == "CLIPBOARD":
            self._event_bus.publish("clipboard", msg)

        elif msg_type == "AREXXPORTS":
            self._event_bus.publish("arexxports", msg)

        elif msg_type == "AREXXRESULT":
            self._event_bus.publish("arexxresult", msg)

        elif msg_type == "CAPABILITIES":
            self._event_bus.publish("capabilities", msg)

        elif msg_type == "PROCLIST":
            self._event_bus.publish("proclist", msg)

        elif msg_type == "PROCSTAT":
            self._event_bus.publish("procstat", msg)

        elif msg_type == "TAILDATA":
            self._event_bus.publish("taildata", msg)

        elif msg_type == "CHECKSUM":
            self._event_bus.publish("checksum", msg)

        elif msg_type == "ASSIGNS":
            self._event_bus.publish("assigns", msg)

        elif msg_type == "PROTECT":
            self._event_bus.publish("protect", msg)

        elif msg_type == "VERSION":
            self._event_bus.publish("version", msg)

        elif msg_type == "ENV":
            self._event_bus.publish("env", msg)

        elif msg_type == "PORTS":
            self._event_bus.publish("ports", msg)

        elif msg_type == "SYSINFO":
            self._event_bus.publish("sysinfo", msg)

        elif msg_type == "UPTIME":
            self._event_bus.publish("uptime", msg)

    def _schedule_reconnect(self) -> None:
        if self._reconnect_task and not self._reconnect_task.done():
            return
        delay = PTY_RESTART_DELAY if self._mode == "pty" else RECONNECT_INTERVAL
        self._reconnect_task = asyncio.ensure_future(self._reconnect_loop(delay))

    async def _reconnect_loop(self, delay: float) -> None:
        await asyncio.sleep(delay)
        if not self._connected and self._auto_reconnect:
            logger.info("Attempting %s reconnect...", self._mode)
            try:
                await self.connect()
            except Exception as e:
                logger.error("Reconnect failed: %s", e)
                self._schedule_reconnect()

    def get_status(self) -> dict[str, Any]:
        now = time.time()
        result: dict[str, Any] = {
            "connected": self._connected,
            "mode": self._mode,
            "logCount": len(self._state.logs),
            "varCount": len(self._state.vars),
            "lastHeartbeat": self._state.last_heartbeat,
            "serialConnectedAt": self._state.serial_connected_at,
            "lastBridgeMessageAt": self._state.last_bridge_message_at,
            "lastHeartbeatAt": self._state.last_heartbeat_at,
            "bridgeSilentSec": (
                round(now - self._state.last_bridge_message_at, 1)
                if self._state.last_bridge_message_at else None
            ),
        }
        if self._mode == "tcp":
            result["host"] = self._host
            result["port"] = self._port
        else:
            result["ptyPath"] = self._pty_path
        return result
