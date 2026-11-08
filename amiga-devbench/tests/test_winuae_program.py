# SPDX-License-Identifier: MIT
"""The m68k-amigaos toolchain's Hunk executables: parsing, DWARF and
locating the program through its loaded segments."""
import asyncio
from pathlib import Path

from amiga_devbench.winuae_debug import WinuaeDebugger  # noqa: F401  (import order)
from amiga_devbench.winuae_files import parse_hunk_file, parse_symbols
from tests.test_winuae_debug import session

FIXTURE = Path(__file__).parent / 'fixtures' / 'winuae_program'


def program_bytes() -> bytes:
    return FIXTURE.read_bytes()


def test_toolchain_hunk_file_parses():
    parsed = parse_hunk_file(program_bytes())
    assert [(h.kind, h.size) for h in parsed.hunks] == [
        ('code', 0x6a0), ('data', 0x2c), ('bss', 0x2c), ('data', 0x08)]
    assert len(parsed.debug) == 1 and len(parsed.debug[0]) == 0x1cc0
    names = {n for h in parsed.hunks for n, _ in h.symbols}
    assert {'_walk', '_main', '_origin', '_counter', '_chip_words'} <= names
    # Markers of empty DWARF sections sit outside their hunk and are dropped.
    assert not any(n.startswith('__debug_') for n in names)


def test_symbols_and_hunk_inspect_accept_toolchain_output():
    async def run():
        async with session() as (c, _):
            listed = await c.call('symbols', file=str(FIXTURE), prefix='_wa')
            assert listed['format'] == 'hunk'
            assert [(s['name'], s['section']) for s in listed['symbols']] == [('_walk', 0)]
            inspected = await c.call('hunk_inspect', file=str(FIXTURE))
            assert [h['kind'] for h in inspected['hunks']] == ['code', 'data', 'bss', 'data']
    asyncio.run(run())


def test_parse_symbols_places_data_symbols_in_their_hunk():
    symbols = {s['name']: (s['section'], s['offset']) for s in parse_symbols(program_bytes())['symbols']}
    assert symbols['_origin'][0] == 1 and symbols['_counter'][0] == 2 and symbols['_chip_words'] == (3, 0)


# ── DWARF from the Hunk executable ──────────────────────────────────

import json  # noqa: E402

import pytest  # noqa: E402

from amiga_devbench.winuae_files import relocate  # noqa: E402
from amiga_devbench.winuae_program import dwarf_bases, split_dwarf  # noqa: E402
from tests.test_winuae_features import Machine  # noqa: E402

BASES = [0x2000, 0x3000, 0x3100, 0x3200]   # where the fake AmigaDOS loaded each hunk
STACK = 0x3800


class Loaded(Machine):
    """A fake target running the fixture, loaded at BASES."""

    def __init__(self, program=None, process_segments=None):
        super().__init__()
        self.memory = bytearray(0x4000)
        hunks = parse_hunk_file(program or program_bytes()).hunks
        _, images = relocate(hunks, [dict(address=a, capacity=h.size, memory='chip' if i == 3 else 'fast')
                                     for i, (a, h) in enumerate(zip(BASES, hunks))])
        for base, image in zip(BASES, images):
            self.memory[base:base + len(image)] = image
        self.segments = process_segments or [dict(index=i, address=a, size=h.size + 8)
                                             for i, (a, h) in enumerate(zip(BASES, hunks))]
        self.caps['commands'] += ['segments']

    def handle(self, cmd, writer):
        if cmd.startswith('qRcmd,') and bytes.fromhex(cmd[6:]).decode().startswith('segments'):
            return json.dumps(dict(segments=self.segments)).encode().hex()
        return super().handle(cmd, writer)

    def stop_at(self, address, return_to=None):
        self.regs[17] = address
        self.regs[15] = STACK
        if return_to is not None:
            self.memory[STACK:STACK + 4] = return_to.to_bytes(4, 'big')


def symbol(name):
    hunk, offset = next((h.index, o) for h in parse_hunk_file(program_bytes()).hunks
                        for n, o in h.symbols if n == name)
    return BASES[hunk] + offset


def test_debug_hunk_splits_into_dwarf_sections():
    sections = split_dwarf(parse_hunk_file(program_bytes()).debug[0])
    assert sections['.debug_info'][4:6] == b'\x00\x04'          # DWARF 4
    assert sections['.debug_frame'][4:8] == b'\xff\xff\xff\xff'  # CIE
    assert sections['.debug_loclists'] == b''
    hunks = parse_hunk_file(program_bytes()).hunks
    assert dwarf_bases(hunks) == [0, 0x6a0, 0x6cc, 0x6f8]


def dwarf_test(check):
    pytest.importorskip('elftools')

    async def run():
        target = Loaded()
        async with session(target, timeout=5) as (c, t):
            await c.call('connect')
            await c.call('pause')
            await check(c, t)
    asyncio.run(run())


def test_source_line_of_a_hunk_program():
    async def check(c, t):
        t.stop_at(symbol('_walk'))
        await c.call('pause')
        found = await c.call('source', file=str(FIXTURE))
        assert found['functions'] == ['walk']
        assert found['locations'][0]['file'] == 'winuae_program.c'
        assert found['locations'][0]['line'] == 19   # the toolchain puts the prologue on the brace
        assert found['linked_address'] == symbol('_walk') - BASES[0]
    dwarf_test(check)


def test_global_variables_in_data_bss_and_chip_hunks():
    async def check(c, t):
        t.stop_at(symbol('_main'))
        await c.call('pause')
        x = await c.call('variable', file=str(FIXTURE), expression='origin.x')
        assert x['value'] == 3, x
        tag = await c.call('variable', file=str(FIXTURE), expression='origin.y')
        assert tag['value'] == 4
        t.memory[symbol('_counter'):symbol('_counter') + 4] = (41).to_bytes(4, 'big')
        assert (await c.call('variable', file=str(FIXTURE), expression='counter'))['value'] == 41
        assert (await c.call('variable', file=str(FIXTURE), expression='chip_words[1]'))['value'] == 2
    dwarf_test(check)


def test_backtrace_through_call_frame_information():
    async def check(c, t):
        t.stop_at(symbol('_walk'), return_to=symbol('_main') + 0x20)
        await c.call('pause')
        trace = await c.call('backtrace', file=str(FIXTURE), max_frames=2)
        assert [f['source']['functions'][0] for f in trace['frames'][:2]] == ['walk', 'main'], trace
        assert trace['frames'][0]['unwind'] == 'dwarf-cfi'
    dwarf_test(check)


def test_hunk_indices_name_sections_in_explicit_mappings():
    async def check(c, t):
        t.stop_at(symbol('_main'))
        await c.call('pause')
        mappings = [dict(section=i, address=a, size=0x800) for i, a in enumerate(BASES)]
        x = await c.call('variable', file=str(FIXTURE), mappings=mappings, expression='origin.x')
        assert x['value'] == 3
    dwarf_test(check)


def test_segments_of_another_program_are_reported():
    pytest.importorskip('elftools')

    async def run():
        target = Loaded(process_segments=[dict(index=0, address=0x2000, size=0x40)])
        async with session(target, timeout=5) as (c, t):
            await c.call('connect')
            await c.call('pause')
            with pytest.raises(Exception, match='does not fit'):
                await c.call('source', file=str(FIXTURE))
    asyncio.run(run())


def test_a_build_without_g_has_no_dwarf(tmp_path):
    async def run():
        async with session(Loaded(), timeout=5) as (c, _):
            await c.call('connect')
            await c.call('pause')
            stripped = tmp_path / 'stripped'
            data = program_bytes()
            stripped.write_bytes(data[:data.index(b'\x00\x00\x03\xf1')])  # drop the debug hunk
            with pytest.raises(Exception, match='no DWARF; build it with -g'):
                await c.call('source', file=str(stripped))
    asyncio.run(run())
