# SPDX-License-Identifier: MIT
"""The stdio launcher against a real MCP HTTP server that the tests stop and
restart, and against a devbench it starts itself."""
import contextlib
import json
import os
import queue
import signal
import socket
import subprocess
import sys
import textwrap
import threading
import time
from pathlib import Path

import httpx
import pytest

from amiga_devbench.mcp_stdio import CONNECT_TOOL
from amiga_devbench.rundir import private_dir

FAKE_SERVER = textwrap.dedent("""
    import os
    import sys
    import time
    import uvicorn
    from mcp.server.fastmcp import FastMCP
    from starlette.responses import JSONResponse

    mcp = FastMCP("fake-devbench")

    @mcp.tool()
    def echo(text: str) -> str:
        return "echo: " + text

    @mcp.tool()
    async def wait_for(path: str) -> str:
        import asyncio
        while not os.path.exists(path):
            await asyncio.sleep(0.05)
        return "released"

    @mcp.tool()
    def crash(marker: str) -> str:
        with open(marker, "a") as f:
            f.write("ran\\n")
        os._exit(1)             # gone while the client waits for the answer

    app = mcp.streamable_http_app()
    app.add_route("/health", lambda request: JSONResponse({"status": "ok"}))
    time.sleep(float(sys.argv[2]) if len(sys.argv) > 2 else 0)   # a slow start
    uvicorn.run(app, host="127.0.0.1", port=int(sys.argv[1]), log_level="warning")
""")


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def wait_listening(port: int, timeout: float = 15.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        with socket.socket() as s:
            if s.connect_ex(("127.0.0.1", port)) == 0:
                return
        time.sleep(0.1)
    raise AssertionError(f"nothing listens on {port}")


class FakeDevbench:
    def __init__(self, tmp_path: Path, port: int):
        self.script = tmp_path / "fake_devbench.py"
        self.script.write_text(FAKE_SERVER)
        self.port = port
        self.proc: subprocess.Popen | None = None

    def start(self) -> None:
        self.proc = subprocess.Popen([sys.executable, str(self.script), str(self.port)])
        wait_listening(self.port)

    def stop(self) -> None:
        if self.proc:
            self.proc.terminate()
            self.proc.wait(10)
            self.proc = None


class Client:
    """Drives the launcher over its stdin/stdout like Codex does."""

    def __init__(self, cwd: Path, *args: str):
        self.proc = subprocess.Popen(
            [sys.executable, "-m", "amiga_devbench.mcp_stdio", *args],
            cwd=cwd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        self.inbox: queue.Queue = queue.Queue()
        self.notes: list[dict] = []
        self._next = 0
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self) -> None:
        for line in self.proc.stdout:
            self.inbox.put(json.loads(line))

    def notify(self, method: str) -> None:
        self.proc.stdin.write(json.dumps({"jsonrpc": "2.0", "method": method}) + "\n")
        self.proc.stdin.flush()

    def send(self, method: str, params: dict | None = None) -> int:
        self._next += 1
        msg = {"jsonrpc": "2.0", "id": self._next, "method": method}
        if params is not None:
            msg["params"] = params
        self.proc.stdin.write(json.dumps(msg) + "\n")
        self.proc.stdin.flush()
        return self._next

    def wait(self, wanted: int, timeout: float = 30) -> dict:
        deadline = time.monotonic() + timeout
        while True:
            m = self.inbox.get(timeout=max(0.1, deadline - time.monotonic()))
            if m.get("id") == wanted and "method" not in m:
                return m
            self.notes.append(m)

    def request(self, method: str, params: dict | None = None, timeout: float = 30) -> dict:
        return self.wait(self.send(method, params), timeout)

    def initialize(self, timeout: float = 30) -> dict:
        r = self.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                        "clientInfo": {"name": "test", "version": "0"}},
                         timeout=timeout)
        self.notify("notifications/initialized")
        return r["result"]

    def tools(self) -> list[str]:
        return [t["name"] for t in self.request("tools/list")["result"]["tools"]]

    def call(self, name: str, **arguments) -> dict:
        return self.request("tools/call", {"name": name, "arguments": arguments})

    def text(self, reply: dict) -> str:
        return reply["result"]["content"][0]["text"]

    def saw_tools_changed(self) -> bool:
        return any(n.get("method") == "notifications/tools/list_changed" for n in self.notes)

    def close(self) -> None:
        if self.proc.poll() is None:
            self.proc.stdin.close()
            try:
                self.proc.wait(10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(10)


@pytest.fixture
def workdir(tmp_path):
    (tmp_path / "devbench.toml").write_text("")
    return tmp_path


@pytest.fixture
def fake(workdir):
    server = FakeDevbench(workdir, free_port())
    yield server
    server.stop()


def launcher(workdir: Path, port: int, *extra: str) -> Client:
    return Client(workdir, *extra, "--config", str(workdir / "devbench.toml"),
                  "--port", str(port))


def test_offline_says_what_is_wrong(workdir):
    port = free_port()
    c = launcher(workdir, port, "--no-start")
    try:
        init = c.initialize()
        assert init["serverInfo"]["version"] == "offline"
        assert init["capabilities"]["tools"]["listChanged"] is True
        assert f"DevBench is not running at http://127.0.0.1:{port}/mcp" in init["instructions"]
        assert "Start it with:" in init["instructions"]
        assert c.tools() == [CONNECT_TOOL]
        r = c.call(CONNECT_TOOL)
        assert r["result"]["isError"] and "not running" in c.text(r)
        r = c.call("amiga_ping")
        assert r["result"]["isError"] and "not running" in c.text(r)
        assert c.request("ping")["result"] == {}
    finally:
        c.close()


def test_proxies_to_devbench(workdir, fake):
    fake.start()
    c = launcher(workdir, fake.port, "--no-start")
    try:
        init = c.initialize()
        assert init["serverInfo"]["name"] == "fake-devbench"
        assert init["capabilities"]["tools"]["listChanged"] is True
        assert c.tools() == ["echo", "wait_for", "crash"]
        assert "echo: hi" in c.text(c.call("echo", text="hi"))
    finally:
        c.close()


def test_goes_offline_and_comes_back(workdir, fake):
    fake.start()
    c = launcher(workdir, fake.port, "--no-start")
    try:
        c.initialize()
        fake.stop()
        r = c.call("echo", text="hi")
        assert r["result"]["isError"]
        assert "Lost the connection to DevBench" in c.text(r)
        assert "Start it with:" in c.text(r)
        assert c.saw_tools_changed()
        assert c.tools() == [CONNECT_TOOL]

        fake.start()
        c.notes.clear()
        r = c.call(CONNECT_TOOL)
        assert not r["result"]["isError"], c.text(r)
        assert c.saw_tools_changed()
        assert c.tools() == ["echo", "wait_for", "crash"]
        assert "echo: back" in c.text(c.call("echo", text="back"))
    finally:
        c.close()


def test_a_ping_is_answered_while_a_call_runs(workdir, fake, tmp_path):
    fake.start()
    release = tmp_path / "release"
    c = launcher(workdir, fake.port, "--no-start")
    try:
        c.initialize()
        call = c.send("tools/call", {"name": "wait_for", "arguments": {"path": str(release)}})
        assert c.request("ping", timeout=10)["result"] == {}
        assert not any(n.get("id") == call for n in c.notes)     # still running
        release.touch()
        assert "released" in c.text(c.wait(call))
    finally:
        release.touch()
        c.close()


def test_a_tool_call_without_params_offline(workdir):
    c = launcher(workdir, free_port(), "--no-start")
    try:
        c.initialize()
        assert c.request("tools/call")["error"]["code"] == -32602
        assert c.request("ping")["result"] == {}
    finally:
        c.close()


def test_an_interrupted_call_is_not_sent_again(workdir, fake, tmp_path):
    fake.start()
    marker = tmp_path / "ran"
    c = launcher(workdir, fake.port, "--no-start")
    try:
        c.initialize()
        r = c.call("crash", marker=str(marker))
        assert r["result"]["isError"]
        assert "may or may not have carried out" in c.text(r), c.text(r)
        assert marker.read_text() == "ran\n"       # once, not replayed
    finally:
        c.close()


def test_a_second_launcher_waits_for_a_starting_devbench(workdir):
    from amiga_devbench.mcp_stdio import Devbench, Unreachable
    port = free_port()
    script = workdir / "slow_devbench.py"
    script.write_text(FAKE_SERVER)
    first = Devbench(["--config", str(workdir / "devbench.toml"), "--port", str(port)], True, 1.0)
    first.command = [sys.executable, str(script), str(port), "4"]
    second = Devbench(["--config", str(workdir / "devbench.toml"), "--port", str(port)], True, 30.0)
    spawned = []
    second._spawn = lambda: spawned.append(1)      # must not be needed
    pid = None
    try:
        with pytest.raises(Unreachable, match="didn't answer within"):
            first.ensure()
        pid = int(Path(first._starting_path()).read_text())
        note = second.ensure()          # waits for the first one's devbench
        assert f"pid {pid}" in note and "finished starting" in note, note
        assert not spawned
        assert not os.path.exists(first._starting_path())
    finally:
        if pid:
            with contextlib.suppress(OSError):
                os.kill(pid, signal.SIGTERM)


def test_new_session_after_devbench_restart(workdir, fake):
    fake.start()
    c = launcher(workdir, fake.port, "--no-start")
    try:
        c.initialize()
        fake.stop()
        fake.start()            # the old session is gone; the next call relinks
        assert "echo: again" in c.text(c.call("echo", text="again"))
    finally:
        c.close()


def test_never_starts_on_a_port_someone_else_holds(workdir):
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        s.listen()
        port = s.getsockname()[1]
        c = launcher(workdir, port, "--start-timeout", "1")
        try:
            init = c.initialize()
            assert f"Port {port} is in use" in init["instructions"]
            assert not (workdir / "logs" / f"devbench-{port}.log").exists()
        finally:
            c.close()


def test_starts_devbench_and_leaves_it_running(workdir):
    port, serial = free_port(), free_port()
    args = ("--simulator", "--serial-port", str(serial), "--no-emulator")
    pid_file = Path(private_dir()) / f"devbench-{port}.pid"
    starting = Path(private_dir()) / f"launcher-{port}.starting"
    clients = [launcher(workdir, port, "--start-timeout", "60", *args)]
    try:
        first = clients[0]
        init = first.initialize(timeout=90)       # the 60 s start and the handshake
        assert init["serverInfo"]["name"] == "amiga-dev", init
        assert "amiga_ping" in first.tools()
        first.close()
        # devbench outlives the client that started it ...
        assert httpx.get(f"http://127.0.0.1:{port}/health").status_code == 200
        pid = int(pid_file.read_text())
        # ... and the next client uses it rather than starting another
        clients.append(launcher(workdir, port, *args))
        clients[1].initialize()
        assert int(pid_file.read_text()) == pid
    finally:
        # The launchers first, so that none starts a devbench after this.
        for c in clients:
            c.proc.kill()
            c.proc.wait(10)
        pids = {int(f.read_text()) for f in (pid_file, starting) if f.exists()}
        for pid in pids:
            with contextlib.suppress(OSError):
                os.kill(pid, signal.SIGTERM)


def test_a_closed_session_is_not_sent_to():
    import asyncio
    from amiga_devbench.mcp_stdio import Backend, Unreachable

    async def run():
        async def deliver(msg):
            pass
        backend = Backend(f"http://127.0.0.1:{free_port()}/mcp", deliver)
        await backend.http.aclose()     # as another request's failure does
        with pytest.raises(Unreachable) as failure:
            await backend.post({"jsonrpc": "2.0", "id": 1, "method": "ping"})
        assert not failure.value.sent   # so it may go to a new session
    asyncio.run(run())


def test_a_foreign_runtime_directory_is_reported(workdir, monkeypatch):
    from amiga_devbench import mcp_stdio
    from amiga_devbench.rundir import NotOurDirectory

    def refuse():
        raise NotOurDirectory("/tmp/x is not a directory of ours")
    monkeypatch.setattr(mcp_stdio, "private_dir", refuse)
    devbench = mcp_stdio.Devbench(["--config", str(workdir / "devbench.toml"),
                                   "--port", str(free_port())], True, 1.0)
    with pytest.raises(mcp_stdio.Unreachable, match="not a directory of ours"):
        devbench.ensure()


def test_a_user_without_a_login_name_gets_a_runtime_directory(monkeypatch, tmp_path):
    import getpass
    import tempfile

    from amiga_devbench import rundir

    def nameless():
        raise KeyError("getpwuid(): uid not found")
    monkeypatch.setattr(getpass, "getuser", nameless)
    monkeypatch.setattr(tempfile, "gettempdir", lambda: str(tmp_path))
    assert os.path.isdir(rundir.private_dir())


@pytest.mark.skipif(os.name == "nt", reason="non-blocking pipes are POSIX")
def test_replies_wait_for_a_full_non_blocking_stdout(monkeypatch):
    from amiga_devbench import mcp_stdio

    r, w = os.pipe()
    os.set_blocking(w, False)

    class Out:
        def fileno(self):
            return w
    monkeypatch.setattr(sys, "stdout", Out())
    data = b"x" * (1 << 20)          # far more than a pipe holds
    received = bytearray()

    def read_slowly():
        time.sleep(0.2)              # let the pipe fill first
        while len(received) < len(data):
            received.extend(os.read(r, 65536))
    reader = threading.Thread(target=read_slowly)
    reader.start()
    try:
        mcp_stdio._write_out(data)
    finally:
        reader.join(10)
        os.close(r)
        os.close(w)
    assert bytes(received) == data
