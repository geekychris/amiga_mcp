"""Daemon v1.24: the fork's v1.21 (protocol level 2) and v1.22 (INPUTPOS) carried
onto upstream main, which had meanwhile shipped its own v1.21 (STOP signals the
registered client's task, STOP|HARD), plus the fix for the CAPABILITIES sprintf
(%d -> %ld: amiga.lib's %d is 16-bit, which hung real hardware on v1.21-v1.23).
No new verb: the verb list is v1.22's.

Golden inputs in tests/fixtures/bridge_v1.24_{68k,ppc}.txt are generated with
gen_bridge_fixtures.py; the v1.22 fixtures stay untouched.
"""
from tests.test_protocol_level2 import _load


def test_v1_24_declares_the_v1_22_verbs():
    for arch in ("68k", "ppc"):
        old = _load(f"bridge_v1.22_{arch}.txt")["CAPABILITIES"]
        new = _load(f"bridge_v1.24_{arch}.txt")["CAPABILITIES"]
        assert new["version"] == "AmigaBridge v1.24"
        assert new["protocolLevel"] == 2
        assert new["commands"] == old["commands"]
        assert new["profiles"] == old["profiles"]
        assert new["features"] == old["features"]
        assert new["platform"] == old["platform"]
