# SPDX-License-Identifier: MIT
"""amiga_winuae_debug_run against the fake WinUAE target running the
toolchain fixture."""
import asyncio
from contextlib import asynccontextmanager
from types import SimpleNamespace

import pytest

from amiga_devbench import winuae_program
from amiga_devbench.winuae_run import debug_run
from tests.test_winuae_debug import session
from tests.test_winuae_program import BASES, FIXTURE, Loaded, symbol


class Bridge:
    """The guest bridge: records commands and confirms STOP."""

    def __init__(self, connected=True):
        self.connected = connected
        self.sent = []
        self.queue = None

    def send(self, message):
        self.sent.append(message)
        if message['type'] == 'STOP' and self.queue is not None:
            self.queue.put_nowait(('ok', dict(context='STOP', message='Client stopped')))

    @asynccontextmanager
    async def subscribe(self, *events):
        self.queue = asyncio.Queue()
        try:
            yield self.queue
        finally:
            self.queue = None


async def ok(*_args, **_kwargs):
    return SimpleNamespace(success=True, duration=5, output='', errors='', message='copied', files=[])


TOOLS = dict(builder=SimpleNamespace(build=ok), deployer=SimpleNamespace(deploy_smart=ok))


def run(check, target=None):
    pytest.importorskip('elftools')

    async def go():
        async with session(target or Loaded(), timeout=5) as (c, t):
            await check(c, t)
    asyncio.run(go())


@pytest.fixture(autouse=True)
def fixture_project(monkeypatch):
    monkeypatch.setattr(winuae_program, 'project_binary', lambda project: str(FIXTURE))


def test_debug_run_stops_at_main_with_its_source():
    bridge = Bridge()

    async def check(c, t):
        t.breakpoint_stops = [BASES[0], symbol('_main')]   # the entry, then main
        result = await debug_run(c.call, 'examples/winuae_program', conn=bridge, bus=bridge,
                                 timeout=5, **TOOLS)
        assert result['ok'], result
        assert result['pc'] == f'0x{symbol("_main"):08x}'
        assert result['source']['functions'] == ['main']
        assert [m['type'] for m in bridge.sent] == ['STOP', 'LAUNCH']
        assert bridge.sent[1]['command'] == 'DH2:Dev/winuae_program'
        armed = [x for x in t.commands if x.startswith('qRcmd,')
                 and bytes.fromhex(x[6:]).decode() == 'process-break name winuae_program']
        assert armed
        assert f'Z0,{symbol("_main"):x},2' in t.commands
    run(check)


def test_debug_run_can_stop_at_the_entry_without_the_bridge():
    async def check(c, t):
        t.breakpoint_stops = [BASES[0]]
        result = await debug_run(c.call, 'winuae_program', stop_at='entry', launch='none', build=False,
                                 timeout=5)
        assert result['ok'] and result['pc'] == f'0x{BASES[0]:08x}', result
        assert result['stopped_at'] == 'entry'
    run(check)


def test_debug_run_needs_the_bridge_to_launch():
    async def check(c, _):
        with pytest.raises(Exception, match="launch='none'"):
            await debug_run(c.call, 'winuae_program', conn=Bridge(connected=False), bus=Bridge(), **TOOLS)
    run(check)


def test_debug_run_reports_a_missing_function_at_the_entry():
    async def check(c, t):
        t.breakpoint_stops = [BASES[0]]
        result = await debug_run(c.call, 'winuae_program', stop_at='nosuch', launch='none', build=False,
                                 timeout=5)
        assert not result['ok'] and result['stopped_at'] == 'entry'
        assert "'nosuch'" in result['error']
    run(check)


def test_debug_run_stops_after_a_failed_build():
    async def failed(*_args, **_kwargs):
        return SimpleNamespace(success=False, duration=5, output='', errors='main.c:1: error')

    async def check(c, t):
        result = await debug_run(c.call, 'winuae_program', launch='none', builder=SimpleNamespace(build=failed),
                                 deployer=None)
        assert not result['ok'] and 'error' in result['errors']
        assert not any(x.startswith('qRcmd,') for x in t.commands)
    run(check)
