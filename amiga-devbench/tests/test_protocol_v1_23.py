"""Daemon v1.23: the fork's v1.21 (protocol level 2) and v1.22 (INPUTPOS) carried
onto upstream main, which had meanwhile shipped its own v1.21 (STOP signals the
registered client's task, STOP|HARD). No new verb: the verb list is v1.22's.

Golden inputs in tests/fixtures/bridge_v1.23_{68k,ppc}.txt are generated with
gen_bridge_fixtures.py; the v1.22 fixtures stay untouched.
"""
from tests.test_protocol_level2 import _load


def test_v1_23_declares_the_v1_22_verbs():
    for arch in ("68k", "ppc"):
        old = _load(f"bridge_v1.22_{arch}.txt")["CAPABILITIES"]
        new = _load(f"bridge_v1.23_{arch}.txt")["CAPABILITIES"]
        assert new["version"] == "AmigaBridge v1.23"
        assert new["protocolLevel"] == 2
        assert new["commands"] == old["commands"]
        assert new["profiles"] == old["profiles"]
        assert new["features"] == old["features"]
        assert new["platform"] == old["platform"]
