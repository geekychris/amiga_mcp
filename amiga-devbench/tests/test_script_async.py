"""Unit tests for the async SCRIPT execution path (run_script no-freeze fix).

Mocks the daemon's 'ASYNC|<capfile>' reply and the capture file so the host
orchestration (script_execute: parse ack, poll for the completion sentinel,
return output, clean up) is verified without a live Amiga. The underlying
READFILE primitive that _read_amiga_file_text uses is pre-existing/proven; here
we mock it to focus on the new logic.
"""
import asyncio

from amiga_devbench import mcp_tools


class _FakeBus:
    def __init__(self):
        self.q = None

    def subscribe(self, *events):
        bus = self

        class _CM:
            async def __aenter__(self):
                bus.q = asyncio.Queue()
                return bus.q

            async def __aexit__(self, *a):
                bus.q = None
                return False

        return _CM()


class _FakeConn:
    def __init__(self, bus, reply="ASYNC|T:ab_sout_X", connected=True):
        self.bus = bus
        self.sent = []
        self.connected = connected
        self._reply = reply

    def send(self, msg):
        self.sent.append(msg)
        if msg.get("type") == "SCRIPT" and self._reply is not None:
            cid = msg["id"]
            reply = self._reply
            asyncio.get_event_loop().call_soon(
                lambda: self.bus.q and self.bus.q.put_nowait(
                    ("cmd", {"id": cid, "status": "OK", "data": reply})))


def _patch_read(seq):
    """Replace _read_amiga_file_text with one returning successive values."""
    calls = {"n": 0}

    async def fake(conn, bus, path, **kw):
        i = min(calls["n"], len(seq) - 1)
        calls["n"] += 1
        return seq[i]

    mcp_tools._read_amiga_file_text = fake


async def _async_path_returns_output():
    bus = _FakeBus()
    conn = _FakeConn(bus, reply="ASYNC|T:ab_sout_7")
    # first poll: empty (still running); second: output + sentinel
    _patch_read(["", "hello world\n" + mcp_tools.SCRIPT_SENTINEL + "\nignored"])
    status, output = await mcp_tools.script_execute(conn, bus, "echo hi", timeout=10)
    assert status == "ok", status
    assert output == "hello world", repr(output)
    assert any(m.get("type") == "DELETEFILE" and m.get("path") == "T:ab_sout_7"
               for m in conn.sent), f"no cleanup: {conn.sent}"


async def _legacy_sync_daemon():
    bus = _FakeBus()
    conn = _FakeConn(bus, reply="line1;line2")     # old daemon: data IS the output
    _patch_read(["should not be used"])
    status, output = await mcp_tools.script_execute(conn, bus, "echo hi", timeout=10)
    assert status == "ok", status
    assert output == "line1\nline2", repr(output)


async def _no_ack_is_timeout():
    bus = _FakeBus()
    conn = _FakeConn(bus, reply=None, connected=True)   # never replies, still connected
    status, output = await mcp_tools.script_execute(conn, bus, "echo hi", timeout=2)
    assert status == "timeout", status


async def _no_ack_disconnected():
    bus = _FakeBus()
    conn = _FakeConn(bus, reply=None, connected=False)
    status, output = await mcp_tools.script_execute(conn, bus, "echo hi", timeout=2)
    assert status == "disconnected", status


async def _still_running_when_sentinel_never_comes():
    bus = _FakeBus()
    conn = _FakeConn(bus, reply="ASYNC|T:ab_sout_9")
    _patch_read(["partial output, no sentinel yet"])
    status, output = await mcp_tools.script_execute(conn, bus, "loop", timeout=1.2)
    assert status == "running", status
    assert "partial output" in output, repr(output)


# ── list_dir_all paging (BUG1) ──

class _PagingConn:
    connected = True

    def __init__(self):
        self.last_offset = 0

    def send(self, msg):
        self.last_offset = msg.get("offset", 0)


class _PagingBus:
    def __init__(self, conn, pages):
        self.conn, self.pages = conn, pages

    async def wait_for(self, ev, timeout=5.0, predicate=None):
        return self.pages.get(self.conn.last_offset)


async def _paging_collects_all():
    # daemon returns 2 pages, total=5
    pages = {0: {"path": "X:", "count": 5, "entries": [{"name": c} for c in "ABC"]},
             3: {"path": "X:", "count": 5, "entries": [{"name": c} for c in "DE"]}}
    conn = _PagingConn()
    got = await mcp_tools.list_dir_all(conn, _PagingBus(conn, pages), "X:")
    assert [e["name"] for e in got] == list("ABCDE"), got


async def _paging_stops_on_empty_page():
    # count claims 9 but the 2nd page is empty -> must NOT loop forever
    pages = {0: {"path": "Y:", "count": 9, "entries": [{"name": "Z"}]},
             1: {"path": "Y:", "count": 9, "entries": []}}
    conn = _PagingConn()
    got = await mcp_tools.list_dir_all(conn, _PagingBus(conn, pages), "Y:")
    assert [e["name"] for e in got] == ["Z"], got


def test_all():
    orig = mcp_tools._read_amiga_file_text
    try:
        asyncio.run(_async_path_returns_output())
        asyncio.run(_legacy_sync_daemon())
        asyncio.run(_no_ack_is_timeout())
        asyncio.run(_no_ack_disconnected())
        asyncio.run(_still_running_when_sentinel_never_comes())
        asyncio.run(_paging_collects_all())
        asyncio.run(_paging_stops_on_empty_page())
    finally:
        mcp_tools._read_amiga_file_text = orig


if __name__ == "__main__":
    test_all()
    print("OK: all host bridge-helper tests passed")
