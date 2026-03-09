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

    msg = await _event_bus.wait_for("clients", timeout=3.0)
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

    msg = await _event_bus.wait_for("tasks", timeout=3.0)
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

    async with _event_bus.subscribe("mem") as queue:
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

    msg = await _event_bus.wait_for("volumes", timeout=3.0)
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
        Route("/api/connect", api_connect, methods=["POST"]),
        Route("/api/disconnect", api_disconnect, methods=["POST"]),
        Route("/health", health, methods=["GET"]),
        # Web UI - serve index.html at root
        Route("/", serve_index, methods=["GET"]),
        # MCP endpoint - mount the ASGI handler directly
        Route("/mcp", mcp_asgi_handler, methods=["GET", "POST", "DELETE"]),
    ]

    app = Starlette(routes=routes, lifespan=lifespan)

    return app


def run(args: Any) -> None:
    """Run the server with uvicorn."""
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

    app = create_app(args)

    config = uvicorn.Config(
        app,
        host="0.0.0.0",
        port=args.port,
        log_level=args.log_level.lower(),
    )
    server = uvicorn.Server(config)
    server.run()
