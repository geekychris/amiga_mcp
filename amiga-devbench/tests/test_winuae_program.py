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
