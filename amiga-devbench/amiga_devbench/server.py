"""Main server orchestrator - combines MCP, Web API, and static files."""

from __future__ import annotations

import asyncio
import json
import logging
import os
import signal
import time
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Any, AsyncIterator

import uvicorn
from starlette.applications import Starlette
from starlette.middleware import Middleware
from starlette.requests import Request
from starlette.responses import JSONResponse, HTMLResponse, Response
from starlette.routing import Route, Mount
from starlette.staticfiles import StaticFiles
from sse_starlette.sse import EventSourceResponse

from .builder import Builder
from .config import DevBenchConfig
from .deployer import Deployer
from . import file_transfer
from .emulator import EmulatorManager
from .fsuae_rpc import FsuaeRpcClient
from .mcp_tools import mcp, init_tools
from .persistent_log import PersistentLog
from .protocol import format_hex_dump, level_name
from .serial_conn import SerialConnection
from .simulator import AmigaSimulator
from .state import AmigaState, EventBus
from .debugger import DebuggerState, annotate_with_symbols
from .gdb_server import GDBServer
from .traffic_log import TrafficLog

logger = logging.getLogger(__name__)

# Shared instances (set during startup)
_conn: SerialConnection | None = None
_state: AmigaState | None = None
_event_bus: EventBus | None = None
_builder: Builder | None = None
_deployer: Deployer | None = None
_emulator: EmulatorManager | None = None
_config: DevBenchConfig | None = None
_traffic: TrafficLog | None = None
_plog: PersistentLog | None = None
_dbg_state: DebuggerState | None = None
_dbg_stepping = False  # True while step/next is in progress (blocks other serial commands)
_gdb_server: GDBServer | None = None
_fsuae_rpc: FsuaeRpcClient | None = None


def _build_fsuae_env(cfg: DevBenchConfig) -> dict[str, str]:
    """Env-var overlay for the FS-UAE subprocess.

    Stock fs-uae ignores these vars, so this is safe regardless of build.
    The patched build (geekychris/fsuae_remote_patch) exposes its HTTP RPC
    when FSUAE_RPC_PORT is set.
    """
    env: dict[str, str] = {}
    if cfg.fsuae_rpc_enabled.lower() in ("auto", "on") and cfg.fsuae_rpc_port:
        env["FSUAE_RPC_PORT"] = str(cfg.fsuae_rpc_port)
        if cfg.fsuae_rpc_pause_at_boot:
            env["FSUAE_RPC_PAUSE_AT_BOOT"] = "1"
    if cfg.fsuae_gdb_port:
        env["FSUAE_GDB_PORT"] = str(cfg.fsuae_gdb_port)
    return env


# ─── Web API Routes ───

async def api_status(request: Request) -> JSONResponse:
    assert _conn is not None
    return JSONResponse(_conn.get_status())


async def api_events(request: Request) -> EventSourceResponse:
    """SSE endpoint for real-time updates."""
    assert _conn is not None and _event_bus is not None

    async def event_generator():
        # Send current status immediately
        yield {"event": "status", "data": json.dumps(_conn.get_status())}

        # Include emulator status in initial push
        if _emulator:
            yield {"event": "emulator_status", "data": json.dumps(_emulator.get_status())}

        # Initial fsuae-rpc snapshot so the UI can render the badge immediately
        if _fsuae_rpc:
            yield {"event": "fsuae_rpc_status", "data": json.dumps(_fsuae_rpc.snapshot())}

        async with _event_bus.subscribe("log", "heartbeat", "var", "connected",
                                        "disconnected", "clients", "tasks", "dir",
                                        "emulator_status", "crash", "snoop",
                                        "test", "taildata", "port_conflict",
                                        "dbg_stop", "dbg_running", "dbg_detached",
                                        "fsuae_rpc_status", "fsuae_event") as queue:
            while True:
                try:
                    evt, data = await asyncio.wait_for(queue.get(), timeout=30.0)

                    if evt == "log":
                        yield {"event": "log", "data": json.dumps({
                            "level": data.get("level"),
                            "tick": data.get("tick"),
                            "message": data.get("message"),
                            "client": data.get("client"),
                            "timestamp": data.get("timestamp"),
                        })}
                    elif evt == "heartbeat":
                        yield {"event": "heartbeat", "data": json.dumps({
                            "tick": data.get("tick"),
                            "freeChip": data.get("freeChip"),
                            "freeFast": data.get("freeFast"),
                        })}
                    elif evt == "var":
                        yield {"event": "var", "data": json.dumps({
                            "name": data.get("name"),
                            "varType": data.get("varType"),
                            "value": data.get("value"),
                            "client": data.get("client"),
                        })}
                    elif evt == "connected":
                        yield {"event": "connected", "data": "{}"}
                    elif evt == "disconnected":
                        yield {"event": "disconnected", "data": "{}"}
                    elif evt == "clients":
                        yield {"event": "clients", "data": json.dumps(data.get("names", []))}
                    elif evt == "tasks":
                        yield {"event": "tasks", "data": json.dumps({"tasks": data.get("tasks", [])})}
                    elif evt == "dir":
                        yield {"event": "dir", "data": json.dumps({
                            "path": data.get("path"), "entries": data.get("entries", []),
                        })}
                    elif evt == "emulator_status":
                        yield {"event": "emulator_status", "data": json.dumps(data)}
                    elif evt == "crash":
                        # Enrich crash data with alert decode
                        alert_hex = data.get("alertNum", "00000000")
                        yield {"event": "crash", "data": json.dumps({
                            **data,
                            "alertDetail": _decode_alert(alert_hex),
                        })}
                    elif evt == "snoop":
                        yield {"event": "snoop", "data": json.dumps(data)}
                    elif evt == "test":
                        yield {"event": "test", "data": json.dumps(data)}
                    elif evt == "taildata":
                        yield {"event": "taildata", "data": json.dumps({
                            "path": data.get("path"),
                            "data": data.get("data"),
                        })}
                    elif evt == "port_conflict":
                        yield {"event": "port_conflict", "data": json.dumps(data)}
                    elif evt == "dbg_stop":
                        if _dbg_state:
                            _dbg_state.update_from_stop(data)
                        yield {"event": "dbg_stop", "data": json.dumps(
                            _dbg_state.to_dict() if _dbg_state else data)}
                    elif evt == "dbg_running":
                        if _dbg_state:
                            _dbg_state.stopped = False
                        yield {"event": "dbg_running", "data": "{}"}
                    elif evt == "dbg_detached":
                        if _dbg_state:
                            _dbg_state.reset()
                        yield {"event": "dbg_detached", "data": "{}"}
                    elif evt == "fsuae_rpc_status":
                        yield {"event": "fsuae_rpc_status", "data": json.dumps(data)}
                    elif evt == "fsuae_event":
                        yield {"event": "fsuae_event", "data": json.dumps(data)}

                except asyncio.TimeoutError:
                    # Send keepalive comment
                    yield {"comment": "keepalive"}
                except asyncio.CancelledError:
                    break

    return EventSourceResponse(event_generator())


async def api_logs(request: Request) -> JSONResponse:
    assert _state is not None
    count = int(request.query_params.get("count", "200"))
    level = request.query_params.get("level")
    logs = _state.get_recent_logs(count, level)
    return JSONResponse({"logs": [
        {
            "level": l.get("level"),
            "tick": l.get("tick"),
            "message": l.get("message"),
            "timestamp": l.get("timestamp"),
            "client": l.get("client"),
        }
        for l in logs
    ]})


async def api_clients(request: Request) -> JSONResponse:
    assert _conn is not None and _event_bus is not None
    if not _conn.connected:
        return JSONResponse({"clients": [], "error": "Not connected"})
    try:
        _conn.send({"type": "LISTCLIENTS"})
    except Exception:
        return JSONResponse({"clients": [], "error": "Send failed"})

    msg = await _event_bus.wait_for("clients", timeout=5.0)
    if msg:
        return JSONResponse({"clients": msg.get("names", [])})
    return JSONResponse({"clients": [], "message": "No response (bridge may not support LISTCLIENTS)"})


async def api_tasks(request: Request) -> JSONResponse:
    assert _conn is not None and _event_bus is not None
    if not _conn.connected:
        return JSONResponse({"tasks": [], "error": "Not connected"})
    try:
        _conn.send({"type": "LISTTASKS"})
    except Exception:
        return JSONResponse({"tasks": [], "error": "Send failed"})

    msg = await _event_bus.wait_for("tasks", timeout=5.0)
    if msg:
        return JSONResponse({"tasks": msg.get("tasks", [])})
    return JSONResponse({"tasks": [], "message": "No response (bridge may not support LISTTASKS)"})


async def api_dir(request: Request) -> JSONResponse:
    assert _conn is not None and _event_bus is not None
    dir_path = request.query_params.get("path", "SYS:")
    if not _conn.connected:
        return JSONResponse({"path": dir_path, "entries": [], "error": "Not connected"})
    try:
        from .mcp_tools import list_dir_all
        entries = await list_dir_all(_conn, _event_bus, dir_path)
    except Exception:
        return JSONResponse({"path": dir_path, "entries": [], "error": "Send failed"})
    if entries is None:
        return JSONResponse({"path": dir_path, "entries": [], "message": "No response"})
    return JSONResponse({"path": dir_path, "entries": entries})


async def api_file(request: Request) -> JSONResponse:
    assert _conn is not None and _event_bus is not None
    file_path = request.query_params.get("path", "")
    offset = int(request.query_params.get("offset", "0"))
    size = int(request.query_params.get("size", "4096"))
    if not _conn.connected:
        return JSONResponse({"error": "Not connected"})
    try:
        _conn.send({"type": "READFILE", "path": file_path, "offset": offset, "size": size})
    except Exception:
        return JSONResponse({"error": "Send failed"})

    msg = await _event_bus.wait_for(
        "file", timeout=5.0,
        predicate=lambda d: d.get("path") == file_path,
    )
    if msg:
        return JSONResponse({
            "path": msg["path"], "size": msg["size"],
            "offset": msg["offset"], "hexData": msg.get("hexData", ""),
        })
    return JSONResponse({"error": "No response"})


async def api_memory(request: Request) -> JSONResponse:
    assert _conn is not None and _event_bus is not None
    address = request.query_params.get("address", "00000004")
    size = min(int(request.query_params.get("size", "256")), 4096)
    if not _conn.connected:
        return JSONResponse({"error": "Not connected"})

    chunks: list[dict] = []

    async with _event_bus.subscribe("mem", "err") as queue:
        try:
            _conn.send({"type": "INSPECT", "address": address, "size": size})
        except Exception:
            return JSONResponse({"error": "Send failed"})

        deadline = asyncio.get_event_loop().time() + 15.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                if evt == "err" and "INSPECT" in data.get("context", ""):
                    msg = data.get("message") or data.get("context") or "Address not accessible"
                    return JSONResponse({"error": msg})
                if evt == "mem":
                    chunks.append(data)
                    received = sum(c["size"] for c in chunks)
                    if received >= size:
                        break
            except asyncio.TimeoutError:
                break

    if chunks:
        all_hex = "".join(c["hexData"] for c in chunks)
        dump = format_hex_dump(address, all_hex)
        received = sum(c["size"] for c in chunks)
        if received < size:
            dump += "\n(partial - timed out)"
        return JSONResponse({"address": address, "size": received, "dump": dump})
    return JSONResponse({"error": "Timed out waiting for memory dump"})


async def api_vars(request: Request) -> JSONResponse:
    assert _state is not None
    vars_list = [
        {
            "name": v.get("name"),
            "varType": v.get("varType"),
            "value": v.get("value"),
            "client": v.get("client"),
        }
        for v in _state.vars.values()
    ]
    return JSONResponse({"vars": vars_list})


async def api_volumes(request: Request) -> JSONResponse:
    assert _conn is not None and _event_bus is not None
    if not _conn.connected:
        return JSONResponse({"volumes": [], "error": "Not connected"})
    try:
        _conn.send({"type": "LISTVOLUMES"})
    except Exception:
        return JSONResponse({"volumes": [], "error": "Send failed"})

    msg = await _event_bus.wait_for("volumes", timeout=5.0)
    if msg:
        return JSONResponse({"volumes": msg.get("volumes", [])})
    return JSONResponse({"volumes": [], "message": "No response"})


async def api_command(request: Request) -> JSONResponse:
    assert _conn is not None and _event_bus is not None
    if not _conn.connected:
        return JSONResponse({"error": "Not connected"})

    body = await request.json()
    command = body.get("command", "")
    if not command:
        return JSONResponse({"error": "Missing 'command' field"}, status_code=400)

    parts = command.split("|")
    cmd_type = parts[0].upper()

    # Handle SETVAR specially
    if cmd_type == "SETVAR" and len(parts) >= 3:
        try:
            _conn.send({"type": "SETVAR", "name": parts[1], "value": "|".join(parts[2:])})
            return JSONResponse({"message": f"Set {parts[1]} = {'|'.join(parts[2:])}"})
        except Exception as e:
            return JSONResponse({"error": str(e)})

    # Wrap in EXEC
    cmd_id = int(time.time() * 1000) % 100000

    async with _event_bus.subscribe("cmd") as queue:
        try:
            _conn.send({"type": "EXEC", "id": cmd_id, "expression": command})
        except Exception as e:
            return JSONResponse({"error": str(e)})

        deadline = asyncio.get_event_loop().time() + 5.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                if data.get("id") == cmd_id:
                    return JSONResponse({"response": f"[{data['status']}] {data['data']}"})
            except asyncio.TimeoutError:
                break

    return JSONResponse({"message": "Command sent (no response received)"})


async def api_command_raw(request: Request) -> JSONResponse:
    """Send a raw protocol line to the bridge and return the first response."""
    assert _conn is not None and _event_bus is not None
    if not _conn.connected:
        return JSONResponse({"error": "Not connected"})

    body = await request.json()
    line = body.get("line", "").strip()
    if not line:
        return JSONResponse({"error": "Missing 'line' field"}, status_code=400)

    # Subscribe to all events before sending so we catch the response
    async with _event_bus.subscribe("*") as queue:
        _conn.send_raw(line)

        # Collect responses for up to 3 seconds (some commands return multiple lines)
        responses = []
        try:
            deadline = asyncio.get_event_loop().time() + 3.0
            while True:
                remaining = deadline - asyncio.get_event_loop().time()
                if remaining <= 0:
                    break
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                # Skip heartbeat/status noise
                if evt in ("heartbeat", "status", "connected", "disconnected"):
                    continue
                if isinstance(data, dict):
                    # Compact representation
                    dtype = data.get("type", evt)
                    responses.append(f"[{dtype}] {json.dumps(data, default=str)}")
                else:
                    responses.append(f"[{evt}] {data}")
                # Most commands send a single response
                if len(responses) >= 1:
                    # Wait a tiny bit more for multi-line responses
                    deadline = min(deadline, asyncio.get_event_loop().time() + 0.3)
        except asyncio.TimeoutError:
            pass

    if responses:
        return JSONResponse({"response": "\n".join(responses)})
    return JSONResponse({"response": f"Sent: {line} (no response)"})


async def api_launch(request: Request) -> JSONResponse:
    """Launch a DOS command on the Amiga via the bridge's LAUNCH handler."""
    assert _conn is not None and _event_bus is not None
    if not _conn.connected:
        return JSONResponse({"error": "Not connected"})

    body = await request.json()
    command = body.get("command", "")
    if not command:
        return JSONResponse({"error": "Missing 'command' field"}, status_code=400)

    cmd_id = int(time.time() * 1000) % 100000

    async with _event_bus.subscribe("cmd") as queue:
        try:
            _conn.send({"type": "LAUNCH", "id": cmd_id, "command": command})
        except Exception as e:
            return JSONResponse({"error": str(e)})

        deadline = asyncio.get_event_loop().time() + 10.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                if data.get("id") == cmd_id:
                    return JSONResponse({
                        "status": data["status"],
                        "output": data.get("data", ""),
                    })
            except asyncio.TimeoutError:
                break

    return JSONResponse({"status": "timeout", "output": "No response from Amiga"})


async def api_run(request: Request) -> JSONResponse:
    """Launch a program asynchronously on the Amiga (doesn't wait for exit)."""
    assert _conn is not None and _event_bus is not None
    if not _conn.connected:
        return JSONResponse({"error": "Not connected"})

    body = await request.json()
    command = body.get("command", "")
    if not command:
        return JSONResponse({"error": "Missing 'command' field"}, status_code=400)

    cmd_id = int(time.time() * 1000) % 100000

    async with _event_bus.subscribe("cmd") as queue:
        try:
            _conn.send({"type": "RUN", "id": cmd_id, "command": command})
        except Exception as e:
            return JSONResponse({"error": str(e)})

        deadline = asyncio.get_event_loop().time() + 5.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                if data.get("id") == cmd_id:
                    return JSONResponse({
                        "status": data["status"],
                        "output": data.get("data", ""),
                    })
            except asyncio.TimeoutError:
                break

    return JSONResponse({"status": "timeout", "output": "No response from Amiga"})


async def api_break(request: Request) -> JSONResponse:
    """Send CTRL-C break signal to a named task/process on the Amiga."""
    assert _conn is not None and _event_bus is not None
    if not _conn.connected:
        return JSONResponse({"error": "Not connected"})

    body = await request.json()
    name = body.get("name", "")
    if not name:
        return JSONResponse({"error": "Missing 'name' field"}, status_code=400)

    async with _event_bus.subscribe("ok", "err") as queue:
        try:
            _conn.send({"type": "BREAK", "name": name})
        except Exception as e:
            return JSONResponse({"error": str(e)})

        deadline = asyncio.get_event_loop().time() + 3.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                ctx = data.get("context", "")
                if ctx == "BREAK":
                    return JSONResponse({
                        "status": "ok" if evt == "ok" else "error",
                        "message": data.get("message", f"Break sent to {name}"),
                    })
                if "Task not found" in data.get("message", ""):
                    return JSONResponse({"status": "error", "message": data["message"]})
            except asyncio.TimeoutError:
                break

    return JSONResponse({"status": "timeout", "message": "No response from bridge"})


async def api_hooks(request: Request) -> JSONResponse:
    """List hooks registered by clients."""
    assert _conn is not None and _event_bus is not None
    if not _conn.connected:
        return JSONResponse({"error": "Not connected"})
    client = request.query_params.get("client", "")
    try:
        _conn.send({"type": "LISTHOOKS", "client": client})
    except Exception:
        return JSONResponse({"error": "Send failed"})
    msg = await _event_bus.wait_for("hooks", timeout=5.0)
    if msg:
        return JSONResponse({"client": msg.get("client"), "hooks": msg.get("hooks", [])})
    return JSONResponse({"hooks": [], "message": "No response"})


async def api_call_hook(request: Request) -> JSONResponse:
    """Call a hook on a client."""
    assert _conn is not None and _event_bus is not None
    if not _conn.connected:
        return JSONResponse({"error": "Not connected"})
    body = await request.json()
    client = body.get("client", "")
    hook = body.get("hook", "")
    hook_args = body.get("args", "")
    if not client or not hook:
        return JSONResponse({"error": "Missing client or hook"}, status_code=400)
    cmd_id = int(time.time() * 1000) % 100000
    async with _event_bus.subscribe("cmd") as queue:
        try:
            _conn.send({"type": "CALLHOOK", "id": cmd_id, "client": client,
                        "hook": hook, "args": hook_args})
        except Exception as e:
            return JSONResponse({"error": str(e)})
        deadline = asyncio.get_event_loop().time() + 5.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                if data.get("id") == cmd_id:
                    result = data.get("data", "")
                    # Unescape newlines/pipes from serial protocol
                    result = result.replace("\\n", "\n").replace("\\|", "|")
                    return JSONResponse({"status": data["status"], "data": result})
            except asyncio.TimeoutError:
                break
    return JSONResponse({"status": "timeout", "data": "No response"})


async def api_memregions(request: Request) -> JSONResponse:
    """List memory regions registered by clients."""
    assert _conn is not None and _event_bus is not None
    if not _conn.connected:
        return JSONResponse({"error": "Not connected"})
    client = request.query_params.get("client", "")
    try:
        _conn.send({"type": "LISTMEMREGS", "client": client})
    except Exception:
        return JSONResponse({"error": "Send failed"})
    msg = await _event_bus.wait_for("memregs", timeout=5.0)
    if msg:
        return JSONResponse({"client": msg.get("client"), "memregs": msg.get("memregs", [])})
    return JSONResponse({"memregs": [], "message": "No response"})


async def api_read_memregion(request: Request) -> JSONResponse:
    """Read data from a named memory region."""
    assert _conn is not None and _event_bus is not None
    if not _conn.connected:
        return JSONResponse({"error": "Not connected"})
    body = await request.json()
    client = body.get("client", "")
    region = body.get("region", "")
    if not client or not region:
        return JSONResponse({"error": "Missing client or region"}, status_code=400)
    chunks: list[dict] = []
    async with _event_bus.subscribe("mem", "err") as queue:
        try:
            _conn.send({"type": "READMEMREG", "client": client, "region": region})
        except Exception as e:
            return JSONResponse({"error": str(e)})
        deadline = asyncio.get_event_loop().time() + 5.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                if evt == "err":
                    return JSONResponse({"error": data.get("message", "Error")})
                chunks.append(data)
                break  # Expect single chunk for registered regions
            except asyncio.TimeoutError:
                break
    if chunks:
        all_hex = "".join(c["hexData"] for c in chunks)
        dump = format_hex_dump(chunks[0]["address"], all_hex)
        return JSONResponse({"dump": dump, "address": chunks[0]["address"],
                             "size": chunks[0]["size"]})
    return JSONResponse({"error": "No response"})


async def api_client_info(request: Request) -> JSONResponse:
    """Get detailed info about a client."""
    assert _conn is not None and _event_bus is not None
    if not _conn.connected:
        return JSONResponse({"error": "Not connected"})
    client = request.query_params.get("client", "")
    if not client:
        return JSONResponse({"error": "Missing client name"}, status_code=400)
    try:
        _conn.send({"type": "CLIENTINFO", "client": client})
    except Exception:
        return JSONResponse({"error": "Send failed"})
    msg = await _event_bus.wait_for("cinfo", timeout=5.0)
    if msg:
        return JSONResponse(msg)
    return JSONResponse({"error": "No response"})


async def api_stop(request: Request) -> JSONResponse:
    """Stop a client process."""
    assert _conn is not None and _event_bus is not None
    if not _conn.connected:
        return JSONResponse({"error": "Not connected"})
    body = await request.json()
    name = body.get("name", "")
    if not name:
        return JSONResponse({"error": "Missing name"}, status_code=400)
    async with _event_bus.subscribe("ok", "err") as queue:
        try:
            signal = body.get("signal", "CTRLC")
            _conn.send({"type": "STOP", "name": name, "signal": signal})
        except Exception as e:
            return JSONResponse({"error": str(e)})
        deadline = asyncio.get_event_loop().time() + 3.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                ctx = data.get("context", "")
                if "STOP" in ctx or "Client" in ctx:
                    return JSONResponse({
                        "status": "ok" if evt == "ok" else "error",
                        "message": data.get("message", ""),
                    })
            except asyncio.TimeoutError:
                break
    return JSONResponse({"status": "timeout"})


async def api_script(request: Request) -> JSONResponse:
    """Run an AmigaDOS script on the Amiga."""
    assert _conn is not None and _event_bus is not None
    if not _conn.connected:
        return JSONResponse({"error": "Not connected"})
    body = await request.json()
    script = body.get("script", "")
    if not script:
        return JSONResponse({"error": "Missing script"}, status_code=400)
    # Convert newlines to semicolons for the protocol
    script_line = script.replace("\n", ";")
    # Runs asynchronously on the Amiga (bridge stays responsive); shared helper
    # polls the capture file for completion.
    from .mcp_tools import script_execute
    try:
        status, output = await script_execute(_conn, _event_bus, script_line, timeout=60.0)
    except Exception as e:
        return JSONResponse({"error": str(e)})
    return JSONResponse({"status": status, "output": output})


async def api_write_memory(request: Request) -> JSONResponse:
    """Write data to a memory address on the Amiga."""
    assert _conn is not None and _event_bus is not None
    if not _conn.connected:
        return JSONResponse({"error": "Not connected"})
    body = await request.json()
    address = body.get("address", "")
    hex_data = body.get("hexData", "")
    if not address or not hex_data:
        return JSONResponse({"error": "Missing address or hexData"}, status_code=400)
    async with _event_bus.subscribe("ok", "err") as queue:
        try:
            _conn.send({"type": "WRITEMEM", "address": address, "hexData": hex_data})
        except Exception as e:
            return JSONResponse({"error": str(e)})
        deadline = asyncio.get_event_loop().time() + 3.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                ctx = data.get("context", "")
                if "WRITEMEM" in ctx:
                    msg = data.get("message") or ctx or ("Written" if evt == "ok" else "Write failed")
                    return JSONResponse({
                        "status": "ok" if evt == "ok" else "error",
                        "message": msg,
                    })
            except asyncio.TimeoutError:
                break
    return JSONResponse({"status": "timeout"})


async def api_run_cycle(request: Request) -> JSONResponse:
    """Build, deploy, stop old instance, launch, and wait for client connect."""
    assert _builder is not None and _deployer is not None
    assert _conn is not None and _event_bus is not None

    body = await request.json()
    project = body.get("project", "")
    if not project:
        return JSONResponse({"error": "Missing 'project' field"}, status_code=400)

    command = body.get("command")
    binary_name = project.rstrip("/").split("/")[-1]
    launch_command = command or f"Dropbox:Dev/{binary_name}"

    result: dict[str, Any] = {"project": project, "binary": binary_name}

    # 1. Build
    build_result = await _builder.build(project)
    result["build"] = {
        "success": build_result.success,
        "duration_ms": build_result.duration,
        "output": build_result.output or "",
        "errors": build_result.errors or "",
    }
    if not build_result.success:
        return JSONResponse(result)

    # 2. Deploy
    deploy_result = _deployer.deploy(project)
    result["deploy"] = {
        "success": deploy_result.success,
        "message": deploy_result.message,
        "files": deploy_result.files if deploy_result.files else [],
    }
    if not deploy_result.success:
        return JSONResponse(result)

    # 3. Stop existing client (if connected)
    stop_status = "skipped"
    stop_message = ""
    if _conn.connected:
        try:
            async with _event_bus.subscribe("ok", "err") as queue:
                _conn.send({"type": "STOP", "name": binary_name})
                deadline = asyncio.get_event_loop().time() + 1.0
                while True:
                    remaining = deadline - asyncio.get_event_loop().time()
                    if remaining <= 0:
                        stop_status = "timeout"
                        break
                    try:
                        evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                        ctx = data.get("context", "")
                        if "STOP" in ctx or "Client" in ctx:
                            stop_status = "ok" if evt == "ok" else "error"
                            stop_message = data.get("message", "")
                            break
                    except asyncio.TimeoutError:
                        stop_status = "timeout"
                        break
        except Exception as e:
            stop_status = "error"
            stop_message = str(e)

        await asyncio.sleep(0.15)
    result["stop"] = {"status": stop_status, "message": stop_message}

    # 4. Launch
    launch_status = "skipped"
    launch_output = ""
    if _conn.connected:
        cmd_id = int(time.time() * 1000) % 100000
        try:
            _conn.send({"type": "LAUNCH", "id": cmd_id, "command": launch_command})
            msg = await _event_bus.wait_for(
                "cmd", timeout=3.0,
                predicate=lambda d: d.get("id") == cmd_id,
            )
            if msg:
                launch_status = msg["status"]
                launch_output = msg.get("data", "")
            else:
                launch_status = "sent"
                launch_output = f"No response for: {launch_command}"
        except Exception as e:
            launch_status = "error"
            launch_output = str(e)
    result["launch"] = {"status": launch_status, "command": launch_command, "output": launch_output}

    # 5. Wait for client to appear
    client_connected = False
    if _conn.connected:
        for attempt in range(4):
            await asyncio.sleep(0.25)
            try:
                _conn.send({"type": "LISTCLIENTS"})
                clients_msg = await _event_bus.wait_for("clients", timeout=0.5)
                if clients_msg:
                    names = clients_msg.get("names", [])
                    if binary_name in names:
                        client_connected = True
                        break
            except Exception:
                pass
    result["client"] = {"connected": client_connected, "name": binary_name}

    return JSONResponse(result)


async def api_ping(request: Request) -> JSONResponse:
    assert _conn is not None and _event_bus is not None
    if not _conn.connected:
        return JSONResponse({"error": "Not connected"})
    try:
        _conn.send({"type": "PING"})
    except Exception as e:
        return JSONResponse({"error": str(e)})

    # Bridge responds with PONG (not HB), so listen for both
    msg = await _event_bus.wait_for("pong", timeout=5.0)
    if msg:
        return JSONResponse({
            "message": (
                f"Amiga alive - clients: {msg['clientCount']}, "
                f"chip: {msg['freeChip']} bytes, fast: {msg['freeFast']} bytes"
            ),
            "pong": {
                "clientCount": msg["clientCount"],
                "freeChip": msg["freeChip"],
                "freeFast": msg["freeFast"],
            },
        })
    # Fallback: maybe an HB arrived instead
    msg = await _event_bus.wait_for("heartbeat", timeout=0.5)
    if msg:
        return JSONResponse({
            "message": (
                f"Amiga alive - tick: {msg['tick']}, "
                f"chip: {msg['freeChip']} bytes, fast: {msg['freeFast']} bytes"
            ),
            "heartbeat": {
                "tick": msg["tick"],
                "freeChip": msg["freeChip"],
                "freeFast": msg["freeFast"],
            },
        })
    return JSONResponse({"error": "No response from Amiga (timeout)"})


async def api_connect(request: Request) -> JSONResponse:
    assert _conn is not None
    try:
        if _conn.connected:
            _conn.disconnect()
        body = await request.json() if request.headers.get("content-length", "0") != "0" else {}
        mode = body.get("mode")
        if mode == "tcp":
            host = body.get("host", "127.0.0.1")
            port = body.get("port", 1234)
            _conn.set_target(host, port)
        elif mode == "pty":
            pty_path = body.get("ptyPath", "/tmp/amiga-serial")
            _conn.set_mode("pty", pty_path=pty_path)
        await _conn.connect()
        return JSONResponse({"message": f"Connected ({_conn.mode})", "status": _conn.get_status()})
    except Exception as e:
        return JSONResponse({"error": f"Connection failed: {e}"})


async def api_disconnect(request: Request) -> JSONResponse:
    assert _conn is not None
    _conn.disconnect()
    return JSONResponse({"message": "Disconnected"})


async def health(request: Request) -> JSONResponse:
    assert _conn is not None
    return JSONResponse({
        "status": "ok",
        "serial": _conn.get_status(),
    })


async def serve_index(request: Request) -> HTMLResponse:
    """Serve the web UI index.html with no-cache headers."""
    web_dir = Path(__file__).parent / "web"
    index_file = web_dir / "index.html"
    if index_file.is_file():
        return HTMLResponse(
            index_file.read_text(),
            headers={
                "Cache-Control": "no-cache, no-store, must-revalidate",
                "Pragma": "no-cache",
                "Expires": "0",
            },
        )
    return HTMLResponse("<h1>Web UI not found</h1>", status_code=404)


async def serve_static(request: Request) -> Response:
    """Serve static files from the web directory."""
    filename = request.path_params["filename"]
    # Prevent path traversal
    if "/" in filename or "\\" in filename or ".." in filename:
        return Response("Not found", status_code=404)
    web_dir = Path(__file__).parent / "web"
    filepath = web_dir / filename
    if not filepath.is_file():
        return Response("Not found", status_code=404)
    content_types = {".png": "image/png", ".jpg": "image/jpeg", ".svg": "image/svg+xml", ".ico": "image/x-icon"}
    ct = content_types.get(filepath.suffix, "application/octet-stream")
    return Response(filepath.read_bytes(), media_type=ct)


# ─── Application Factory ───

def create_app(args: Any, cfg: DevBenchConfig | None = None) -> Starlette:
    """Create the Starlette application with MCP + Web API + static files."""
    global _conn, _state, _event_bus, _builder, _deployer, _emulator, _config, _traffic, _plog, _dbg_state, _gdb_server, _fsuae_rpc

    _state = AmigaState()
    _dbg_state = DebuggerState()
    _traffic = TrafficLog(maxlen=2000)
    _event_bus = EventBus()

    # Persistent log file
    project_root_early = cfg.project_root if cfg else (args.project_root or str(Path.cwd()))
    _plog = PersistentLog(Path(project_root_early) / "logs")

    # Use config if provided, fall back to args
    if cfg:
        _config = cfg
        use_tcp = cfg.serial_mode == "tcp" or cfg.simulator
        serial_host = cfg.serial_host if use_tcp else None
    else:
        _config = None
        serial_host = args.serial_host or (
            "127.0.0.1" if args.simulator or args.serial_host else None
        )
        use_tcp = serial_host is not None or args.simulator

    _conn = SerialConnection(
        state=_state,
        event_bus=_event_bus,
        host=serial_host or cfg.serial_host if cfg else "127.0.0.1",
        port=cfg.serial_port if cfg else args.serial_port,
        pty_path=cfg.pty_path if cfg else args.pty_path,
    )
    _conn._dbg_state = _dbg_state  # Direct state updates for debugger messages

    # Wire persistent log callbacks for TX/RX
    if _plog:
        _conn.on_tx = _plog.write_tx
        _conn.on_rx = _plog.write_rx

    if not use_tcp:
        _conn.set_mode("pty", pty_path=cfg.pty_path if cfg else args.pty_path)

    project_root = cfg.project_root if cfg else args.project_root
    deploy_dir = cfg.deploy_dir if cfg else args.deploy_dir

    _builder = Builder(project_root)
    _deployer = Deployer(project_root, deploy_dir)

    # Emulator manager
    if cfg:
        _emulator = EmulatorManager(
            binary=cfg.emulator_binary,
            config_file=cfg.emulator_config,
            event_bus=_event_bus,
            extra_env=_build_fsuae_env(cfg),
        )
    else:
        _emulator = EmulatorManager(event_bus=_event_bus)

    # FS-UAE remote-debug HTTP client (no-op against stock fs-uae)
    if cfg:
        _fsuae_rpc = FsuaeRpcClient(
            port=cfg.fsuae_rpc_port,
            enabled=cfg.fsuae_rpc_enabled,
            event_bus=_event_bus,
        )
    else:
        _fsuae_rpc = FsuaeRpcClient(event_bus=_event_bus)

    # Initialize MCP tools with shared state
    init_tools(_conn, _state, _builder, _deployer, _event_bus, fsuae_rpc=_fsuae_rpc)

    # Add an INFO Logs-tab line for EVERY tool call (Claude's via /mcp and the
    # web UI's). Traffic-view recording already happens in McpLoggingWrapper
    # (Claude) and the /api/command handler (web UI), so we only add the Logs
    # entry here to avoid double-recording.
    _orig_call_tool = mcp._tool_manager.call_tool

    async def _logging_call_tool(name, arguments=None, *a, **kw):
        params = arguments if isinstance(arguments, dict) else {}
        argstr = ", ".join(f"{k}={v}" for k, v in params.items())
        logger.info("MCP tool: %s(%s)", name, argstr)
        if _state is not None:
            _state.add_log({"level": "I", "client": "mcp",
                            "message": f"tool: {name}({argstr})",
                            "timestamp": time.time()})
        return await _orig_call_tool(name, arguments, *a, **kw)

    mcp._tool_manager.call_tool = _logging_call_tool

    # Get the MCP session manager (triggers lazy init)
    _mcp_app_inner = mcp.streamable_http_app()
    session_manager = mcp._session_manager

    # Build the MCP ASGI handler directly
    from mcp.server.fastmcp.server import StreamableHTTPASGIApp
    mcp_asgi_handler = StreamableHTTPASGIApp(session_manager)

    _sim_mode = cfg.simulator if cfg else args.simulator
    _sim_port = cfg.serial_port if cfg else args.serial_port
    _srv_port = cfg.server_port if cfg else args.port
    _pty = cfg.pty_path if cfg else args.pty_path
    _emu_auto = cfg.emulator_auto_start if cfg else False

    @asynccontextmanager
    async def lifespan(app: Starlette) -> AsyncIterator[None]:
        """Combined lifespan: run MCP session manager + our startup."""
        sim = None

        async with session_manager.run():
            # Start simulator if requested
            if _sim_mode:
                sim = AmigaSimulator(port=_sim_port)
                await sim.start()
                await asyncio.sleep(0.2)

            # Auto-start emulator if configured
            if _emu_auto and _emulator and not _sim_mode:
                logger.info("Auto-starting emulator...")
                started = await _emulator.start()
                if started:
                    # Give FS-UAE time to boot and open TCP port
                    logger.info("Waiting for emulator to boot...")
                    await asyncio.sleep(3.0)

            # Print startup banner
            print()
            print("=" * 60)
            print("  Amiga DevBench")
            print("=" * 60)
            print(f"  MCP endpoint:  http://localhost:{_srv_port}/mcp")
            print(f"  Web UI:        http://localhost:{_srv_port}/")
            print(f"  Health check:  http://localhost:{_srv_port}/health")
            print(f"  Mode:          {'TCP' if use_tcp else 'PTY'}")
            if use_tcp:
                print(f"  Serial:        {_conn._host}:{_conn._port}")
            else:
                print(f"  PTY path:      {_pty}")
            if _sim_mode:
                print(f"  Simulator:     running on port {_sim_port}")
            if _emulator and _emulator.is_running:
                print(f"  Emulator:      running (pid {_emulator.pid})")

            # Start fs-uae-rpc availability poller (no-op against stock fs-uae)
            if _fsuae_rpc:
                _fsuae_rpc.start_poller()
                # One immediate probe so /api/status has a result right away
                await _fsuae_rpc.probe()
                snap = _fsuae_rpc.snapshot()
                if snap["status"] == "available":
                    print(f"  FS-UAE RPC:    {snap['base_url']} ({snap.get('service', 'v1')})")
                elif snap["status"] == "disabled":
                    pass  # silent — user opted out
                else:
                    print(f"  FS-UAE RPC:    probing {snap['base_url']} (currently {snap['status']})")

            # Start GDB RSP server
            _gdb_port = cfg.gdb_port if cfg and hasattr(cfg, 'gdb_port') else 2159
            _gdb_server = GDBServer(_conn, _event_bus, _dbg_state, port=_gdb_port)
            try:
                await _gdb_server.start()
                print(f"  GDB RSP:       localhost:{_gdb_port}")
            except Exception as e:
                logger.warning("GDB server failed to start: %s", e)
                _gdb_server = None

            print("=" * 60)
            print()

            # Auto-connect
            try:
                await _conn.connect()
                if use_tcp:
                    logger.info("Auto-connected via TCP")
                else:
                    logger.info("PTY active: %s", _pty)
            except Exception as e:
                logger.warning("Auto-connect failed: %s (use amiga_connect to retry)", e)

            # Background task: listen for bridge READY and auto-enable crash handler
            _auto_crash = cfg.crash_handler_auto_enable if cfg else False

            async def _on_bridge_ready():
                async with _event_bus.subscribe("ready") as queue:
                    while True:
                        try:
                            evt, data = await queue.get()
                            logger.info("Bridge READY received: %s", data)
                            # Surface the running daemon build so it is obvious
                            # which binary is live after an update.
                            if _conn and _conn.connected:
                                try:
                                    async with _event_bus.subscribe("version") as vq:
                                        _conn.send({"type": "VERSION"})
                                        _, vd = await asyncio.wait_for(vq.get(), timeout=3.0)
                                        logger.warning(
                                            "*** Connected to %s v%s.%s (build %s) ***",
                                            vd.get("name", "AmigaBridge"),
                                            vd.get("major", "?"), vd.get("minor", "?"),
                                            vd.get("date", "?"),
                                        )
                                except Exception:
                                    pass
                            if _auto_crash and _conn and _conn.connected:
                                logger.info("Auto-enabling crash handler...")
                                await asyncio.sleep(0.5)
                                _conn.send({"type": "CRASHINIT"})
                        except asyncio.CancelledError:
                            break
                        except Exception as e:
                            logger.error("Error in bridge ready handler: %s", e)

            ready_task = asyncio.ensure_future(_on_bridge_ready())

            # Background listener to keep _dbg_state in sync with debugger events
            async def _dbg_state_listener():
                async with _event_bus.subscribe("dbg_stop", "dbg_running", "dbg_detached", "dbg_state") as queue:
                    while True:
                        try:
                            evt, data = await queue.get()
                            if evt == "dbg_stop" and _dbg_state:
                                _dbg_state.update_from_stop(data)
                            elif evt == "dbg_running" and _dbg_state:
                                _dbg_state.stopped = False
                            elif evt == "dbg_detached" and _dbg_state:
                                _dbg_state.reset()
                            elif evt == "dbg_state" and _dbg_state:
                                _dbg_state.update_from_state(data)
                        except asyncio.CancelledError:
                            break
                        except Exception:
                            pass

            dbg_listener_task = asyncio.ensure_future(_dbg_state_listener())

            # Background: auto-pause fs-uae when the bridge reports a crash,
            # so the CPU state is frozen at the fault moment instead of
            # continuing past the alert. Opt out via
            # [fsuae_rpc] auto_pause_on_crash = false.
            _auto_pause = cfg.fsuae_auto_pause_on_crash if cfg else True

            async def _crash_auto_pauser():
                async with _event_bus.subscribe("crash") as queue:
                    while True:
                        try:
                            _evt, data = await queue.get()
                            if not _auto_pause or not _fsuae_rpc or not _fsuae_rpc.available:
                                continue
                            logger.info("Auto-pausing fs-uae on bridge crash event")
                            try:
                                await _fsuae_rpc.pause()
                                # Surface as a fsuae_event so the UI can react
                                _event_bus.publish("fsuae_event", {
                                    "event": "auto_paused",
                                    "reason": "bridge_crash",
                                    "alertNum": data.get("alertNum"),
                                    "pc": data.get("pc"),
                                })
                            except Exception as e:
                                logger.warning("Auto-pause failed: %s", e)
                        except asyncio.CancelledError:
                            break
                        except Exception as e:
                            logger.error("Error in crash auto-pause handler: %s", e)

            crash_pause_task = asyncio.ensure_future(_crash_auto_pauser())

            # Apply any auto-snapshot config from devbench.toml. Off (interval=0)
            # by default; only starts the worker if explicitly enabled.
            if cfg and cfg.fsuae_auto_snapshot_interval_s > 0:
                _auto_snap_apply(cfg.fsuae_auto_snapshot_interval_s,
                                 cfg.fsuae_auto_snapshot_ring_size)

            # Start persistent event logger
            if _plog:
                _plog.start(_event_bus)

            yield

            ready_task.cancel()
            dbg_listener_task.cancel()
            crash_pause_task.cancel()
            if _auto_snap.task and not _auto_snap.task.done():
                _auto_snap.task.cancel()

            # Shutdown
            if _gdb_server:
                await _gdb_server.stop()
            if _fsuae_rpc:
                await _fsuae_rpc.aclose()
            if _plog:
                _plog.stop()
            if _conn:
                _conn.disconnect()
            if _builder:
                await _builder.shutdown()
            if _emulator and _emulator.is_running:
                await _emulator.stop()
            if sim:
                await sim.stop()
            logger.info("Shut down complete")

    # ─── Tool API endpoints ───

    # Fixed screenshot save path
    _screenshot_dir = Path("/tmp/amiga-screenshots")
    _screenshot_dir.mkdir(exist_ok=True)
    _last_screenshot_path: list[str] = [""]  # mutable container for closure

    async def api_tool_screenshot(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        window = request.query_params.get("window", "")
        # Subscribe BEFORE sending so no SCRINFO/SCRRGB/SCRRLE/SCRDATA is missed.
        scrinfo = None
        scrdata_lines = []
        scrrgb_lines = []
        scrrle_lines = []
        expected_total = 0
        async with _event_bus.subscribe("scrinfo", "scrdata", "scrrgb", "scrrle", "err") as queue:
            cmd = {"type": "SCREENSHOT"}
            if window:
                cmd["window"] = window
            _conn.send(cmd)
            deadline = asyncio.get_event_loop().time() + 60.0
            while True:
                remaining = deadline - asyncio.get_event_loop().time()
                if remaining <= 0:
                    break
                try:
                    evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                except asyncio.TimeoutError:
                    break
                if evt == "err" and "SCREENSHOT" in data.get("context", ""):
                    return JSONResponse({"error": data.get("message", "screenshot failed")})
                if evt == "scrinfo":
                    scrinfo = data
                    expected_total = data["height"] * data["depth"]
                elif evt == "scrrle":
                    # RLE-compressed row (true-colour or chunky): one per row.
                    scrrle_lines.append(data)
                    if scrinfo and len(scrrle_lines) >= scrinfo["height"]:
                        break
                elif evt == "scrrgb":
                    # True-colour (RTG): one RGB row per line.
                    scrrgb_lines.append(data)
                    if scrinfo and len(scrrgb_lines) >= scrinfo["height"]:
                        break
                elif evt == "scrdata":
                    scrdata_lines.append(data)
                    # Chunky rows (plane==255) send one line per row.
                    need = (scrinfo["height"] if data.get("plane") == 255 else expected_total) if scrinfo else 0
                    if need and len(scrdata_lines) >= need:
                        break
        if not scrinfo:
            return JSONResponse({"error": "No SCRINFO response"})
        if not scrdata_lines and not scrrgb_lines and not scrrle_lines:
            return JSONResponse({"error": "No pixel data received"})
        try:
            from .screenshot import save_screenshot
            import datetime
            ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
            label = window.replace(" ", "_") if window else "screen"
            save_path = str(_screenshot_dir / f"{label}_{ts}.png")
            path = save_screenshot(scrinfo, scrdata_lines, save_path,
                                   rgb_lines=scrrgb_lines or None,
                                   rle_lines=scrrle_lines or None)
            _last_screenshot_path[0] = path
            filename = Path(path).name  # save_screenshot may switch .png/.ppm
            return JSONResponse({
                "path": path,
                "filename": filename,
                "viewUrl": f"/api/screenshot/view?file={filename}",
                "width": scrinfo.get("width"),
                "height": scrinfo.get("height"),
                "depth": scrinfo.get("depth"),
            })
        except Exception as e:
            return JSONResponse({"error": str(e)})

    async def api_screenshot_view(request: Request) -> Response:
        filename = request.query_params.get("file", "")
        if filename:
            path = _screenshot_dir / filename
        elif _last_screenshot_path[0]:
            path = Path(_last_screenshot_path[0])
        else:
            return Response("No screenshot available", status_code=404)
        if not path.exists():
            return Response("Screenshot not found", status_code=404)
        # Prevent path traversal
        if not str(path.resolve()).startswith(str(_screenshot_dir.resolve())):
            return Response("Invalid path", status_code=400)
        content_type = "image/png" if str(path).endswith(".png") else "image/x-portable-pixmap"
        return Response(path.read_bytes(), media_type=content_type)

    async def api_screenshot_list(request: Request) -> JSONResponse:
        files = sorted(_screenshot_dir.glob("*.png"), key=lambda p: p.stat().st_mtime, reverse=True)
        return JSONResponse({"screenshots": [
            {"filename": f.name, "viewUrl": f"/api/screenshot/view?file={f.name}"}
            for f in files[:20]
        ]})

    async def api_windows(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "LISTWINDOWS"})
        msg = await _event_bus.wait_for("winlist", timeout=3.0)
        if msg:
            return JSONResponse({"windows": msg.get("windows", [])})
        return JSONResponse({"windows": []})

    async def api_tool_palette(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "PALETTE"})
        msg = await _event_bus.wait_for("palette", timeout=5.0)
        if msg:
            # Parse palette string "rgb,rgb,..." into list of ints
            palette_str = msg.get("palette", "")
            colors = []
            if palette_str:
                for entry in palette_str.split(","):
                    entry = entry.strip()
                    if len(entry) >= 3:
                        r = int(entry[0], 16)
                        g = int(entry[1], 16)
                        b = int(entry[2], 16)
                        colors.append((r << 8) | (g << 4) | b)
                    else:
                        colors.append(0)
            return JSONResponse({"depth": msg.get("depth"), "colors": colors})
        return JSONResponse({"error": "No response"})

    async def api_tool_copper(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "COPPERLIST"})
        # Collect all COPPER chunks (bridge sends in multiple messages)
        all_hex = ""
        base_addr = 0
        deadline = asyncio.get_event_loop().time() + 5.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            msg = await _event_bus.wait_for("copper", timeout=remaining)
            if not msg:
                break
            hex_data = msg.get("hexData", "")
            if not all_hex:
                addr_str = msg.get("address", "0")
                base_addr = int(addr_str, 16) if isinstance(addr_str, str) else addr_str
            all_hex += hex_data
        if not all_hex:
            return JSONResponse({"error": "No copper list data"})
        try:
            from .copper import decode_copper_list
            listing = decode_copper_list(all_hex, base_addr)
            return JSONResponse({"listing": listing})
        except Exception as e:
            return JSONResponse({"error": str(e)})

    async def api_tool_sprites(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "SPRITES"})
        lines = []
        deadline = asyncio.get_event_loop().time() + 5.0
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            msg = await _event_bus.wait_for("sprite", timeout=remaining)
            if msg:
                lines.append(f"Sprite {msg.get('id',0)}: VSTART={msg.get('vstart',0)} VSTOP={msg.get('vstop',0)} HSTART={msg.get('hstart',0)} ATT={msg.get('attached',0)}")
            else:
                break
        return JSONResponse({"listing": "\n".join(lines) if lines else "No sprite data"})

    async def api_tool_disasm(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        addr_str = request.query_params.get("address", "0")
        count = int(request.query_params.get("count", "20"))
        project = request.query_params.get("project", "")
        addr = int(addr_str.replace("$", "").replace("0x", ""), 16)
        size = min(count * 10, 4096)
        _conn.send({"type": "INSPECT", "address": f"{addr:08X}", "size": str(size)})
        msg = await _event_bus.wait_for("mem", timeout=5.0)
        if msg:
            try:
                from .disasm import disassemble_hex, format_listing
                from . import symbols as sym_mod
                hex_data = msg.get("hexData", "")
                result = disassemble_hex(hex_data, addr, count)
                # Auto-detect project from loaded symbols if not specified
                if not project:
                    for name, table in sym_mod.get_all_tables().items():
                        if table.lookup_address(addr):
                            project = name
                            break
                listing = format_listing(result, project=project or None)
                return JSONResponse({"listing": listing})
            except Exception as e:
                return JSONResponse({"error": str(e)})
        return JSONResponse({"error": "No memory data response"})

    async def api_symbols_load(request: Request) -> JSONResponse:
        """Load symbols for a project."""
        body = await request.json()
        project = body.get("project", "")
        if not project:
            return JSONResponse({"error": "Missing project name"}, status_code=400)
        from . import symbols
        table = await symbols.load_symbols(project)
        if not table.symbols:
            return JSONResponse({"error": f"No symbols found for '{project}'"}, status_code=404)
        funcs = [s for s in table.symbols if s.sym_type in ("T", "t")]
        data_syms = [s for s in table.symbols if s.sym_type in ("D", "d", "B", "b")]
        return JSONResponse({
            "project": project,
            "binary": table.binary_path,
            "total": len(table.symbols),
            "functions": len(funcs),
            "data": len(data_syms),
            "sourceLines": len(table.source_lines),
            "structs": list(table.struct_types.keys()),
            "funcSources": len(table.func_source),
        })

    async def api_symbols_lookup(request: Request) -> JSONResponse:
        """Look up a symbol by address."""
        addr_str = request.query_params.get("address", "0")
        project = request.query_params.get("project", "")
        addr = int(addr_str.replace("$", "").replace("0x", ""), 16)
        from . import symbols
        ann = symbols.annotate_address_full(addr, project or None)
        return JSONResponse(ann)

    async def api_symbols_functions(request: Request) -> JSONResponse:
        """List functions for a project."""
        project = request.query_params.get("project", "")
        if not project:
            return JSONResponse({"error": "Missing project"}, status_code=400)
        from . import symbols
        funcs = symbols.list_functions(project)
        return JSONResponse({"project": project, "functions": funcs})

    async def api_symbols_structs(request: Request) -> JSONResponse:
        """List struct types for a project."""
        project = request.query_params.get("project", "")
        if not project:
            return JSONResponse({"error": "Missing project"}, status_code=400)
        from . import symbols
        structs = symbols.list_structs(project)
        return JSONResponse({"project": project, "structs": structs})

    async def api_symbols_loaded(request: Request) -> JSONResponse:
        """List all currently loaded symbol tables."""
        from . import symbols
        tables = symbols.get_all_tables()
        result = []
        for name, table in tables.items():
            funcs = [s for s in table.symbols if s.sym_type in ("T", "t")]
            result.append({
                "project": name,
                "symbols": len(table.symbols),
                "functions": len(funcs),
                "sourceLines": len(table.source_lines),
                "structs": list(table.struct_types.keys()),
                "hasDebugInfo": len(table.source_lines) > 0,
            })
        return JSONResponse({"tables": result})

    async def api_tool_crash(request: Request) -> JSONResponse:
        if _state and _state.last_crash:
            from . import symbols as sym_mod
            c = _state.last_crash
            alert_num = c.get('alertNum', '00000000')
            alert_name = c.get('alertName', 'Unknown')
            alert_detail = _decode_alert(alert_num)

            report = f"Alert: ${alert_num} ({alert_name})\n"
            report += f"Type: {alert_detail}\n"
            dregs = c.get('dataRegs', [])
            aregs = c.get('addrRegs', [])
            if dregs:
                report += "D0-D7: " + " ".join(dregs) + "\n"
            if aregs:
                report += "A0-A7: " + " ".join(aregs) + "\n"
            sp = c.get('sp', '00000000')
            report += f"SP: ${sp}\n"

            pc_addr = None
            if aregs and len(aregs) > 5:
                for reg_idx in [5, 4, 3, 2, 1, 0]:
                    try:
                        addr = int(aregs[reg_idx], 16)
                        if 0x1000 < addr < 0x10000000:
                            pc_addr = aregs[reg_idx]
                            break
                    except (ValueError, IndexError):
                        pass

            # Symbolic annotations for address registers
            reg_symbols = {}
            for i, val in enumerate(aregs):
                try:
                    addr = int(val, 16)
                    sym = sym_mod.annotate_address(addr)
                    if sym:
                        src = sym_mod.source_line_for_address(addr)
                        reg_symbols[f"A{i}"] = {"symbol": sym, "source": src} if src else {"symbol": sym}
                except (ValueError, TypeError):
                    pass

            # Symbolic stack trace
            stack = c.get('stackHex', '')
            stack_trace = []
            if stack:
                groups = [stack[i:i+8] for i in range(0, len(stack), 8)]
                report += "Stack: " + " ".join(groups) + "\n"
                if len(stack) >= 8 and pc_addr is None:
                    pc_addr = stack[:8]

                # Resolve stack entries as potential return addresses
                for i in range(0, min(len(stack), 64), 8):
                    word_hex = stack[i:i + 8]
                    if len(word_hex) < 8:
                        break
                    addr = int(word_hex, 16)
                    if addr > 0x1000 and addr < 0x10000000:
                        sym = sym_mod.annotate_address(addr)
                        if sym:
                            src = sym_mod.source_line_for_address(addr)
                            entry = {"offset": i // 2, "address": word_hex, "symbol": sym}
                            if src:
                                entry["source"] = src
                            stack_trace.append(entry)

            return JSONResponse({
                "report": report,
                "crash": c,
                "alertDetail": alert_detail,
                "pcAddr": pc_addr,
                "regSymbols": reg_symbols,
                "stackTrace": stack_trace,
            })
        return JSONResponse({"report": "No crash recorded"})

    def _decode_alert(alert_hex: str) -> str:
        """Decode an AmigaOS alert number into human-readable description."""
        try:
            num = int(alert_hex, 16)
        except ValueError:
            return "Unknown alert"

        dead = "DEADEND" if (num & 0x80000000) else "RECOVERABLE"

        # Subsystem (bits 24-30)
        subsys_map = {
            0x00: "exec.library",
            0x01: "exec.library",
            0x02: "graphics.library",
            0x03: "layers.library",
            0x04: "intuition.library",
            0x05: "math#?.library",
            0x07: "dos.library",
            0x08: "ramlib",
            0x09: "icon.library",
            0x0A: "expansion.library",
            0x0B: "diskfont.library",
            0x10: "audio.device",
            0x11: "console.device",
            0x12: "gameport.device",
            0x13: "keyboard.device",
            0x14: "trackdisk.device",
            0x15: "timer.device",
            0x20: "cia.resource",
            0x21: "disk.resource",
            0x22: "misc.resource",
            0x30: "bootstrap",
            0x31: "workbench",
            0x32: "diskcopy",
        }
        subsys_code = (num >> 24) & 0x7F
        subsys = subsys_map.get(subsys_code, f"subsystem ${subsys_code:02X}")

        # General error type (bits 16-23)
        general_map = {
            0x01: "No memory",
            0x02: "Make library failed",
            0x03: "Open library failed",
            0x04: "Open device failed",
            0x05: "Open resource failed",
            0x06: "I/O error",
            0x07: "No signal",
            0x08: "Bad parameter",
            0x09: "Close library failed",
            0x0A: "Close device failed",
            0x0B: "Create process failed",
        }
        general_code = (num >> 16) & 0xFF
        general = general_map.get(general_code, f"error ${general_code:02X}")

        # Specific code (bits 0-15)
        specific = num & 0xFFFF

        return f"{dead} | {subsys} | {general} | specific=${specific:04X}"

    async def api_crash_enable(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "CRASHINIT"})
        msg = await _event_bus.wait_for("ok", timeout=5.0)
        return JSONResponse({"status": "Crash handler enabled"})

    async def api_crash_disable(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "CRASHREMOVE"})
        msg = await _event_bus.wait_for("ok", timeout=5.0)
        return JSONResponse({"status": "Crash handler disabled"})

    async def api_crashtest(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        # Auto-enable crash handler before testing
        _conn.send({"type": "CRASHINIT"})
        await _event_bus.wait_for("ok", timeout=3.0)
        _conn.send({"type": "CRASHTEST"})
        msg = await _event_bus.wait_for("crash", timeout=10.0)
        if msg:
            return JSONResponse({"status": "Crash captured", "crash": msg})
        return JSONResponse({"status": "Alert sent but no crash data received (guru may have been dismissed)"})

    async def api_tool_resources(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        client = request.query_params.get("client", "")
        if not client:
            return JSONResponse({"error": "Missing client parameter"})
        _conn.send({"type": "LISTRESOURCES", "client": client})
        msg = await _event_bus.wait_for("resources", timeout=5.0)
        if msg:
            return JSONResponse({"report": str(msg)})
        return JSONResponse({"error": "No response"})

    async def api_tool_perf(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        client = request.query_params.get("client", "")
        if not client:
            return JSONResponse({"error": "Missing client parameter"})
        _conn.send({"type": "GETPERF", "client": client})
        msg = await _event_bus.wait_for("perf", timeout=5.0)
        if msg:
            return JSONResponse({"report": str(msg)})
        return JSONResponse({"error": "No response"})

    async def api_projects(request: Request) -> JSONResponse:
        root = str(_builder._root) if _builder else "."
        examples_dir = Path(root) / "examples"
        projects = []
        if examples_dir.exists():
            for d in sorted(examples_dir.iterdir()):
                if d.is_dir() and (d / "Makefile").exists():
                    projects.append(d.name)
        return JSONResponse({"projects": projects})

    async def api_tool_create_project(request: Request) -> JSONResponse:
        body = await request.json()
        name = body.get("name", "")
        template = body.get("template", "window")
        if not name:
            return JSONResponse({"error": "Missing project name"}, status_code=400)
        try:
            from .scaffolder import create_project
            result = create_project(str(_builder._root) if _builder else ".", name, template)
            return JSONResponse({"message": result})
        except Exception as e:
            return JSONResponse({"error": str(e)})

    # ─── Library / Device List & Info ───

    async def api_tool_list_libs(request: Request) -> JSONResponse:
        """List all loaded libraries."""
        assert _conn is not None and _event_bus is not None
        if not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "LISTLIBS"})
        msg = await _event_bus.wait_for("libs", timeout=5.0)
        if msg:
            return JSONResponse({"libs": msg.get("libs", []), "count": msg.get("count", 0)})
        return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_list_devices(request: Request) -> JSONResponse:
        """List all loaded devices."""
        assert _conn is not None and _event_bus is not None
        if not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "LISTDEVS"})
        msg = await _event_bus.wait_for("devices", timeout=5.0)
        if msg:
            return JSONResponse({"devices": msg.get("devices", []), "count": msg.get("count", 0)})
        return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_libinfo(request: Request) -> JSONResponse:
        """Get detailed info about a specific library."""
        assert _conn is not None and _event_bus is not None
        if not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        body = await request.json()
        name = body.get("name", "")
        if not name:
            return JSONResponse({"error": "Missing library name"}, status_code=400)

        _conn.send({"type": "LIBINFO", "name": name})

        async with _event_bus.subscribe("libinfo", "err") as q:
            try:
                evt, data = await asyncio.wait_for(q.get(), timeout=5.0)
                if evt == "err" and data.get("context") == "LIBINFO":
                    return JSONResponse({"error": data.get("message", "Unknown error")}, status_code=404)
                return JSONResponse(data)
            except asyncio.TimeoutError:
                return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_devinfo(request: Request) -> JSONResponse:
        """Get detailed info about a specific device."""
        assert _conn is not None and _event_bus is not None
        if not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        body = await request.json()
        name = body.get("name", "")
        if not name:
            return JSONResponse({"error": "Missing device name"}, status_code=400)

        _conn.send({"type": "DEVINFO", "name": name})

        async with _event_bus.subscribe("devinfo", "err") as q:
            try:
                evt, data = await asyncio.wait_for(q.get(), timeout=5.0)
                if evt == "err" and data.get("context") == "DEVINFO":
                    return JSONResponse({"error": data.get("message", "Unknown error")}, status_code=404)
                return JSONResponse(data)
            except asyncio.TimeoutError:
                return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_libfuncs(request: Request) -> JSONResponse:
        """Get jump table entries for a library or device."""
        assert _conn is not None and _event_bus is not None
        if not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        name = request.query_params.get("name", "")
        libtype = request.query_params.get("type", "lib")
        start = int(request.query_params.get("start", "0"))
        if not name:
            return JSONResponse({"error": "Missing name"}, status_code=400)

        _conn.send({"type": "LIBFUNCS", "name": name, "libtype": libtype, "start": start})

        async with _event_bus.subscribe("libfuncs", "err") as q:
            try:
                evt, data = await asyncio.wait_for(q.get(), timeout=5.0)
                if evt == "err" and data.get("context") == "LIBFUNCS":
                    return JSONResponse({"error": data.get("message", "Unknown error")}, status_code=404)
                return JSONResponse(data)
            except asyncio.TimeoutError:
                return JSONResponse({"error": "Timeout"}, status_code=504)

    # ─── SnoopDos ───

    async def api_tool_snoop_start(request: Request) -> JSONResponse:
        """Start SnoopDos monitoring."""
        assert _conn is not None and _event_bus is not None
        if not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "SNOOPSTART"})
        async with _event_bus.subscribe("ok", "err") as q:
            deadline = asyncio.get_event_loop().time() + 5.0
            while True:
                remaining = deadline - asyncio.get_event_loop().time()
                if remaining <= 0:
                    return JSONResponse({"error": "Timeout"}, status_code=504)
                try:
                    evt, data = await asyncio.wait_for(q.get(), timeout=remaining)
                    ctx = data.get("context", "")
                    if ctx == "SNOOPSTART":
                        if evt == "err":
                            return JSONResponse({"error": data.get("message")}, status_code=500)
                        return JSONResponse({"status": "started"})
                    # Not our response, keep waiting
                except asyncio.TimeoutError:
                    return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_snoop_stop(request: Request) -> JSONResponse:
        """Stop SnoopDos monitoring."""
        assert _conn is not None and _event_bus is not None
        if not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "SNOOPSTOP"})
        async with _event_bus.subscribe("ok", "err") as q:
            deadline = asyncio.get_event_loop().time() + 5.0
            while True:
                remaining = deadline - asyncio.get_event_loop().time()
                if remaining <= 0:
                    return JSONResponse({"error": "Timeout"}, status_code=504)
                try:
                    evt, data = await asyncio.wait_for(q.get(), timeout=remaining)
                    ctx = data.get("context", "")
                    if ctx == "SNOOPSTOP":
                        if evt == "err":
                            return JSONResponse({"error": data.get("message")}, status_code=500)
                        return JSONResponse({"status": "stopped"})
                except asyncio.TimeoutError:
                    return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_snoop_status(request: Request) -> JSONResponse:
        """Get SnoopDos monitoring status."""
        assert _conn is not None and _event_bus is not None
        if not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "SNOOPSTATUS"})
        async with _event_bus.subscribe("snoopstate") as q:
            try:
                _, data = await asyncio.wait_for(q.get(), timeout=5.0)
                return JSONResponse(data)
            except asyncio.TimeoutError:
                return JSONResponse({"error": "Timeout"}, status_code=504)

    # ─── Audio Inspector ───

    async def api_tool_audio_channels(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "AUDIOCHANNELS"})
        msg = await _event_bus.wait_for("audiochannels", timeout=5.0)
        if msg:
            return JSONResponse(msg)
        return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_audio_sample(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        address = request.query_params.get("address", "0")
        size = request.query_params.get("size", "256")
        _conn.send({"type": "AUDIOSAMPLE", "address": address, "size": int(size)})
        async with _event_bus.subscribe("audiosample", "err") as q:
            try:
                evt, data = await asyncio.wait_for(q.get(), timeout=5.0)
                if evt == "err":
                    return JSONResponse({"error": data.get("message", "?")}, status_code=500)
                return JSONResponse(data)
            except asyncio.TimeoutError:
                return JSONResponse({"error": "Timeout"}, status_code=504)

    # ─── Intuition Inspector ───

    async def api_tool_screens(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "LISTSCREENS"})
        msg = await _event_bus.wait_for("screens", timeout=5.0)
        if msg:
            return JSONResponse(msg)
        return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_screen_windows(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        screen = request.query_params.get("screen", "")
        _conn.send({"type": "LISTWINDOWS2", "screen": screen})
        async with _event_bus.subscribe("windows", "err") as q:
            try:
                evt, data = await asyncio.wait_for(q.get(), timeout=5.0)
                if evt == "err":
                    return JSONResponse({"error": data.get("message", "?")}, status_code=500)
                return JSONResponse(data)
            except asyncio.TimeoutError:
                return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_gadgets(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        window = request.query_params.get("window", "")
        if not window:
            return JSONResponse({"error": "Missing window address"}, status_code=400)
        _conn.send({"type": "LISTGADGETS", "window": window})
        async with _event_bus.subscribe("gadgets", "err") as q:
            try:
                evt, data = await asyncio.wait_for(q.get(), timeout=5.0)
                if evt == "err":
                    return JSONResponse({"error": data.get("message", "?")}, status_code=500)
                return JSONResponse(data)
            except asyncio.TimeoutError:
                return JSONResponse({"error": "Timeout"}, status_code=504)

    # ─── Input Injection ───

    async def api_tool_input_key(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        body = await request.json()
        rawkey = body.get("rawkey", "0")
        direction = body.get("direction", "down")
        _conn.send({"type": "INPUTKEY", "rawkey": rawkey, "direction": direction})
        msg = await _event_bus.wait_for("ok", timeout=5.0)
        if msg:
            return JSONResponse({"status": "ok", "message": msg.get("message", "")})
        return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_input_move(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        body = await request.json()
        dx = body.get("dx", 0)
        dy = body.get("dy", 0)
        _conn.send({"type": "INPUTMOVE", "dx": dx, "dy": dy})
        msg = await _event_bus.wait_for("ok", timeout=5.0)
        if msg:
            return JSONResponse({"status": "ok", "message": msg.get("message", "")})
        return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_input_click(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        body = await request.json()
        button = body.get("button", "left")
        direction = body.get("direction", "down")
        _conn.send({"type": "INPUTCLICK", "button": button, "direction": direction})
        msg = await _event_bus.wait_for("ok", timeout=5.0)
        if msg:
            return JSONResponse({"status": "ok", "message": msg.get("message", "")})
        return JSONResponse({"error": "Timeout"}, status_code=504)

    # ─── Window/Screen Management ───

    async def _win_action(request: Request, cmd_type: str, key: str = "window") -> JSONResponse:
        """Generic handler for window/screen management commands that return OK."""
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        body = await request.json()
        cmd = {"type": cmd_type}
        cmd.update(body)
        _conn.send(cmd)
        msg = await _event_bus.wait_for("ok", timeout=5.0)
        if msg:
            return JSONResponse({"status": "ok", "message": msg.get("message", "")})
        return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_win_activate(request: Request) -> JSONResponse:
        return await _win_action(request, "WINACTIVATE")

    async def api_tool_win_tofront(request: Request) -> JSONResponse:
        return await _win_action(request, "WINTOFRONT")

    async def api_tool_win_toback(request: Request) -> JSONResponse:
        return await _win_action(request, "WINTOBACK")

    async def api_tool_win_zip(request: Request) -> JSONResponse:
        return await _win_action(request, "WINZIP")

    async def api_tool_win_move(request: Request) -> JSONResponse:
        return await _win_action(request, "WINMOVE")

    async def api_tool_win_size(request: Request) -> JSONResponse:
        return await _win_action(request, "WINSIZE")

    async def api_tool_scr_tofront(request: Request) -> JSONResponse:
        return await _win_action(request, "SCRTOFRONT")

    async def api_tool_scr_toback(request: Request) -> JSONResponse:
        return await _win_action(request, "SCRTOBACK")

    # ─── New Tool Endpoints ───

    async def api_tool_memory_search(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        address = request.query_params.get("address", "0")
        size = request.query_params.get("size", "65536")
        pattern = request.query_params.get("pattern", "")
        search_type = request.query_params.get("type", "hex")

        if search_type == "ascii":
            # Convert ASCII to hex
            pattern = pattern.encode("latin-1").hex()

        _conn.send({"type": "SEARCH", "address": address, "size": size, "pattern": pattern})
        async with _event_bus.subscribe("search", "err") as q:
            try:
                evt, data = await asyncio.wait_for(q.get(), timeout=10.0)
                if evt == "err":
                    return JSONResponse({"error": data.get("message", "Unknown error")}, status_code=500)
                return JSONResponse({"count": data["count"], "addresses": data["addresses"]})
            except asyncio.TimeoutError:
                return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_bitmap(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        address = request.query_params.get("address", "0")
        width = int(request.query_params.get("width", "320"))
        height = int(request.query_params.get("height", "256"))
        depth = int(request.query_params.get("depth", "5"))

        bytes_per_row = (width + 15) // 16 * 2  # word-aligned
        total_size = bytes_per_row * height * depth

        # Read memory - collect chunks
        chunks: list[dict] = []
        async with _event_bus.subscribe("mem", "err") as queue:
            try:
                _conn.send({"type": "INSPECT", "address": address, "size": str(total_size)})
            except Exception as e:
                return JSONResponse({"error": str(e)})

            deadline = asyncio.get_event_loop().time() + 15.0
            while True:
                remaining = deadline - asyncio.get_event_loop().time()
                if remaining <= 0:
                    break
                try:
                    evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                    if evt == "err":
                        return JSONResponse({"error": data.get("message", "Memory read error")})
                    if evt == "mem":
                        chunks.append(data)
                        received = sum(c["size"] for c in chunks)
                        if received >= total_size:
                            break
                except asyncio.TimeoutError:
                    break

        if not chunks:
            return JSONResponse({"error": "No memory response"})

        all_hex = "".join(c["hexData"] for c in chunks)
        if not all_hex:
            return JSONResponse({"error": "Empty memory data"})

        raw_bytes = bytes.fromhex(all_hex)

        # Convert planar to chunky using screenshot module
        from .screenshot import planar_to_chunky, render_png

        # Organize into planes_data[row][plane] = bytes
        planes_data: list[list[bytes]] = []
        for y in range(height):
            row_planes: list[bytes] = []
            for p in range(depth):
                offset = (y * depth + p) * bytes_per_row
                row_planes.append(raw_bytes[offset:offset + bytes_per_row])
            planes_data.append(row_planes)

        pixel_indices = planar_to_chunky(width, height, depth, planes_data)

        # Use a default Amiga palette (Workbench 2.0 style)
        default_palette = [
            (170, 170, 170), (0, 0, 0), (255, 255, 255), (102, 136, 187),
            (255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0),
            (255, 0, 255), (0, 255, 255), (170, 85, 0), (255, 170, 85),
            (85, 85, 85), (170, 170, 170), (187, 187, 187), (221, 221, 221),
        ]
        # Extend palette to cover all possible color indices
        while len(default_palette) < (1 << depth):
            default_palette.append((0, 0, 0))

        png_data = render_png(width, height, pixel_indices, default_palette)

        import datetime
        ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"bitmap_{ts}.png"
        ext = ".png" if png_data[:4] == b"\x89PNG" else ".ppm"
        if ext != ".png":
            filename = filename.rsplit(".", 1)[0] + ext
        save_path = str(_screenshot_dir / filename)
        with open(save_path, "wb") as f:
            f.write(png_data)

        return JSONResponse({
            "path": save_path,
            "filename": filename,
            "viewUrl": f"/api/screenshot/view?file={filename}",
            "width": width,
            "height": height,
            "depth": depth,
        })

    async def api_tool_memmap(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "MEMMAP"})
        msg = await _event_bus.wait_for("memmap", timeout=5.0)
        if msg:
            return JSONResponse({"count": msg["count"], "regions": msg["regions"]})
        return JSONResponse({"error": "No response"})

    async def api_tool_stackinfo(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        task = request.query_params.get("task", "")
        if not task:
            return JSONResponse({"error": "Missing task parameter"}, status_code=400)
        _conn.send({"type": "STACKINFO", "task": task})
        msg = await _event_bus.wait_for("stackinfo", timeout=5.0)
        if msg:
            return JSONResponse({
                "task": msg["task"],
                "spLower": msg["spLower"],
                "spUpper": msg["spUpper"],
                "spReg": msg["spReg"],
                "size": msg["size"],
                "used": msg["used"],
                "free": msg["free"],
            })
        return JSONResponse({"error": "No response"})

    async def api_tool_chipregs(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "CHIPREGS"})
        msg = await _event_bus.wait_for("chipregs", timeout=5.0)
        if msg:
            return JSONResponse({"registers": msg["registers"]})
        return JSONResponse({"error": "No response"})

    async def api_tool_readregs(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "READREGS"})
        msg = await _event_bus.wait_for("regs", timeout=5.0)
        if msg:
            return JSONResponse({"registers": msg["registers"]})
        return JSONResponse({"error": "No response"})

    async def api_tool_iff(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        file_path = request.query_params.get("path", "")
        if not file_path:
            return JSONResponse({"error": "Missing path parameter"}, status_code=400)

        # Read file in chunks
        all_hex = ""
        offset = 0
        chunk_size = 4096
        while True:
            _conn.send({"type": "READFILE", "path": file_path, "offset": offset, "size": chunk_size})
            msg = await _event_bus.wait_for(
                "file", timeout=5.0,
                predicate=lambda d: d.get("path") == file_path,
            )
            if not msg:
                break
            hex_data = msg.get("hexData", "")
            if not hex_data:
                break
            all_hex += hex_data
            file_size = msg.get("size", 0)
            offset += chunk_size
            if offset >= file_size:
                break

        if not all_hex:
            return JSONResponse({"error": "Could not read file"})

        try:
            data = bytes.fromhex(all_hex)
            width, height, depth_val, palette, body_pixels = _decode_iff_ilbm(data)

            from .screenshot import render_png
            png_data = render_png(width, height, body_pixels, palette)

            import datetime
            ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
            basename = file_path.split(":")[-1].split("/")[-1].replace(".", "_")
            filename = f"iff_{basename}_{ts}.png"
            ext = ".png" if png_data[:4] == b"\x89PNG" else ".ppm"
            if ext != ".png":
                filename = filename.rsplit(".", 1)[0] + ext
            save_path = str(_screenshot_dir / filename)
            with open(save_path, "wb") as f:
                f.write(png_data)

            return JSONResponse({
                "path": save_path,
                "filename": filename,
                "viewUrl": f"/api/screenshot/view?file={filename}",
                "width": width,
                "height": height,
                "depth": depth_val,
            })
        except Exception as e:
            return JSONResponse({"error": f"IFF decode failed: {e}"})

    def _decode_iff_ilbm(data: bytes) -> tuple:
        """Decode IFF ILBM file, return (width, height, depth, palette, pixel_indices)."""
        import struct
        if len(data) < 12:
            raise ValueError("File too small for IFF")
        form_id = data[0:4]
        if form_id != b"FORM":
            raise ValueError(f"Not an IFF file (got {form_id!r})")
        form_type = data[8:12]
        if form_type != b"ILBM":
            raise ValueError(f"Not ILBM (got {form_type!r})")

        # Parse chunks
        bmhd = None
        cmap_colors: list[tuple[int, int, int]] = []
        body_data = b""
        pos = 12
        while pos < len(data) - 8:
            chunk_id = data[pos:pos + 4]
            chunk_size = struct.unpack(">I", data[pos + 4:pos + 8])[0]
            chunk_data = data[pos + 8:pos + 8 + chunk_size]

            if chunk_id == b"BMHD":
                if len(chunk_data) >= 20:
                    w, h, x, y, planes, masking, compression, pad, trans, xa, ya, pw, ph = struct.unpack(
                        ">HHhhBBBBHBBHH", chunk_data[:20]
                    )
                    bmhd = {
                        "width": w, "height": h, "x": x, "y": y,
                        "planes": planes, "masking": masking,
                        "compression": compression, "transparentColor": trans,
                    }
            elif chunk_id == b"CMAP":
                for i in range(0, len(chunk_data), 3):
                    if i + 2 < len(chunk_data):
                        cmap_colors.append((chunk_data[i], chunk_data[i + 1], chunk_data[i + 2]))
            elif chunk_id == b"BODY":
                body_data = chunk_data

            # Chunks are word-aligned
            pos += 8 + chunk_size + (chunk_size & 1)

        if not bmhd:
            raise ValueError("No BMHD chunk found")

        width = bmhd["width"]
        height = bmhd["height"]
        depth = bmhd["planes"]
        compression = bmhd["compression"]
        masking = bmhd["masking"]

        # Decompress body if ByteRun1
        if compression == 1:
            body_data = _byterun1_decompress(body_data)

        # Convert planar body to pixel indices
        bytes_per_row = ((width + 15) // 16) * 2
        total_planes = depth + (1 if masking == 1 else 0)

        from .screenshot import planar_to_chunky

        planes_data: list[list[bytes]] = []
        for y in range(height):
            row_planes: list[bytes] = []
            for p in range(depth):
                row_offset = (y * total_planes + p) * bytes_per_row
                row_planes.append(body_data[row_offset:row_offset + bytes_per_row])
            planes_data.append(row_planes)

        pixel_indices = planar_to_chunky(width, height, depth, planes_data)

        # Default palette if CMAP missing
        if not cmap_colors:
            cmap_colors = [(0, 0, 0)] * (1 << depth)
        while len(cmap_colors) < (1 << depth):
            cmap_colors.append((0, 0, 0))

        return width, height, depth, cmap_colors, pixel_indices

    def _byterun1_decompress(data: bytes) -> bytes:
        """Decompress ByteRun1 packed data."""
        result = bytearray()
        i = 0
        while i < len(data):
            n = data[i]
            if n > 127:
                n = n - 256  # convert to signed
            i += 1
            if n >= 0:
                # Copy n+1 bytes literally
                count = n + 1
                result.extend(data[i:i + count])
                i += count
            elif n == -128:
                # NOP
                pass
            else:
                # Repeat next byte (-n+1) times
                count = -n + 1
                if i < len(data):
                    result.extend(bytes([data[i]]) * count)
                    i += 1
        return bytes(result)

    _snapshots: list[dict] = []

    async def api_tool_snapshot(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        body = await request.json()
        action = body.get("action", "list")

        if action == "list":
            return JSONResponse({"snapshots": [
                {"id": i, "name": s["name"], "address": s["address"],
                 "size": s["size"], "timestamp": s["timestamp"]}
                for i, s in enumerate(_snapshots)
            ]})

        if action == "clear":
            _snapshots.clear()
            return JSONResponse({"message": "Snapshots cleared"})

        if action == "take":
            address = body.get("address", "0")
            size = int(body.get("size", 256))
            name = body.get("name", f"snap_{len(_snapshots)}")

            # Read memory
            chunks: list[dict] = []
            async with _event_bus.subscribe("mem", "err") as queue:
                try:
                    _conn.send({"type": "INSPECT", "address": address, "size": str(size)})
                except Exception as e:
                    return JSONResponse({"error": str(e)})

                deadline = asyncio.get_event_loop().time() + 15.0
                while True:
                    remaining = deadline - asyncio.get_event_loop().time()
                    if remaining <= 0:
                        break
                    try:
                        evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                        if evt == "err":
                            return JSONResponse({"error": data.get("message", "Memory read error")})
                        if evt == "mem":
                            chunks.append(data)
                            received = sum(c["size"] for c in chunks)
                            if received >= size:
                                break
                    except asyncio.TimeoutError:
                        break

            if not chunks:
                return JSONResponse({"error": "No memory response"})

            all_hex = "".join(c["hexData"] for c in chunks)
            snap_data = bytes.fromhex(all_hex)
            snap_id = len(_snapshots)
            _snapshots.append({
                "id": snap_id,
                "name": name,
                "address": address,
                "size": len(snap_data),
                "data": snap_data,
                "timestamp": time.time(),
            })
            return JSONResponse({"id": snap_id, "name": name, "size": len(snap_data)})

        if action == "diff":
            id1 = int(body.get("id1", 0))
            id2 = int(body.get("id2", 1))
            if id1 >= len(_snapshots) or id2 >= len(_snapshots):
                return JSONResponse({"error": "Invalid snapshot ID"})
            s1 = _snapshots[id1]
            s2 = _snapshots[id2]
            min_len = min(len(s1["data"]), len(s2["data"]))
            changes = []
            for i in range(min_len):
                if s1["data"][i] != s2["data"][i]:
                    changes.append({
                        "offset": i,
                        "old": f"{s1['data'][i]:02x}",
                        "new": f"{s2['data'][i]:02x}",
                    })
            return JSONResponse({
                "snap1": {"id": id1, "name": s1["name"]},
                "snap2": {"id": id2, "name": s2["name"]},
                "changeCount": len(changes),
                "changes": changes[:1000],  # limit to first 1000 changes
            })

        return JSONResponse({"error": f"Unknown action: {action}"})

    async def api_tool_bootlog(request: Request) -> JSONResponse:
        assert _state is not None
        count = int(request.query_params.get("count", "100"))
        logs = _state.get_recent_logs(count=len(_state.logs))
        # Return earliest logs (boot time)
        boot_logs = logs[:count]
        return JSONResponse({"logs": [
            {
                "level": l.get("level"),
                "tick": l.get("tick"),
                "message": l.get("message"),
                "timestamp": l.get("timestamp"),
                "client": l.get("client"),
            }
            for l in boot_logs
        ]})

    async def api_tool_sysinfo(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})

        result: dict[str, Any] = {}

        # Ping
        try:
            _conn.send({"type": "PING"})
            pong = await _event_bus.wait_for("pong", timeout=2.0)
            if pong:
                result["clients"] = pong.get("clientCount", 0)
                result["freeChip"] = pong.get("freeChip", 0)
                result["freeFast"] = pong.get("freeFast", 0)
        except Exception:
            pass

        # Tasks
        try:
            _conn.send({"type": "LISTTASKS"})
            tasks = await _event_bus.wait_for("tasks", timeout=2.0)
            if tasks:
                result["taskCount"] = len(tasks.get("tasks", []))
                result["tasks"] = tasks.get("tasks", [])
        except Exception:
            pass

        # Libraries
        try:
            _conn.send({"type": "LISTLIBS"})
            libs = await _event_bus.wait_for("libs", timeout=2.0)
            if libs:
                result["libCount"] = len(libs.get("libs", []))
                result["libs"] = libs.get("libs", [])
        except Exception:
            pass

        # Volumes
        try:
            _conn.send({"type": "LISTVOLUMES"})
            vols = await _event_bus.wait_for("volumes", timeout=2.0)
            if vols:
                result["volumes"] = vols.get("names", [])
        except Exception:
            pass

        return JSONResponse(result)

    # ─── Files Tab Endpoints ───

    async def api_file_execute(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        body = await request.json()
        file_path = body.get("path", "")
        if not file_path:
            return JSONResponse({"error": "Missing path"}, status_code=400)

        cmd_id = int(time.time() * 1000) % 100000
        async with _event_bus.subscribe("cmd") as queue:
            try:
                _conn.send({"type": "LAUNCH", "id": cmd_id, "command": file_path})
            except Exception as e:
                return JSONResponse({"error": str(e)})

            deadline = asyncio.get_event_loop().time() + 10.0
            while True:
                remaining = deadline - asyncio.get_event_loop().time()
                if remaining <= 0:
                    break
                try:
                    evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                    if data.get("id") == cmd_id:
                        return JSONResponse({
                            "status": data["status"],
                            "output": data.get("data", ""),
                        })
                except asyncio.TimeoutError:
                    break

        return JSONResponse({"status": "timeout", "output": "No response from Amiga"})

    async def api_file_view(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        file_path = request.query_params.get("path", "")
        mode = request.query_params.get("mode", "text")
        if not file_path:
            return JSONResponse({"error": "Missing path parameter"}, status_code=400)

        # Read file
        read_size = 8192
        _conn.send({"type": "READFILE", "path": file_path, "offset": 0, "size": read_size})
        msg = await _event_bus.wait_for(
            "file", timeout=5.0,
            predicate=lambda d: d.get("path") == file_path,
        )
        if not msg:
            return JSONResponse({"error": "No response"})

        hex_data = msg.get("hexData", "")
        file_size = msg.get("size", 0)

        if mode == "text":
            try:
                content = bytes.fromhex(hex_data).decode("latin-1")
                return JSONResponse({
                    "path": file_path,
                    "size": file_size,
                    "mode": "text",
                    "content": content,
                })
            except Exception as e:
                return JSONResponse({"error": f"Decode failed: {e}"})
        else:
            # Hex mode - return formatted hex dump
            from .protocol import format_hex_dump
            dump = format_hex_dump("00000000", hex_data)
            return JSONResponse({
                "path": file_path,
                "size": file_size,
                "mode": "hex",
                "content": dump,
            })

    async def api_file_save(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        body = await request.json()
        file_path = body.get("path", "")
        content = body.get("content", "")
        if not file_path:
            return JSONResponse({"error": "Missing path"}, status_code=400)

        hex_data = content.encode("latin-1").hex()
        async with _event_bus.subscribe("ok", "err") as queue:
            try:
                _conn.send({"type": "WRITEFILE", "path": file_path, "offset": 0, "hexData": hex_data})
            except Exception as e:
                return JSONResponse({"error": str(e)})

            deadline = asyncio.get_event_loop().time() + 5.0
            while True:
                remaining = deadline - asyncio.get_event_loop().time()
                if remaining <= 0:
                    break
                try:
                    evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                    ctx = data.get("context", "")
                    if "WRITEFILE" in ctx or "FILE" in ctx:
                        return JSONResponse({
                            "status": "ok" if evt == "ok" else "error",
                            "message": data.get("message", ""),
                        })
                except asyncio.TimeoutError:
                    break

        return JSONResponse({"status": "timeout"})

    async def api_file_run_with(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        body = await request.json()
        file_path = body.get("path", "")
        app = body.get("app", "")
        if not file_path or not app:
            return JSONResponse({"error": "Missing path or app"}, status_code=400)

        cmd_id = int(time.time() * 1000) % 100000
        command = f"{app} {file_path}"
        async with _event_bus.subscribe("cmd") as queue:
            try:
                _conn.send({"type": "LAUNCH", "id": cmd_id, "command": command})
            except Exception as e:
                return JSONResponse({"error": str(e)})

            deadline = asyncio.get_event_loop().time() + 10.0
            while True:
                remaining = deadline - asyncio.get_event_loop().time()
                if remaining <= 0:
                    break
                try:
                    evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                    if data.get("id") == cmd_id:
                        return JSONResponse({
                            "status": data["status"],
                            "output": data.get("data", ""),
                        })
                except asyncio.TimeoutError:
                    break

        return JSONResponse({"status": "timeout", "output": "No response from Amiga"})

    # ─── Emulator Management Endpoints ───

    async def api_emulator_status(request: Request) -> JSONResponse:
        if not _emulator:
            return JSONResponse({"error": "Emulator manager not initialized"})
        return JSONResponse(_emulator.get_status())

    async def api_emulator_start(request: Request) -> JSONResponse:
        if not _emulator:
            return JSONResponse({"error": "Emulator manager not initialized"})
        if _emulator.is_running:
            return JSONResponse({"status": "already_running", "pid": _emulator.pid})
        ok = await _emulator.start()
        if not ok:
            return JSONResponse({"error": "Failed to start emulator"})

        # Wait for boot and auto-connect
        pid = _emulator.pid
        await asyncio.sleep(3.0)
        if _conn and not _conn.connected:
            try:
                await _conn.connect()
                logger.info("Auto-connected after emulator start")
            except Exception as e:
                logger.warning("Auto-connect failed after emulator start: %s", e)

        return JSONResponse({"status": "started", "pid": pid})

    async def api_emulator_stop(request: Request) -> JSONResponse:
        if not _emulator:
            return JSONResponse({"error": "Emulator manager not initialized"})
        if not _emulator.is_running:
            return JSONResponse({"status": "not_running"})
        # Disconnect serial first so it can reconnect after restart
        if _conn and _conn.connected:
            _conn.disconnect()
        await _emulator.stop()
        return JSONResponse({"status": "stopped"})

    async def api_emulator_restart(request: Request) -> JSONResponse:
        if not _emulator:
            return JSONResponse({"error": "Emulator manager not initialized"})
        # Disconnect serial
        if _conn and _conn.connected:
            _conn.disconnect()
        ok = await _emulator.restart()
        if ok:
            # Wait a bit then try reconnecting serial
            await asyncio.sleep(3.0)
            try:
                await _conn.connect()
            except Exception as e:
                logger.warning("Serial reconnect after restart failed: %s", e)
            return JSONResponse({"status": "restarted", "pid": _emulator.pid})
        return JSONResponse({"error": "Failed to restart emulator"})

    async def api_emulator_config_get(request: Request) -> JSONResponse:
        if not _emulator:
            return JSONResponse({"error": "Emulator manager not initialized"})
        content = _emulator.read_config()
        return JSONResponse({"config": content, "path": _emulator._config_file})

    async def api_emulator_config_save(request: Request) -> JSONResponse:
        if not _emulator:
            return JSONResponse({"error": "Emulator manager not initialized"})
        body = await request.json()
        content = body.get("config", "")
        if not content.strip():
            return JSONResponse({"error": "Empty config"})
        _emulator.write_config(content)
        return JSONResponse({"status": "saved", "path": _emulator._config_file})

    # ─── FS-UAE Remote-Debug RPC Endpoints ───
    # All endpoints return {"ok": ...} pass-through from the patched fs-uae
    # build, or {"ok": False, "err": "fsuae-rpc not available"} when the
    # stock binary is in use.

    async def api_fsuae_status(request: Request) -> JSONResponse:
        if not _fsuae_rpc:
            return JSONResponse({"status": "disabled", "enabled": "off"})
        return JSONResponse(_fsuae_rpc.snapshot())

    async def api_fsuae_probe(request: Request) -> JSONResponse:
        if not _fsuae_rpc:
            return JSONResponse({"ok": False, "err": "fsuae-rpc client not initialized"})
        await _fsuae_rpc.probe()
        return JSONResponse(_fsuae_rpc.snapshot())

    def _require_rpc() -> JSONResponse | None:
        if not _fsuae_rpc:
            return JSONResponse({"ok": False, "err": "fsuae-rpc client not initialized"})
        if not _fsuae_rpc.available:
            snap = _fsuae_rpc.snapshot()
            return JSONResponse({
                "ok": False,
                "err": f"fsuae-rpc not available (status={snap['status']}). "
                       "Launch the patched fs-uae build with FSUAE_RPC_PORT set, "
                       "or see https://github.com/geekychris/fsuae_remote_patch",
            })
        return None

    async def api_fsuae_cpu(request: Request) -> JSONResponse:
        err = _require_rpc()
        if err: return err
        return JSONResponse(await _fsuae_rpc.cpu())

    async def api_fsuae_state(request: Request) -> JSONResponse:
        err = _require_rpc()
        if err: return err
        return JSONResponse(await _fsuae_rpc.state())

    async def api_fsuae_pause(request: Request) -> JSONResponse:
        err = _require_rpc()
        if err: return err
        return JSONResponse(await _fsuae_rpc.pause())

    async def api_fsuae_resume(request: Request) -> JSONResponse:
        err = _require_rpc()
        if err: return err
        return JSONResponse(await _fsuae_rpc.resume())

    async def api_fsuae_step(request: Request) -> JSONResponse:
        err = _require_rpc()
        if err: return err
        n = int(request.query_params.get("n", "1"))
        mode = request.query_params.get("mode")
        return JSONResponse(await _fsuae_rpc.step(n=n, mode=mode))

    async def api_fsuae_reset(request: Request) -> JSONResponse:
        err = _require_rpc()
        if err: return err
        hard = request.query_params.get("hard", "1") != "0"
        return JSONResponse(await _fsuae_rpc.reset(hard=hard))

    async def api_fsuae_mem(request: Request) -> JSONResponse:
        err = _require_rpc()
        if err: return err
        if request.method == "POST":
            addr = request.query_params.get("addr", "")
            hex_bytes = request.query_params.get("hex", "")
            if not addr or not hex_bytes:
                return JSONResponse({"ok": False, "err": "missing addr or hex"})
            return JSONResponse(await _fsuae_rpc.mem_write(addr, hex_bytes))
        addr = request.query_params.get("addr", "")
        length = int(request.query_params.get("len", "64"))
        if not addr:
            return JSONResponse({"ok": False, "err": "missing addr"})
        return JSONResponse(await _fsuae_rpc.mem_read(addr, length))

    async def api_fsuae_disasm(request: Request) -> JSONResponse:
        err = _require_rpc()
        if err: return err
        addr = request.query_params.get("addr", "pc")
        count = int(request.query_params.get("count", "16"))
        annotate = request.query_params.get("annotate", "1") != "0"
        library = request.query_params.get("library")
        return JSONResponse(await _fsuae_rpc.disasm(addr=addr, count=count,
                                                    annotate=annotate, library=library))

    async def api_fsuae_custom(request: Request) -> JSONResponse:
        err = _require_rpc()
        if err: return err
        return JSONResponse(await _fsuae_rpc.custom())

    async def api_fsuae_memmap(request: Request) -> JSONResponse:
        err = _require_rpc()
        if err: return err
        return JSONResponse(await _fsuae_rpc.memmap())

    async def api_fsuae_stack(request: Request) -> JSONResponse:
        err = _require_rpc()
        if err: return err
        depth = int(request.query_params.get("depth", "32"))
        return JSONResponse(await _fsuae_rpc.stack(depth=depth))

    async def api_fsuae_breakpoints(request: Request) -> JSONResponse:
        err = _require_rpc()
        if err: return err
        if request.method == "GET":
            return JSONResponse(await _fsuae_rpc.bp_list())
        addr = request.query_params.get("addr", "")
        if not addr:
            return JSONResponse({"ok": False, "err": "missing addr"})
        skip = int(request.query_params.get("skip", "0"))
        oneshot = request.query_params.get("oneshot", "0") != "0"
        return JSONResponse(await _fsuae_rpc.bp_add(addr, skip=skip, oneshot=oneshot))

    async def api_fsuae_breakpoints_clear(request: Request) -> JSONResponse:
        err = _require_rpc()
        if err: return err
        return JSONResponse(await _fsuae_rpc.bp_clear())

    async def api_fsuae_bp_by_symbol(request: Request) -> JSONResponse:
        """Cross-debugger feature: install a fs-uae CPU breakpoint at the
        address of a function known to the bridge debugger's symbol tables.
        Zero ambient cost — pure translation; only invoked on user action.
        """
        err = _require_rpc()
        if err: return err
        name = request.query_params.get("name", "")
        project = request.query_params.get("project") or None
        if not name:
            return JSONResponse({"ok": False, "err": "missing name"})
        from . import symbols
        hit = symbols.lookup_function_address(name, project)
        if hit is None:
            return JSONResponse({
                "ok": False,
                "err": f"no symbol '{name}' in loaded projects — load symbols in the Debugger tab first",
            })
        resolved_project, addr = hit
        skip = int(request.query_params.get("skip", "0"))
        oneshot = request.query_params.get("oneshot", "0") != "0"
        body = await _fsuae_rpc.bp_add(f"0x{addr:08x}", skip=skip, oneshot=oneshot)
        body["symbol"] = name
        body["resolved_project"] = resolved_project
        return JSONResponse(body)

    async def api_fsuae_watchpoints(request: Request) -> JSONResponse:
        err = _require_rpc()
        if err: return err
        if request.method == "GET":
            return JSONResponse(await _fsuae_rpc.wp_list())
        addr = request.query_params.get("addr", "")
        if not addr:
            return JSONResponse({"ok": False, "err": "missing addr"})
        size = int(request.query_params.get("size", "1"))
        rwi = request.query_params.get("rwi", "RW")
        mustchange = request.query_params.get("mustchange", "0") != "0"
        val = request.query_params.get("val")
        valmask = request.query_params.get("valmask")
        return JSONResponse(await _fsuae_rpc.wp_add(
            addr, size=size, rwi=rwi, mustchange=mustchange, val=val, valmask=valmask,
        ))

    async def api_fsuae_watchpoints_clear(request: Request) -> JSONResponse:
        err = _require_rpc()
        if err: return err
        return JSONResponse(await _fsuae_rpc.wp_clear())

    async def api_fsuae_watchpoints_last(request: Request) -> JSONResponse:
        err = _require_rpc()
        if err: return err
        return JSONResponse(await _fsuae_rpc.wp_last())

    async def api_fsuae_state_save(request: Request) -> JSONResponse:
        err = _require_rpc()
        if err: return err
        path = request.query_params.get("path", "")
        if not path:
            return JSONResponse({"ok": False, "err": "missing path"})
        return JSONResponse(await _fsuae_rpc.state_save(path))

    async def api_fsuae_state_load(request: Request) -> JSONResponse:
        err = _require_rpc()
        if err: return err
        path = request.query_params.get("path", "")
        if not path:
            return JSONResponse({"ok": False, "err": "missing path"})
        return JSONResponse(await _fsuae_rpc.state_load(path))

    async def api_fsuae_symbol_lookup(request: Request) -> JSONResponse:
        err = _require_rpc()
        if err: return err
        addr = request.query_params.get("addr", "")
        if not addr:
            return JSONResponse({"ok": False, "err": "missing addr"})
        return JSONResponse(await _fsuae_rpc.symbol_lookup(addr))

    async def api_fsuae_fd_libraries(request: Request) -> JSONResponse:
        err = _require_rpc()
        if err: return err
        return JSONResponse(await _fsuae_rpc.fd_libraries())

    async def api_fsuae_fd_lookup(request: Request) -> JSONResponse:
        err = _require_rpc()
        if err: return err
        try:
            offset = int(request.query_params.get("offset", "0"))
        except ValueError:
            return JSONResponse({"ok": False, "err": "bad offset"})
        library = request.query_params.get("library", "exec")
        return JSONResponse(await _fsuae_rpc.fd_lookup(offset, library=library))

    async def api_fsuae_fd_load(request: Request) -> JSONResponse:
        err = _require_rpc()
        if err: return err
        path = request.query_params.get("path", "")
        library = request.query_params.get("library", "")
        if not path or not library:
            return JSONResponse({"ok": False, "err": "missing path or library"})
        return JSONResponse(await _fsuae_rpc.fd_load(path, library))

    async def api_fsuae_cpu_write(request: Request) -> JSONResponse:
        err = _require_rpc()
        if err: return err
        reg = request.query_params.get("reg", "")
        value = request.query_params.get("value", "")
        if not reg or value == "":
            return JSONResponse({"ok": False, "err": "missing reg or value"})
        return JSONResponse(await _fsuae_rpc.cpu_write(reg, value))

    # ─── Snapshot management ───
    # Slot-based wrapper around state_save/load — file paths are managed for
    # the user under ~/.amiga-devbench/snapshots/slot-{N}.uss. Plus a diff
    # endpoint that shells out to fsuae_remote_patch/tools/uss_diff.py if
    # we can find it, so users can compare two snapshots without leaving the UI.

    _SNAPSHOT_DIR = Path.home() / ".amiga-devbench" / "snapshots"
    _LABELS_FILE = _SNAPSHOT_DIR / ".labels.json"

    def _slot_path(slot: int) -> Path:
        return _SNAPSHOT_DIR / f"slot-{slot}.uss"

    def _load_labels() -> dict[str, str]:
        """Read the sidecar label map. Keys are 'slot-N' or 'auto-N'; values
        are user-set human labels. Returns {} if file missing or unreadable.
        Sidecar JSON lives alongside the .uss files so labels travel with
        them if the user copies the dir."""
        if not _LABELS_FILE.exists():
            return {}
        try:
            return json.loads(_LABELS_FILE.read_text())
        except (json.JSONDecodeError, OSError):
            return {}

    def _save_labels(labels: dict[str, str]) -> None:
        _SNAPSHOT_DIR.mkdir(parents=True, exist_ok=True)
        # Drop empty labels so the file stays clean
        cleaned = {k: v for k, v in labels.items() if v}
        _LABELS_FILE.write_text(json.dumps(cleaned, indent=2, sort_keys=True))

    def _find_uss_diff_tool() -> str | None:
        """Locate fsuae_remote_patch/tools/uss_diff.py. We don't ship our own
        diff because the .uss format is complex chunk-IFF — better to delegate."""
        candidates = [
            Path.home() / ".amiga-devbench" / "fsuae_remote_patch" / "tools" / "uss_diff.py",
            Path.home() / "code" / "fsuae_remote_patch" / "tools" / "uss_diff.py",
            Path("/Users/chris/code/claude_world/fsuae_remote_patch/tools/uss_diff.py"),
            Path("/tmp/fsuae-src/uss_diff.py"),
        ]
        for c in candidates:
            if c.is_file():
                return str(c)
        return None

    async def api_fsuae_snapshot_list(request: Request) -> JSONResponse:
        _SNAPSHOT_DIR.mkdir(parents=True, exist_ok=True)
        slots = []
        for n in range(1, 10):
            p = _slot_path(n)
            if p.exists():
                stat = p.stat()
                slots.append({
                    "slot": n,
                    "path": str(p),
                    "size": stat.st_size,
                    "mtime": stat.st_mtime,
                })
            else:
                slots.append({"slot": n, "path": str(p), "size": 0, "mtime": None})
        # Auto-snapshot ring: only include slots that exist on disk.
        auto = []
        for n in range(_auto_snap.ring_size):
            p = _SNAPSHOT_DIR / f"auto-{n}.uss"
            if p.exists():
                stat = p.stat()
                auto.append({
                    "slot": f"auto-{n}",
                    "path": str(p),
                    "size": stat.st_size,
                    "mtime": stat.st_mtime,
                })
        return JSONResponse({
            "ok": True,
            "dir": str(_SNAPSHOT_DIR),
            "slots": slots,
            "auto": auto,
            "labels": _load_labels(),
            "autosnap": {
                "enabled": _auto_snap.interval > 0,
                "interval": _auto_snap.interval,
                "ring_size": _auto_snap.ring_size,
            },
        })

    async def api_fsuae_snapshot_label(request: Request) -> JSONResponse:
        """Set or clear the user-facing label for a snapshot slot.

        slot is the key as it appears in the slots/auto lists: `slot-1` .. `slot-9`
        or `auto-0` .. `auto-N`. An empty label deletes the entry.
        Labels are stored in ~/.amiga-devbench/snapshots/.labels.json so they
        survive across devbench restarts and travel with the snapshot dir.
        """
        slot = request.query_params.get("slot", "").strip()
        label = request.query_params.get("label", "").strip()
        if not slot:
            return JSONResponse({"ok": False, "err": "missing slot"})
        # Validate slot name shape
        ok = False
        if slot.startswith("slot-"):
            try:
                n = int(slot[5:])
                ok = 1 <= n <= 9
            except ValueError:
                ok = False
        elif slot.startswith("auto-"):
            try:
                int(slot[5:])
                ok = True
            except ValueError:
                ok = False
        if not ok:
            return JSONResponse({"ok": False, "err": "slot must be slot-1..slot-9 or auto-N"})
        # Cap length so the sidecar file stays bounded
        if len(label) > 80:
            label = label[:80]
        labels = _load_labels()
        if label:
            labels[slot] = label
        else:
            labels.pop(slot, None)
        _save_labels(labels)
        return JSONResponse({"ok": True, "slot": slot, "label": label, "labels": labels})

    async def api_fsuae_snapshot_slot(request: Request) -> JSONResponse:
        err = _require_rpc()
        if err: return err
        try:
            slot = int(request.path_params.get("slot", "0"))
        except (TypeError, ValueError):
            return JSONResponse({"ok": False, "err": "bad slot"})
        if not (1 <= slot <= 9):
            return JSONResponse({"ok": False, "err": "slot must be 1..9"})
        action = request.path_params.get("action", "")
        _SNAPSHOT_DIR.mkdir(parents=True, exist_ok=True)
        path = str(_slot_path(slot))
        if action == "save":
            r = await _fsuae_rpc.state_save(path)
            r["slot"] = slot; r["path"] = path
            return JSONResponse(r)
        elif action == "load":
            if not _slot_path(slot).exists():
                return JSONResponse({"ok": False, "err": f"slot {slot} is empty"})
            r = await _fsuae_rpc.state_load(path)
            r["slot"] = slot; r["path"] = path
            return JSONResponse(r)
        return JSONResponse({"ok": False, "err": "action must be save or load"})

    # ─── Auto-snapshot ring buffer (opt-in) ───
    # Off by default. When `_auto_snap.interval > 0`, a background task saves
    # the emulator state to a rotating ring of slots every N seconds. Files
    # are named auto-0.uss .. auto-{ring_size-1}.uss, written round-robin.
    # Each save is ~19MB and stalls the emulator briefly, so the user has to
    # opt in explicitly via config OR by hitting the runtime-toggle endpoint.

    class _AutoSnapState:
        def __init__(self) -> None:
            self.interval: int = 0   # 0 = off
            self.ring_size: int = 5
            self.next_slot: int = 0
            self.task: asyncio.Task | None = None
            self.last_save: float = 0.0
            self.last_error: str | None = None
            self.saves_total: int = 0

    _auto_snap = _AutoSnapState()

    async def _auto_snap_loop() -> None:
        while _auto_snap.interval > 0:
            try:
                await asyncio.sleep(_auto_snap.interval)
                if _auto_snap.interval <= 0:  # cancelled between sleeps
                    break
                if not _fsuae_rpc or not _fsuae_rpc.available:
                    continue
                _SNAPSHOT_DIR.mkdir(parents=True, exist_ok=True)
                slot = _auto_snap.next_slot % max(1, _auto_snap.ring_size)
                path = str(_SNAPSHOT_DIR / f"auto-{slot}.uss")
                r = await _fsuae_rpc.state_save(path)
                if r.get("ok"):
                    _auto_snap.next_slot += 1
                    _auto_snap.last_save = time.time()
                    _auto_snap.saves_total += 1
                    _auto_snap.last_error = None
                else:
                    _auto_snap.last_error = r.get("err") or "save failed"
            except asyncio.CancelledError:
                break
            except Exception as e:
                _auto_snap.last_error = f"{type(e).__name__}: {e}"

    def _auto_snap_apply(interval: int, ring_size: int) -> None:
        """Apply interval/ring_size and start/stop the background task as needed."""
        if ring_size > 0:
            _auto_snap.ring_size = ring_size
        prev_interval = _auto_snap.interval
        _auto_snap.interval = max(0, interval)
        if _auto_snap.interval > 0 and (_auto_snap.task is None or _auto_snap.task.done()):
            _auto_snap.task = asyncio.ensure_future(_auto_snap_loop())
            logger.info("auto-snapshot: enabled (every %ds, ring %d)",
                        _auto_snap.interval, _auto_snap.ring_size)
        elif _auto_snap.interval == 0 and _auto_snap.task and not _auto_snap.task.done():
            _auto_snap.task.cancel()
            logger.info("auto-snapshot: disabled")
        if _auto_snap.interval != prev_interval and _event_bus:
            _event_bus.publish("fsuae_event", {
                "event": "auto_snapshot_config",
                "interval": _auto_snap.interval,
                "ring_size": _auto_snap.ring_size,
            })

    async def api_fsuae_autosnap_status(request: Request) -> JSONResponse:
        return JSONResponse({
            "ok": True,
            "interval": _auto_snap.interval,
            "ring_size": _auto_snap.ring_size,
            "next_slot": _auto_snap.next_slot % max(1, _auto_snap.ring_size),
            "last_save": _auto_snap.last_save,
            "last_error": _auto_snap.last_error,
            "saves_total": _auto_snap.saves_total,
            "enabled": _auto_snap.interval > 0,
        })

    async def api_fsuae_autosnap_set(request: Request) -> JSONResponse:
        try:
            interval = int(request.query_params.get("interval", "0"))
            ring_size = int(request.query_params.get("ring_size",
                                                     str(_auto_snap.ring_size)))
        except ValueError:
            return JSONResponse({"ok": False, "err": "bad interval or ring_size"})
        if interval < 0 or interval > 3600:
            return JSONResponse({"ok": False, "err": "interval must be 0..3600 seconds"})
        if not (1 <= ring_size <= 64):
            return JSONResponse({"ok": False, "err": "ring_size must be 1..64"})
        _auto_snap_apply(interval, ring_size)
        return await api_fsuae_autosnap_status(request)

    async def api_fsuae_snapshot_diff(request: Request) -> JSONResponse:
        a = request.query_params.get("a", "")
        b = request.query_params.get("b", "")
        if not a or not b:
            return JSONResponse({"ok": False, "err": "need ?a=path&b=path"})
        # Allow slot-N shorthand for convenience
        for label, val in (("a", a), ("b", b)):
            if val.startswith("slot-") and val[5:].isdigit():
                if label == "a":
                    a = str(_slot_path(int(val[5:])))
                else:
                    b = str(_slot_path(int(val[5:])))
        if not Path(a).exists() or not Path(b).exists():
            return JSONResponse({"ok": False, "err": f"snapshot not found: {a if not Path(a).exists() else b}"})

        tool = _find_uss_diff_tool()
        if tool:
            # Delegate to uss_diff.py — it understands the .uss chunk format.
            try:
                proc = await asyncio.create_subprocess_exec(
                    "python3", tool, a, b,
                    stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE,
                )
                stdout, stderr = await asyncio.wait_for(proc.communicate(), timeout=30.0)
                return JSONResponse({
                    "ok": proc.returncode == 0,
                    "via": "uss_diff.py",
                    "tool": tool,
                    "output": stdout.decode("utf-8", "replace"),
                    "error": stderr.decode("utf-8", "replace") if stderr else "",
                    "a": a, "b": b,
                })
            except Exception as e:
                return JSONResponse({"ok": False, "err": f"uss_diff.py failed: {e}"})

        # Fallback: dumb byte-level summary. Useful indicator, not authoritative.
        import hashlib
        def _summarize(p: str) -> dict:
            data = Path(p).read_bytes()
            return {
                "path": p,
                "size": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
        sum_a, sum_b = _summarize(a), _summarize(b)
        return JSONResponse({
            "ok": True,
            "via": "byte-summary (uss_diff.py not found in expected paths)",
            "a": sum_a, "b": sum_b,
            "identical": sum_a["sha256"] == sum_b["sha256"],
            "size_delta": sum_b["size"] - sum_a["size"],
            "hint": "install fsuae_remote_patch in ~/.amiga-devbench/ or ~/code/ for a structural diff",
        })

    async def api_devbench_config_get(request: Request) -> JSONResponse:
        if not _config:
            return JSONResponse({"error": "No config loaded"})
        return JSONResponse({
            "serial": {
                "mode": _config.serial_mode,
                "host": _config.serial_host,
                "port": _config.serial_port,
                "pty_path": _config.pty_path,
            },
            "emulator": {
                "binary": _config.emulator_binary,
                "config": _config.emulator_config,
                "auto_start": _config.emulator_auto_start,
            },
            "server": {
                "port": _config.server_port,
                "log_level": _config.log_level,
            },
            "paths": {
                "project_root": _config.project_root,
                "deploy_dir": _config.deploy_dir,
            },
            "bridge": {
                "crash_handler_auto_enable": _config.crash_handler_auto_enable,
            },
            "fsuae_rpc": {
                "enabled": _config.fsuae_rpc_enabled,
                "port": _config.fsuae_rpc_port,
                "pause_at_boot": _config.fsuae_rpc_pause_at_boot,
                "gdb_port": _config.fsuae_gdb_port,
                "auto_pause_on_crash": _config.fsuae_auto_pause_on_crash,
            },
        })

    async def api_devbench_config_save(request: Request) -> JSONResponse:
        if not _config:
            return JSONResponse({"error": "No config loaded"})
        body = await request.json()
        # Update config fields
        serial = body.get("serial", {})
        if "host" in serial:
            _config.serial_host = serial["host"]
        if "port" in serial:
            _config.serial_port = int(serial["port"])
        if "mode" in serial:
            _config.serial_mode = serial["mode"]
        emu = body.get("emulator", {})
        if "binary" in emu:
            _config.emulator_binary = emu["binary"]
        if "config" in emu:
            _config.emulator_config = emu["config"]
        if "auto_start" in emu:
            _config.emulator_auto_start = bool(emu["auto_start"])
        paths = body.get("paths", {})
        if "deploy_dir" in paths:
            _config.deploy_dir = paths["deploy_dir"]
        bridge = body.get("bridge", {})
        if "crash_handler_auto_enable" in bridge:
            _config.crash_handler_auto_enable = bool(bridge["crash_handler_auto_enable"])
        rpc = body.get("fsuae_rpc", {})
        if "enabled" in rpc:
            val = rpc["enabled"]
            if isinstance(val, bool):
                _config.fsuae_rpc_enabled = "on" if val else "off"
            else:
                _config.fsuae_rpc_enabled = str(val).lower()
        if "port" in rpc:
            _config.fsuae_rpc_port = int(rpc["port"])
        if "pause_at_boot" in rpc:
            _config.fsuae_rpc_pause_at_boot = bool(rpc["pause_at_boot"])
        if "gdb_port" in rpc:
            _config.fsuae_gdb_port = int(rpc["gdb_port"])
        if "auto_pause_on_crash" in rpc:
            _config.fsuae_auto_pause_on_crash = bool(rpc["auto_pause_on_crash"])
        # Save to file
        from .config import save_config
        path = save_config(_config)
        return JSONResponse({"status": "saved", "path": path})

    # ---- Helper: run SCRIPT command and get output ----
    async def _run_script(script: str, timeout: float = 10.0) -> tuple[str | None, str | None]:
        """Run a SCRIPT command and return (output, error). One will be None.

        Uses the shared async helper: the script runs without blocking the
        bridge and its capture file is polled for completion.
        """
        if not _conn or not _conn.connected:
            return None, "Not connected"
        from .mcp_tools import script_execute
        status, output = await script_execute(_conn, _event_bus, script, timeout=timeout)
        if status == "ok":
            return output, None
        if status == "running":
            # Past the (short) web timeout but bridge is fine; return partial.
            return output, None
        if status == "error":
            return None, output or "Error"
        if status == "disconnected":
            return None, "Not connected"
        return None, "Timeout"

    # ---- Assign Manager ----
    async def api_tool_assigns(request: Request) -> JSONResponse:
        """List AmigaOS assigns using SCRIPT command."""
        output, err = await _run_script("assign LIST")
        if err:
            return JSONResponse({"error": err}, status_code=504 if err == "Timeout" else 500)
        assigns = []
        for line in (output or "").splitlines():
            line = line.strip()
            if not line or line.startswith("Volumes:") or line.startswith("Directories:"):
                continue
            parts = line.split(None, 1)
            if len(parts) >= 2:
                assigns.append({"name": parts[0], "path": parts[1]})
            elif len(parts) == 1:
                assigns.append({"name": parts[0], "path": ""})
        return JSONResponse({"assigns": assigns})

    async def api_tool_assign_set(request: Request) -> JSONResponse:
        """Create or modify an assign."""
        body = await request.json()
        name = body.get("name", "")
        path = body.get("path", "")
        add = body.get("add", False)
        if not name or not path:
            return JSONResponse({"error": "Missing name or path"}, status_code=400)
        cmd = f"assign {name} {path}" + (" ADD" if add else "")
        output, err = await _run_script(cmd, timeout=5.0)
        if err:
            return JSONResponse({"error": err}, status_code=504 if err == "Timeout" else 500)
        return JSONResponse({"status": "ok", "output": output or ""})

    async def api_tool_assign_remove(request: Request) -> JSONResponse:
        """Remove an assign."""
        body = await request.json()
        name = body.get("name", "")
        if not name:
            return JSONResponse({"error": "Missing name"}, status_code=400)
        output, err = await _run_script(f"assign {name} REMOVE", timeout=5.0)
        if err:
            return JSONResponse({"error": err}, status_code=504 if err == "Timeout" else 500)
        return JSONResponse({"status": "ok"})

    # ---- Font Browser ----
    async def api_tool_fonts(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "LISTFONTS"})
        msg = await _event_bus.wait_for("fonts", timeout=10.0)
        if msg:
            return JSONResponse({"fonts": msg.get("fonts", [])})
        return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_fontinfo(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        name = request.query_params.get("name", "")
        size = request.query_params.get("size", "8")
        _conn.send({"type": "FONTINFO", "name": name, "size": size})
        msg = await _event_bus.wait_for("fontinfo", timeout=5.0)
        if msg:
            return JSONResponse(msg)
        return JSONResponse({"error": "Timeout"}, status_code=504)

    # ---- Locale/Catalog Inspector ----
    async def api_tool_locale(request: Request) -> JSONResponse:
        """List locale catalogs."""
        output, err = await _run_script('list LOCALE:Catalogs/ DIRS ALL LFORMAT "%p%n"')
        if err:
            return JSONResponse({"error": err}, status_code=504 if err == "Timeout" else 500)
        catalogs = [l.strip() for l in (output or "").splitlines() if l.strip()]
        return JSONResponse({"catalogs": catalogs})

    async def api_tool_locale_strings(request: Request) -> JSONResponse:
        """List catalog files for a specific language/app."""
        path = request.query_params.get("path", "LOCALE:Catalogs/")
        output, err = await _run_script(f'list {path} LFORMAT "%p%n %l"')
        if err:
            return JSONResponse({"error": err}, status_code=504 if err == "Timeout" else 500)
        entries = []
        for line in (output or "").splitlines():
            line = line.strip()
            if line:
                parts = line.rsplit(" ", 1)
                entries.append({"path": parts[0], "size": parts[1] if len(parts) > 1 else ""})
        return JSONResponse({"entries": entries})

    # ---- Custom Chip Write Logger ----
    async def api_tool_chiplog_start(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "CHIPLOGSTART"})
        msg = await _event_bus.wait_for("ok", timeout=3.0, predicate=lambda m: m.get("context") == "CHIPLOG")
        return JSONResponse({"status": "started"})

    async def api_tool_chiplog_stop(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "CHIPLOGSTOP"})
        return JSONResponse({"status": "stopped"})

    async def api_tool_chiplog_snapshot(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "CHIPLOGSNAPSHOT"})
        msg = await _event_bus.wait_for("chiplog", timeout=5.0)
        if msg:
            return JSONResponse({"registers": msg.get("registers", {})})
        return JSONResponse({"error": "Timeout"}, status_code=504)

    # ---- Memory Pool Tracker ----
    async def api_tool_pool_start(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "POOLSTART"})
        return JSONResponse({"status": "started"})

    async def api_tool_pool_stop(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "POOLSTOP"})
        return JSONResponse({"status": "stopped"})

    async def api_tool_pools(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "POOLS"})
        msg = await _event_bus.wait_for("pools", timeout=5.0)
        if msg:
            return JSONResponse({"pools": msg.get("pools", [])})
        return JSONResponse({"error": "Timeout"}, status_code=504)

    # ---- Startup-Sequence Editor ----
    async def api_tool_startup_read(request: Request) -> JSONResponse:
        """Read S:Startup-Sequence or S:User-Startup using SCRIPT type command."""
        file_path = request.query_params.get("file", "S:Startup-Sequence")
        allowed = ["S:Startup-Sequence", "S:User-Startup", "S:Shell-Startup"]
        if file_path not in allowed:
            return JSONResponse({"error": f"Not allowed: {file_path}"}, status_code=403)
        output, err = await _run_script(f"type {file_path}")
        if err:
            return JSONResponse({"error": err}, status_code=504 if err == "Timeout" else 500)
        return JSONResponse({"path": file_path, "content": output or "", "size": len(output or "")})

    async def api_tool_startup_write(request: Request) -> JSONResponse:
        """Write to S:Startup-Sequence or S:User-Startup via SCRIPT echo commands."""
        body = await request.json()
        file_path = body.get("file", "")
        content = body.get("content", "")
        allowed = ["S:Startup-Sequence", "S:User-Startup", "S:Shell-Startup"]
        if file_path not in allowed:
            return JSONResponse({"error": f"Not allowed: {file_path}"}, status_code=403)
        # Write via READFILE/WRITEFILE protocol with hex encoding
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        hex_data = content.encode("latin-1", errors="replace").hex()
        _conn.send({"type": "WRITEFILE", "path": file_path, "offset": 0, "hexData": hex_data})
        async with _event_bus.subscribe("ok", "err") as q:
            try:
                evt, data = await asyncio.wait_for(q.get(), timeout=10.0)
                if evt == "err":
                    return JSONResponse({"error": data.get("message", "Write failed")})
                return JSONResponse({"status": "ok", "path": file_path})
            except asyncio.TimeoutError:
                return JSONResponse({"error": "Timeout"}, status_code=504)

    # ---- Preferences Editor ----
    async def api_tool_prefs_list(request: Request) -> JSONResponse:
        """List available Workbench preferences files."""
        output, err = await _run_script('list ENV:sys/ LFORMAT "%n %l"')
        if err:
            return JSONResponse({"error": err}, status_code=504 if err == "Timeout" else 500)
        prefs = []
        for line in (output or "").splitlines():
            parts = line.strip().split()
            if len(parts) >= 2 and parts[0].endswith(".prefs"):
                prefs.append({"name": parts[0], "size": int(parts[1]) if parts[1].isdigit() else 0})
        return JSONResponse({"prefs": prefs})

    async def api_tool_prefs_read(request: Request) -> JSONResponse:
        """Read a prefs file content (hex dump for IFF binary)."""
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        name = request.query_params.get("name", "")
        if not name.endswith(".prefs"):
            return JSONResponse({"error": "Invalid prefs file"}, status_code=400)
        path = f"ENV:sys/{name}"
        # Read up to 4KB of the prefs file
        _conn.send({"type": "READFILE", "path": path, "offset": 0, "size": 4096})
        msg = await _event_bus.wait_for("file", timeout=10.0)
        if msg:
            return JSONResponse({"path": path, "content": msg.get("content", ""), "size": msg.get("size", 0)})
        return JSONResponse({"error": "Timeout"}, status_code=504)

    # ---- Visual Diff for Screenshots ----
    async def api_tool_screenshot_diff(request: Request) -> JSONResponse:
        """Compare two screenshots and return diff analysis."""
        body = await request.json()
        path_a = body.get("path_a", "")
        path_b = body.get("path_b", "")
        threshold = int(body.get("threshold", 10))
        if not path_a or not path_b:
            return JSONResponse({"error": "Need path_a and path_b"}, status_code=400)
        try:
            from .screenshot_diff import compare_screenshots
            result = compare_screenshots(path_a, path_b, threshold)
            return JSONResponse(result)
        except ImportError:
            return JSONResponse({"error": "screenshot_diff module not available"}, status_code=500)
        except Exception as e:
            return JSONResponse({"error": str(e)}, status_code=500)

    async def api_tool_screenshot_diff_view(request: Request) -> JSONResponse:
        """View a diff image."""
        path = request.query_params.get("path", "")
        if not path or not os.path.exists(path):
            return JSONResponse({"error": "File not found"}, status_code=404)
        from starlette.responses import FileResponse
        return FileResponse(path, media_type="image/png")

    # ---- CLI History ----
    _cli_history: list[str] = []
    _cli_aliases: dict[str, str] = {}

    # Load saved history/aliases
    _history_file = Path(PROJECT_ROOT) / ".devbench_history" if "PROJECT_ROOT" in dir() else None

    async def api_cli_history(request: Request) -> JSONResponse:
        """Get CLI command history."""
        return JSONResponse({"history": _cli_history[-200:]})

    async def api_cli_history_add(request: Request) -> JSONResponse:
        """Add a command to CLI history."""
        body = await request.json()
        cmd = body.get("command", "").strip()
        if cmd and (not _cli_history or _cli_history[-1] != cmd):
            _cli_history.append(cmd)
            # Keep max 500 entries
            if len(_cli_history) > 500:
                _cli_history[:] = _cli_history[-500:]
        return JSONResponse({"status": "ok"})

    async def api_cli_aliases(request: Request) -> JSONResponse:
        """Get/set CLI aliases."""
        if request.method == "GET":
            return JSONResponse({"aliases": _cli_aliases})
        body = await request.json()
        if "set" in body:
            _cli_aliases[body["set"]["name"]] = body["set"]["value"]
        if "remove" in body:
            _cli_aliases.pop(body["remove"], None)
        return JSONResponse({"aliases": _cli_aliases})

    # ---- ARexx Bridge ----
    async def api_tool_arexx_ports(request: Request) -> JSONResponse:
        """List all public message ports (potential ARexx targets)."""
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "AREXXPORTS"})
        msg = await _event_bus.wait_for("arexxports", timeout=5.0)
        if msg:
            return JSONResponse({"count": msg.get("count", 0), "ports": msg.get("ports", [])})
        return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_arexx_send(request: Request) -> JSONResponse:
        """Send an ARexx command to a named port."""
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        body = await request.json()
        port = body.get("port", "")
        command = body.get("command", "")
        if not port or not command:
            return JSONResponse({"error": "Missing port or command"}, status_code=400)
        _conn.send({"type": "AREXXSEND", "port": port, "command": command})
        msg = await _event_bus.wait_for("arexxresult", timeout=15.0)
        if msg:
            return JSONResponse({"rc": msg.get("rc", -1), "result": msg.get("result", "")})
        return JSONResponse({"error": "Timeout"}, status_code=504)

    # ---- Clipboard Bridge ----
    async def api_tool_clipboard_get(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "CLIPGET"})
        msg = await _event_bus.wait_for("clipboard", timeout=5.0)
        if msg:
            return JSONResponse({"text": msg.get("text", ""), "length": msg.get("length", 0)})
        return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_clipboard_set(request: Request) -> JSONResponse:
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        body = await request.json()
        text = body.get("text", "")
        _conn.send({"type": "CLIPSET", "text": text})
        async with _event_bus.subscribe("ok", "err") as q:
            try:
                evt, data = await asyncio.wait_for(q.get(), timeout=5.0)
                if evt == "err":
                    return JSONResponse({"error": data.get("message", "Error")})
                return JSONResponse({"status": "ok"})
            except asyncio.TimeoutError:
                return JSONResponse({"error": "Timeout"}, status_code=504)

    # ---- Protocol-Native Tools ----
    async def api_tool_capabilities(request: Request) -> JSONResponse:
        """Query bridge capabilities."""
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "CAPABILITIES"})
        msg = await _event_bus.wait_for("capabilities", timeout=5.0)
        if msg:
            return JSONResponse(msg)
        return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_proclist(request: Request) -> JSONResponse:
        """List running processes."""
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "PROCLIST"})
        msg = await _event_bus.wait_for("proclist", timeout=5.0)
        if msg:
            return JSONResponse(msg)
        return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_signal(request: Request) -> JSONResponse:
        """Send a signal to a task."""
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        body = await request.json()
        task_id = body.get("id", "")
        sig_type = body.get("sigType", "")
        if not task_id or not sig_type:
            return JSONResponse({"error": "Missing id or sigType"}, status_code=400)
        _conn.send({"type": "SIGNAL", "id": task_id, "sigType": sig_type})
        msg = await _event_bus.wait_for("ok", timeout=5.0, predicate=lambda m: m.get("context") == "SIGNAL")
        if msg:
            return JSONResponse({"status": "ok"})
        return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_checksum(request: Request) -> JSONResponse:
        """Compute checksum of a file."""
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        path = request.query_params.get("path", "")
        if not path:
            return JSONResponse({"error": "Missing path"}, status_code=400)
        _conn.send({"type": "CHECKSUM", "path": path})
        msg = await _event_bus.wait_for("checksum", timeout=10.0)
        if msg:
            return JSONResponse(msg)
        return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_protect(request: Request) -> JSONResponse:
        """Get or set file protection bits."""
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        if request.method == "POST":
            body = await request.json()
            path = body.get("path", "")
            bits = body.get("bits", "")
            if not path or not bits:
                return JSONResponse({"error": "Missing path or bits"}, status_code=400)
            _conn.send({"type": "PROTECT", "path": path, "bits": bits})
        else:
            path = request.query_params.get("path", "")
            if not path:
                return JSONResponse({"error": "Missing path"}, status_code=400)
            _conn.send({"type": "PROTECT", "path": path})
        msg = await _event_bus.wait_for("protect", timeout=5.0)
        if msg:
            return JSONResponse(msg)
        return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_rename(request: Request) -> JSONResponse:
        """Rename a file or directory."""
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        body = await request.json()
        old_path = body.get("oldPath", "")
        new_path = body.get("newPath", "")
        if not old_path or not new_path:
            return JSONResponse({"error": "Missing oldPath or newPath"}, status_code=400)
        _conn.send({"type": "RENAME", "oldPath": old_path, "newPath": new_path})
        msg = await _event_bus.wait_for("ok", timeout=5.0, predicate=lambda m: m.get("context") == "RENAME")
        if msg:
            return JSONResponse({"status": "ok"})
        return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_copy(request: Request) -> JSONResponse:
        """Copy a file."""
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        body = await request.json()
        src = body.get("src", "")
        dst = body.get("dst", "")
        if not src or not dst:
            return JSONResponse({"error": "Missing src or dst"}, status_code=400)
        _conn.send({"type": "COPY", "src": src, "dst": dst})
        msg = await _event_bus.wait_for("ok", timeout=10.0, predicate=lambda m: m.get("context") == "COPY")
        if msg:
            return JSONResponse({"status": "ok"})
        return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_set_comment(request: Request) -> JSONResponse:
        """Set a file comment."""
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        body = await request.json()
        path = body.get("path", "")
        comment = body.get("comment", "")
        if not path:
            return JSONResponse({"error": "Missing path"}, status_code=400)
        _conn.send({"type": "SETCOMMENT", "path": path, "comment": comment})
        msg = await _event_bus.wait_for("ok", timeout=5.0, predicate=lambda m: m.get("context") == "SETCOMMENT")
        if msg:
            return JSONResponse({"status": "ok"})
        return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_append(request: Request) -> JSONResponse:
        """Append hex data to a file."""
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        body = await request.json()
        path = body.get("path", "")
        hex_data = body.get("hexData", "")
        if not path or not hex_data:
            return JSONResponse({"error": "Missing path or hexData"}, status_code=400)
        _conn.send({"type": "APPEND", "path": path, "hexData": hex_data})
        msg = await _event_bus.wait_for("ok", timeout=5.0, predicate=lambda m: m.get("context") == "APPEND")
        if msg:
            return JSONResponse({"status": "ok"})
        return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_tail_start(request: Request) -> JSONResponse:
        """Start tailing a file."""
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        body = await request.json()
        path = body.get("path", "")
        if not path:
            return JSONResponse({"error": "Missing path"}, status_code=400)
        _conn.send({"type": "TAIL", "path": path})
        msg = await _event_bus.wait_for("ok", timeout=5.0, predicate=lambda m: m.get("context") == "TAIL")
        if msg:
            return JSONResponse({"status": "ok"})
        return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_tail_stop(request: Request) -> JSONResponse:
        """Stop tailing a file."""
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "STOPTAIL"})
        msg = await _event_bus.wait_for("ok", timeout=5.0, predicate=lambda m: m.get("context") == "STOPTAIL")
        if msg:
            return JSONResponse({"status": "ok"})
        return JSONResponse({"error": "Timeout"}, status_code=504)

    # ─── New System API Routes ───

    async def api_tool_version(request: Request) -> JSONResponse:
        """Get AmigaBridge daemon version."""
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "VERSION"})
        msg = await _event_bus.wait_for("version", timeout=5.0)
        if msg:
            return JSONResponse(msg)
        return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_get_env(request: Request) -> JSONResponse:
        """Get an AmigaOS environment variable."""
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        name = request.query_params.get("name", "")
        archive = request.query_params.get("archive", "false").lower() == "true"
        if not name:
            return JSONResponse({"error": "Missing name"}, status_code=400)
        _conn.send({"type": "GETENV", "name": name, "archive": archive})
        async with _event_bus.subscribe("env", "err") as queue:
            try:
                evt, data = await asyncio.wait_for(queue.get(), timeout=5.0)
                if evt == "err":
                    return JSONResponse({"error": data.get("message", "Unknown")})
                if evt == "env":
                    return JSONResponse(data)
            except asyncio.TimeoutError:
                pass
        return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_set_env(request: Request) -> JSONResponse:
        """Set an AmigaOS environment variable."""
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        body = await request.json()
        name = body.get("name", "")
        value = body.get("value", "")
        archive = body.get("archive", False)
        if not name:
            return JSONResponse({"error": "Missing name"}, status_code=400)
        _conn.send({"type": "SETENV", "name": name, "value": value, "archive": archive})
        msg = await _event_bus.wait_for("ok", timeout=5.0,
                                         predicate=lambda m: m.get("context") == "SETENV")
        if msg:
            return JSONResponse({"status": "ok", "name": name, "value": value})
        return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_set_date(request: Request) -> JSONResponse:
        """Set file modification date."""
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        body = await request.json()
        path = body.get("path", "")
        date_str = body.get("date", "")
        if not path:
            return JSONResponse({"error": "Missing path"}, status_code=400)
        from datetime import datetime as _dt
        AMIGA_EPOCH = _dt(1978, 1, 1)
        if date_str:
            try:
                dt = _dt.strptime(date_str, "%Y-%m-%d %H:%M:%S")
            except ValueError:
                return JSONResponse({"error": "date must be YYYY-MM-DD HH:MM:SS"}, status_code=400)
        else:
            dt = _dt.now()
        delta = dt - AMIGA_EPOCH
        days = delta.days
        mins = dt.hour * 60 + dt.minute
        ticks = dt.second * 50
        _conn.send({"type": "SETDATE", "path": path, "days": days, "mins": mins, "ticks": ticks})
        msg = await _event_bus.wait_for("ok", timeout=5.0,
                                         predicate=lambda m: m.get("context") == "SETDATE")
        if msg:
            return JSONResponse({"status": "ok", "path": path, "date": dt.strftime("%Y-%m-%d %H:%M:%S")})
        return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_list_volumes(request: Request) -> JSONResponse:
        """List mounted volumes with usage stats."""
        if not _conn or not _conn.connected:
            return JSONResponse({"volumes": [], "error": "Not connected"})
        _conn.send({"type": "VOLUMES"})
        msg = await _event_bus.wait_for("volumes", timeout=5.0)
        if msg:
            return JSONResponse({"volumes": msg.get("volumes", [])})
        return JSONResponse({"volumes": [], "message": "No response"})

    async def api_tool_list_ports(request: Request) -> JSONResponse:
        """List public message ports."""
        if not _conn or not _conn.connected:
            return JSONResponse({"ports": [], "error": "Not connected"})
        _conn.send({"type": "PORTS"})
        msg = await _event_bus.wait_for("ports", timeout=5.0)
        if msg:
            return JSONResponse({"ports": msg.get("ports", [])})
        return JSONResponse({"ports": [], "message": "No response"})

    async def api_tool_sysinfo_full(request: Request) -> JSONResponse:
        """Get aggregated system information (memory, CPU, exec version, PAL/NTSC)."""
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "SYSINFO"})
        msg = await _event_bus.wait_for("sysinfo", timeout=5.0)
        if msg:
            return JSONResponse(msg)
        return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_uptime(request: Request) -> JSONResponse:
        """Get AmigaBridge daemon uptime."""
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "UPTIME"})
        msg = await _event_bus.wait_for("uptime", timeout=5.0)
        if msg:
            return JSONResponse(msg)
        return JSONResponse({"error": "Timeout"}, status_code=504)

    async def api_tool_reboot(request: Request) -> JSONResponse:
        """Reboot the Amiga."""
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "REBOOT"})
        return JSONResponse({"status": "ok", "message": "Reboot command sent"})

    async def api_tool_shutdown_bridge(request: Request) -> JSONResponse:
        """Shut down the AmigaBridge daemon."""
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        _conn.send({"type": "SHUTDOWN"})
        msg = await _event_bus.wait_for("ok", timeout=5.0,
                                         predicate=lambda m: m.get("context") == "SHUTDOWN")
        if msg:
            return JSONResponse({"status": "ok", "message": "Bridge shutting down"})
        return JSONResponse({"status": "ok", "message": "Shutdown sent (no confirmation)"})

    # ─── Traffic Log API ───

    async def api_traffic_list(request: Request) -> JSONResponse:
        """List traffic log entries with optional filtering."""
        assert _traffic is not None
        kind = request.query_params.get("kind")
        search = request.query_params.get("q")
        limit = int(request.query_params.get("limit", "100"))
        offset = int(request.query_params.get("offset", "0"))
        return JSONResponse(_traffic.list_entries(kind=kind, search=search, limit=limit, offset=offset))

    async def api_traffic_detail(request: Request) -> JSONResponse:
        """Get full detail for a traffic entry."""
        assert _traffic is not None
        entry_id = request.query_params.get("id", "")
        detail = _traffic.get_detail(entry_id)
        if not detail:
            return JSONResponse({"error": "Not found"}, status_code=404)
        return JSONResponse(detail)

    async def api_traffic_clear(request: Request) -> JSONResponse:
        """Clear traffic log."""
        assert _traffic is not None
        _traffic.clear()
        return JSONResponse({"status": "ok"})

    async def api_traffic_replay(request: Request) -> JSONResponse:
        """Replay an MCP tool call."""
        assert _traffic is not None
        body = await request.json()
        entry_id = body.get("id", "")
        detail = _traffic.get_detail(entry_id)
        if not detail:
            return JSONResponse({"error": "Not found"}, status_code=404)
        if detail["kind"] != "mcp":
            return JSONResponse({"error": "Only MCP calls can be replayed"}, status_code=400)

        tool_name = detail["method"]
        args = detail.get("request_body") or {}

        # Find the tool function from FastMCP
        tool_fn = None
        for t in mcp._tool_manager.list_tools():
            if t.name == tool_name:
                tool_fn = t
                break
        if not tool_fn:
            return JSONResponse({"error": f"Tool '{tool_name}' not found"}, status_code=404)

        t0 = time.time()
        try:
            result = await mcp._tool_manager.call_tool(tool_name, args)
            duration = (time.time() - t0) * 1000
            resp_text = ""
            for item in result:
                if hasattr(item, "text"):
                    resp_text += item.text
            # Traffic record for web-UI-initiated tool calls (Claude's go via
            # McpLoggingWrapper; the call_tool wrapper only adds the Logs line).
            _traffic.record(
                kind="mcp", method=tool_name, path=f"mcp://{tool_name}",
                request_body=args, response_body=resp_text,
                duration_ms=round(duration, 1),
            )
            return JSONResponse({"result": resp_text, "duration_ms": round(duration, 1)})
        except Exception as exc:
            _traffic.record(
                kind="mcp", method=tool_name, path=f"mcp://{tool_name}",
                request_body=args, status=500,
                duration_ms=round((time.time() - t0) * 1000, 1), error=str(exc),
            )
            return JSONResponse({"error": str(exc)}, status_code=500)

    # ─── MCP logging wrapper ───
    # Wrap the MCP ASGI handler to capture tool calls from the JSON-RPC stream

    class McpLoggingWrapper:
        """ASGI wrapper that logs MCP tool calls.

        Must be a class so Starlette's Route recognizes __call__(scope,
        receive, send) as an ASGI app rather than an endpoint function.
        """

        def __init__(self, inner):
            self._inner = inner

        async def __call__(self, scope, receive, send):
            if scope["type"] != "http":
                return await self._inner(scope, receive, send)

            body_parts = []
            async def logging_receive():
                msg = await receive()
                if msg.get("type") == "http.request":
                    body_parts.append(msg.get("body", b""))
                return msg

            response_status = [200]
            response_parts = []

            async def logging_send(msg):
                if msg.get("type") == "http.response.start":
                    response_status[0] = msg.get("status", 200)
                elif msg.get("type") == "http.response.body":
                    chunk = msg.get("body", b"")
                    if chunk:
                        response_parts.append(chunk)
                await send(msg)

            t0 = time.time()
            try:
                await self._inner(scope, logging_receive, logging_send)
            except Exception:
                raise
            finally:
                duration = (time.time() - t0) * 1000
                try:
                    self._log_traffic(body_parts, response_parts, resp_by_id={},
                                      response_status=response_status[0], duration=duration)
                except Exception:
                    pass

        @staticmethod
        def _parse_sse_responses(raw: bytes) -> list[dict]:
            """Extract JSON-RPC responses from SSE or plain JSON body."""
            text = raw.decode("utf-8", errors="replace")
            results = []
            # Try plain JSON first
            try:
                data = json.loads(text)
                return data if isinstance(data, list) else [data]
            except (json.JSONDecodeError, ValueError):
                pass
            # Parse SSE: look for "data: {...}" lines
            for line in text.split("\n"):
                line = line.strip()
                if line.startswith("data:"):
                    payload = line[5:].strip()
                    if payload:
                        try:
                            results.append(json.loads(payload))
                        except (json.JSONDecodeError, ValueError):
                            pass
            return results

        def _log_traffic(self, body_parts, response_parts, resp_by_id, response_status, duration):
            raw_body = b"".join(body_parts)
            if not raw_body:
                return
            req_data = json.loads(raw_body)
            reqs = req_data if isinstance(req_data, list) else [req_data]

            raw_resp = b"".join(response_parts)
            resp_list = self._parse_sse_responses(raw_resp) if raw_resp else []
            resp_by_id = {}
            for r in resp_list:
                if isinstance(r, dict) and "id" in r:
                    resp_by_id[r["id"]] = r

            for req in reqs:
                if not isinstance(req, dict):
                    continue
                rpc_method = req.get("method", "")
                params = req.get("params", {})
                req_id = req.get("id")
                resp = resp_by_id.get(req_id, {}) if req_id else {}

                if rpc_method == "tools/call":
                    tool_name = params.get("name", "unknown")
                    tool_args = params.get("arguments", {})
                    result = resp.get("result", {})
                    content = result.get("content", []) if isinstance(result, dict) else []
                    resp_text = ""
                    for item in content:
                        if isinstance(item, dict) and item.get("type") == "text":
                            resp_text += item.get("text", "")
                    is_error = result.get("isError", False) if isinstance(result, dict) else False
                    _traffic.record(
                        kind="mcp",
                        method=tool_name,
                        path=f"mcp://{tool_name}",
                        request_body=tool_args,
                        response_body=resp_text if resp_text else None,
                        status=500 if is_error else response_status,
                        duration_ms=round(duration, 1),
                        error=resp_text if is_error else None,
                    )
                elif rpc_method and rpc_method not in ("notifications/initialized",):
                    result = resp.get("result") if resp else None
                    _traffic.record(
                        kind="mcp",
                        method=rpc_method,
                        path=f"mcp://{rpc_method}",
                        request_body=params if params else None,
                        response_body=result,
                        status=response_status,
                        duration_ms=round(duration, 1),
                    )

    mcp_asgi_handler = McpLoggingWrapper(mcp_asgi_handler)

    # ─── REST traffic logging middleware ───

    # ─── File Transfer (host <-> Amiga via serial) ───

    async def api_transfer(request: Request) -> JSONResponse:
        """Transfer files between host and Amiga over serial."""
        if not _conn or not _conn.connected:
            return JSONResponse({"error": "Not connected"})
        body = await request.json()
        source = body.get("source", "")
        dest = body.get("dest", "")
        direction = body.get("direction", "push")  # push = host->amiga, pull = amiga->host
        if not source or not dest:
            return JSONResponse({"error": "Missing source or dest"}, status_code=400)

        try:
            if direction == "push":
                # Host -> Amiga: support glob patterns
                import glob as globmod
                if '*' in source or '?' in source:
                    files = sorted(globmod.glob(source))
                    if not files:
                        return JSONResponse({"error": f"No files match: {source}"})
                    pairs = []
                    for f in files:
                        fname = Path(f).name
                        amiga_dest = dest.rstrip("/") + "/" + fname if dest.endswith(("/", ":")) else dest
                        pairs.append((f, amiga_dest))
                    result = await file_transfer.push_files(_conn, _event_bus, pairs)
                else:
                    result = await file_transfer.push_file(_conn, _event_bus, source, dest)
            elif direction == "pull":
                # Amiga -> Host
                result = await file_transfer.pull_file(_conn, _event_bus, source, dest)
            else:
                return JSONResponse({"error": f"Invalid direction: {direction}"}, status_code=400)

            return JSONResponse({
                "success": result.success,
                "message": result.message,
                "bytes": result.bytes_transferred,
                "elapsed": round(result.elapsed, 2),
                "crc_match": result.crc_match,
                "files": result.files,
            })
        except Exception as exc:
            logger.exception("Transfer failed")
            return JSONResponse({"error": str(exc)}, status_code=500)

    _SKIP_TRAFFIC_PATHS = {"/api/events", "/api/traffic", "/api/traffic/detail",
                           "/api/traffic/clear", "/api/traffic/replay", "/health", "/", "/mcp"}

    from starlette.middleware.base import BaseHTTPMiddleware

    class TrafficMiddleware(BaseHTTPMiddleware):
        async def dispatch(self, request: Request, call_next):
            path = request.url.path
            if path in _SKIP_TRAFFIC_PATHS or not path.startswith("/api/"):
                return await call_next(request)

            method = request.method
            req_body = None
            if method in ("POST", "PUT", "PATCH"):
                try:
                    raw = await request.body()
                    if raw:
                        req_body = json.loads(raw)
                except (json.JSONDecodeError, UnicodeDecodeError):
                    pass

            t0 = time.time()
            try:
                response = await call_next(request)
                duration = (time.time() - t0) * 1000

                # Capture response body for JSON responses
                resp_body = None
                if hasattr(response, "body"):
                    try:
                        resp_body = json.loads(response.body)
                    except (json.JSONDecodeError, UnicodeDecodeError, AttributeError):
                        pass

                full_path = path + ("?" + str(request.query_params) if request.query_params else "")
                _traffic.record(
                    kind="rest",
                    method=method,
                    path=full_path,
                    request_body=req_body,
                    response_body=resp_body,
                    status=response.status_code,
                    duration_ms=round(duration, 1),
                )
                if _plog:
                    _plog.write_api(method, full_path, status=response.status_code, duration_ms=round(duration, 1))
                return response
            except Exception as exc:
                duration = (time.time() - t0) * 1000
                _traffic.record(
                    kind="rest",
                    method=method,
                    path=path,
                    request_body=req_body,
                    status=500,
                    duration_ms=round(duration, 1),
                    error=str(exc),
                )
                if _plog:
                    _plog.write_api(method, path, status=500, duration_ms=round(duration, 1), error=str(exc))
                raise

    # ─── Debugger API Routes ───

    async def api_debugger_status(request: Request) -> JSONResponse:
        assert _dbg_state is not None
        return JSONResponse(_dbg_state.to_dict())

    async def api_debugger_attach(request: Request) -> JSONResponse:
        assert _conn is not None and _event_bus is not None and _dbg_state is not None
        if not _conn.connected:
            return JSONResponse({"error": "not connected"}, status_code=400)
        body = await request.json()
        target = body.get("target", "")
        if not target:
            return JSONResponse({"error": "target required"}, status_code=400)

        # Send DBGATTACH, then poll _dbg_state which the SSE handler updates.
        # This avoids event bus subscription race conditions under VAR load.
        _conn.send_raw(f"DBGATTACH|{target}")

        for attempt in range(20):
            await asyncio.sleep(0.4)
            # The SSE handler updates _dbg_state when DBGSTATE arrives
            if _dbg_state.attached:
                return JSONResponse(_dbg_state.to_dict())
            # Re-send DBGSTATUS as a nudge every few attempts
            if attempt % 3 == 2:
                _conn.send_raw("DBGSTATUS")
        return JSONResponse({"error": "timeout"}, status_code=504)

    async def api_debugger_detach(request: Request) -> JSONResponse:
        assert _conn is not None and _event_bus is not None and _dbg_state is not None
        if not _conn.connected:
            return JSONResponse({"error": "not connected"}, status_code=400)
        async with _event_bus.subscribe("dbg_detached") as queue:
            _conn.send({"type": "DBGDETACH"})
            try:
                await asyncio.wait_for(queue.get(), timeout=5.0)
            except asyncio.TimeoutError:
                pass
        _dbg_state.reset()
        return JSONResponse({"ok": True})

    async def api_debugger_bp_set(request: Request) -> JSONResponse:
        assert _conn is not None and _event_bus is not None and _dbg_state is not None
        if not _conn.connected:
            return JSONResponse({"error": "not connected"}, status_code=400)
        body = await request.json()
        address = body.get("address", "")
        if not address:
            return JSONResponse({"error": "address required"}, status_code=400)

        # Add code base offset if the address looks like a symbol offset
        # (small address, below the typical load address range)
        try:
            addr_int = int(address, 16)
            if _dbg_state and _dbg_state.code_base > 0 and addr_int < _dbg_state.code_base:
                addr_int += _dbg_state.code_base
                address = f"{addr_int:08x}"
                logger.info("BP address relocated: +0x%X base = 0x%X", _dbg_state.code_base, addr_int)
        except ValueError:
            pass

        # Send BPSET and trust it works — the bridge always processes it.
        # Don't wait for BPINFO response (unreliable under VAR load).
        _conn.send_raw(f"BPSET|{address}")
        await asyncio.sleep(0.3)  # Give bridge time to process
        return JSONResponse({"ok": True, "address": addr_int if 'addr_int' in dir() else 0})

    async def api_debugger_bp_clear(request: Request) -> JSONResponse:
        assert _conn is not None and _event_bus is not None
        if not _conn.connected:
            return JSONResponse({"error": "not connected"}, status_code=400)
        body = await request.json()
        bp_id = body.get("id", "")
        async with _event_bus.subscribe("ok", "err") as queue:
            _conn.send({"type": "BPCLEAR", "id": str(bp_id)})
            deadline = asyncio.get_event_loop().time() + 5.0
            while True:
                remaining = deadline - asyncio.get_event_loop().time()
                if remaining <= 0:
                    break
                try:
                    evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                    if evt == "ok" and "BPCLEAR" in data.get("context", ""):
                        return JSONResponse({"ok": True})
                    if evt == "err" and "BPCLEAR" in data.get("context", ""):
                        return JSONResponse({"ok": False, "error": data.get("message", "")})
                except asyncio.TimeoutError:
                    break
        return JSONResponse({"ok": False, "error": "timeout"})

    async def api_debugger_bp_list(request: Request) -> JSONResponse:
        assert _conn is not None and _event_bus is not None and _dbg_state is not None
        if not _conn.connected:
            return JSONResponse({"error": "not connected"}, status_code=400)
        async with _event_bus.subscribe("dbg_bplist") as queue:
            _conn.send({"type": "BPLIST"})
            try:
                evt, msg = await asyncio.wait_for(queue.get(), timeout=5.0)
                _dbg_state.update_breakpoints(msg.get("breakpoints", []))
                return JSONResponse({"breakpoints": msg.get("breakpoints", [])})
            except asyncio.TimeoutError:
                pass
        return JSONResponse({"breakpoints": []})

    async def api_debugger_step(request: Request) -> JSONResponse:
        """Source-level step into: keep stepping instructions until
        the source line changes (enters a function or advances a line)."""
        assert _conn is not None and _event_bus is not None and _dbg_state is not None
        if not _conn.connected:
            return JSONResponse({"error": "not connected"}, status_code=400)

        # Ensure we have current PC by polling DBGSTATUS
        if not _dbg_state.stopped or _dbg_state.pc == 0:
            _conn.send_raw("DBGSTATUS")
            for _ in range(10):
                await asyncio.sleep(0.3)
                if _dbg_state.stopped and _dbg_state.pc > 0:
                    break

        start_line = None
        code_base = _dbg_state.code_base
        start_pc = _dbg_state.pc
        logger.info("STEP: pc=0x%X cb=0x%X stopped=%s", start_pc, code_base, _dbg_state.stopped)
        if code_base > 0 and start_pc >= code_base:
            try:
                from . import symbols as sym_mod
                tables = sym_mod.get_all_tables()
                for proj_name, sym_table in tables.items():
                    src = sym_table.lookup_source_line(start_pc - code_base)
                    if src:
                        start_line = src  # (file, line)
                    break
            except Exception:
                pass

        # Step into: if the current source line calls a function we have symbols
        # for, set a temp BP at that function's first line. Otherwise, step to
        # the next sequential source line (same as step-over).
        global _dbg_stepping
        _dbg_stepping = True
        try:
            next_addr = 0
            if code_base > 0 and start_line:
                try:
                    from . import symbols as sym_mod
                    tables = sym_mod.get_all_tables()
                    rel_pc = start_pc - code_base if start_pc >= code_base else start_pc
                    for proj_name, sym_table in tables.items():
                        # Try to find a function call on this source line
                        call_target = 0
                        if start_line:
                            src_file, src_line_num = start_line
                            try:
                                resolved = src_file
                                if not os.path.isabs(resolved):
                                    candidate = os.path.join(os.getcwd(), "examples", proj_name, resolved)
                                    if os.path.isfile(candidate):
                                        resolved = candidate
                                with open(resolved) as sf:
                                    all_lines = sf.readlines()
                                    if 0 < src_line_num <= len(all_lines):
                                        line_text = all_lines[src_line_num - 1]
                                        for func_name in sym_table.func_source:
                                            if func_name == "main":
                                                continue
                                            if func_name + "(" in line_text:
                                                func_sym = sym_table.by_name.get(func_name)
                                                if func_sym:
                                                    call_target = code_base + func_sym.address
                                                    break
                            except Exception:
                                pass

                        if call_target > 0:
                            next_addr = call_target
                        else:
                            # No call found — step to next sequential line
                            best = 0xFFFFFFFF
                            for sl in sym_table.source_lines:
                                if sl.address > rel_pc and sl.address < best:
                                    best = sl.address
                            if best < 0xFFFFFFFF:
                                next_addr = code_base + best
                        break
                except Exception:
                    pass

            if next_addr == 0:
                return JSONResponse({"error": "no symbol info for step"}, status_code=400)

            # Set temp BP, continue, poll for stop, then clear temp BP.
            # Keep user BPs intact.
            addr_hex = f"{next_addr:08x}"
            _dbg_state.stopped = False
            _conn.send_raw(f"BPSET|{addr_hex}")
            await asyncio.sleep(0.15)
            _conn.send_raw("DBGCONT")

            # Poll until stopped
            for attempt in range(30):
                await asyncio.sleep(0.3)
                if _dbg_state.stopped:
                    # Clear only the temp BP
                    _conn.send_raw(f"BPCLEAR|{addr_hex}")
                    await asyncio.sleep(0.1)
                    return JSONResponse(_dbg_state.to_dict())
                if attempt % 4 == 3:
                    _conn.send_raw("DBGSTATUS")
            _conn.send_raw(f"BPCLEAR|{addr_hex}")
            return JSONResponse({"error": "step timeout"}, status_code=504)
        finally:
            _dbg_stepping = False

    async def api_debugger_next(request: Request) -> JSONResponse:
        """Source-level step over: find the next source line's address,
        set a temp BP there, and continue. This skips over function
        calls since the temp BP is on the NEXT line in the same function."""
        assert _conn is not None and _event_bus is not None and _dbg_state is not None
        if not _conn.connected:
            return JSONResponse({"error": "not connected"}, status_code=400)

        global _dbg_stepping
        _dbg_stepping = True
        try:
          return await _do_debugger_next()
        finally:
            _dbg_stepping = False

    async def _do_debugger_next() -> JSONResponse:
        assert _conn is not None and _event_bus is not None and _dbg_state is not None

        # Ensure we have current PC
        if not _dbg_state.stopped or _dbg_state.pc == 0:
            _conn.send_raw("DBGSTATUS")
            for _ in range(10):
                await asyncio.sleep(0.3)
                if _dbg_state.stopped and _dbg_state.pc > 0:
                    break

        # Find the next source line address after the current PC
        code_base = _dbg_state.code_base
        current_pc = _dbg_state.pc
        next_line_addr = 0

        if code_base > 0 and current_pc >= code_base:
            rel_pc = current_pc - code_base
            try:
                from . import symbols as sym_mod
                tables = sym_mod.get_all_tables()
                for proj_name, sym_table in tables.items():
                    # Find the source line for current PC
                    cur_src = sym_table.lookup_source_line(rel_pc)
                    if cur_src:
                        cur_file, cur_line = cur_src
                        # Find the next source line in the same file
                        best_addr = 0xFFFFFFFF
                        for sl in sym_table.source_lines:
                            if sl.file == cur_file and sl.address > rel_pc and sl.address < best_addr:
                                best_addr = sl.address
                        if best_addr < 0xFFFFFFFF:
                            next_line_addr = code_base + best_addr
                    break
            except Exception:
                pass

        if next_line_addr > 0:
            # Set temp BP, continue, poll, clear temp BP
            addr_hex = f"{next_line_addr:08x}"
            _dbg_state.stopped = False
            _conn.send_raw(f"BPSET|{addr_hex}")
            await asyncio.sleep(0.15)
            _conn.send_raw("DBGCONT")

            for attempt in range(30):
                await asyncio.sleep(0.3)
                if _dbg_state.stopped:
                    _conn.send_raw(f"BPCLEAR|{addr_hex}")
                    await asyncio.sleep(0.1)
                    return JSONResponse(_dbg_state.to_dict())
                if attempt % 4 == 3:
                    _conn.send_raw("DBGSTATUS")
            _conn.send_raw(f"BPCLEAR|{addr_hex}")
            return JSONResponse({"error": "timeout"}, status_code=504)
        else:
            # No next line — just continue (will hit next user BP)
            _conn.send_raw("DBGCONT")
            _dbg_state.stopped = False
            return JSONResponse({"error": "no next line found"}, status_code=400)

    async def api_debugger_continue(request: Request) -> JSONResponse:
        assert _conn is not None and _event_bus is not None and _dbg_state is not None
        if not _conn.connected:
            return JSONResponse({"error": "not connected"}, status_code=400)
        _conn.send_raw("DBGCONT")
        _dbg_state.stopped = False
        return JSONResponse({"ok": True, "state": "running"})

    async def api_debugger_regs(request: Request) -> JSONResponse:
        assert _conn is not None and _event_bus is not None and _dbg_state is not None
        if not _conn.connected:
            return JSONResponse({"error": "not connected"}, status_code=400)
        # Don't send serial commands while stepping — return cached state
        if _dbg_stepping:
            return JSONResponse(_dbg_state.to_dict())
        async with _event_bus.subscribe("dbg_regs") as queue:
            _conn.send({"type": "DBGREGS"})
            try:
                evt, msg = await asyncio.wait_for(queue.get(), timeout=5.0)
                _dbg_state.update_from_regs(msg)
                return JSONResponse(_dbg_state.to_dict())
            except asyncio.TimeoutError:
                pass
        return JSONResponse({"error": "timeout"}, status_code=504)

    async def api_debugger_setreg(request: Request) -> JSONResponse:
        assert _conn is not None and _event_bus is not None
        if not _conn.connected:
            return JSONResponse({"error": "not connected"}, status_code=400)
        body = await request.json()
        reg = body.get("reg", "")
        value = body.get("value", "")
        if not reg or not value:
            return JSONResponse({"error": "reg and value required"}, status_code=400)
        async with _event_bus.subscribe("ok", "err") as queue:
            _conn.send({"type": "DBGSETREG", "reg": reg, "value": value})
            deadline = asyncio.get_event_loop().time() + 5.0
            while True:
                remaining = deadline - asyncio.get_event_loop().time()
                if remaining <= 0:
                    break
                try:
                    evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                    if evt == "ok" and "DBGSETREG" in data.get("context", ""):
                        return JSONResponse({"ok": True})
                except asyncio.TimeoutError:
                    break
        return JSONResponse({"ok": False})

    async def api_debugger_backtrace(request: Request) -> JSONResponse:
        assert _conn is not None and _event_bus is not None and _dbg_state is not None
        if not _conn.connected:
            return JSONResponse({"error": "not connected"}, status_code=400)
        # Don't send serial commands while stepping — return cached data
        if _dbg_stepping:
            return JSONResponse({
                "depth": len(_dbg_state.backtrace),
                "frames": [{"depth": f.depth, "pc": f.pc, "pcHex": f"{f.pc:08X}",
                            "symbol": f.symbol, "file": f.source_file, "line": f.source_line}
                           for f in _dbg_state.backtrace],
            })
        msg = None
        async with _event_bus.subscribe("dbg_bt") as queue:
            _conn.send({"type": "DBGBT"})
            try:
                evt, msg = await asyncio.wait_for(queue.get(), timeout=5.0)
            except asyncio.TimeoutError:
                pass
        if msg:
            _dbg_state.update_backtrace(msg.get("frames", []))
            # Try to annotate with symbols
            try:
                from .symbols import _loaded_tables
                if _loaded_tables:
                    sym_table = list(_loaded_tables.values())[0]
                    annotate_with_symbols(_dbg_state, sym_table)
            except Exception:
                pass
            return JSONResponse({
                "depth": msg.get("depth", 0),
                "frames": [
                    {
                        "depth": f.depth,
                        "pc": f.pc,
                        "pcHex": f"{f.pc:08X}",
                        "symbol": f.symbol,
                        "file": f.source_file,
                        "line": f.source_line,
                    }
                    for f in _dbg_state.backtrace
                ],
            })
        return JSONResponse({"depth": 0, "frames": []})

    async def api_debugger_source(request: Request) -> JSONResponse:
        """Get source code context around a PC address."""
        pc_str = request.query_params.get("pc", "0")
        project = request.query_params.get("project", "")
        context_lines = int(request.query_params.get("context", "10"))

        try:
            pc = int(pc_str, 16)
        except ValueError:
            return JSONResponse({"error": "invalid pc"}, status_code=400)

        # If PC is an absolute address and we have a code base,
        # subtract it to get the symbol-relative offset
        lookup_pc = pc
        if _dbg_state and _dbg_state.code_base > 0 and pc >= _dbg_state.code_base:
            lookup_pc = pc - _dbg_state.code_base

        try:
            from . import symbols as sym_mod
            tables = sym_mod.get_all_tables()
            for proj_name, sym_table in tables.items():
                if project and proj_name != project:
                    continue
                src = sym_table.lookup_source_line(lookup_pc)
                if src:
                    file_path, line_num = src
                    # Resolve relative paths to project directory
                    if not os.path.isabs(file_path):
                        proj_dir = os.path.join(os.getcwd(), "examples", project or proj_name)
                        candidate = os.path.join(proj_dir, file_path)
                        if os.path.isfile(candidate):
                            file_path = candidate
                    # Read source file from host filesystem
                    try:
                        with open(file_path) as f:
                            lines = f.readlines()
                        start = max(0, line_num - context_lines - 1)
                        end = min(len(lines), line_num + context_lines)
                        return JSONResponse({
                            "file": file_path,
                            "line": line_num,
                            "startLine": start + 1,
                            "lines": [l.rstrip("\n") for l in lines[start:end]],
                            "symbol": sym_table.lookup_address(pc),
                        })
                    except FileNotFoundError:
                        return JSONResponse({
                            "file": file_path,
                            "line": line_num,
                            "error": "source file not found on host",
                            "symbol": sym_table.lookup_address(pc),
                        })
        except Exception:
            pass

        return JSONResponse({"error": "no source mapping for this address"})

    async def api_debugger_source_file(request: Request) -> JSONResponse:
        """Read an entire source file for display in the debugger."""
        file_path = request.query_params.get("file", "")
        if not file_path:
            return JSONResponse({"error": "file required"}, status_code=400)

        # Also accept project-relative paths
        project = request.query_params.get("project", "")
        if project and not os.path.isabs(file_path):
            proj_root = os.path.join(os.getcwd(), "examples", project)
            candidate = os.path.join(proj_root, file_path)
            if os.path.isfile(candidate):
                file_path = candidate

        try:
            with open(file_path) as f:
                lines = f.readlines()

            # Get breakpoint-able line numbers (lines with source mappings)
            bp_lines: dict[int, int] = {}  # line_num -> address
            try:
                from . import symbols as sym_mod
                tables = sym_mod.get_all_tables()
                for proj_name, sym_table in tables.items():
                    if project and proj_name != project:
                        continue
                    for sl in sym_table.source_lines:
                        if sl.file == file_path or os.path.basename(sl.file) == os.path.basename(file_path):
                            bp_lines[sl.line] = sl.address
            except Exception:
                pass

            return JSONResponse({
                "file": file_path,
                "totalLines": len(lines),
                "lines": [l.rstrip("\n") for l in lines],
                "breakpointLines": bp_lines,
            })
        except FileNotFoundError:
            return JSONResponse({"error": f"File not found: {file_path}"}, status_code=404)

    async def api_debugger_sources_list(request: Request) -> JSONResponse:
        """List source files - from symbol tables if loaded, otherwise scan project dir."""
        project = request.query_params.get("project", "")
        files: dict[str, int] = {}  # file -> mapped line count

        # Try symbol tables first
        try:
            from . import symbols as sym_mod
            tables = sym_mod.get_all_tables()
            for proj_name, sym_table in tables.items():
                if project and proj_name != project:
                    continue
                for sl in sym_table.source_lines:
                    if sl.file not in files:
                        files[sl.file] = 0
                    files[sl.file] += 1
        except Exception:
            pass

        # If no symbol sources found, scan project directory on disk
        if not files and project:
            proj_dir = os.path.join(os.getcwd(), "examples", project)
            if os.path.isdir(proj_dir):
                for fname in sorted(os.listdir(proj_dir)):
                    if fname.endswith(('.c', '.h')):
                        full_path = os.path.join(proj_dir, fname)
                        try:
                            with open(full_path) as f:
                                line_count = sum(1 for _ in f)
                            files[full_path] = line_count
                        except Exception:
                            pass

        result = [{"file": f, "mappedLines": c} for f, c in sorted(files.items())]
        return JSONResponse({"sources": result})

    async def api_debugger_locals(request: Request) -> JSONResponse:
        """Get local variables for the current stopped PC."""
        assert _dbg_state is not None
        if not _dbg_state.stopped:
            return JSONResponse({"locals": [], "func": ""})

        pc = _dbg_state.pc
        code_base = _dbg_state.code_base
        rel_pc = pc - code_base if code_base > 0 and pc >= code_base else pc

        # Get A5 (frame pointer) from saved registers
        a5 = _dbg_state.regs[13] if len(_dbg_state.regs) > 13 else 0

        func_name = ""
        locals_info: list[dict] = []

        try:
            from . import symbols as sym_mod
            tables = sym_mod.get_all_tables()
            for proj_name, sym_table in tables.items():
                fn, local_vars = sym_table.get_locals_at(rel_pc)
                if fn and local_vars:
                    func_name = fn
                    for lv in local_vars:
                        entry: dict = {
                            "name": lv["name"],
                            "type": lv["type"],
                            "kind": lv["kind"],
                            "offset": lv["offset"],
                        }
                        # Note: can't read memory via INSPECT while target is paused
                        # in Wait(). Would need to read tc_SPReg-based stack frame.
                        locals_info.append(entry)
                    break
        except Exception:
            pass

        return JSONResponse({"func": func_name, "a5": a5, "locals": locals_info})

    async def api_debugger_break(request: Request) -> JSONResponse:
        """Break (stop) the running target."""
        assert _conn is not None and _event_bus is not None and _dbg_state is not None
        if not _conn.connected:
            return JSONResponse({"error": "not connected"}, status_code=400)
        if not _dbg_state.attached:
            return JSONResponse({"error": "not attached"}, status_code=400)
        if _dbg_state.stopped:
            return JSONResponse(_dbg_state.to_dict())

        # Send DBGBREAK, then poll _dbg_state
        _conn.send_raw("DBGBREAK")

        for attempt in range(15):
            await asyncio.sleep(0.3)
            if _dbg_state.stopped:
                return JSONResponse(_dbg_state.to_dict())
            if attempt % 3 == 2:
                _conn.send_raw("DBGSTATUS")

        if _dbg_state.stopped:
            return JSONResponse(_dbg_state.to_dict())
        return JSONResponse({"error": "timeout"}, status_code=504)

    # Define routes
    routes = [
        # Web API
        Route("/api/status", api_status, methods=["GET"]),
        Route("/api/events", api_events, methods=["GET"]),
        Route("/api/logs", api_logs, methods=["GET"]),
        Route("/api/clients", api_clients, methods=["GET"]),
        Route("/api/tasks", api_tasks, methods=["GET"]),
        Route("/api/dir", api_dir, methods=["GET"]),
        Route("/api/file", api_file, methods=["GET"]),
        Route("/api/memory", api_memory, methods=["GET"]),
        Route("/api/vars", api_vars, methods=["GET"]),
        Route("/api/volumes", api_volumes, methods=["GET"]),
        Route("/api/command", api_command, methods=["POST"]),
        Route("/api/command/raw", api_command_raw, methods=["POST"]),
        Route("/api/launch", api_launch, methods=["POST"]),
        Route("/api/run", api_run, methods=["POST"]),
        Route("/api/ping", api_ping, methods=["POST"]),
        Route("/api/break", api_break, methods=["POST"]),
        Route("/api/hooks", api_hooks, methods=["GET"]),
        Route("/api/hooks/call", api_call_hook, methods=["POST"]),
        Route("/api/memregions", api_memregions, methods=["GET"]),
        Route("/api/memregions/read", api_read_memregion, methods=["POST"]),
        Route("/api/client-info", api_client_info, methods=["GET"]),
        Route("/api/stop", api_stop, methods=["POST"]),
        Route("/api/run-cycle", api_run_cycle, methods=["POST"]),
        Route("/api/script", api_script, methods=["POST"]),
        Route("/api/memory/write", api_write_memory, methods=["POST"]),
        Route("/api/connect", api_connect, methods=["POST"]),
        Route("/api/disconnect", api_disconnect, methods=["POST"]),
        # Tool endpoints
        Route("/api/screenshot", api_tool_screenshot, methods=["GET"]),
        Route("/api/screenshot/view", api_screenshot_view, methods=["GET"]),
        Route("/api/screenshot/list", api_screenshot_list, methods=["GET"]),
        Route("/api/windows", api_windows, methods=["GET"]),
        Route("/api/palette", api_tool_palette, methods=["GET"]),
        Route("/api/copper", api_tool_copper, methods=["GET"]),
        Route("/api/sprites", api_tool_sprites, methods=["GET"]),
        Route("/api/disasm", api_tool_disasm, methods=["GET"]),
        Route("/api/symbols/load", api_symbols_load, methods=["POST"]),
        Route("/api/symbols/lookup", api_symbols_lookup, methods=["GET"]),
        Route("/api/symbols/functions", api_symbols_functions, methods=["GET"]),
        Route("/api/symbols/structs", api_symbols_structs, methods=["GET"]),
        Route("/api/symbols/loaded", api_symbols_loaded, methods=["GET"]),
        Route("/api/crash", api_tool_crash, methods=["GET"]),
        Route("/api/crash/enable", api_crash_enable, methods=["POST"]),
        Route("/api/crash/disable", api_crash_disable, methods=["POST"]),
        Route("/api/crashtest", api_crashtest, methods=["POST"]),
        Route("/api/resources", api_tool_resources, methods=["GET"]),
        Route("/api/perf", api_tool_perf, methods=["GET"]),
        Route("/api/projects", api_projects, methods=["GET"]),
        Route("/api/create-project", api_tool_create_project, methods=["POST"]),
        # New tool endpoints
        Route("/api/tools/memory-search", api_tool_memory_search, methods=["GET"]),
        Route("/api/tools/bitmap", api_tool_bitmap, methods=["GET"]),
        Route("/api/tools/memmap", api_tool_memmap, methods=["GET"]),
        Route("/api/tools/stackinfo", api_tool_stackinfo, methods=["GET"]),
        Route("/api/tools/chipregs", api_tool_chipregs, methods=["GET"]),
        Route("/api/tools/readregs", api_tool_readregs, methods=["GET"]),
        Route("/api/tools/iff", api_tool_iff, methods=["GET"]),
        Route("/api/tools/snapshot", api_tool_snapshot, methods=["POST"]),
        Route("/api/tools/bootlog", api_tool_bootlog, methods=["GET"]),
        Route("/api/tools/sysinfo", api_tool_sysinfo, methods=["GET"]),
        # Library/Device info and SnoopDos
        Route("/api/tool/libs", api_tool_list_libs, methods=["GET"]),
        Route("/api/tool/devices", api_tool_list_devices, methods=["GET"]),
        Route("/api/tool/libinfo", api_tool_libinfo, methods=["POST"]),
        Route("/api/tool/devinfo", api_tool_devinfo, methods=["POST"]),
        Route("/api/tool/libfuncs", api_tool_libfuncs, methods=["GET"]),
        Route("/api/tool/snoop/start", api_tool_snoop_start, methods=["POST"]),
        Route("/api/tool/snoop/stop", api_tool_snoop_stop, methods=["POST"]),
        Route("/api/tool/snoop/status", api_tool_snoop_status, methods=["GET"]),
        # Audio inspector
        Route("/api/tool/audio/channels", api_tool_audio_channels, methods=["GET"]),
        Route("/api/tool/audio/sample", api_tool_audio_sample, methods=["GET"]),
        # Intuition inspector
        Route("/api/tool/screens", api_tool_screens, methods=["GET"]),
        Route("/api/tool/screen/windows", api_tool_screen_windows, methods=["GET"]),
        Route("/api/tool/gadgets", api_tool_gadgets, methods=["GET"]),
        # Input injection
        Route("/api/tool/input/key", api_tool_input_key, methods=["POST"]),
        Route("/api/tool/input/move", api_tool_input_move, methods=["POST"]),
        Route("/api/tool/input/click", api_tool_input_click, methods=["POST"]),
        Route("/api/tool/win/activate", api_tool_win_activate, methods=["POST"]),
        Route("/api/tool/win/tofront", api_tool_win_tofront, methods=["POST"]),
        Route("/api/tool/win/toback", api_tool_win_toback, methods=["POST"]),
        Route("/api/tool/win/zip", api_tool_win_zip, methods=["POST"]),
        Route("/api/tool/win/move", api_tool_win_move, methods=["POST"]),
        Route("/api/tool/win/size", api_tool_win_size, methods=["POST"]),
        Route("/api/tool/scr/tofront", api_tool_scr_tofront, methods=["POST"]),
        Route("/api/tool/scr/toback", api_tool_scr_toback, methods=["POST"]),
        # Assign Manager
        Route("/api/tools/assigns", api_tool_assigns, methods=["GET"]),
        Route("/api/tools/assign/set", api_tool_assign_set, methods=["POST"]),
        Route("/api/tools/assign/remove", api_tool_assign_remove, methods=["POST"]),
        # Font Browser
        Route("/api/tools/fonts", api_tool_fonts, methods=["GET"]),
        Route("/api/tools/fontinfo", api_tool_fontinfo, methods=["GET"]),
        # Locale/Catalog Inspector
        Route("/api/tools/locale/catalogs", api_tool_locale, methods=["GET"]),
        Route("/api/tools/locale/strings", api_tool_locale_strings, methods=["GET"]),
        # Custom Chip Write Logger
        Route("/api/tools/chiplog/start", api_tool_chiplog_start, methods=["POST"]),
        Route("/api/tools/chiplog/stop", api_tool_chiplog_stop, methods=["POST"]),
        Route("/api/tools/chiplog/snapshot", api_tool_chiplog_snapshot, methods=["GET"]),
        # Memory Pool Tracker
        Route("/api/tools/pool/start", api_tool_pool_start, methods=["POST"]),
        Route("/api/tools/pool/stop", api_tool_pool_stop, methods=["POST"]),
        Route("/api/tools/pools", api_tool_pools, methods=["GET"]),
        # Startup-Sequence Editor
        Route("/api/tools/startup/read", api_tool_startup_read, methods=["GET"]),
        Route("/api/tools/startup/write", api_tool_startup_write, methods=["POST"]),
        # Preferences Editor
        Route("/api/tools/prefs/list", api_tool_prefs_list, methods=["GET"]),
        Route("/api/tools/prefs/read", api_tool_prefs_read, methods=["GET"]),
        # Visual Diff
        Route("/api/tools/screenshot/diff", api_tool_screenshot_diff, methods=["POST"]),
        Route("/api/tools/screenshot/diffview", api_tool_screenshot_diff_view, methods=["GET"]),
        # CLI History
        Route("/api/cli/history", api_cli_history, methods=["GET"]),
        Route("/api/cli/history/add", api_cli_history_add, methods=["POST"]),
        Route("/api/cli/aliases", api_cli_aliases, methods=["GET", "POST"]),
        # ARexx Bridge
        Route("/api/tools/arexx/ports", api_tool_arexx_ports, methods=["GET"]),
        Route("/api/tools/arexx/send", api_tool_arexx_send, methods=["POST"]),
        # Clipboard Bridge
        Route("/api/tools/clipboard/get", api_tool_clipboard_get, methods=["GET"]),
        Route("/api/tools/clipboard/set", api_tool_clipboard_set, methods=["POST"]),
        # Protocol-native tools
        Route("/api/tools/capabilities", api_tool_capabilities, methods=["GET"]),
        Route("/api/tools/proclist", api_tool_proclist, methods=["GET"]),
        Route("/api/tools/signal", api_tool_signal, methods=["POST"]),
        Route("/api/tools/checksum", api_tool_checksum, methods=["GET"]),
        Route("/api/tools/protect", api_tool_protect, methods=["GET", "POST"]),
        Route("/api/tools/rename", api_tool_rename, methods=["POST"]),
        Route("/api/tools/copy", api_tool_copy, methods=["POST"]),
        Route("/api/tools/setcomment", api_tool_set_comment, methods=["POST"]),
        Route("/api/tools/append", api_tool_append, methods=["POST"]),
        Route("/api/tools/tail/start", api_tool_tail_start, methods=["POST"]),
        Route("/api/tools/tail/stop", api_tool_tail_stop, methods=["POST"]),
        # System tools
        Route("/api/tools/version", api_tool_version, methods=["GET"]),
        Route("/api/tools/env/get", api_tool_get_env, methods=["GET"]),
        Route("/api/tools/env/set", api_tool_set_env, methods=["POST"]),
        Route("/api/tools/setdate", api_tool_set_date, methods=["POST"]),
        Route("/api/tools/volumes", api_tool_list_volumes, methods=["GET"]),
        Route("/api/tools/ports", api_tool_list_ports, methods=["GET"]),
        Route("/api/tools/sysinfo2", api_tool_sysinfo_full, methods=["GET"]),
        Route("/api/tools/uptime", api_tool_uptime, methods=["GET"]),
        Route("/api/tools/reboot", api_tool_reboot, methods=["POST"]),
        Route("/api/tools/shutdown", api_tool_shutdown_bridge, methods=["POST"]),
        # Files tab endpoints
        Route("/api/file/execute", api_file_execute, methods=["POST"]),
        Route("/api/file/view", api_file_view, methods=["GET"]),
        Route("/api/file/save", api_file_save, methods=["POST"]),
        Route("/api/file/run-with", api_file_run_with, methods=["POST"]),
        # Emulator management
        Route("/api/emulator/status", api_emulator_status, methods=["GET"]),
        Route("/api/emulator/start", api_emulator_start, methods=["POST"]),
        Route("/api/emulator/stop", api_emulator_stop, methods=["POST"]),
        Route("/api/emulator/restart", api_emulator_restart, methods=["POST"]),
        Route("/api/emulator/config", api_emulator_config_get, methods=["GET"]),
        Route("/api/emulator/config", api_emulator_config_save, methods=["POST"]),
        # FS-UAE remote-debug RPC (patched fs-uae only; degrades gracefully)
        Route("/api/fsuae/status", api_fsuae_status, methods=["GET"]),
        Route("/api/fsuae/probe", api_fsuae_probe, methods=["POST"]),
        Route("/api/fsuae/cpu", api_fsuae_cpu, methods=["GET"]),
        Route("/api/fsuae/state", api_fsuae_state, methods=["GET"]),
        Route("/api/fsuae/pause", api_fsuae_pause, methods=["POST"]),
        Route("/api/fsuae/resume", api_fsuae_resume, methods=["POST"]),
        Route("/api/fsuae/step", api_fsuae_step, methods=["POST"]),
        Route("/api/fsuae/reset", api_fsuae_reset, methods=["POST"]),
        Route("/api/fsuae/mem", api_fsuae_mem, methods=["GET", "POST"]),
        Route("/api/fsuae/disasm", api_fsuae_disasm, methods=["GET"]),
        Route("/api/fsuae/custom", api_fsuae_custom, methods=["GET"]),
        Route("/api/fsuae/memmap", api_fsuae_memmap, methods=["GET"]),
        Route("/api/fsuae/stack", api_fsuae_stack, methods=["GET"]),
        Route("/api/fsuae/breakpoints", api_fsuae_breakpoints, methods=["GET", "POST"]),
        Route("/api/fsuae/breakpoints/clear", api_fsuae_breakpoints_clear, methods=["POST"]),
        Route("/api/fsuae/breakpoints/by-symbol", api_fsuae_bp_by_symbol, methods=["POST"]),
        Route("/api/fsuae/watchpoints", api_fsuae_watchpoints, methods=["GET", "POST"]),
        Route("/api/fsuae/watchpoints/clear", api_fsuae_watchpoints_clear, methods=["POST"]),
        Route("/api/fsuae/watchpoints/last", api_fsuae_watchpoints_last, methods=["GET"]),
        Route("/api/fsuae/state/save", api_fsuae_state_save, methods=["POST"]),
        Route("/api/fsuae/state/load", api_fsuae_state_load, methods=["POST"]),
        Route("/api/fsuae/cpu/write", api_fsuae_cpu_write, methods=["POST"]),
        Route("/api/fsuae/snapshot/list", api_fsuae_snapshot_list, methods=["GET"]),
        Route("/api/fsuae/snapshot/slot/{slot}/{action}", api_fsuae_snapshot_slot, methods=["POST"]),
        Route("/api/fsuae/snapshot/label", api_fsuae_snapshot_label, methods=["POST"]),
        Route("/api/fsuae/snapshot/diff", api_fsuae_snapshot_diff, methods=["GET"]),
        Route("/api/fsuae/snapshot/autosnap/status", api_fsuae_autosnap_status, methods=["GET"]),
        Route("/api/fsuae/snapshot/autosnap/set", api_fsuae_autosnap_set, methods=["POST"]),
        Route("/api/fsuae/symbols/lookup", api_fsuae_symbol_lookup, methods=["GET"]),
        Route("/api/fsuae/fd/libraries", api_fsuae_fd_libraries, methods=["GET"]),
        Route("/api/fsuae/fd/lookup", api_fsuae_fd_lookup, methods=["GET"]),
        Route("/api/fsuae/fd/load", api_fsuae_fd_load, methods=["POST"]),
        # DevBench config
        Route("/api/config", api_devbench_config_get, methods=["GET"]),
        Route("/api/config", api_devbench_config_save, methods=["POST"]),
        # Traffic log
        Route("/api/traffic", api_traffic_list, methods=["GET"]),
        Route("/api/traffic/detail", api_traffic_detail, methods=["GET"]),
        Route("/api/traffic/clear", api_traffic_clear, methods=["POST"]),
        Route("/api/traffic/replay", api_traffic_replay, methods=["POST"]),

        # File transfer (host <-> Amiga serial)
        Route("/api/transfer", api_transfer, methods=["POST"]),
        # Debugger
        Route("/api/debugger/status", api_debugger_status, methods=["GET"]),
        Route("/api/debugger/attach", api_debugger_attach, methods=["POST"]),
        Route("/api/debugger/detach", api_debugger_detach, methods=["POST"]),
        Route("/api/debugger/breakpoints/set", api_debugger_bp_set, methods=["POST"]),
        Route("/api/debugger/breakpoints/clear", api_debugger_bp_clear, methods=["POST"]),
        Route("/api/debugger/breakpoints", api_debugger_bp_list, methods=["GET"]),
        Route("/api/debugger/step", api_debugger_step, methods=["POST"]),
        Route("/api/debugger/next", api_debugger_next, methods=["POST"]),
        Route("/api/debugger/continue", api_debugger_continue, methods=["POST"]),
        Route("/api/debugger/registers", api_debugger_regs, methods=["GET"]),
        Route("/api/debugger/registers/set", api_debugger_setreg, methods=["POST"]),
        Route("/api/debugger/backtrace", api_debugger_backtrace, methods=["GET"]),
        Route("/api/debugger/locals", api_debugger_locals, methods=["GET"]),
        Route("/api/debugger/break", api_debugger_break, methods=["POST"]),
        Route("/api/debugger/source", api_debugger_source, methods=["GET"]),
        Route("/api/debugger/source/file", api_debugger_source_file, methods=["GET"]),
        Route("/api/debugger/sources", api_debugger_sources_list, methods=["GET"]),
        Route("/health", health, methods=["GET"]),
        # Web UI - serve index.html at root
        Route("/", serve_index, methods=["GET"]),
        Route("/static/{filename}", serve_static, methods=["GET"]),
        # MCP endpoint - mount the ASGI handler directly
        Route("/mcp", mcp_asgi_handler, methods=["GET", "POST", "DELETE"]),
    ]

    app = Starlette(routes=routes, lifespan=lifespan)
    app.add_middleware(TrafficMiddleware)

    return app


_PID_FILE = "/tmp/amiga-devbench.pid"


def _kill_stale_instance() -> None:
    """Kill any previously running devbench process."""
    import signal as _signal

    try:
        with open(_PID_FILE) as f:
            old_pid = int(f.read().strip())
        # Check if process is still running. On Windows os.kill(pid, 0) can
        # raise OSError (WinError 87) or even SystemError for a stale/recycled
        # PID rather than a clean ProcessLookupError, so treat any failure here
        # as "not running".
        try:
            os.kill(old_pid, 0)
        except (OSError, SystemError):
            return  # Already dead (or not queryable on this platform)
        logger.info("Killing stale devbench process (pid %d)", old_pid)
        os.kill(old_pid, _signal.SIGTERM)
        # Wait briefly for it to die
        import time as _time
        for _ in range(10):
            _time.sleep(0.2)
            try:
                os.kill(old_pid, 0)
            except OSError:
                return  # Died
        # Force kill
        logger.warning("Force-killing stale devbench (pid %d)", old_pid)
        os.kill(old_pid, _signal.SIGKILL)
    except (FileNotFoundError, ValueError, ProcessLookupError):
        pass


def _write_pid_file() -> None:
    with open(_PID_FILE, "w") as f:
        f.write(str(os.getpid()))


def _remove_pid_file() -> None:
    try:
        os.unlink(_PID_FILE)
    except FileNotFoundError:
        pass


def run(args: Any, cfg: DevBenchConfig | None = None) -> None:
    """Run the server with uvicorn."""
    import atexit

    effective_log_level = cfg.log_level if cfg else args.log_level
    effective_port = cfg.server_port if cfg else args.port

    log_level = getattr(logging, effective_log_level.upper(), logging.INFO)

    logging.basicConfig(
        level=log_level,
        format="%(asctime)s [%(name)s] %(levelname)s: %(message)s",
        datefmt="%H:%M:%S",
    )

    # Ensure our module loggers are set to the requested level
    # (uvicorn may override root logger config)
    for name in ("amiga_devbench", "amiga_devbench.serial_conn",
                 "amiga_devbench.server", "amiga_devbench.protocol"):
        logging.getLogger(name).setLevel(log_level)

    # Kill any stale instance before starting
    _kill_stale_instance()
    _write_pid_file()
    atexit.register(_remove_pid_file)

    app = create_app(args, cfg)

    config = uvicorn.Config(
        app,
        host="0.0.0.0",
        port=effective_port,
        log_level=effective_log_level.lower(),
    )
    server = uvicorn.Server(config)
    server.run()
