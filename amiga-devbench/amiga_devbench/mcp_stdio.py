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
import concurrent.futures
import contextlib
import json
import os
import queue
import select
import signal
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any

import httpx

from .rundir import NotOurDirectory, private_dir
from .rundir import pid_alive as _alive

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
        except (OSError, NotOurDirectory) as e:
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
        try:
            self._record_starting(proc.pid)
        except OSError:
            # Unrecorded, it would race the next launcher's DevBench: stop it.
            proc.kill()
            proc.wait()
            raise
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


class PrivateDevbench(Devbench):
    """A DevBench and WinUAE of this session's own, on free ports, stopped
    when the session ends. See winuae_private."""

    # Options the private session sets itself, with the number of values each takes.
    OWN = {"--port": 1, "--serial-port": 1, "--serial-host": 1, "--mode": 1, "--deploy-dir": 1,
           "--emulator-binary": 1, "--emulator-config": 1, "--winuae-debug-port": 1, "--winuae-debug-host": 1,
           "--gdb-port": 1, "--fsuae-rpc": 1, "--no-emulator": 0, "--emulator": 0,
           "--simulator": 0, "--no-save-config": 0}

    def __init__(self, devbench_args: list[str], start_timeout: float):
        from . import winuae_private as wp
        from .__main__ import build_parser
        from .config import apply_cli_overrides, load_config
        from .emulator import is_winuae

        parser = build_parser()
        args = parser.parse_args(devbench_args)
        cfg = load_config(config_path=args.config, project_root=args.project_root)
        apply_cli_overrides(cfg, args)
        binary, config = cfg.emulator_binary, cfg.emulator_config
        if not is_winuae(binary or ""):
            raise wp.PrivateError(
                "--private needs WinUAE: pass --emulator-binary (WinUAE or WinUAE.app) and "
                "--emulator-config (the .uae of DevBench's WinUAE guest), or set them in [emulator]")
        if cfg.emulator_ssh:
            raise wp.PrivateError(
                f"the configuration drives a remote emulator over SSH ({cfg.emulator_ssh}); a "
                "private session runs WinUAE on this machine. Choose a profile without emulator_ssh")
        for name in wp.sweep():
            log(f"removed the private session {name} of a launcher that is gone")
        self.session = wp.new_session()
        try:
            self.gdb, self.bridge, self.bridge_gdb = wp.free_port(), wp.free_port(), wp.free_port()
            guest = wp.prepare_guest(config, self.session, self.bridge)
        except BaseException:
            wp.release_session(self.session)
            raise
        self.kept, skip = [], 0
        for arg in devbench_args:
            if skip:
                skip -= 1
                continue
            name = _full_option(parser, arg.split("=", 1)[0])
            if name in self.OWN:
                skip = self.OWN[name] if "=" not in arg else 0
                continue
            self.kept.append(arg)
        self.binary = binary
        self.may_start = True
        self.start_timeout = start_timeout
        self.log_path = self.session / "devbench.log"
        self.proc: subprocess.Popen | None = None
        self.guest = guest
        self._use_port(wp.free_port())
        log(f"private session {self.session.name}: DevBench {self.url}, "
            f"WinUAE GDB port {self.gdb}, guest bridge port {self.bridge}")

    def _use_port(self, http: int) -> None:
        """Run the session's DevBench on this HTTP port."""
        own = ["--port", str(http), "--mode", "tcp", "--serial-host", "127.0.0.1",
               "--serial-port", str(self.bridge), "--deploy-dir", str(self.guest.folder),
               "--emulator", "--emulator-binary", self.binary,
               "--emulator-config", str(self.guest.config),
               "--winuae-debug-host", "127.0.0.1", "--winuae-debug-port", str(self.gdb),
               "--gdb-port", str(self.bridge_gdb), "--fsuae-rpc", "off", "--no-save-config"]
        self.http = self.port = http
        self.url = f"http://127.0.0.1:{http}/mcp"
        self.args = self.kept + own
        self.command = [sys.executable, "-m", "amiga_devbench", *self.args]

    def start_hint(self) -> str:
        return (f"This session's private DevBench is not running; call {CONNECT_TOOL} to start "
                f"it again. Its log is {self.log_path}.")

    def describe(self) -> str:
        return (f"This session has a private WinUAE (DevBench web UI http://127.0.0.1:{self.http}/, "
                f"WinUAE GDB port {self.gdb}, guest bridge port {self.bridge}, bridge GDB port "
                f"{self.bridge_gdb}). It boots a copy of "
                f"{self.guest.source}; the user's other mounts are read-only in it. Deployed programs "
                f"land in {self.guest.folder}. DevBench and WinUAE stop when this session ends.")

    def _spawn(self) -> subprocess.Popen:
        self.proc = super()._spawn()
        (self.session / "devbench.pid").write_text(str(self.proc.pid))
        return self.proc

    def ensure(self, may_start: bool | None = None) -> str:
        from . import winuae_private as wp
        if self.proc is not None and self.proc.poll() is None:
            if self._healthy():
                return f"Private DevBench is running at {self.url}."
        elif _port_state(self.port) != "refused":
            # Not this session's DevBench: something else took the port.
            self._use_port(wp.free_port())
            log(f"private session {self.session.name}: DevBench moves to {self.url}")
        return super().ensure(may_start)

    def close(self) -> None:
        """Stop the session's DevBench, which stops its WinUAE, and remove
        the session folder once nothing of the session runs."""
        from . import winuae_private as wp
        if self.proc is not None and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(20)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        if wp.stop_session(self.session):       # anything DevBench left behind
            wp.release_session(self.session)
            log(f"private session {self.session.name} stopped")
        else:
            log(f"private session {self.session.name}: processes still run; "
                "the next --private launch removes it")


class Unavailable(Devbench):
    """A DevBench that cannot be set up; every connection attempt says why."""

    def __init__(self, reason: str):
        self.reason, self.may_start, self.url, self.port = reason, False, "", 0

    def start_hint(self) -> str:
        return ""

    def ensure(self, may_start: bool | None = None) -> str:
        raise Unreachable(self.reason)


def _full_option(parser: argparse.ArgumentParser, name: str) -> str:
    """The option an abbreviation such as --no-emu stands for, as argparse
    reads it; anything else unchanged."""
    if not name.startswith("--") or name in parser._option_string_actions:
        return name
    matches = [o for o in parser._option_string_actions if o.startswith(name)]
    return matches[0] if len(matches) == 1 else name


def _quote(s: str) -> str:
    if s and all(c.isalnum() or c in "-_./:=@" for c in s):
        return s
    return "'" + s.replace("'", "'\\''") + "'"


def _shell_command(cwd: str, command: list[str]) -> str:
    """A command line that runs ``command`` in ``cwd``: for cmd.exe on
    Windows, for a POSIX shell elsewhere."""
    if os.name == "nt":
        # Every argument in double quotes, so that cmd.exe's & | < > ^ ( )
        # in a path don't split the command.
        quoted = " ".join('"' + a.replace('"', '""') + '"' for a in command)
        return f'cd /d "{cwd}" && {quoted}'
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
        if self.http.is_closed:     # another request's failure closed this session
            raise Unreachable("The session with DevBench was closed.")
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
                else:
                    data = json.loads(await r.aread() or b"null")
                    for msg in data if isinstance(data, list) else [data]:
                        if isinstance(msg, dict):
                            replies.append(msg)
                if want is not None and not replies:
                    raise Unreachable("DevBench ended its answer without a reply.", sent=True)
        except RuntimeError as e:
            if not self.http.is_closed:
                raise
            raise Unreachable("The session with DevBench was closed.") from e
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
        with contextlib.suppress(Exception, asyncio.CancelledError):
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
            # Only a courtesy: any failure (asyncio can even raise
            # RuntimeError for a reused socket) must not lose the reply
            # that the caller still owes the client.
            with contextlib.suppress(Exception):
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
        # Replies go out in order through one writer thread, a daemon so
        # that a client that stops reading can't hold up the launcher's exit.
        self._out: queue.Queue = queue.Queue()
        threading.Thread(target=_stdout_writer, args=(self._out,), daemon=True).start()
        self._link_lock = asyncio.Lock()
        self._own_ids = 0

    # Writing to the client

    async def send(self, msg: dict) -> None:
        line = json.dumps(msg, separators=(",", ":")) + "\n"
        written: concurrent.futures.Future = concurrent.futures.Future()
        self._out.put((line, written))
        await asyncio.wrap_future(written)

    async def reply(self, msg: dict, result: dict) -> None:
        await self.send({"jsonrpc": "2.0", "id": msg["id"], "result": result})

    async def error(self, msg: dict, code: int, text: str) -> None:
        await self.send({"jsonrpc": "2.0", "id": msg["id"],
                         "error": {"code": code, "message": text}})

    async def tools_changed(self) -> None:
        # Queued without waiting for the write: it's sent while holding the
        # link lock, which a client that doesn't read must not keep held.
        line = json.dumps({"jsonrpc": "2.0", "method": "notifications/tools/list_changed"})
        self._out.put((line + "\n", concurrent.futures.Future()))

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
                                await self.tools_changed()  # it may have restarted
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
            if isinstance(self.devbench, PrivateDevbench):
                result["instructions"] = (self.devbench.describe() + "\n\n"
                                          + (result.get("instructions") or "")).strip()
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


def _stdout_writer(lines: queue.Queue) -> None:
    while True:
        line, written = lines.get()
        if not written.set_running_or_notify_cancel():
            continue                    # its sender was cancelled (shutting down)
        try:
            _write_out(line.encode())
        except BaseException as e:      # the client is gone
            written.set_exception(e)
        else:
            written.set_result(None)


def _write_out(data: bytes) -> None:
    """Write all of data to stdout, waiting while the pipe is full even if
    the client made it non-blocking."""
    fd = sys.stdout.fileno()
    view = memoryview(data)
    while view:
        try:
            view = view[os.write(fd, view):]
        except BlockingIOError:
            if os.name == "nt":
                time.sleep(0.05)
            else:
                select.select([], [fd], [])


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
    parser.add_argument("--private", action="store_true",
                        help="Give this session its own DevBench and WinUAE on free ports, "
                             "booting a copy of the WinUAE guest; both stop when the session ends")
    own, devbench_args = parser.parse_known_args(argv)
    if own.private and own.no_start:
        parser.error("--private starts its own DevBench; it cannot be combined with --no-start")
    try:
        if own.private:
            from .winuae_private import PrivateError
            try:
                devbench = PrivateDevbench(devbench_args, max(own.start_timeout, 30.0))
            except (PrivateError, OSError) as e:
                devbench = Unavailable(f"Cannot set up a private WinUAE session: {e}")
        else:
            devbench = Devbench(devbench_args, not own.no_start, own.start_timeout)
    except SystemExit:
        if not own.private:
            log("the options after the launcher's own must be devbench's "
                "(python -m amiga_devbench --help)")
        raise
    # A client usually ends the session by closing stdin; on SIGTERM, still
    # stop a private session's DevBench and WinUAE.
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
    try:
        with contextlib.suppress(KeyboardInterrupt):
            asyncio.run(_serve(Launcher(devbench)))
    finally:
        if isinstance(devbench, PrivateDevbench):
            devbench.close()


if __name__ == "__main__":
    main()
