"""MCP tool definitions using FastMCP."""

from __future__ import annotations

import asyncio
import logging
import time
from typing import Any

from mcp.server.fastmcp import FastMCP

from .protocol import format_hex_dump, level_name
from .state import AmigaState, EventBus
from .serial_conn import SerialConnection
from .builder import Builder
from .deployer import Deployer

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

    async with bus.subscribe("mem") as queue:
        conn.send({"type": "INSPECT", "address": address, "size": expected})
        deadline = asyncio.get_event_loop().time() + 15.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
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
                    return f"[{data['status']}] {data.get('data', '')}"
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
