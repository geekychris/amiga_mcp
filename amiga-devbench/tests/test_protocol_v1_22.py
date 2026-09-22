"""Daemon v1.22 adds INPUTPOS (absolute pointer position) to the input profile.

Golden inputs in tests/fixtures/bridge_v1.22_{68k,ppc}.txt are generated the
same way as the v1.21 ones (gen_bridge_fixtures.py); the v1.21 fixtures stay
untouched so the level-2 tests keep asserting the daemon they were written for.
"""
from tests.test_protocol_level2 import _load, PPC_DROPPED_VERBS


def test_v1_22_68k_is_v1_21_plus_inputpos():
    old = _load("bridge_v1.21_68k.txt")["CAPABILITIES"]["commands"]
    new = _load("bridge_v1.22_68k.txt")
    caps = new["CAPABILITIES"]
    assert caps["version"] == "AmigaBridge v1.22"
    assert caps["protocolLevel"] == 2
    assert len(caps["commands"]) == len(set(caps["commands"])) == 121
    assert set(caps["commands"]) - set(old) == {"INPUTPOS"}
    assert caps["profiles"] == _load("bridge_v1.21_68k.txt")["CAPABILITIES"]["profiles"]


def test_v1_22_ppc_declares_inputpos_too():
    cmds = set(_load("bridge_v1.22_ppc.txt")["CAPABILITIES"]["commands"])
    assert len(cmds) == 86
    assert "INPUTPOS" in cmds
    assert not (cmds & PPC_DROPPED_VERBS)
    k68 = set(_load("bridge_v1.22_68k.txt")["CAPABILITIES"]["commands"])
    assert k68 - cmds == PPC_DROPPED_VERBS
