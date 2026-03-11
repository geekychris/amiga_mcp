"""MCP tool definitions using FastMCP."""

from __future__ import annotations

import asyncio
import logging
import time
from typing import Any

from mcp.server.fastmcp import FastMCP

from .protocol import format_hex_dump, level_name
from .disasm import disassemble_hex, format_listing
from .state import AmigaState, EventBus
from .serial_conn import SerialConnection
from .builder import Builder
from .deployer import Deployer
from .scaffolder import create_project
from .screenshot import save_screenshot, parse_palette
from .copper import decode_copper_list, format_copper_list

logger = logging.getLogger(__name__)

# Module-level holders, set during server init
_conn: SerialConnection | None = None
_state: AmigaState | None = None
_builder: Builder | None = None
_deployer: Deployer | None = None
_event_bus: EventBus | None = None

mcp = FastMCP("amiga-dev")


def init_tools(
    conn: SerialConnection,
    state: AmigaState,
    builder: Builder,
    deployer: Deployer,
    event_bus: EventBus,
) -> None:
    """Initialize module-level references for MCP tools."""
    global _conn, _state, _builder, _deployer, _event_bus
    _conn = conn
    _state = state
    _builder = builder
    _deployer = deployer
    _event_bus = event_bus


def _require_connected() -> tuple[SerialConnection, AmigaState, EventBus]:
    assert _conn is not None and _state is not None and _event_bus is not None
    if not _conn.connected:
        raise RuntimeError("Not connected to Amiga")
    return _conn, _state, _event_bus


# ─── Build Tools ───

@mcp.tool()
async def amiga_build(project: str | None = None) -> str:
    """Build an Amiga project using Docker cross-compiler. Omit project to build all."""
    assert _builder is not None
    result = await _builder.build(project)
    parts = [f"Build {'SUCCEEDED' if result.success else 'FAILED'} ({result.duration}ms)"]
    if result.output:
        parts.append(f"\n--- Output ---\n{result.output}")
    if result.errors:
        parts.append(f"\n--- Errors ---\n{result.errors}")
    return "".join(parts)


@mcp.tool()
async def amiga_clean(project: str | None = None) -> str:
    """Clean build artifacts for a project."""
    assert _builder is not None
    result = await _builder.clean(project)
    return f"Clean {'done' if result.success else 'failed'}: {result.errors or 'OK'}"


# ─── Connection Tools ───

@mcp.tool()
async def amiga_connect(
    mode: str | None = None,
    host: str | None = None,
    port: int | None = None,
    pty_path: str | None = None,
) -> str:
    """Connect to the Amiga emulator. Uses TCP mode by default. Set mode='pty' for FS-UAE PTY serial."""
    assert _conn is not None
    try:
        if _conn.connected:
            _conn.disconnect()

        conn_mode = mode or _conn.mode

        if conn_mode == "tcp":
            h = host or "127.0.0.1"
            p = port or 1234
            _conn.set_target(h, p)
            await _conn.connect()
            return f"Connected via TCP to {h}:{p}"
        else:
            pp = pty_path or "/tmp/amiga-serial"
            _conn.set_mode("pty", pty_path=pp)
            await _conn.connect()
            return f"Connected via PTY at {pp}. Configure FS-UAE serial_port={pp}"
    except Exception as e:
        return f"Connection failed: {e}"


@mcp.tool()
async def amiga_disconnect() -> str:
    """Disconnect from Amiga serial port."""
    assert _conn is not None
    _conn.disconnect()
    return "Disconnected"


# ─── Ping / Status ───

@mcp.tool()
async def amiga_ping() -> str:
    """Ping the Amiga to get status/heartbeat."""
    conn, state, bus = _require_connected()
    conn.send({"type": "PING"})
    msg = await bus.wait_for("pong", timeout=5.0)
    if msg:
        return (
            f"Amiga alive - clients: {msg.get('clientCount', '?')}, "
            f"chip: {msg.get('freeChip', '?')} bytes, fast: {msg.get('freeFast', '?')} bytes"
        )
    return "No response from Amiga (timeout)"


# ─── Log Tools ───

@mcp.tool()
async def amiga_log(count: int = 50, level: str | None = None) -> str:
    """Get recent log messages from buffer."""
    assert _state is not None
    logs = _state.get_recent_logs(count, level)
    if not logs:
        return "No log messages"
    return "\n".join(
        f"[{level_name(l['level'])}] tick={l.get('tick', '')} {l['message']}"
        for l in logs
    )


@mcp.tool()
async def amiga_watch_logs(duration_ms: int = 30000, level: str | None = None) -> str:
    """Stream Amiga logs in real-time. Returns after duration_ms (default 30s)."""
    conn, state, bus = _require_connected()
    level_filter = level.upper()[0] if level else None
    count = 0

    async with bus.subscribe("log") as queue:
        deadline = asyncio.get_event_loop().time() + duration_ms / 1000
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                if level_filter and data.get("level") != level_filter:
                    continue
                count += 1
            except asyncio.TimeoutError:
                break

    return f"Log watch ended. {count} messages received in {duration_ms / 1000}s."


@mcp.tool()
async def amiga_watch_status(duration_ms: int = 30000) -> str:
    """Stream heartbeats and variable changes in real-time."""
    conn, state, bus = _require_connected()
    hb_count = 0
    var_count = 0

    async with bus.subscribe("heartbeat", "var") as queue:
        deadline = asyncio.get_event_loop().time() + duration_ms / 1000
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                if evt == "heartbeat":
                    hb_count += 1
                elif evt == "var":
                    var_count += 1
            except asyncio.TimeoutError:
                break

    return (
        f"Status watch ended. {hb_count} heartbeats, "
        f"{var_count} variable updates in {duration_ms / 1000}s."
    )


# ─── Memory Inspection ───

@mcp.tool()
async def amiga_inspect_memory(address: str, size: int) -> str:
    """Request a memory dump from the Amiga."""
    conn, state, bus = _require_connected()
    expected = min(size, 4096)
    chunks: list[dict] = []

    async with bus.subscribe("mem", "err") as queue:
        conn.send({"type": "INSPECT", "address": address, "size": expected})
        deadline = asyncio.get_event_loop().time() + 15.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                if evt == "err" and "INSPECT" in data.get("context", ""):
                    return data.get("message") or "Address not accessible"
                if evt == "mem":
                    chunks.append(data)
                    received = sum(c["size"] for c in chunks)
                    if received >= expected:
                        break
            except asyncio.TimeoutError:
                break

    if chunks:
        all_hex = "".join(c["hexData"] for c in chunks)
        result = format_hex_dump(address, all_hex)
        received = sum(c["size"] for c in chunks)
        if received < expected:
            result += "\n(partial - timed out)"
        return result
    return "Timed out waiting for memory dump"


# ─── Variable Tools ───

@mcp.tool()
async def amiga_get_var(name: str) -> str:
    """Get current value of a registered variable on the Amiga."""
    conn, state, bus = _require_connected()
    conn.send({"type": "GETVAR", "name": name})

    msg = await bus.wait_for(
        "var", timeout=5.0,
        predicate=lambda d: d.get("name") == name,
    )
    if msg:
        return f"{name} ({msg.get('varType', '?')}) = {msg.get('value', '?')}"

    # Check cache
    cached = state.vars.get(name)
    if cached:
        return f"{name} ({cached.get('varType', '?')}) = {cached.get('value', '?')} (cached)"
    return f"Variable '{name}' not found or timed out"


@mcp.tool()
async def amiga_set_var(name: str, value: str) -> str:
    """Set the value of a registered variable on the Amiga."""
    conn, state, bus = _require_connected()
    conn.send({"type": "SETVAR", "name": name, "value": value})
    return f"Set {name} = {value}"


# ─── Exec ───

@mcp.tool()
async def amiga_exec(command: str) -> str:
    """Send a custom command to the running Amiga app."""
    conn, state, bus = _require_connected()
    cmd_id = int(time.time() * 1000) % 100000

    async with bus.subscribe("cmd") as queue:
        conn.send({"type": "EXEC", "id": cmd_id, "expression": command})
        deadline = asyncio.get_event_loop().time() + 5.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                if data.get("id") == cmd_id:
                    return f"[{data['status']}] {data['data']}"
            except asyncio.TimeoutError:
                break

    return "Command sent (no response received)"


# ─── System Info Tools ───

@mcp.tool()
async def amiga_list_clients() -> str:
    """List connected Amiga debug clients."""
    conn, state, bus = _require_connected()
    conn.send({"type": "LISTCLIENTS"})
    msg = await bus.wait_for("clients", timeout=5.0)
    if msg:
        names = msg.get("names", [])
        return f"Clients ({len(names)}): {', '.join(names) if names else 'none'}"
    return "No response (bridge may not support LISTCLIENTS)"


@mcp.tool()
async def amiga_list_tasks() -> str:
    """List running tasks on the Amiga."""
    conn, state, bus = _require_connected()
    conn.send({"type": "LISTTASKS"})
    msg = await bus.wait_for("tasks", timeout=5.0)
    if msg:
        tasks = msg.get("tasks", [])
        if not tasks:
            return "No tasks found"
        lines = [f"Tasks ({len(tasks)}):"]
        for t in tasks:
            lines.append(
                f"  {t.get('name', '?'):30s} pri={t.get('priority', '?'):3} "
                f"state={t.get('state', '?'):10s} stack={t.get('stackSize', '?')}"
            )
        return "\n".join(lines)
    return "No response (bridge may not support LISTTASKS)"


@mcp.tool()
async def amiga_list_libs() -> str:
    """List loaded libraries on the Amiga."""
    conn, state, bus = _require_connected()
    conn.send({"type": "LISTLIBS"})
    msg = await bus.wait_for("libs", timeout=5.0)
    if msg:
        libs = msg.get("libs", [])
        if not libs:
            return "No libraries found"
        lines = [f"Libraries ({len(libs)}):"]
        for lib in libs:
            lines.append(f"  {lib.get('name', '?'):30s} v{lib.get('version', '?')}.{lib.get('revision', '?')}")
        return "\n".join(lines)
    return "No response (bridge may not support LISTLIBS)"


@mcp.tool()
async def amiga_lib_info(name: str) -> str:
    """Get detailed information about a specific Amiga library (version, openCnt, base address, etc)."""
    conn, state, bus = _require_connected()
    conn.send({"type": "LIBINFO", "name": name})
    async with bus.subscribe("libinfo", "err") as q:
        try:
            evt, data = await asyncio.wait_for(q.get(), timeout=5.0)
            if evt == "err" and data.get("context") == "LIBINFO":
                return f"Error: {data.get('message', 'Unknown')}"
            if evt == "libinfo":
                lines = [
                    f"Library: {data.get('name', '?')}",
                    f"  Version:   {data.get('version', '?')}.{data.get('revision', '?')}",
                    f"  Open count: {data.get('openCnt', '?')}",
                    f"  Flags:     0x{data.get('flags', 0):02x}",
                    f"  Neg size:  {data.get('negSize', '?')} bytes (jump table)",
                    f"  Pos size:  {data.get('posSize', '?')} bytes (data)",
                    f"  Base addr: 0x{data.get('baseAddr', '?')}",
                    f"  ID string: {data.get('idString', 'n/a')}",
                ]
                return "\n".join(lines)
        except asyncio.TimeoutError:
            pass
    return "No response from bridge"


@mcp.tool()
async def amiga_dev_info(name: str) -> str:
    """Get detailed information about a specific Amiga device (version, openCnt, base address, etc)."""
    conn, state, bus = _require_connected()
    conn.send({"type": "DEVINFO", "name": name})
    async with bus.subscribe("devinfo", "err") as q:
        try:
            evt, data = await asyncio.wait_for(q.get(), timeout=5.0)
            if evt == "err" and data.get("context") == "DEVINFO":
                return f"Error: {data.get('message', 'Unknown')}"
            if evt == "devinfo":
                lines = [
                    f"Device: {data.get('name', '?')}",
                    f"  Version:   {data.get('version', '?')}.{data.get('revision', '?')}",
                    f"  Open count: {data.get('openCnt', '?')}",
                    f"  Flags:     0x{data.get('flags', 0):02x}",
                    f"  Neg size:  {data.get('negSize', '?')} bytes (jump table)",
                    f"  Pos size:  {data.get('posSize', '?')} bytes (data)",
                    f"  Base addr: 0x{data.get('baseAddr', '?')}",
                    f"  ID string: {data.get('idString', 'n/a')}",
                ]
                return "\n".join(lines)
        except asyncio.TimeoutError:
            pass
    return "No response from bridge"


# ─── File System Tools ───

@mcp.tool()
async def amiga_list_dir(path: str = "SYS:") -> str:
    """List directory contents on the Amiga."""
    conn, state, bus = _require_connected()
    conn.send({"type": "LISTDIR", "path": path})
    msg = await bus.wait_for(
        "dir", timeout=5.0,
        predicate=lambda d: d.get("path") == path,
    )
    if msg:
        entries = msg.get("entries", [])
        if not entries:
            return f"Empty directory: {path}"
        lines = [f"Directory: {path} ({len(entries)} entries)"]
        for e in entries:
            kind = "DIR " if e.get("type") == "dir" else "FILE"
            size = f"{e.get('size', 0):>8}" if e.get("type") != "dir" else "       -"
            lines.append(f"  {kind} {e.get('name', '?'):30s} {size}  {e.get('date', '')}")
        return "\n".join(lines)
    return "No response (bridge may not support LISTDIR)"


@mcp.tool()
async def amiga_read_file(path: str, offset: int = 0, size: int = 4096) -> str:
    """Read a file from the Amiga filesystem."""
    conn, state, bus = _require_connected()
    conn.send({"type": "READFILE", "path": path, "offset": offset, "size": size})
    msg = await bus.wait_for(
        "file", timeout=5.0,
        predicate=lambda d: d.get("path") == path,
    )
    if msg:
        hex_data = msg.get("hexData", "")
        if hex_data:
            return format_hex_dump(f"{offset:08x}", hex_data)
        return f"File is empty or could not be read: {path}"
    return "No response (bridge may not support READFILE)"


@mcp.tool()
async def amiga_write_file(path: str, offset: int, hex_data: str) -> str:
    """Write data to a file on the Amiga filesystem (hex-encoded)."""
    conn, state, bus = _require_connected()
    conn.send({"type": "WRITEFILE", "path": path, "offset": offset, "hexData": hex_data})
    return f"Write command sent: {path} at offset {offset}, {len(hex_data) // 2} bytes"


# ─── Process Tools ───

@mcp.tool()
async def amiga_launch(command: str) -> str:
    """Launch a program on the Amiga."""
    conn, state, bus = _require_connected()
    cmd_id = int(time.time() * 1000) % 100000
    conn.send({"type": "LAUNCH", "id": cmd_id, "command": command})

    msg = await bus.wait_for(
        "cmd", timeout=5.0,
        predicate=lambda d: d.get("id") == cmd_id,
    )
    if msg:
        return f"[{msg['status']}] {msg.get('data', '')}"
    return f"Launch command sent: {command} (no response)"


@mcp.tool()
async def amiga_dos_command(command: str) -> str:
    """Execute an AmigaDOS command."""
    conn, state, bus = _require_connected()
    cmd_id = int(time.time() * 1000) % 100000
    conn.send({"type": "DOSCOMMAND", "id": cmd_id, "command": command})

    msg = await bus.wait_for(
        "cmd", timeout=5.0,
        predicate=lambda d: d.get("id") == cmd_id,
    )
    if msg:
        return f"[{msg['status']}] {msg['data']}"
    return f"DOS command sent: {command} (no response)"


# ─── Hook Tools ───

@mcp.tool()
async def amiga_list_hooks(client: str = "") -> str:
    """List hooks registered by Amiga debug clients."""
    conn, state, bus = _require_connected()
    conn.send({"type": "LISTHOOKS", "client": client})
    msg = await bus.wait_for("hooks", timeout=5.0)
    if msg:
        hooks = msg.get("hooks", [])
        if not hooks:
            return f"No hooks registered{f' for {client}' if client else ''}"
        lines = [f"Hooks for {msg.get('client', 'all')}:"]
        for h in hooks:
            lines.append(f"  {h['name']}: {h.get('description', '')}")
        return "\n".join(lines)
    return "No response"


@mcp.tool()
async def amiga_call_hook(client: str, hook: str, args: str = "") -> str:
    """Call a named hook on an Amiga client. Returns the hook's result."""
    conn, state, bus = _require_connected()
    cmd_id = int(time.time() * 1000) % 100000
    async with bus.subscribe("cmd") as queue:
        conn.send({"type": "CALLHOOK", "id": cmd_id, "client": client,
                    "hook": hook, "args": args})
        deadline = asyncio.get_event_loop().time() + 5.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                if data.get("id") == cmd_id:
                    result = data.get("data", "")
                    # Unescape newlines and pipes from serial protocol
                    result = result.replace("\\n", "\n").replace("\\|", "|")
                    return f"[{data['status']}] {result}"
            except asyncio.TimeoutError:
                break
    return "Hook call timed out"


# ─── Memory Region Tools ───

@mcp.tool()
async def amiga_list_memregions(client: str = "") -> str:
    """List memory regions registered by Amiga debug clients."""
    conn, state, bus = _require_connected()
    conn.send({"type": "LISTMEMREGS", "client": client})
    msg = await bus.wait_for("memregs", timeout=5.0)
    if msg:
        memregs = msg.get("memregs", [])
        if not memregs:
            return f"No memory regions registered{f' for {client}' if client else ''}"
        lines = [f"Memory regions for {msg.get('client', 'all')}:"]
        for m in memregs:
            lines.append(
                f"  {m['name']}: 0x{m['address']} ({m['size']} bytes) - {m.get('description', '')}"
            )
        return "\n".join(lines)
    return "No response"


@mcp.tool()
async def amiga_read_memregion(client: str, region: str) -> str:
    """Read data from a named memory region registered by a client."""
    conn, state, bus = _require_connected()
    chunks: list[dict] = []

    async with bus.subscribe("mem", "err") as queue:
        conn.send({"type": "READMEMREG", "client": client, "region": region})
        deadline = asyncio.get_event_loop().time() + 5.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                if evt == "err":
                    return f"Error: {data.get('message', 'Unknown error')}"
                chunks.append(data)
                break
            except asyncio.TimeoutError:
                break

    if chunks:
        all_hex = "".join(c["hexData"] for c in chunks)
        return format_hex_dump(chunks[0]["address"], all_hex)
    return "Timed out waiting for memory region data"


# ─── Client Info Tools ───

@mcp.tool()
async def amiga_client_info(client: str) -> str:
    """Get detailed info about a connected Amiga client (vars, hooks, memory regions)."""
    conn, state, bus = _require_connected()
    conn.send({"type": "CLIENTINFO", "client": client})
    msg = await bus.wait_for("cinfo", timeout=5.0)
    if msg:
        lines = [f"Client: {msg.get('client', '?')} (id={msg.get('id', '?')}, msgs={msg.get('msgCount', 0)})"]
        if msg.get("vars"):
            lines.append(f"  Variables: {', '.join(msg['vars'])}")
        if msg.get("hooks"):
            lines.append(f"  Hooks: {', '.join(msg['hooks'])}")
        if msg.get("memregs"):
            lines.append(f"  Memory regions: {', '.join(msg['memregs'])}")
        return "\n".join(lines)
    return f"Client '{client}' not found or no response"


@mcp.tool()
async def amiga_stop_client(name: str) -> str:
    """Stop a running Amiga client process (sends CTRL-C, then SHUTDOWN if needed)."""
    conn, state, bus = _require_connected()
    async with bus.subscribe("ok", "err") as queue:
        conn.send({"type": "STOP", "name": name})
        deadline = asyncio.get_event_loop().time() + 3.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                ctx = data.get("context", "")
                if "STOP" in ctx or "Client" in ctx:
                    status = "ok" if evt == "ok" else "error"
                    return f"[{status}] {data.get('message', name)}"
            except asyncio.TimeoutError:
                break
    return f"Stop command sent to {name} (no confirmation)"


# ─── Script Execution ───

@mcp.tool()
async def amiga_run_script(script: str) -> str:
    """Write and execute an AmigaDOS script on the Amiga. Use newlines to separate commands.
    The script is written to T:, executed via 'Execute', and output is captured."""
    conn, state, bus = _require_connected()
    cmd_id = int(time.time() * 1000) % 100000
    # Convert newlines to semicolons for protocol transport
    script_line = script.replace("\n", ";")

    async with bus.subscribe("cmd") as queue:
        conn.send({"type": "SCRIPT", "id": cmd_id, "script": script_line})
        deadline = asyncio.get_event_loop().time() + 30.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                if data.get("id") == cmd_id:
                    output = data.get("data", "")
                    # Convert semicolons back to newlines for display
                    output = output.replace(";", "\n")
                    return f"[{data['status']}]\n{output}" if output else f"[{data['status']}]"
            except asyncio.TimeoutError:
                break
    return "Script execution timed out"


# ─── Memory Write Tool ───

@mcp.tool()
async def amiga_write_memory(address: str, hex_data: str) -> str:
    """Write hex data to a memory address on the Amiga. Use with caution - no memory protection!"""
    conn, state, bus = _require_connected()
    async with bus.subscribe("ok", "err") as queue:
        conn.send({"type": "WRITEMEM", "address": address, "hexData": hex_data})
        deadline = asyncio.get_event_loop().time() + 3.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                if "WRITEMEM" in data.get("context", ""):
                    if evt == "ok":
                        return f"Wrote {len(hex_data) // 2} bytes to 0x{address}"
                    return f"Error: {data.get('message', 'Write failed')}"
            except asyncio.TimeoutError:
                break
    return "Write command sent (no confirmation)"


# ─── Deploy Tools ───

@mcp.tool()
async def amiga_deploy(project: str | None = None) -> str:
    """Deploy built binaries to AmiKit shared folder."""
    assert _deployer is not None
    result = _deployer.deploy(project)
    parts = [result.message]
    if result.files:
        parts.append("Files: " + ", ".join(result.files))
    return "\n".join(parts)


@mcp.tool()
async def amiga_build_deploy_run(project: str, command: str | None = None) -> str:
    """Build a project, deploy it, and optionally launch it on the Amiga."""
    assert _builder is not None and _deployer is not None

    # Build
    build_result = await _builder.build(project)
    if not build_result.success:
        return f"Build FAILED ({build_result.duration}ms)\n{build_result.errors}"

    # Deploy
    deploy_result = _deployer.deploy(project)
    if not deploy_result.success:
        return f"Build OK but deploy failed: {deploy_result.message}"

    result = (
        f"Build SUCCEEDED ({build_result.duration}ms)\n"
        f"Deploy: {deploy_result.message}"
    )

    # Optionally launch
    if command and _conn and _conn.connected:
        assert _event_bus is not None
        cmd_id = int(time.time() * 1000) % 100000
        _conn.send({"type": "LAUNCH", "id": cmd_id, "command": command})
        msg = await _event_bus.wait_for(
            "proc", timeout=5.0,
            predicate=lambda d: d.get("id") == cmd_id,
        )
        if msg:
            result += f"\nLaunch: [{msg['status']}] {msg.get('output', '')}"
        else:
            result += f"\nLaunch command sent: {command}"

    return result


@mcp.tool()
async def amiga_run(project: str, command: str | None = None) -> str:
    """Build, deploy, stop previous instance, and launch an Amiga program. One-shot development cycle."""
    assert _builder is not None and _deployer is not None

    steps: list[str] = []
    binary_name = project.rstrip("/").split("/")[-1]
    launch_command = command or f"Dropbox:Dev/{binary_name}"

    # 1. Build
    build_result = await _builder.build(project)
    steps.append(f"Build: {'OK' if build_result.success else 'FAILED'} ({build_result.duration}ms)")
    if not build_result.success:
        if build_result.errors:
            steps.append(f"Errors:\n{build_result.errors}")
        return "\n".join(steps)

    # 2. Deploy
    deploy_result = _deployer.deploy(project)
    steps.append(f"Deploy: {'OK' if deploy_result.success else 'FAILED'} - {deploy_result.message}")
    if not deploy_result.success:
        return "\n".join(steps)

    # 3. Stop existing client (if connected)
    if _conn and _conn.connected:
        assert _event_bus is not None
        try:
            async with _event_bus.subscribe("ok", "err") as queue:
                _conn.send({"type": "STOP", "name": binary_name})
                deadline = asyncio.get_event_loop().time() + 2.0
                while True:
                    remaining = deadline - asyncio.get_event_loop().time()
                    if remaining <= 0:
                        break
                    try:
                        evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                        ctx = data.get("context", "")
                        if "STOP" in ctx or "Client" in ctx:
                            msg_text = data.get("message", binary_name)
                            steps.append(f"Stop: [{evt}] {msg_text}")
                            break
                    except asyncio.TimeoutError:
                        break
                else:
                    steps.append(f"Stop: sent (no confirmation)")
        except Exception:
            steps.append(f"Stop: skipped (send failed)")

        # Brief pause to let the old process clean up
        await asyncio.sleep(0.3)

        # 4. Launch
        cmd_id = int(time.time() * 1000) % 100000
        try:
            _conn.send({"type": "LAUNCH", "id": cmd_id, "command": launch_command})
            msg = await _event_bus.wait_for(
                "cmd", timeout=5.0,
                predicate=lambda d: d.get("id") == cmd_id,
            )
            if msg:
                steps.append(f"Launch: [{msg['status']}] {msg.get('data', '')}")
            else:
                steps.append(f"Launch: sent {launch_command} (no response)")
        except Exception as e:
            steps.append(f"Launch: failed ({e})")

        # 5. Wait for client to appear
        client_connected = False
        for attempt in range(6):
            await asyncio.sleep(0.5)
            try:
                _conn.send({"type": "LISTCLIENTS"})
                clients_msg = await _event_bus.wait_for("clients", timeout=1.0)
                if clients_msg:
                    names = clients_msg.get("names", [])
                    if binary_name in names:
                        client_connected = True
                        break
            except Exception:
                pass
        steps.append(f"Client: {'connected' if client_connected else 'not detected (timeout)'}")
    else:
        steps.append("Stop: skipped (not connected to Amiga)")
        steps.append("Launch: skipped (not connected to Amiga)")
        steps.append("Client: skipped (not connected to Amiga)")

    return "\n".join(steps)


# ─── Project Scaffolding ───

@mcp.tool()
async def amiga_create_project(name: str, template: str = "window") -> str:
    """Create a new Amiga project with boilerplate code. Templates: window, screen, headless."""
    assert _builder is not None
    project_root = _builder._root
    return create_project(project_root, name, template)


# ─── Disassembly Tool ───

@mcp.tool()
async def amiga_disassemble(address: str, count: int = 20) -> str:
    """Disassemble 68k machine code at a memory address on the Amiga.
    Returns an assembly listing with addresses, hex bytes, and mnemonics.
    Annotates JSR/JMP through A6 with exec.library, dos.library, intuition.library,
    and graphics.library LVO names when recognized."""
    conn, state, bus = _require_connected()

    # Read enough bytes: max 68k instruction is 10 bytes, plus we want some margin
    read_size = min(count * 10, 4096)
    addr_int = int(address, 16) if isinstance(address, str) else address
    addr_hex = f"{addr_int:08X}"

    chunks: list[dict] = []
    async with bus.subscribe("mem", "err") as queue:
        conn.send({"type": "INSPECT", "address": addr_hex, "size": read_size})
        deadline = asyncio.get_event_loop().time() + 15.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                if evt == "err" and "INSPECT" in data.get("context", ""):
                    return f"Cannot read memory at ${addr_hex}: {data.get('message', 'not accessible')}"
                if evt == "mem":
                    chunks.append(data)
                    received = sum(c["size"] for c in chunks)
                    if received >= read_size:
                        break
            except asyncio.TimeoutError:
                break

    if not chunks:
        return f"Timed out reading memory at ${addr_hex}"

    hex_data = "".join(c["hexData"] for c in chunks)
    instructions = disassemble_hex(hex_data, addr_int, count)
    if not instructions:
        return f"No instructions decoded at ${addr_hex}"

    # Use symbol annotations if any tables are loaded
    from . import symbols
    project = None
    for name, table in symbols.get_all_tables().items():
        if table.lookup_address(addr_int):
            project = name
            break
    return format_listing(instructions, project=project)


# ─── Screenshot Tool ───

@mcp.tool()
async def amiga_screenshot(window: str = "") -> str:
    """Capture a screenshot of the Amiga screen or a specific window. Returns the image file path."""
    conn, state, bus = _require_connected()

    scrinfo_msg = None
    scrdata_msgs: list[dict] = []
    expected_total = 0

    async with bus.subscribe("scrinfo", "scrdata", "err") as queue:
        if window:
            conn.send({"type": "SCREENSHOT", "window": window})
        else:
            conn.send({"type": "SCREENSHOT"})

        deadline = asyncio.get_event_loop().time() + 30.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                if evt == "err" and "SCREENSHOT" in data.get("context", ""):
                    return f"Screenshot failed: {data.get('message', 'unknown error')}"
                if evt == "scrinfo":
                    scrinfo_msg = data
                    expected_total = data["height"] * data["depth"]
                elif evt == "scrdata":
                    scrdata_msgs.append(data)
                    if scrinfo_msg and len(scrdata_msgs) >= expected_total:
                        break
            except asyncio.TimeoutError:
                break

    if not scrinfo_msg:
        return "Timed out waiting for screenshot header"

    if not scrdata_msgs:
        return f"Got header ({scrinfo_msg['width']}x{scrinfo_msg['height']}x{scrinfo_msg['depth']}) but no pixel data"

    path = save_screenshot(scrinfo_msg, scrdata_msgs)
    return f"Screenshot saved: {path} ({scrinfo_msg['width']}x{scrinfo_msg['height']}, {scrinfo_msg['depth']} planes, {len(scrdata_msgs)} rows received)"


# ─── Palette Tools ───

@mcp.tool()
async def amiga_get_palette(screen: str = "") -> str:
    """Read the color palette of the current Amiga screen."""
    conn, state, bus = _require_connected()

    async with bus.subscribe("palette", "err") as queue:
        conn.send({"type": "PALETTE"})
        deadline = asyncio.get_event_loop().time() + 5.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                if evt == "err" and "PALETTE" in data.get("context", ""):
                    return f"Palette read failed: {data.get('message', 'unknown error')}"
                if evt == "palette":
                    depth = data["depth"]
                    colors = parse_palette(data["palette"])
                    num_colors = len(colors)
                    lines = [f"Palette ({num_colors} colors, depth={depth}):"]
                    for i, (r, g, b) in enumerate(colors):
                        # Show both 4-bit and 8-bit values
                        r4 = r // 17
                        g4 = g // 17
                        b4 = b // 17
                        lines.append(
                            f"  {i:2d}: ${r4:X}{g4:X}{b4:X}  "
                            f"R={r:3d} G={g:3d} B={b:3d}  "
                            f"#{r:02X}{g:02X}{b:02X}"
                        )
                    return "\n".join(lines)
            except asyncio.TimeoutError:
                break
    return "Timed out waiting for palette data"


@mcp.tool()
async def amiga_set_palette(index: int, r: int, g: int, b: int) -> str:
    """Set a color in the Amiga screen palette. r/g/b are 0-15 (OCS 4-bit RGB)."""
    conn, state, bus = _require_connected()

    if not (0 <= r <= 15 and 0 <= g <= 15 and 0 <= b <= 15):
        return "Error: r, g, b must be 0-15"
    if index < 0 or index > 255:
        return "Error: index must be 0-255"

    rgb_hex = f"{r:X}{g:X}{b:X}"

    async with bus.subscribe("ok", "err") as queue:
        conn.send({"type": "SETPALETTE", "index": index, "rgb": rgb_hex})
        deadline = asyncio.get_event_loop().time() + 5.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                if "SETPALETTE" in data.get("context", ""):
                    if evt == "ok":
                        return f"Color {index} set to ${rgb_hex} (R={r} G={g} B={b})"
                    return f"Error: {data.get('message', 'failed')}"
            except asyncio.TimeoutError:
                break
    return "Set palette command sent (no confirmation)"


# ─── Copper List Tool ───

@mcp.tool()
async def amiga_copper_list() -> str:
    """Read and decode the current Amiga copper list."""
    conn, state, bus = _require_connected()

    copper_chunks: list[dict] = []

    async with bus.subscribe("copper", "err") as queue:
        conn.send({"type": "COPPERLIST"})
        deadline = asyncio.get_event_loop().time() + 10.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                if evt == "err" and "COPPERLIST" in data.get("context", ""):
                    return f"Copper list read failed: {data.get('message', 'unknown error')}"
                if evt == "copper":
                    copper_chunks.append(data)
                    # Check if last chunk contains END marker
                    hex_data = data["hexData"]
                    if "fffffffffe" in hex_data.lower() or "FFFFFFFFFE" in hex_data:
                        break
                    # Brief pause to collect more chunks
                    remaining = deadline - asyncio.get_event_loop().time()
                    if remaining <= 0.5:
                        break
            except asyncio.TimeoutError:
                break

    if not copper_chunks:
        return "Timed out waiting for copper list data"

    # Combine all chunks
    all_hex = ""
    base_addr = int(copper_chunks[0]["address"], 16)
    for chunk in copper_chunks:
        all_hex += chunk["hexData"]

    instructions = decode_copper_list(all_hex, base_addr)
    return format_copper_list(instructions)


# ─── Sprite Inspector Tool ───

@mcp.tool()
async def amiga_sprites() -> str:
    """Inspect hardware sprite data and positions."""
    conn, state, bus = _require_connected()

    sprites: list[dict] = []

    async with bus.subscribe("sprite", "err") as queue:
        conn.send({"type": "SPRITES"})
        deadline = asyncio.get_event_loop().time() + 5.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                if evt == "err" and "SPRITES" in data.get("context", ""):
                    return f"Sprite inspection failed: {data.get('message', 'unknown error')}"
                if evt == "sprite":
                    sprites.append(data)
                    # Keep collecting for a short time to get all sprites
                    if len(sprites) >= 8:
                        break
            except asyncio.TimeoutError:
                break

    if not sprites:
        return "No active sprites found (or timed out)"

    lines = [f"Hardware Sprites ({len(sprites)} active):"]
    lines.append(f"{'ID':>2s}  {'VStart':>6s}  {'VStop':>5s}  {'HStart':>6s}  {'Attach':>6s}  {'Height':>6s}  {'Data':>6s}")
    lines.append("-" * 55)

    for spr in sorted(sprites, key=lambda s: s["id"]):
        height = spr["vstop"] - spr["vstart"] if spr["vstop"] > spr["vstart"] else 0
        data_bytes = len(spr.get("hexData", "")) // 2
        attached = "Yes" if spr.get("attached") else "No"

        lines.append(
            f"{spr['id']:2d}  {spr['vstart']:6d}  {spr['vstop']:5d}  "
            f"{spr['hstart']:6d}  {attached:>6s}  {height:6d}  {data_bytes:6d}B"
        )

        # Show first few words of sprite data
        hex_data = spr.get("hexData", "")
        if hex_data:
            # Show control words and first few data words
            ctrl_hex = hex_data[:8]  # First 4 bytes = 2 control words
            data_hex = hex_data[8:40]  # Next 16 bytes of image data
            lines.append(f"      Ctrl: {ctrl_hex}  Data: {data_hex}{'...' if len(hex_data) > 40 else ''}")

    return "\n".join(lines)


# ─── Crash Catcher ───

@mcp.tool()
async def amiga_last_crash() -> str:
    """Get details of the last Amiga crash/guru meditation caught by the bridge."""
    conn, state, bus = _require_connected()

    # First check cached crash data
    if state.last_crash:
        return _format_crash(state.last_crash)

    # Query the bridge for last crash data
    async with bus.subscribe("crash", "err") as queue:
        conn.send({"type": "LASTCRASH"})
        deadline = asyncio.get_event_loop().time() + 5.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                if evt == "crash":
                    state.last_crash = data
                    return _format_crash(data)
                if evt == "err" and "LASTCRASH" in data.get("context", ""):
                    return "No crash data recorded"
            except asyncio.TimeoutError:
                break

    return "No crash data available"


def _format_crash(crash: dict) -> str:
    """Format crash data into a readable report with symbolic annotations."""
    from . import symbols

    lines = [
        "=== AMIGA CRASH REPORT ===",
        f"Alert: 0x{crash.get('alertNum', '?')} ({crash.get('alertName', 'Unknown')})",
        f"Time: {crash.get('timestamp', '?')}",
        "",
        "Data Registers:",
    ]

    dregs = crash.get("dataRegs", [])
    for i, val in enumerate(dregs):
        lines.append(f"  D{i}: 0x{val}")

    lines.append("")
    lines.append("Address Registers:")
    aregs = crash.get("addrRegs", [])
    for i, val in enumerate(aregs):
        ann = ""
        try:
            addr = int(val, 16)
            sym = symbols.annotate_address(addr)
            if sym:
                ann = f"  ; {sym}"
                src = symbols.source_line_for_address(addr)
                if src:
                    ann += f" [{src}]"
        except (ValueError, TypeError):
            pass
        lines.append(f"  A{i}: 0x{val}{ann}")

    lines.append("")
    lines.append(f"Stack Pointer: 0x{crash.get('sp', '?')}")

    stack_hex = crash.get("stackHex", "")
    if stack_hex:
        lines.append("")
        lines.append("Stack Trace:")
        sp = int(crash.get("sp", "0"), 16)
        # Try to resolve stack entries as return addresses
        for i in range(0, min(len(stack_hex), 64), 8):
            word_hex = stack_hex[i:i + 8]
            if len(word_hex) < 8:
                break
            addr = int(word_hex, 16)
            offset = i // 2
            sym = symbols.annotate_address(addr)
            if sym and addr > 0x1000:
                src = symbols.source_line_for_address(addr)
                src_str = f" [{src}]" if src else ""
                lines.append(f"  SP+{offset:02d}: 0x{word_hex} -> {sym}{src_str}")
            else:
                lines.append(f"  SP+{offset:02d}: 0x{word_hex}")

        lines.append("")
        lines.append("Raw Stack:")
        from .protocol import format_hex_dump
        lines.append(format_hex_dump(f"{sp:08X}", stack_hex))

    return "\n".join(lines)


# ─── Resource Tracker ───

@mcp.tool()
async def amiga_list_resources(client: str) -> str:
    """List tracked resources (allocations, open files) for an Amiga client. Shows potential leaks."""
    conn, state, bus = _require_connected()

    async with bus.subscribe("resources", "err") as queue:
        conn.send({"type": "LISTRESOURCES", "client": client})
        deadline = asyncio.get_event_loop().time() + 5.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                if evt == "err" and "LISTRESOURCES" in data.get("context", ""):
                    return f"Error: {data.get('message', 'failed')}"
                if evt == "resources":
                    resources = data.get("resources", [])
                    if not resources:
                        return f"No tracked resources for {client}"

                    lines = [f"Resources for {data.get('client', client)} ({len(resources)} tracked):"]
                    lines.append(f"{'Type':<8s} {'Tag':<24s} {'Ptr':<12s} {'Size':>8s} {'State':<8s}")
                    lines.append("-" * 65)

                    leaks = 0
                    for r in resources:
                        state_str = r.get("state", "?")
                        if state_str == "OPEN":
                            leaks += 1
                        lines.append(
                            f"{r.get('type', '?'):<8s} "
                            f"{r.get('tag', '?'):<24s} "
                            f"0x{r.get('ptr', '0'):<10s} "
                            f"{r.get('size', 0):>8d} "
                            f"{state_str:<8s}"
                        )

                    open_res = [r for r in resources if r.get("state") == "OPEN"]
                    closed_res = [r for r in resources if r.get("state") == "CLOSED"]
                    lines.append("")
                    lines.append(f"Open: {len(open_res)}, Closed: {len(closed_res)}")
                    if open_res:
                        lines.append(f"** {len(open_res)} potentially leaked resource(s) **")

                    return "\n".join(lines)
            except asyncio.TimeoutError:
                break

    return "Timed out waiting for resource data"


# ─── Performance Profiler ───

@mcp.tool()
async def amiga_perf_report(client: str) -> str:
    """Get performance profiling data from an Amiga client (frame timing, section timing)."""
    conn, state, bus = _require_connected()

    async with bus.subscribe("perf", "err") as queue:
        conn.send({"type": "GETPERF", "client": client})
        deadline = asyncio.get_event_loop().time() + 5.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                if evt == "err" and "GETPERF" in data.get("context", ""):
                    return f"Error: {data.get('message', 'failed')}"
                if evt == "perf":
                    return _format_perf(data)
            except asyncio.TimeoutError:
                break

    return "Timed out waiting for performance data"


def _format_perf(perf: dict) -> str:
    """Format performance data into a readable report."""
    lines = [
        f"=== PERFORMANCE REPORT: {perf.get('client', '?')} ===",
        "",
        "Frame Timing (VHPOS units, ~64us/line):",
        f"  Avg: {perf.get('frameAvg', 0)}",
        f"  Min: {perf.get('frameMin', 0)}",
        f"  Max: {perf.get('frameMax', 0)}",
        f"  Frames: {perf.get('frameCount', 0)}",
    ]

    # Convert VHPOS to approximate microseconds
    # VHPOS high byte = line number, each line ~ 64us on PAL
    frame_avg = perf.get("frameAvg", 0)
    if frame_avg > 0:
        avg_lines = frame_avg >> 8
        avg_us = avg_lines * 64
        lines.append(f"  (~{avg_us}us avg, ~{avg_us / 1000:.1f}ms)")

        # Estimate FPS: PAL frame = 312 lines at 64us = ~20ms
        if avg_us > 0:
            fps = 1000000 / avg_us
            lines.append(f"  (~{fps:.1f} fps)")

    sections = perf.get("sections", [])
    if sections:
        lines.append("")
        lines.append("Section Timing:")
        lines.append(f"  {'Label':<20s} {'Avg':>8s} {'Min':>8s} {'Max':>8s} {'Count':>8s}")
        lines.append("  " + "-" * 56)
        for s in sections:
            lines.append(
                f"  {s.get('label', '?'):<20s} "
                f"{s.get('avg', 0):>8d} "
                f"{s.get('min', 0):>8d} "
                f"{s.get('max', 0):>8d} "
                f"{s.get('count', 0):>8d}"
            )
    else:
        lines.append("")
        lines.append("No named sections profiled")

    return "\n".join(lines)


# ---- Symbol Table ----

@mcp.tool()
async def amiga_load_symbols(project: str) -> str:
    """Load debug symbols from a compiled Amiga binary. Parses nm symbols and
    STABS debug info (source lines, struct types) if compiled with -g.
    Enables symbolic disassembly and crash analysis."""
    from . import symbols
    table = await symbols.load_symbols(project)
    if not table.symbols:
        return f"No symbols found for project '{project}' (binary: {table.binary_path})"
    funcs = [s for s in table.symbols if s.sym_type in ("T", "t")]
    data = [s for s in table.symbols if s.sym_type in ("D", "d", "B", "b")]
    parts = [f"Loaded {len(table.symbols)} symbols ({len(funcs)} functions, {len(data)} data) from {table.binary_path}"]
    if table.source_lines:
        parts.append(f"STABS debug info: {len(table.source_lines)} source line mappings")
    if table.struct_types:
        parts.append(f"Struct types: {', '.join(table.struct_types.keys())}")
    if table.func_source:
        parts.append(f"Function sources: {len(table.func_source)} mapped to source files")
    return "\n".join(parts)


@mcp.tool()
async def amiga_lookup_symbol(address: str, project: str = "") -> str:
    """Look up a symbol by hex address. Returns symbol name, offset, and source file:line if available."""
    from . import symbols
    addr = int(address, 16)
    ann = symbols.annotate_address_full(addr, project or None)
    if "symbol" not in ann:
        return f"0x{addr:08x}: no symbol found (load symbols first with amiga_load_symbols)"
    parts = [f"0x{addr:08x} = {ann['symbol']}"]
    if "file" in ann:
        parts.append(f"Source: {ann['file']}:{ann.get('line', '?')}")
    return "\n".join(parts)


@mcp.tool()
async def amiga_list_functions(project: str) -> str:
    """List all function symbols loaded for a project, with source file:line if available."""
    from . import symbols
    funcs = symbols.list_functions(project)
    if not funcs:
        return f"No functions found for '{project}' (load symbols first)"
    lines = [f"Functions in {project} ({len(funcs)}):", ""]
    for f in funcs:
        src = ""
        if "file" in f:
            src = f"  [{f['file']}:{f.get('line', '?')}]"
        lines.append(f"  {f['address']}  {f['name']}{src}")
    return "\n".join(lines)


# ---- Audio Inspector ----

@mcp.tool()
async def amiga_audio_channels() -> str:
    """Read Paula audio channel status (DMA enable, interrupt request/enable)."""
    conn, state, bus = _require_connected()
    conn.send({"type": "AUDIOCHANNELS"})
    msg = await bus.wait_for("audiochannels", timeout=5.0)
    if not msg:
        return "Timeout waiting for audio channel data"
    dma = int(msg.get("dmaEnabled", "0"), 16)
    ireq = int(msg.get("intReq", "0"), 16)
    iena = int(msg.get("intEna", "0"), 16)
    lines = ["Paula Audio Channels:", ""]
    for ch in range(4):
        dma_on = bool(dma & (1 << ch))
        int_req = bool(ireq & (1 << ch))
        int_ena = bool(iena & (1 << ch))
        lines.append(f"  Channel {ch}: DMA={'ON' if dma_on else 'off'}  IntReq={'YES' if int_req else 'no'}  IntEna={'YES' if int_ena else 'no'}")
    return "\n".join(lines)


@mcp.tool()
async def amiga_audio_sample(address: str, size: int = 256) -> str:
    """Read audio sample data from chip RAM. Returns hex dump."""
    conn, state, bus = _require_connected()
    if size > 512:
        size = 512
    conn.send({"type": "AUDIOSAMPLE", "address": address, "size": size})
    async with bus.subscribe("audiosample", "err") as q:
        import asyncio
        deadline = asyncio.get_event_loop().time() + 5.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                return "Timeout"
            try:
                evt, data = await asyncio.wait_for(q.get(), timeout=remaining)
                if evt == "err":
                    return f"Error: {data.get('message', '?')}"
                if evt == "audiosample":
                    hex_data = data.get("hexData", "")
                    from . import protocol
                    return protocol.format_hex_dump(data.get("address", "0"), hex_data)
            except asyncio.TimeoutError:
                return "Timeout"


# ---- Intuition Inspector ----

@mcp.tool()
async def amiga_list_screens() -> str:
    """List all Intuition screens with details (title, dimensions, depth, modes)."""
    conn, state, bus = _require_connected()
    conn.send({"type": "LISTSCREENS"})
    msg = await bus.wait_for("screens", timeout=5.0)
    if not msg:
        return "Timeout waiting for screen list"
    screens = msg.get("screens", [])
    if not screens:
        return "No screens found"
    lines = [f"Screens ({len(screens)}):", ""]
    for s in screens:
        modes = []
        vm = int(s.get("viewModes", "0"), 16)
        if vm & 0x8000:
            modes.append("HIRES")
        if vm & 0x0004:
            modes.append("LACE")
        if vm & 0x0800:
            modes.append("HAM")
        mode_str = "+".join(modes) if modes else "LORES"
        lines.append(f"  {s['title']}  {s['width']}x{s['height']}  {s['depth']}bpp  {mode_str}  @{s['addr']}")
    return "\n".join(lines)


@mcp.tool()
async def amiga_list_screen_windows(screen: str = "") -> str:
    """List windows on a screen. Pass screen hex address or empty for first screen."""
    conn, state, bus = _require_connected()
    conn.send({"type": "LISTWINDOWS2", "screen": screen})
    async with bus.subscribe("windows", "err") as q:
        import asyncio
        deadline = asyncio.get_event_loop().time() + 5.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                return "Timeout"
            try:
                evt, data = await asyncio.wait_for(q.get(), timeout=remaining)
                if evt == "err":
                    return f"Error: {data.get('message', '?')}"
                if evt == "windows":
                    windows = data.get("windows", [])
                    if not windows:
                        return "No windows found on this screen"
                    lines = [f"Windows on screen @{data.get('screenAddr', '?')} ({len(windows)}):", ""]
                    for w in windows:
                        lines.append(f"  {w['title']}  pos=({w['left']},{w['top']})  size={w['width']}x{w['height']}  @{w['addr']}")
                    return "\n".join(lines)
            except asyncio.TimeoutError:
                return "Timeout"


@mcp.tool()
async def amiga_list_gadgets(window: str) -> str:
    """List gadgets for a window. Pass window hex address."""
    conn, state, bus = _require_connected()
    conn.send({"type": "LISTGADGETS", "window": window})
    async with bus.subscribe("gadgets", "err") as q:
        import asyncio
        deadline = asyncio.get_event_loop().time() + 5.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                return "Timeout"
            try:
                evt, data = await asyncio.wait_for(q.get(), timeout=remaining)
                if evt == "err":
                    return f"Error: {data.get('message', '?')}"
                if evt == "gadgets":
                    gadgets = data.get("gadgets", [])
                    if not gadgets:
                        return "No gadgets found"
                    lines = [f"Gadgets ({len(gadgets)}):", ""]
                    for g in gadgets:
                        lines.append(f"  ID={g['id']}  pos=({g['left']},{g['top']})  size={g['width']}x{g['height']}  type={g['gadgetType']}  text={g['text']}  @{g['addr']}")
                    return "\n".join(lines)
            except asyncio.TimeoutError:
                return "Timeout"


# ---- Input Injection ----

@mcp.tool()
async def amiga_input_key(rawkey: str, direction: str = "down") -> str:
    """Inject a keyboard event. rawkey is hex Amiga raw key code (e.g. 45=Esc, 44=Return). direction: down/up."""
    conn, state, bus = _require_connected()
    conn.send({"type": "INPUTKEY", "rawkey": rawkey, "direction": direction})
    msg = await bus.wait_for("ok", timeout=5.0)
    if msg:
        return msg.get("message", "Key injected")
    return "Timeout"


@mcp.tool()
async def amiga_input_mouse_move(dx: int, dy: int) -> str:
    """Inject a relative mouse movement (dx, dy pixels)."""
    conn, state, bus = _require_connected()
    conn.send({"type": "INPUTMOVE", "dx": dx, "dy": dy})
    msg = await bus.wait_for("ok", timeout=5.0)
    if msg:
        return msg.get("message", "Mouse moved")
    return "Timeout"


@mcp.tool()
async def amiga_input_click(button: str = "left", direction: str = "down") -> str:
    """Inject a mouse button event. button: left/right/middle. direction: down/up."""
    conn, state, bus = _require_connected()
    conn.send({"type": "INPUTCLICK", "button": button, "direction": direction})
    msg = await bus.wait_for("ok", timeout=5.0)
    if msg:
        return msg.get("message", "Click injected")
    return "Timeout"


# ---- Test Harness ----

@mcp.tool()
async def amiga_run_tests(project: str, command: str | None = None) -> str:
    """Build, deploy, and run an Amiga test program. Collects test results (pass/fail/total).

    The test program should use ab_test_begin/AB_ASSERT/ab_test_end from the bridge client library.
    Returns structured test results after the program completes."""
    import asyncio
    conn, state, bus = _require_connected()

    # Build and deploy
    from . import builder
    build_result = await builder.build_project(project)
    if "error" in build_result.get("status", "").lower():
        return f"Build failed: {build_result.get('output', '?')}"

    await builder.deploy_project(project)

    # Stop any previous instance
    cmd_name = command or project
    conn.send({"type": "STOP", "name": cmd_name})
    await asyncio.sleep(0.5)

    # Launch the test program
    import time
    cmd_id = int(time.time()) & 0xFFFFFF
    conn.send({"type": "LAUNCH", "id": cmd_id, "command": f"DH2:Dev/{cmd_name}"})

    # Collect test events for up to 30 seconds
    results: list[dict] = []
    suite_name = ""
    async with bus.subscribe("test") as q:
        deadline = asyncio.get_event_loop().time() + 30.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                _, data = await asyncio.wait_for(q.get(), timeout=remaining)
                results.append(data)
                if data.get("type") == "TEST_BEGIN":
                    suite_name = data.get("suite", "")
                if data.get("type") == "TEST_END":
                    break
            except asyncio.TimeoutError:
                break

    # Format results
    if not results:
        return "No test results received (timeout after 30s)"

    lines = [f"Test Suite: {suite_name}", ""]
    passed = failed = 0
    for r in results:
        if r["type"] == "TEST_PASS":
            passed += 1
            lines.append(f"  PASS: {r.get('testName', '?')}")
        elif r["type"] == "TEST_FAIL":
            failed += 1
            lines.append(f"  FAIL: {r.get('testName', '?')} ({r.get('file', '?')}:{r.get('line', 0)})")
        elif r["type"] == "TEST_END":
            lines.append("")
            total = r.get("total", passed + failed)
            lines.append(f"Results: {passed} passed, {failed} failed, {total} total")
            if failed == 0:
                lines.append("ALL TESTS PASSED")
            else:
                lines.append("SOME TESTS FAILED")

    return "\n".join(lines)
