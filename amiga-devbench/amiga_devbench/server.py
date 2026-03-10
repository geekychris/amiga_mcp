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
from .deployer import Deployer
from .mcp_tools import mcp, init_tools
from .protocol import format_hex_dump, level_name
from .serial_conn import SerialConnection
from .simulator import AmigaSimulator
from .state import AmigaState, EventBus

logger = logging.getLogger(__name__)

# Shared instances (set during startup)
_conn: SerialConnection | None = None
_state: AmigaState | None = None
_event_bus: EventBus | None = None
_builder: Builder | None = None
_deployer: Deployer | None = None


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

        async with _event_bus.subscribe("log", "heartbeat", "var", "connected",
                                        "disconnected", "clients", "tasks", "dir") as queue:
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
        _conn.send({"type": "LISTDIR", "path": dir_path})
    except Exception:
        return JSONResponse({"path": dir_path, "entries": [], "error": "Send failed"})

    msg = await _event_bus.wait_for(
        "dir", timeout=5.0,
        predicate=lambda d: d.get("path") == dir_path,
    )
    if msg:
        return JSONResponse({"path": msg.get("path", dir_path), "entries": msg.get("entries", [])})
    return JSONResponse({"path": dir_path, "entries": [], "message": "No response"})


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
        return JSONResponse({"volumes": msg.get("names", [])})
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
            _conn.send({"type": "STOP", "name": name})
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
    cmd_id = int(time.time() * 1000) % 100000
    # Convert newlines to semicolons for the protocol
    script_line = script.replace("\n", ";")
    async with _event_bus.subscribe("cmd") as queue:
        try:
            _conn.send({"type": "SCRIPT", "id": cmd_id, "script": script_line})
        except Exception as e:
            return JSONResponse({"error": str(e)})
        deadline = asyncio.get_event_loop().time() + 30.0
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
    return JSONResponse({"status": "timeout", "output": "Script execution timed out"})


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
                deadline = asyncio.get_event_loop().time() + 2.0
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

        await asyncio.sleep(0.3)
    result["stop"] = {"status": stop_status, "message": stop_message}

    # 4. Launch
    launch_status = "skipped"
    launch_output = ""
    if _conn.connected:
        cmd_id = int(time.time() * 1000) % 100000
        try:
            _conn.send({"type": "LAUNCH", "id": cmd_id, "command": launch_command})
            msg = await _event_bus.wait_for(
                "cmd", timeout=5.0,
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
    """Serve the web UI index.html."""
    web_dir = Path(__file__).parent / "web"
    index_file = web_dir / "index.html"
    if index_file.is_file():
        return HTMLResponse(index_file.read_text())
    return HTMLResponse("<h1>Web UI not found</h1>", status_code=404)


# ─── Application Factory ───

def create_app(args: Any) -> Starlette:
    """Create the Starlette application with MCP + Web API + static files."""
    global _conn, _state, _event_bus, _builder, _deployer

    _state = AmigaState()
    _event_bus = EventBus()

    serial_host = args.serial_host or (
        "127.0.0.1" if args.simulator or args.serial_host else None
    )
    use_tcp = serial_host is not None or args.simulator

    _conn = SerialConnection(
        state=_state,
        event_bus=_event_bus,
        host=serial_host or "127.0.0.1",
        port=args.serial_port,
        pty_path=args.pty_path,
    )

    if not use_tcp:
        _conn.set_mode("pty", pty_path=args.pty_path)

    _builder = Builder(args.project_root)
    _deployer = Deployer(args.project_root, args.deploy_dir)

    # Initialize MCP tools with shared state
    init_tools(_conn, _state, _builder, _deployer, _event_bus)

    # Get the MCP session manager (triggers lazy init)
    _mcp_app_inner = mcp.streamable_http_app()
    session_manager = mcp._session_manager

    # Build the MCP ASGI handler directly
    from mcp.server.fastmcp.server import StreamableHTTPASGIApp
    mcp_asgi_handler = StreamableHTTPASGIApp(session_manager)

    @asynccontextmanager
    async def lifespan(app: Starlette) -> AsyncIterator[None]:
        """Combined lifespan: run MCP session manager + our startup."""
        sim = None

        async with session_manager.run():
            # Start simulator if requested
            if args.simulator:
                sim = AmigaSimulator(port=args.serial_port)
                await sim.start()
                await asyncio.sleep(0.2)

            # Print startup banner
            print()
            print("=" * 60)
            print("  Amiga DevBench")
            print("=" * 60)
            print(f"  MCP endpoint:  http://localhost:{args.port}/mcp")
            print(f"  Web UI:        http://localhost:{args.port}/")
            print(f"  Health check:  http://localhost:{args.port}/health")
            print(f"  Mode:          {'TCP' if use_tcp else 'PTY'}")
            if use_tcp:
                print(f"  Serial:        {_conn._host}:{_conn._port}")
            else:
                print(f"  PTY path:      {args.pty_path}")
            if args.simulator:
                print(f"  Simulator:     running on port {args.serial_port}")
            print("=" * 60)
            print()

            # Auto-connect
            try:
                await _conn.connect()
                if use_tcp:
                    logger.info("Auto-connected via TCP")
                else:
                    logger.info("PTY active: %s", args.pty_path)
            except Exception as e:
                logger.warning("Auto-connect failed: %s (use amiga_connect to retry)", e)

            yield

            # Shutdown
            if _conn:
                _conn.disconnect()
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
        cmd = {"type": "SCREENSHOT"}
        if window:
            cmd["window"] = window
        _conn.send(cmd)
        # Collect SCRINFO + SCRDATA
        scrinfo = await _event_bus.wait_for("scrinfo", timeout=5.0)
        if not scrinfo:
            return JSONResponse({"error": "No SCRINFO response"})
        rows = scrinfo.get("height", 0)
        depth = scrinfo.get("depth", 0)
        total_lines = rows * depth
        scrdata_lines = []
        deadline = asyncio.get_event_loop().time() + 15.0
        while len(scrdata_lines) < total_lines:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            msg = await _event_bus.wait_for("scrdata", timeout=remaining)
            if msg:
                scrdata_lines.append(msg)
        try:
            from .screenshot import save_screenshot
            # Save to fixed location with timestamp
            import datetime
            ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
            label = window.replace(" ", "_") if window else "screen"
            filename = f"{label}_{ts}.png"
            save_path = str(_screenshot_dir / filename)
            path = save_screenshot(scrinfo, scrdata_lines, save_path)
            _last_screenshot_path[0] = path
            return JSONResponse({
                "path": path,
                "filename": filename,
                "viewUrl": f"/api/screenshot/view?file={filename}",
                "width": scrinfo.get("width"),
                "height": rows,
                "depth": depth,
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
        addr = int(addr_str.replace("$", "").replace("0x", ""), 16)
        size = min(count * 10, 4096)
        cmd_id = int(time.time() * 1000) % 100000
        _conn.send({"type": "INSPECT", "address": f"{addr:08X}", "size": str(size)})
        msg = await _event_bus.wait_for("mem", timeout=5.0)
        if msg:
            try:
                from .disasm import disassemble_hex, format_listing
                hex_data = msg.get("hexData", "")
                result = disassemble_hex(hex_data, addr, count)
                listing = format_listing(result)
                return JSONResponse({"listing": listing})
            except Exception as e:
                return JSONResponse({"error": str(e)})
        return JSONResponse({"error": "No memory data response"})

    async def api_tool_crash(request: Request) -> JSONResponse:
        if _state and _state.last_crash:
            c = _state.last_crash
            report = f"Alert: ${c.get('alertNum', '?')} ({c.get('alertName', '?')})\n"
            dregs = c.get('dataRegs', [])
            aregs = c.get('addrRegs', [])
            if dregs:
                report += "D0-D7: " + " ".join(dregs) + "\n"
            if aregs:
                report += "A0-A7: " + " ".join(aregs) + "\n"
            report += f"SP: ${c.get('sp', '?')}\n"
            stack = c.get('stackHex', '')
            if stack:
                # Format stack hex as 4-byte groups
                groups = [stack[i:i+8] for i in range(0, len(stack), 8)]
                report += "Stack: " + " ".join(groups) + "\n"
            return JSONResponse({"report": report, "crash": c})
        return JSONResponse({"report": "No crash recorded"})

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
        Route("/api/crash", api_tool_crash, methods=["GET"]),
        Route("/api/crash/enable", api_crash_enable, methods=["POST"]),
        Route("/api/crash/disable", api_crash_disable, methods=["POST"]),
        Route("/api/crashtest", api_crashtest, methods=["POST"]),
        Route("/api/resources", api_tool_resources, methods=["GET"]),
        Route("/api/perf", api_tool_perf, methods=["GET"]),
        Route("/api/projects", api_projects, methods=["GET"]),
        Route("/api/create-project", api_tool_create_project, methods=["POST"]),
        Route("/health", health, methods=["GET"]),
        # Web UI - serve index.html at root
        Route("/", serve_index, methods=["GET"]),
        # MCP endpoint - mount the ASGI handler directly
        Route("/mcp", mcp_asgi_handler, methods=["GET", "POST", "DELETE"]),
    ]

    app = Starlette(routes=routes, lifespan=lifespan)

    return app


_PID_FILE = "/tmp/amiga-devbench.pid"


def _kill_stale_instance() -> None:
    """Kill any previously running devbench process."""
    import signal as _signal

    try:
        with open(_PID_FILE) as f:
            old_pid = int(f.read().strip())
        # Check if process is still running
        try:
            os.kill(old_pid, 0)
        except OSError:
            return  # Already dead
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


def run(args: Any) -> None:
    """Run the server with uvicorn."""
    import atexit

    log_level = getattr(logging, args.log_level.upper(), logging.INFO)

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

    app = create_app(args)

    config = uvicorn.Config(
        app,
        host="0.0.0.0",
        port=args.port,
        log_level=args.log_level.lower(),
    )
    server = uvicorn.Server(config)
    server.run()
