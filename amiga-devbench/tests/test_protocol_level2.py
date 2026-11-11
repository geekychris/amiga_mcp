"""Protocol level 2 CAPABILITIES and the SYSINFO transport field (daemon v1.21).

Golden inputs in tests/fixtures/bridge_v1.21_{68k,ppc}.txt are generated from
the daemon's own caps_util.c via tests/fixtures/gen_bridge_fixtures.py. The
v1.16 / v1.20 fixtures stay as the level-1 behaviour being preserved.
"""
import logging
import pathlib

from amiga_devbench.protocol import parse_message

from tests.test_characterisation import DEBUGGER_VERBS

FIXTURES = pathlib.Path(__file__).parent / "fixtures"

PPC_DROPPED_VERBS = {
    "CRASHINIT", "CRASHREMOVE", "CRASHTEST", "LASTCRASH",
    "SNOOPSTART", "SNOOPSTOP", "SNOOPSTATUS",
    "POOLSTART", "POOLSTOP", "POOLS",
    "READREGS", "CHIPREGS", "CHIPLOGSTART", "CHIPLOGSTOP", "CHIPLOGSNAPSHOT",
    "SPRITES", "COPPERLIST", "AUDIOCHANNELS", "AUDIOSAMPLE", "LIBFUNCS",
} | DEBUGGER_VERBS

FEATURES_68K = [
    "exec.async", "exec.signals", "env.vars", "fs.tail", "fs.attrs",
    "gfx.planar", "gfx.truecolor", "gfx.palette.read", "gfx.palette.write",
    "gfx.window", "mem.regs", "dbg.attach", "dbg.breakpoints", "dbg.step",
    "dbg.backtrace", "dbg.registers.write", "dbg.crash", "client.lib",
    "amiga.intuition", "amiga.arexx", "amiga.libs", "amiga.libs.jumptable",
    "amiga.copper", "amiga.paula", "amiga.chipset", "amiga.snoop",
    "amiga.assigns", "amiga.pools", "amiga.clipboard", "amiga.fonts",
]
FEATURES_PPC = [
    "exec.async", "exec.signals", "env.vars", "fs.tail", "fs.attrs",
    "gfx.chunky", "gfx.truecolor", "gfx.palette.read", "gfx.palette.write",
    "gfx.window", "client.lib", "amiga.intuition", "amiga.arexx", "amiga.libs",
    "amiga.assigns", "amiga.clipboard", "amiga.fonts",
]
PROFILES_68K = ["core", "mem", "fs", "exec", "gfx", "input", "debug", "client"]
PROFILES_PPC = ["core", "mem", "fs", "exec", "gfx", "input", "client"]


def _load(name):
    msgs = {}
    for line in (FIXTURES / name).read_text().splitlines():
        msg = parse_message(line)
        assert msg is not None, f"{name}: unparseable line {line[:60]!r}"
        msgs[msg["type"]] = msg
    return msgs


def test_v1_21_68k_is_level_2():
    msgs = _load("bridge_v1.21_68k.txt")
    caps = msgs["CAPABILITIES"]
    assert caps["version"] == "AmigaBridge v1.21"
    assert caps["protocolLevel"] == 2
    assert caps["maxLine"] == 8192
    assert "malformed" not in caps
    cmds = caps["commands"]
    assert len(cmds) == 120
    assert len(set(cmds)) == 120
    assert DEBUGGER_VERBS <= set(cmds)
    assert caps["platform"] == "amiga/aos3/unknown"
    assert caps["features"] == FEATURES_68K
    assert caps["profiles"] == PROFILES_68K
    assert msgs["SYSINFO"]["transport"] == "serial"
    assert msgs["SYSINFO"]["cpuType"] == "68030"
    assert (msgs["VERSION"]["major"], msgs["VERSION"]["minor"]) == (1, 21)


def test_v1_21_68k_is_v1_20_plus_debugger_verbs():
    new = _load("bridge_v1.21_68k.txt")["CAPABILITIES"]["commands"]
    old = _load("bridge_v1.20_68k.txt")["CAPABILITIES"]["commands"]
    assert new[:len(old)] == old, "advertising order changed"
    assert set(new) - set(old) == DEBUGGER_VERBS


def test_v1_21_ppc_is_level_2_without_unsupported_verbs():
    msgs = _load("bridge_v1.21_ppc.txt")
    caps = msgs["CAPABILITIES"]
    assert caps["protocolLevel"] == 2
    assert "malformed" not in caps
    cmds = set(caps["commands"])
    assert len(caps["commands"]) == len(cmds) == 85
    assert not (cmds & PPC_DROPPED_VERBS), cmds & PPC_DROPPED_VERBS
    k68 = set(_load("bridge_v1.21_68k.txt")["CAPABILITIES"]["commands"])
    assert k68 - cmds == PPC_DROPPED_VERBS
    assert cmds - k68 == set()
    assert caps["platform"] == "amiga/aos4/unknown"
    assert caps["profiles"] == PROFILES_PPC
    assert "debug" not in caps["profiles"]
    assert caps["features"] == FEATURES_PPC
    for flag in caps["features"]:
        assert flag != "mem.regs"
        assert not flag.startswith("dbg.")
        assert flag not in ("amiga.chipset", "amiga.snoop", "amiga.copper",
                            "amiga.paula", "amiga.pools", "amiga.libs.jumptable")
    assert msgs["SYSINFO"]["transport"] == "tcp"
    assert msgs["SYSINFO"]["cpuType"] == "PowerPC"


def test_level_2_without_new_fields_is_malformed(caplog):
    with caplog.at_level(logging.WARNING, logger="amiga_devbench.protocol"):
        caps = parse_message("CAPABILITIES|AmigaBridge v1.21|2|8192|PING,VERSION")
    assert caps["malformed"] is True
    assert caps["protocolLevel"] == 2
    assert caps["commands"] == ["PING", "VERSION"]
    assert "platform" not in caps and "features" not in caps and "profiles" not in caps
    assert any("protocol level 2" in r.getMessage() for r in caplog.records)


def test_level_3_parses_as_level_2_ignoring_extra_fields():
    caps = parse_message("CAPABILITIES|AmigaBridge v9.9|3|8192|PING,VERSION|"
                         "amiga/aos3/unknown|exec.async|core|something-new|more")
    assert caps["protocolLevel"] == 3
    assert "malformed" not in caps
    assert caps["commands"] == ["PING", "VERSION"]
    assert caps["platform"] == "amiga/aos3/unknown"
    assert caps["features"] == ["exec.async"]
    assert caps["profiles"] == ["core"]
    assert set(caps) == {"type", "version", "protocolLevel", "maxLine", "commands",
                         "platform", "features", "profiles"}


def test_level_2_empty_lists_parse_as_empty():
    caps = parse_message("CAPABILITIES|AmigaBridge v1.21|2|8192|PING|amiga/aos3/unknown||")
    assert caps["features"] == [] and caps["profiles"] == []
    assert "malformed" not in caps


def test_sysinfo_with_and_without_transport():
    with_t = parse_message("SYSINFO|1|2|3|4|40|68|68030|50|tcp")
    assert with_t["transport"] == "tcp"
    assert with_t["vblankHz"] == 50
    without = parse_message("SYSINFO|1|2|3|4|40|68|68030|50")
    assert "transport" not in without
    assert without["vblankHz"] == 50


def test_level_1_fixtures_still_parse_as_level_1():
    for name in ("bridge_v1.16.txt", "bridge_v1.20_68k.txt", "bridge_v1.20_ppc.txt"):
        msgs = _load(name)
        caps = msgs["CAPABILITIES"]
        assert caps["protocolLevel"] == 1
        assert "platform" not in caps
        assert "features" not in caps
        assert "profiles" not in caps
        assert "malformed" not in caps
        assert "transport" not in msgs["SYSINFO"]
