# SPDX-License-Identifier: MIT
"""The stdio launcher against a real MCP HTTP server that the tests stop and
restart, and against a devbench it starts itself."""
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
    import sys
    import uvicorn
    from mcp.server.fastmcp import FastMCP
    from starlette.responses import JSONResponse

    mcp = FastMCP("fake-devbench")

    @mcp.tool()
    def echo(text: str) -> str:
        return "echo: " + text

    app = mcp.streamable_http_app()
    app.add_route("/health", lambda request: JSONResponse({"status": "ok"}))
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

    def request(self, method: str, params: dict | None = None, timeout: float = 30) -> dict:
        self._next += 1
        msg = {"jsonrpc": "2.0", "id": self._next, "method": method}
        if params is not None:
            msg["params"] = params
        self.proc.stdin.write(json.dumps(msg) + "\n")
        self.proc.stdin.flush()
        deadline = time.monotonic() + timeout
        while True:
            m = self.inbox.get(timeout=max(0.1, deadline - time.monotonic()))
            if m.get("id") == self._next and "method" not in m:
                return m
            self.notes.append(m)

    def initialize(self) -> dict:
        r = self.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                        "clientInfo": {"name": "test", "version": "0"}})
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
        self.proc.stdin.close()
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
        assert c.tools() == ["echo"]
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
        assert c.tools() == ["echo"]
        assert "echo: back" in c.text(c.call("echo", text="back"))
    finally:
        c.close()


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
    first = launcher(workdir, port, "--start-timeout", "60", *args)
    try:
        init = first.initialize()
        assert init["serverInfo"]["name"] == "amiga-dev", init
        assert "amiga_ping" in first.tools()
        first.close()
        # devbench outlives the client that started it ...
        assert httpx.get(f"http://127.0.0.1:{port}/health").status_code == 200
        pid = int(pid_file.read_text())
        # ... and the next client uses it rather than starting another
        second = launcher(workdir, port, *args)
        try:
            second.initialize()
            assert int(pid_file.read_text()) == pid
        finally:
            second.close()
    finally:
        if pid_file.exists():
            os.kill(int(pid_file.read_text()), signal.SIGTERM)
