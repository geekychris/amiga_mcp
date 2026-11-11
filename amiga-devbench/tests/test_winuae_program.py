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
        self.segments = process_segments or [dict(index=i, address=a, size=h.size)
                                             for i, (a, h) in enumerate(zip(BASES, hunks))]
        self.caps['commands'] += ['segments']

    def handle(self, cmd, writer):
        monitor = bytes.fromhex(cmd[6:]).decode() if cmd.startswith('qRcmd,') else ''
        if monitor.startswith('segments'):
            return json.dumps(dict(segments=self.segments)).encode().hex()
        if monitor.startswith('disasm '):
            start, count = (int(v, 16) for v in monitor.split()[1:3])
            return '\n'.join(f'{start + 2 * i:08X} 4e71  NOP' for i in range(count)).encode().hex()
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
            with pytest.raises(Exception, match='do not match winuae_program'):
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


# ── Symbols at their loaded addresses ───────────────────────────────

def static_offset(name):
    """Linked (code hunk) offset of a static function, from the DWARF."""
    import io
    from elftools.elf.elffile import ELFFile
    from amiga_devbench.winuae_program import program_elf
    parsed = parse_hunk_file(program_bytes())
    elf = program_elf(parsed.hunks, split_dwarf(parsed.debug[0]))
    for cu in ELFFile(io.BytesIO(elf)).get_dwarf_info().iter_CUs():
        for die in cu.iter_DIEs():
            if die.tag == 'DW_TAG_subprogram' and 'DW_AT_name' in die.attributes \
                    and die.attributes['DW_AT_name'].value == name.encode():
                return die.attributes['DW_AT_low_pc'].value
    raise AssertionError(name)


def test_symbol_breakpoint_lands_at_the_loaded_address():
    async def check(c, t):
        await c.call('breakpoint', operation='add', symbol='walk', file=str(FIXTURE))
        assert f'0,{symbol("_walk"):x},2' in t.points, t.points
        # A static function has no hunk symbol; the DWARF names it.
        await c.call('breakpoint', operation='add', symbol='scale', file=str(FIXTURE))
        assert f'0,{BASES[0] + static_offset("scale"):x},2' in t.points, t.points
        with pytest.raises(Exception, match="No symbol or function 'nosuch'"):
            await c.call('breakpoint', operation='add', symbol='nosuch', file=str(FIXTURE))
    dwarf_test(check)


def test_disassembly_names_symbols_and_lines_of_the_program():
    async def check(c, t):
        listing = await c.call('disassemble', address=symbol('_walk'), count=3, annotate=False,
                               source_xref=True, file=str(FIXTURE))
        first, second = listing['lines'][:2]
        assert first['source']['symbol'] == 'walk'
        assert first['source']['file'] == 'winuae_program.c' and first['source']['line'] == 19
        assert first['source']['function'] == 'walk'
        assert second['source']['symbol'] == 'walk+0x2'
        # Outside the program: no annotation.
        outside = await c.call('disassemble', address=0x100, count=1, annotate=False,
                               source_xref=True, file=str(FIXTURE))
        assert 'source' not in outside['lines'][0]
    dwarf_test(check)


def test_symbol_read_takes_a_project(monkeypatch):
    from amiga_devbench import winuae_program

    async def run():
        monkeypatch.setattr(winuae_program, 'project_binary', lambda project: str(FIXTURE))
        async with session(Loaded(), timeout=5) as (c, _):
            await c.call('connect')
            await c.call('pause')
            read = await c.call('symbol_read', project='winuae_program', symbol='origin', format='u32')
            assert read['values'][0] == 3, read
    asyncio.run(run())
