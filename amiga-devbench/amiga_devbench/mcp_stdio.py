# SPDX-License-Identifier: MIT
"""MCP over stdio for clients that launch their servers, such as Codex and
Claude Code.

The launcher passes MCP messages between the client (stdin/stdout) and a
devbench's HTTP endpoint. It starts that devbench, detached, when nothing
listens on its port, and never stops it: a devbench is shared by the web UI
and every MCP client, and holds the only connection to the Amiga.

When devbench can't be reached, the launcher still answers the client. It
offers a single tool that says what is wrong and connects (starting devbench
if allowed), and tells the client when the devbench tools become available.

    python -m amiga_devbench.mcp_stdio [--no-start] [devbench options]

The devbench options (``--config``, ``--profile``, ``--port`` ...) are the
ones ``python -m amiga_devbench`` takes. They choose the port to connect to
and are passed on unchanged when the launcher starts devbench.
"""

from __future__ import annotations

import argparse
import asyncio
import contextlib
import json
import os
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any

import httpx

from .rundir import pid_alive as _alive
from .rundir import private_dir

CONNECT_TOOL = "amiga_devbench_connect"
LATEST_PROTOCOL = "2025-06-18"
PROTOCOLS = ("2024-11-05", "2025-03-26", "2025-06-18")
_OWN_ID = "amiga-devbench-launcher"
HANDSHAKE_TIMEOUT = 30.0    # for initialize once /health answers
STARTING_STALE = 600.0      # an older record of a starting devbench is ignored


class Unreachable(Exception):
    """Devbench isn't answering; the message says why and what to do.
    ``sent`` is True when the request may have reached devbench, so it
    must not be sent again."""

    def __init__(self, message: str, sent: bool = False):
        super().__init__(message)
        self.sent = sent


def log(msg: str) -> None:
    print(f"amiga-devbench mcp: {msg}", file=sys.stderr, flush=True)


# ── Starting devbench ────────────────────────────────────────────────

def _port_state(port: int) -> str:
    """'open', 'refused' or 'unknown' for 127.0.0.1:port."""
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=1.0):
            return "open"
    except ConnectionRefusedError:
        return "refused"
    except OSError:
        return "unknown"


@contextlib.contextmanager
def _start_lock(port: int):
    """Serialise check-and-start between launchers. A devbench kills any
    devbench already on its port, so two must never start at once."""
    path = os.path.join(private_dir(), f"launcher-{port}.lock")
    with open(path, "a+b") as f:
        if os.name == "nt":
            import msvcrt
            f.seek(0)
            while True:
                try:
                    msvcrt.locking(f.fileno(), msvcrt.LK_LOCK, 1)
                    break
                except OSError:
                    time.sleep(0.1)
            try:
                yield
            finally:
                f.seek(0)
                msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1)
        else:
            import fcntl
            fcntl.flock(f.fileno(), fcntl.LOCK_EX)
            try:
                yield
            finally:
                fcntl.flock(f.fileno(), fcntl.LOCK_UN)


class Devbench:
    """Where devbench runs, and how to start it."""

    def __init__(self, devbench_args: list[str], may_start: bool,
                 start_timeout: float):
        from .__main__ import build_parser
        from .config import apply_cli_overrides, load_config

        args = build_parser().parse_args(devbench_args)
        cfg = load_config(config_path=args.config, project_root=args.project_root)
        apply_cli_overrides(cfg, args)
        self.port = cfg.server_port
        self.url = f"http://127.0.0.1:{self.port}/mcp"
        self.args = devbench_args
        self.may_start = may_start
        self.start_timeout = start_timeout
        self.log_path = Path(cfg.project_root) / "logs" / f"devbench-{self.port}.log"
        self.command = [sys.executable, "-m", "amiga_devbench", *devbench_args]

    def start_hint(self) -> str:
        return f"Start it with:  {_shell_command(os.getcwd(), self.command)}"

    # A devbench that a launcher started and that doesn't answer yet. The
    # record outlives the start lock, so that no other launcher starts a
    # second one meanwhile (a devbench stops any other on its port).

    def _starting_path(self) -> str:
        return os.path.join(private_dir(), f"launcher-{self.port}.starting")

    def _starting(self) -> int | None:
        path = self._starting_path()
        try:
            if time.time() - os.path.getmtime(path) > STARTING_STALE:
                return None
            pid = int(Path(path).read_text().strip())
        except (OSError, ValueError):
            return None
        return pid if _alive(pid) else None

    def _record_starting(self, pid: int | None) -> None:
        path = self._starting_path()
        if pid is None:
            with contextlib.suppress(OSError):
                os.remove(path)
        else:
            Path(path).write_text(str(pid))

    def _healthy(self) -> bool:
        try:
            r = httpx.get(f"http://127.0.0.1:{self.port}/health", timeout=2.0)
            return r.status_code == 200
        except httpx.HTTPError:
            return False

    def _wait_healthy(self, timeout: float, running=None) -> bool:
        """Wait for /health; give up early once ``running()`` is False."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self._healthy():
                return True
            if running is not None and not running():
                return False
            time.sleep(0.25)
        return False

    def _spawn(self) -> subprocess.Popen:
        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        kwargs: dict[str, Any] = {}
        if os.name == "nt":
            kwargs["creationflags"] = (subprocess.DETACHED_PROCESS
                                       | subprocess.CREATE_NEW_PROCESS_GROUP)
        else:
            kwargs["start_new_session"] = True
        with open(self.log_path, "ab") as out:
            return subprocess.Popen(self.command, stdin=subprocess.DEVNULL,
                                    stdout=out, stderr=subprocess.STDOUT, **kwargs)

    def ensure(self, may_start: bool | None = None) -> str:
        """Make sure devbench answers. Returns a note on what happened;
        raises Unreachable with the reason and the fix otherwise."""
        if may_start is None:
            may_start = self.may_start
        try:
            with _start_lock(self.port):
                return self._ensure(may_start)
        except OSError as e:
            raise Unreachable(f"Can't start DevBench: {e}. {self.start_hint()}") from e

    def _ensure(self, may_start: bool) -> str:
        state = _port_state(self.port)
        if state == "refused":
            starting = self._starting()
            if starting is not None:
                if self._wait_healthy(self.start_timeout, lambda: _alive(starting)):
                    self._record_starting(None)
                    return f"DevBench (pid {starting}) finished starting at {self.url}."
                if _alive(starting):
                    raise Unreachable(
                        f"DevBench (pid {starting}) is still starting and doesn't answer "
                        f"yet. See {self.log_path}.")
                state = _port_state(self.port)
        if state == "open":
            if self._wait_healthy(self.start_timeout):
                self._record_starting(None)
                return f"DevBench is running at {self.url}."
            raise Unreachable(
                f"Port {self.port} is in use, but no DevBench answers at "
                f"http://127.0.0.1:{self.port}/health. Stop whatever holds "
                f"the port, or start DevBench on another port with --port.")
        if state == "unknown":
            raise Unreachable(
                f"Can't tell whether anything listens on 127.0.0.1:{self.port}.")
        if not may_start:
            raise Unreachable(
                f"DevBench is not running at {self.url}. {self.start_hint()}")
        log(f"starting devbench: {' '.join(self.command)} (log: {self.log_path})")
        proc = self._spawn()
        self._record_starting(proc.pid)
        # Reap it whenever it exits, so that it doesn't look alive.
        threading.Thread(target=proc.wait, daemon=True).start()
        if self._wait_healthy(self.start_timeout, lambda: proc.poll() is None):
            self._record_starting(None)
            return (f"Started DevBench (pid {proc.pid}) at {self.url}; "
                    f"its log is {self.log_path}.")
        if proc.poll() is None:
            raise Unreachable(
                f"Started DevBench (pid {proc.pid}), but it didn't answer within "
                f"{self.start_timeout:.0f} s. See {self.log_path}.")
        self._record_starting(None)
        raise Unreachable(
            f"DevBench exited during startup (status {proc.returncode}):\n"
            f"{_tail(self.log_path)}\nFull log: {self.log_path}")


def _quote(s: str) -> str:
    if s and all(c.isalnum() or c in "-_./:=@" for c in s):
        return s
    return "'" + s.replace("'", "'\\''") + "'"


def _shell_command(cwd: str, command: list[str]) -> str:
    """A command line that runs ``command`` in ``cwd``: for cmd.exe on
    Windows, for a POSIX shell elsewhere."""
    if os.name == "nt":
        return f'cd /d "{cwd}" && {subprocess.list2cmdline(command)}'
    return f"cd {_quote(cwd)} && " + " ".join(_quote(a) for a in command)


def _tail(path: Path, lines: int = 15) -> str:
    try:
        return "\n".join(path.read_text(errors="replace").splitlines()[-lines:])
    except OSError:
        return "(no log)"


# ── The MCP session ─────────────────────────────────────────────────

class Backend:
    """One MCP session with devbench's streamable HTTP endpoint."""

    def __init__(self, url: str, deliver):
        self.url = url
        self.deliver = deliver
        self.session_id: str | None = None
        self.protocol: str | None = None
        self.http = httpx.AsyncClient(timeout=httpx.Timeout(10.0, read=None))
        self._get_task: asyncio.Task | None = None

    def _headers(self) -> dict[str, str]:
        h = {"Accept": "application/json, text/event-stream",
             "Content-Type": "application/json"}
        if self.session_id:
            h["Mcp-Session-Id"] = self.session_id
        if self.protocol:
            h["MCP-Protocol-Version"] = self.protocol
        return h

    async def post(self, message: dict) -> list[dict]:
        """Send one message. Server messages that arrive before the reply are
        delivered at once; the returned list holds the reply (if any).
        Raises Unreachable when the session or the server has gone, with
        ``sent`` set unless devbench certainly didn't act on the message."""
        replies: list[dict] = []
        want = message.get("id") if "method" in message else None
        try:
            async with self.http.stream("POST", self.url, json=message,
                                        headers=self._headers()) as r:
                if r.status_code == 404 and self.session_id:
                    raise Unreachable("DevBench no longer knows this session "
                                      "(it was restarted).")
                if r.status_code == 202:
                    return replies
                if r.status_code >= 400:
                    body = (await r.aread()).decode(errors="replace")[:300]
                    raise Unreachable(f"DevBench answered HTTP {r.status_code}: {body}")
                sid = r.headers.get("mcp-session-id")
                if sid:
                    self.session_id = sid
                ctype = r.headers.get("content-type", "")
                if ctype.startswith("text/event-stream"):
                    async for msg in _sse_messages(r):
                        if want is not None and msg.get("id") == want and "method" not in msg:
                            replies.append(msg)
                            break
                        await self.deliver(msg)
                    if want is not None and not replies:
                        raise Unreachable("DevBench ended its answer stream without a reply.",
                                          sent=True)
                else:
                    data = json.loads(await r.aread() or b"null")
                    for msg in data if isinstance(data, list) else [data]:
                        if isinstance(msg, dict):
                            replies.append(msg)
        except httpx.TransportError as e:
            # Only a connection that was never made certainly didn't deliver it.
            sent = not isinstance(e, (httpx.ConnectError, httpx.ConnectTimeout,
                                      httpx.PoolTimeout))
            raise Unreachable(f"Lost the connection to DevBench at {self.url} ({e}).",
                              sent=sent) from e
        return replies

    def open_listener(self) -> None:
        """Receive server-initiated messages on the optional GET stream."""
        if self._get_task is None:
            self._get_task = asyncio.create_task(self._listen())

    async def _listen(self) -> None:
        with contextlib.suppress(httpx.HTTPError, asyncio.CancelledError):
            async with self.http.stream("GET", self.url, headers={
                    **self._headers(), "Accept": "text/event-stream"}) as r:
                if r.status_code != 200:
                    return
                async for msg in _sse_messages(r):
                    await self.deliver(msg)

    async def close(self) -> None:
        if self._get_task:
            self._get_task.cancel()
        if self.session_id:
            with contextlib.suppress(httpx.HTTPError):
                await self.http.delete(self.url, headers=self._headers(), timeout=2.0)
        await self.http.aclose()


async def _sse_messages(response: httpx.Response):
    data: list[str] = []
    async for line in response.aiter_lines():
        if line == "":
            if data:
                with contextlib.suppress(ValueError):
                    msg = json.loads("\n".join(data))
                    if isinstance(msg, dict):
                        yield msg
                data = []
        elif line.startswith("data:"):
            data.append(line[5:].lstrip(" "))


class Launcher:
    def __init__(self, devbench: Devbench):
        self.devbench = devbench
        self.backend: Backend | None = None
        self.client_init: dict | None = None
        self.offline_reason = ""
        self._out_lock = asyncio.Lock()
        self._link_lock = asyncio.Lock()
        self._own_ids = 0

    # Writing to the client

    async def send(self, msg: dict) -> None:
        line = json.dumps(msg, separators=(",", ":")) + "\n"
        async with self._out_lock:      # in order, and off the event loop:
            await asyncio.to_thread(_write_stdout, line)    # the client may read slowly

    async def reply(self, msg: dict, result: dict) -> None:
        await self.send({"jsonrpc": "2.0", "id": msg["id"], "result": result})

    async def error(self, msg: dict, code: int, text: str) -> None:
        await self.send({"jsonrpc": "2.0", "id": msg["id"],
                         "error": {"code": code, "message": text}})

    async def tools_changed(self) -> None:
        await self.send({"jsonrpc": "2.0", "method": "notifications/tools/list_changed"})

    # Linking to devbench

    async def _link(self, may_start: bool | None = None) -> tuple[str, dict]:
        """Open a session with devbench, initialised like the client's own.
        Returns the note from Devbench.ensure and devbench's initialize result."""
        note = await asyncio.to_thread(self.devbench.ensure, may_start)
        backend = Backend(self.devbench.url, self.send)
        self._own_ids += 1
        init = {**self.client_init, "id": f"{_OWN_ID}-{self._own_ids}"}
        try:
            try:
                replies = await asyncio.wait_for(backend.post(init), HANDSHAKE_TIMEOUT)
                if not replies or "result" not in replies[0]:
                    raise Unreachable(f"DevBench refused the session: {replies[:1]}")
                backend.protocol = replies[0]["result"].get("protocolVersion")
                await asyncio.wait_for(backend.post(
                    {"jsonrpc": "2.0", "method": "notifications/initialized"}), HANDSHAKE_TIMEOUT)
            except asyncio.TimeoutError:
                raise Unreachable(
                    f"DevBench answers at http://127.0.0.1:{self.devbench.port}/health but "
                    f"didn't open an MCP session at {self.devbench.url} within "
                    f"{HANDSHAKE_TIMEOUT:.0f} s.") from None
        except BaseException:
            await backend.close()
            raise
        backend.open_listener()
        self.backend = backend
        return note, replies[0]["result"]

    async def _go_offline(self, reason: str) -> None:
        if self.backend:
            await self.backend.close()
            self.backend = None
        self.offline_reason = reason
        log(reason)
        await self.tools_changed()

    async def _forward(self, msg: dict) -> None:
        """Pass a client message to devbench. If the session has gone, open
        a new one and, when devbench certainly never saw the message, send
        it once more; if devbench is gone, go offline and say so."""
        for attempt in (1, 2):
            backend = self.backend
            if backend is None:
                return await self._offline(msg)
            try:
                for reply in await backend.post(msg):
                    await self.send(reply)
                return
            except Unreachable as e:
                async with self._link_lock:
                    if self.backend is backend:     # else another task relinked
                        await backend.close()
                        self.backend = None
                        if attempt == 1:
                            with contextlib.suppress(Unreachable):
                                await self._link(may_start=False)
                                log("reconnected to DevBench")
                        if self.backend is None:
                            await self._go_offline(f"{e} {self.devbench.start_hint()}")
                if e.sent:
                    # It may have run (a build, a write): don't run it twice.
                    return await self._interrupted(msg, e)
        if "method" in msg and "id" in msg:
            await self.error(msg, -32603, self.offline_reason)

    async def _interrupted(self, msg: dict, e: Unreachable) -> None:
        if "method" not in msg or "id" not in msg:
            return
        text = (f"{e} DevBench may or may not have carried out this "
                f"{msg['method']} request; it was not sent again."
                + (f" {self.devbench.start_hint()}" if self.backend is None else ""))
        if msg["method"] == "tools/call":
            return await self.reply(msg, _text(text, error=True))
        await self.error(msg, -32603, text)

    # The client's messages

    async def handle(self, msg: dict) -> None:
        method = msg.get("method")
        if method == "initialize" and "id" in msg:
            return await self._initialize(msg)
        if self.client_init is None:
            if "id" in msg and method:
                await self.error(msg, -32002, "Send initialize first.")
            return
        if self.backend is not None:
            return await self._forward(msg)
        await self._offline(msg)

    async def _initialize(self, msg: dict) -> None:
        self.client_init = {k: msg[k] for k in ("jsonrpc", "method", "params") if k in msg}
        try:
            async with self._link_lock:
                note, result = await self._link()
            log(note)
            # Declare tools/list_changed: it's how the client learns that
            # devbench went away or came back.
            tools = result.setdefault("capabilities", {}).setdefault("tools", {})
            tools["listChanged"] = True
            return await self.reply(msg, result)
        except Unreachable as e:
            if self.backend:
                await self.backend.close()
                self.backend = None
            self.offline_reason = str(e)
            log(self.offline_reason)
        requested = (msg.get("params") or {}).get("protocolVersion")
        await self.reply(msg, {
            "protocolVersion": requested if requested in PROTOCOLS else LATEST_PROTOCOL,
            "capabilities": {"tools": {"listChanged": True}},
            "serverInfo": {"name": "amiga-dev", "version": "offline"},
            "instructions": (f"{self.offline_reason}\nThe Amiga DevBench tools appear once "
                             f"it runs: call {CONNECT_TOOL} to check again."),
        })

    async def _offline(self, msg: dict) -> None:
        method = msg.get("method")
        if "id" not in msg or not method:
            return                      # notifications and stray responses
        if method == "ping":
            return await self.reply(msg, {})
        if method == "tools/list":
            return await self.reply(msg, {"tools": [{
                "name": CONNECT_TOOL,
                "description": ("DevBench isn't reachable, so its Amiga tools are "
                                "unavailable. Call this to check again and connect"
                                + (" (starting DevBench if needed)." if self.devbench.may_start
                                   else ".")
                                + f" Last error: {self.offline_reason}"),
                "inputSchema": {"type": "object", "properties": {}},
            }]})
        if method == "tools/call":
            params = msg.get("params")
            if not isinstance(params, dict):
                return await self.error(msg, -32602, "tools/call needs params with a tool name")
            name = params.get("name")
            if name == CONNECT_TOOL:
                return await self._connect_tool(msg)
            return await self.reply(msg, _text(self.offline_reason, error=True))
        if method in ("resources/list", "resources/templates/list", "prompts/list"):
            key = {"resources/list": "resources", "prompts/list": "prompts",
                   "resources/templates/list": "resourceTemplates"}[method]
            return await self.reply(msg, {key: []})
        await self.error(msg, -32601, f"{method} is unavailable: {self.offline_reason}")

    async def _connect_tool(self, msg: dict) -> None:
        try:
            async with self._link_lock:
                note = self.backend and "DevBench is already connected."
                if not note:
                    note, _ = await self._link()
        except Unreachable as e:
            self.offline_reason = str(e)
            return await self.reply(msg, _text(self.offline_reason, error=True))
        log(note)
        await self.tools_changed()
        await self.reply(msg, _text(f"{note} The Amiga DevBench tools are available now."))

    async def close(self) -> None:
        if self.backend:
            await self.backend.close()


def _write_stdout(line: str) -> None:
    sys.stdout.write(line)
    sys.stdout.flush()


def _text(text: str, error: bool = False) -> dict:
    return {"content": [{"type": "text", "text": text}], "isError": error}


async def _serve(launcher: Launcher) -> None:
    loop = asyncio.get_running_loop()
    lines: asyncio.Queue[bytes | None] = asyncio.Queue()

    def read_stdin() -> None:
        # A thread, because asyncio can't read a pipe on stdin everywhere.
        for line in sys.stdin.buffer:
            loop.call_soon_threadsafe(lines.put_nowait, line)
        loop.call_soon_threadsafe(lines.put_nowait, None)

    threading.Thread(target=read_stdin, daemon=True).start()
    tasks: set[asyncio.Task] = set()
    try:
        while (line := await lines.get()) is not None:
            if not line.strip():
                continue
            try:
                msg = json.loads(line)
            except ValueError:
                await launcher.send({"jsonrpc": "2.0", "id": None, "error": {
                    "code": -32700, "message": "Parse error"}})
                continue
            if not isinstance(msg, dict):
                continue
            if msg.get("method") == "initialize":
                await launcher.handle(msg)  # everything else waits for it
                continue
            # Requests run concurrently; a long build mustn't block a ping.
            task = asyncio.create_task(launcher.handle(msg))
            tasks.add(task)
            task.add_done_callback(tasks.discard)
    finally:
        for t in tasks:
            t.cancel()
        await launcher.close()


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        prog="python -m amiga_devbench.mcp_stdio",
        description="MCP over stdio for Amiga DevBench. Starts devbench if it "
                    "isn't running; other options are devbench's own.")
    parser.add_argument("--no-start", action="store_true",
                        help="Only connect; never start devbench")
    parser.add_argument("--start-timeout", type=float, default=20.0,
                        help="Seconds to wait for devbench to answer (default: 20)")
    own, devbench_args = parser.parse_known_args(argv)
    try:
        devbench = Devbench(devbench_args, not own.no_start, own.start_timeout)
    except SystemExit:
        log("the options after the launcher's own must be devbench's "
            "(python -m amiga_devbench --help)")
        raise
    with contextlib.suppress(KeyboardInterrupt):
        asyncio.run(_serve(Launcher(devbench)))


if __name__ == "__main__":
    main()
