"""Characterisation tests: pin the host's current behaviour at upstream 94a8888.

These do not assert what the protocol *should* do; they assert what it does
today, so that a change to the connect handshake, the CAPABILITIES / SYSINFO
parsers or the MCP tool surface shows up as a red test. Golden inputs live in
tests/fixtures/ (see the README there for provenance).
"""
import asyncio
import inspect
import pathlib

from amiga_devbench import serial_conn
from amiga_devbench.protocol import parse_message
from amiga_devbench.serial_conn import SerialConnection
from amiga_devbench.state import AmigaState, EventBus

FIXTURES = pathlib.Path(__file__).parent / "fixtures"

# Dispatched by protocol_handler.c:391-419 but absent from the advertised
# list in system_inspector.c:1242-1277 — the daemon handles them but has never
# advertised them; a host that derives capabilities from that list must know it.
DEBUGGER_VERBS = {
    "DBGATTACH", "DBGDETACH", "BPSET", "BPCLEAR", "BPLIST", "DBGSTEP", "DBGNEXT",
    "DBGCONT", "DBGREGS", "DBGSETREG", "DBGBT", "DBGBREAK", "DBGCLEARALLBP",
    "DBGSTATUS", "DBGLAUNCH",
}

# (fixture, expected advertised command count, LIBFUNCS advertised, cpuType)
BRIDGES = [
    ("bridge_v1.20_68k.txt", 105, True, "68030"),
    ("bridge_v1.20_ppc.txt", 104, False, "PowerPC"),
    ("bridge_v1.16.txt", 105, True, "68030"),
]


def _load(name):
    lines = (FIXTURES / name).read_text().splitlines()
    msgs = {}
    for line in lines:
        msg = parse_message(line)
        assert msg is not None, f"{name}: unparseable line {line[:60]!r}"
        msgs[msg["type"]] = msg
    return msgs


def _check_bridge(name, count, libfuncs, cpu):
    msgs = _load(name)
    assert set(msgs) == {"READY", "CAPABILITIES", "SYSINFO", "VERSION", "HB"}

    assert msgs["READY"]["version"] == "1.0"

    caps = msgs["CAPABILITIES"]
    assert caps["protocolLevel"] == 1
    assert caps["maxLine"] == 8192
    assert len(caps["commands"]) == count, len(caps["commands"])
    assert len(set(caps["commands"])) == count  # no duplicates
    assert ("LIBFUNCS" in caps["commands"]) is libfuncs
    assert not DEBUGGER_VERBS & set(caps["commands"]), "debugger verbs now advertised"

    sysinfo = msgs["SYSINFO"]
    assert sysinfo["cpuType"] == cpu
    raw = next(l for l in (FIXTURES / name).read_text().splitlines() if l.startswith("SYSINFO|"))
    assert len(raw.split("|")) == 9  # type + 8 documented fields
    assert len([k for k in sysinfo if k != "type"]) == 8

    assert msgs["HB"]["type"] == "HB"
    assert msgs["HB"]["tick"] == 1200

    ver = msgs["VERSION"]
    assert ver["name"] == "AmigaBridge"
    assert (ver["major"], ver["minor"]) == (1, int(name.split("v1.")[1][:2]))


def test_bridge_v1_20_68k():
    _check_bridge(*BRIDGES[0])


def test_bridge_v1_20_ppc():
    _check_bridge(*BRIDGES[1])


def test_bridge_v1_16():
    _check_bridge(*BRIDGES[2])


def test_advertised_lists_golden():
    """The 68k and v1.16 lists are identical; PPC is 68k minus LIBFUNCS."""
    k68 = set(_load("bridge_v1.20_68k.txt")["CAPABILITIES"]["commands"])
    ppc = set(_load("bridge_v1.20_ppc.txt")["CAPABILITIES"]["commands"])
    v16 = set(_load("bridge_v1.16.txt")["CAPABILITIES"]["commands"])
    assert k68 ^ v16 == set()
    assert k68 - ppc == {"LIBFUNCS"}
    assert ppc - k68 == set()


def test_mcp_tool_registration_golden():
    from amiga_devbench.mcp_tools import mcp

    expected = (FIXTURES / "mcp_tools_v94a8888.txt").read_text().split()
    assert len(expected) == 129
    actual = sorted(t.name for t in asyncio.run(mcp.list_tools()))
    assert set(actual) - set(expected) == set(), "tools added"
    assert set(expected) - set(actual) == set(), "tools removed"
    assert actual == expected


def test_ready_handshake_publishes_event_and_sends_nothing():
    """READY|1.0 from the daemon -> 'ready' event; the host sends nothing back
    (no CAPABILITIES at connect today - server.py's _on_bridge_ready sends
    VERSION, but that is outside SerialConnection)."""

    async def run():
        bus = EventBus()
        conn = SerialConnection(AmigaState(), bus)
        sent = []
        conn.on_tx = sent.append
        async with bus.subscribe("ready") as q:
            conn._handle_data("READY|1.0\n")
            event, data = q.get_nowait()
        return event, data, sent, conn

    event, data, sent, conn = asyncio.run(run())
    assert event == "ready"
    assert data["version"] == "1.0"
    assert sent == []
    assert conn._watchdog_task is None


def test_silence_watchdog_constants_and_placement():
    assert serial_conn.BRIDGE_SILENCE_TIMEOUT == 15.0
    # The watchdog is started only from connect_tcp (serial_conn.py:252);
    # connect_pty never starts it. Characterised by source inspection because
    # starting a real TCP connection is out of scope for a unit test.
    assert "_start_watchdog()" in inspect.getsource(SerialConnection.connect_tcp)
    assert "_start_watchdog" not in inspect.getsource(SerialConnection.connect_pty)
    assert "_start_watchdog" not in inspect.getsource(SerialConnection.connect)


def test_all():
    for fn in (test_bridge_v1_20_68k, test_bridge_v1_20_ppc, test_bridge_v1_16,
               test_advertised_lists_golden, test_mcp_tool_registration_golden,
               test_ready_handshake_publishes_event_and_sends_nothing,
               test_silence_watchdog_constants_and_placement):
        fn()
    print("OK: characterisation tests passed")


if __name__ == "__main__":
    test_all()
