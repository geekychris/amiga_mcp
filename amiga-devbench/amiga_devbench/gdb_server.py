"""GDB Remote Serial Protocol server for Amiga remote debugging.

Translates GDB RSP packets to AmigaBridge serial protocol commands.
Runs as an asyncio task alongside the main devbench server.

Usage:
  m68k-amigaos-gdb binary -ex "target remote :2159"

  Or configure in CLion/VS Code to connect to localhost:2159.
"""

from __future__ import annotations

import asyncio
import logging
from typing import Any

from .state import EventBus
from .serial_conn import SerialConnection
from .debugger import DebuggerState

logger = logging.getLogger(__name__)

# 68k GDB register order: D0-D7, A0-A7, SR, PC (18 regs, 32-bit each)
# Note: GDB expects SR before PC for m68k
GDB_REG_COUNT = 18
GDB_REG_SIZE = 4  # bytes per register


def _checksum(data: str) -> int:
    """Calculate GDB RSP checksum."""
    return sum(ord(c) for c in data) & 0xFF


def _make_packet(data: str) -> bytes:
    """Create a GDB RSP packet: $data#checksum."""
    cs = _checksum(data)
    return f"${data}#{cs:02x}".encode("ascii")


def _hex32(val: int) -> str:
    """Format a 32-bit value as 8 hex chars, big-endian."""
    return f"{val:08x}"


def _parse_hex(s: str) -> int:
    """Parse hex string to int."""
    return int(s, 16)


class GDBServer:
    """TCP server implementing GDB Remote Serial Protocol."""

    def __init__(
        self,
        conn: SerialConnection,
        bus: EventBus,
        dbg_state: DebuggerState,
        port: int = 2159,
    ) -> None:
        self._conn = conn
        self._bus = bus
        self._dbg = dbg_state
        self._port = port
        self._server: asyncio.Server | None = None
        self._client_writer: asyncio.StreamWriter | None = None
        self._running = False

    async def start(self) -> None:
        """Start the GDB RSP server."""
        self._running = True
        self._server = await asyncio.start_server(
            self._handle_client, "127.0.0.1", self._port,
        )
        logger.info("GDB RSP server listening on port %d", self._port)

    async def stop(self) -> None:
        """Stop the GDB RSP server."""
        self._running = False
        if self._client_writer:
            self._client_writer.close()
            self._client_writer = None
        if self._server:
            self._server.close()
            await self._server.wait_closed()
            self._server = None
        logger.info("GDB RSP server stopped")

    async def _handle_client(
        self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter,
    ) -> None:
        """Handle a GDB client connection."""
        addr = writer.get_extra_info("peername")
        logger.info("GDB client connected from %s", addr)
        self._client_writer = writer

        # Start event listener for async stop notifications
        event_task = asyncio.ensure_future(self._event_forwarder(writer))

        buf = ""
        try:
            while self._running:
                data = await reader.read(4096)
                if not data:
                    break
                buf += data.decode("ascii", errors="replace")

                while buf:
                    # Handle ACK/NAK
                    if buf[0] == "+":
                        buf = buf[1:]
                        continue
                    if buf[0] == "-":
                        buf = buf[1:]
                        logger.warning("GDB NAK received")
                        continue

                    # Handle interrupt (Ctrl-C)
                    if buf[0] == "\x03":
                        buf = buf[1:]
                        await self._handle_interrupt(writer)
                        continue

                    # Find packet: $...#xx
                    dollar = buf.find("$")
                    if dollar < 0:
                        buf = ""
                        break

                    hash_pos = buf.find("#", dollar + 1)
                    if hash_pos < 0 or hash_pos + 2 >= len(buf):
                        break  # Incomplete packet

                    packet_data = buf[dollar + 1:hash_pos]
                    # Skip checksum verification (GDB resends on error)
                    buf = buf[hash_pos + 3:]

                    # ACK the packet
                    writer.write(b"+")
                    await writer.drain()

                    # Process
                    response = await self._process_packet(packet_data)
                    if response is not None:
                        writer.write(_make_packet(response))
                        await writer.drain()

        except (asyncio.CancelledError, ConnectionError, OSError):
            pass
        finally:
            event_task.cancel()
            try:
                await event_task
            except asyncio.CancelledError:
                pass
            self._client_writer = None
            writer.close()
            try:
                await writer.wait_closed()
            except Exception:
                pass
            logger.info("GDB client disconnected")

    async def _event_forwarder(self, writer: asyncio.StreamWriter) -> None:
        """Forward debugger stop events to GDB as async notifications."""
        try:
            async with self._bus.subscribe("dbg_stop") as queue:
                while True:
                    evt, data = await queue.get()
                    if evt == "dbg_stop":
                        # Send stop reply
                        reason = data.get("reason", "breakpoint")
                        sig = 5  # SIGTRAP for breakpoint/step
                        reply = f"S{sig:02x}"
                        try:
                            writer.write(_make_packet(reply))
                            await writer.drain()
                        except (ConnectionError, OSError):
                            break
        except asyncio.CancelledError:
            pass

    async def _handle_interrupt(self, writer: asyncio.StreamWriter) -> None:
        """Handle Ctrl-C interrupt from GDB."""
        # For now, report current state
        # In a full implementation, this would break into the target
        if self._dbg.stopped:
            writer.write(_make_packet("S05"))
            await writer.drain()

    async def _process_packet(self, data: str) -> str | None:
        """Process a GDB RSP packet and return response data (without framing)."""
        if not data:
            return ""

        cmd = data[0]

        # Stop reason query
        if cmd == "?":
            if self._dbg.stopped:
                return "S05"  # SIGTRAP
            return "S00"  # Not stopped

        # Read all registers
        if cmd == "g":
            return await self._read_all_regs()

        # Write all registers
        if cmd == "G":
            return await self._write_all_regs(data[1:])

        # Read memory
        if cmd == "m":
            return await self._read_memory(data[1:])

        # Write memory
        if cmd == "M":
            return await self._write_memory(data[1:])

        # Single step
        if cmd == "s":
            return await self._step(data[1:])

        # Continue
        if cmd == "c":
            return await self._continue(data[1:])

        # Set breakpoint
        if cmd == "Z":
            return await self._set_breakpoint(data[1:])

        # Clear breakpoint
        if cmd == "z":
            return await self._clear_breakpoint(data[1:])

        # Kill / Detach
        if cmd == "k" or cmd == "D":
            self._conn.send_raw("DBGDETACH")
            if cmd == "D":
                return "OK"
            return None

        # Read single register
        if cmd == "p":
            return self._read_reg(data[1:])

        # Write single register
        if cmd == "P":
            return await self._write_reg(data[1:])

        # Query packets
        if data.startswith("qSupported"):
            return "PacketSize=1024"

        if data == "qAttached":
            return "1" if self._dbg.attached else "0"

        if data == "qC":
            return "QC1"  # Thread 1

        if data.startswith("qfThreadInfo"):
            return "m1"

        if data.startswith("qsThreadInfo"):
            return "l"  # End of list

        if data == "qTStatus":
            return ""  # No tracing

        if data.startswith("qRcmd,"):
            # Monitor command - decode and forward
            hex_cmd = data[6:]
            try:
                cmd_str = bytes.fromhex(hex_cmd).decode("ascii")
                return await self._monitor_command(cmd_str)
            except (ValueError, UnicodeDecodeError):
                return "E01"

        # Unsupported packet
        return ""

    async def _read_all_regs(self) -> str:
        """Read all registers. GDB m68k order: D0-D7, A0-A7, SR, PC."""
        if not self._dbg.stopped:
            # Request registers
            self._conn.send_raw("DBGREGS")
            msg = await self._bus.wait_for("dbg_regs", timeout=5.0)

        # Use cached state
        parts = []
        # D0-D7
        for i in range(8):
            parts.append(_hex32(self._dbg.regs[i]))
        # A0-A7
        for i in range(8):
            parts.append(_hex32(self._dbg.regs[8 + i]))
        # SR (zero-extended to 32 bits)
        parts.append(_hex32(self._dbg.regs[17]))
        # PC
        parts.append(_hex32(self._dbg.regs[16]))

        return "".join(parts)

    async def _write_all_regs(self, hex_data: str) -> str:
        """Write all registers."""
        if len(hex_data) < GDB_REG_COUNT * GDB_REG_SIZE * 2:
            return "E01"

        for i in range(GDB_REG_COUNT):
            offset = i * 8
            val = _parse_hex(hex_data[offset:offset + 8])

            # Map GDB order to our order
            if i < 16:
                reg_idx = i
            elif i == 16:
                reg_idx = 17  # SR
            else:
                reg_idx = 16  # PC

            # Determine register name
            if reg_idx < 8:
                name = f"D{reg_idx}"
            elif reg_idx < 16:
                name = f"A{reg_idx - 8}"
            elif reg_idx == 16:
                name = "PC"
            else:
                name = "SR"

            self._conn.send_raw(f"DBGSETREG|{name}|{val:08x}")
            self._dbg.regs[reg_idx] = val

        return "OK"

    def _read_reg(self, data: str) -> str:
        """Read a single register by number."""
        try:
            regnum = _parse_hex(data)
        except ValueError:
            return "E01"

        if regnum < 16:
            return _hex32(self._dbg.regs[regnum])
        elif regnum == 16:
            return _hex32(self._dbg.regs[17])  # SR
        elif regnum == 17:
            return _hex32(self._dbg.regs[16])  # PC
        return "E01"

    async def _write_reg(self, data: str) -> str:
        """Write a single register: P<regnum>=<value>."""
        parts = data.split("=")
        if len(parts) != 2:
            return "E01"

        try:
            regnum = _parse_hex(parts[0])
            value = _parse_hex(parts[1])
        except ValueError:
            return "E01"

        if regnum < 8:
            name = f"D{regnum}"
            idx = regnum
        elif regnum < 16:
            name = f"A{regnum - 8}"
            idx = regnum
        elif regnum == 16:
            name = "SR"
            idx = 17
        elif regnum == 17:
            name = "PC"
            idx = 16
        else:
            return "E01"

        self._conn.send_raw(f"DBGSETREG|{name}|{value:08x}")
        self._dbg.regs[idx] = value
        return "OK"

    async def _read_memory(self, data: str) -> str:
        """Read memory: m<addr>,<length>."""
        parts = data.split(",")
        if len(parts) != 2:
            return "E01"

        try:
            addr = _parse_hex(parts[0])
            length = _parse_hex(parts[1])
        except ValueError:
            return "E01"

        length = min(length, 4096)

        # Send INSPECT command
        self._conn.send({"type": "INSPECT", "address": f"{addr:08x}", "size": length})

        # Collect MEM responses
        chunks: list[str] = []
        received = 0

        async with self._bus.subscribe("mem", "err") as queue:
            deadline = asyncio.get_event_loop().time() + 5.0
            while received < length:
                remaining = deadline - asyncio.get_event_loop().time()
                if remaining <= 0:
                    break
                try:
                    evt, msg = await asyncio.wait_for(queue.get(), timeout=remaining)
                    if evt == "err":
                        return "E01"
                    if evt == "mem":
                        hex_data = msg.get("hexData", "")
                        chunks.append(hex_data)
                        received += msg.get("size", 0)
                except asyncio.TimeoutError:
                    break

        if chunks:
            result = "".join(chunks).lower()
            # Trim to requested length
            return result[:length * 2]
        return "E01"

    async def _write_memory(self, data: str) -> str:
        """Write memory: M<addr>,<length>:<hex>."""
        colon = data.find(":")
        if colon < 0:
            return "E01"

        header = data[:colon]
        hex_data = data[colon + 1:]

        parts = header.split(",")
        if len(parts) != 2:
            return "E01"

        try:
            addr = _parse_hex(parts[0])
        except ValueError:
            return "E01"

        self._conn.send({"type": "WRITEMEM", "address": f"{addr:08x}", "hexData": hex_data})
        msg = await self._bus.wait_for("ok", timeout=5.0,
                                        predicate=lambda m: "WRITEMEM" in m.get("context", ""))
        return "OK" if msg else "E01"

    async def _step(self, data: str) -> str:
        """Single step. Optionally start at address."""
        if data:
            try:
                addr = _parse_hex(data)
                self._conn.send_raw(f"DBGSETREG|PC|{addr:08x}")
            except ValueError:
                pass

        self._conn.send_raw("DBGSTEP")

        # Wait for stop
        msg = await self._bus.wait_for("dbg_stop", timeout=30.0)
        if msg:
            return "S05"  # SIGTRAP
        return "S05"

    async def _continue(self, data: str) -> str:
        """Continue execution. Optionally start at address."""
        if data:
            try:
                addr = _parse_hex(data)
                self._conn.send_raw(f"DBGSETREG|PC|{addr:08x}")
            except ValueError:
                pass

        self._conn.send_raw("DBGCONT")
        # Don't wait for stop - the event forwarder handles it
        return None  # type: ignore[return-value]

    async def _set_breakpoint(self, data: str) -> str:
        """Set breakpoint: Z<type>,<addr>,<kind>."""
        parts = data.split(",")
        if len(parts) < 2:
            return "E01"

        bp_type = parts[0]
        if bp_type != "0":
            return ""  # Only software breakpoints supported

        try:
            addr = _parse_hex(parts[1])
        except ValueError:
            return "E01"

        self._conn.send_raw(f"BPSET|{addr:08x}")
        msg = await self._bus.wait_for("dbg_bpinfo", timeout=5.0)
        return "OK" if msg else "E01"

    async def _clear_breakpoint(self, data: str) -> str:
        """Clear breakpoint: z<type>,<addr>,<kind>."""
        parts = data.split(",")
        if len(parts) < 2:
            return "E01"

        bp_type = parts[0]
        if bp_type != "0":
            return ""

        try:
            addr = _parse_hex(parts[1])
        except ValueError:
            return "E01"

        self._conn.send_raw(f"BPCLEAR|{addr:08x}")
        msg = await self._bus.wait_for("ok", timeout=5.0,
                                        predicate=lambda m: "BPCLEAR" in m.get("context", ""))
        return "OK" if msg else "E01"

    async def _monitor_command(self, cmd: str) -> str:
        """Handle GDB monitor commands."""
        cmd = cmd.strip()

        if cmd == "status":
            self._conn.send_raw("DBGSTATUS")
            msg = await self._bus.wait_for("dbg_state", timeout=5.0)
            if msg:
                text = f"attached={msg.get('attached')}, stopped={msg.get('stopped')}, target={msg.get('targetName', '?')}"
                return text.encode("ascii").hex()
            return "4e6f20726573706f6e7365"  # "No response"

        if cmd == "bt":
            self._conn.send_raw("DBGBT")
            msg = await self._bus.wait_for("dbg_bt", timeout=5.0)
            if msg:
                frames = msg.get("frames", [])
                text = "\n".join(f"#{i} 0x{f.get('pc', 0):08x}" for i, f in enumerate(frames))
                return text.encode("ascii").hex()
            return ""

        return ""  # Unknown monitor command
