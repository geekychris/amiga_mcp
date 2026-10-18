"""Host-side WinUAE features against the fake RSP target, without WinUAE."""
import asyncio
import json
import struct
import zlib
from types import SimpleNamespace

import pytest

from amiga_devbench import emulator, winuae_chipset, winuae_state
from amiga_devbench.winuae_debug import DebugError, WinuaeDebugger
from amiga_devbench.winuae_diagnostics import planar_png
from amiga_devbench.winuae_files import parse_hunk, parse_symbols, relocate
from amiga_devbench.winuae_guest import build_sequence
from tests.test_winuae_debug import Target, packet, session


class Machine(Target):
    """Fake target with SR stack switching, breakpoint stops and monitor
    commands that return watch IDs."""

    def __init__(self):
        super().__init__()
        self.regs[16] = 0x2700
        self.regs[15] = 0x3000      # ISP while supervisor
        self.usp = 0x2000
        self.breakpoint_stops = []  # PCs reported by successive 'c' commands
        self.caps['commands'] += ['watch', 'watch-range', 'dma-watch', 'condition', 'history', 'input']
        self.next_id = 0

    async def serve(self, reader, writer):
        await super().serve(_Commands(reader, self, writer), writer)

    def handle(self, cmd, writer):
        """Commands handled here instead of the base target; None defers."""
        if cmd.startswith('P10='):
            sr = int(cmd[4:], 16)
            if (sr ^ self.regs[16]) & 0x2000:
                self.regs[15], self.usp = self.usp, self.regs[15]
            self.regs[16] = sr
            return 'OK'
        if cmd == 'c' and self.breakpoint_stops:
            self.regs[17] = self.breakpoint_stops.pop(0)
            writer.write(packet('S05'))
            return ''
        if cmd.startswith('qRcmd,'):
            monitor = bytes.fromhex(cmd[6:]).decode()
            if monitor.startswith(('watch add ', 'watch range ', 'condition add ')):
                self.next_id += 1
                return json.dumps({'id': self.next_id}).encode().hex()
            if monitor.startswith('dma-watch add '):
                self.next_id += 1
                return str(self.next_id).encode().hex()
        return None


class _Commands:
    """Reader wrapper that lets Machine answer some packets itself."""

    def __init__(self, reader, target, writer):
        self.reader, self.target, self.writer = reader, target, writer
        self.queue = b''

    async def readexactly(self, n):
        while len(self.queue) < n:
            ch = await self.reader.readexactly(1)
            if ch == b'$':
                raw = (await self.reader.readuntil(b'#'))[:-1]
                checksum = await self.reader.readexactly(2)
                reply = self.target.handle(raw.decode(), self.writer)
                if reply is not None:
                    self.target.commands.append(raw.decode())
                    if reply:
                        self.writer.write(packet(reply))
                    continue
                self.queue += b'$' + raw + b'#' + checksum
            else:
                self.queue += ch
        data, self.queue = self.queue[:n], self.queue[n:]
        return data

    async def readuntil(self, separator):
        data = b''
        while not data.endswith(separator):
            data += await self.readexactly(1)
        return data


def run(test, target=None):
    async def body():
        async with session(target or Machine(), timeout=.5) as (c, t):
            await c.call('connect')
            await c.call('pause')
            await test(c, t)
    asyncio.run(body())


def test_stack_pointers_switch_sr_and_restore_it():
    async def test(c, t):
        regs = await c.call('registers')
        assert regs['ISP'] == 0x3000 and regs['USP'] == 0x2000 and regs['SR'] == 0x2700
        await c.call('registers', register='USP', value=0x2400)
        regs = await c.call('registers')
        assert regs['USP'] == 0x2400 and regs['A7'] == 0x3000 and t.regs[16] == 0x2700
        with pytest.raises(DebugError, match='MSP'):
            await c.call('registers', register='MSP', value=0x1000)  # the fake target is a 68000
    run(test)


def test_breakpoint_skip_hits_and_oneshot():
    async def test(c, t):
        await c.call('breakpoint', operation='add', address=0x10004, skip=2)
        t.breakpoint_stops = [0x10004, 0x10004, 0x10004]
        await c.call('resume')
        assert not (await c.call('wait', timeout=1))['timed_out']
        point = (await c.call('breakpoint'))[0]
        assert point['hits'] == 3 and point['skip'] == 0 and t.commands.count('c') == 3
        await c.call('breakpoint', operation='clear')
        await c.call('breakpoint', operation='add', address=0x10008, oneshot=True)
        t.breakpoint_stops = [0x10008]
        await c.call('resume')
        assert not (await c.call('wait', timeout=1))['timed_out']
        assert await c.call('breakpoint') == [] and 'z0,10008,2' in t.commands
    run(test)


def test_multiple_steps_wait_for_each_stop():
    async def test(c, t):
        result = await c.call('step', count=3)
        assert result['steps'] == 3 and t.commands.count('s') == 3 and t.regs[17] == 0x10006
    run(test)


def test_watch_commands_follow_fsuae_options():
    async def test(c, t):
        await c.call('watch', operation='add', address=0x2fffc, size=4, access='W', value=0x12345678)
        await c.call('watch', operation='add', address=0x1000, size=16, access='RWI', change_only=False)
        await c.call('watch', operation='add', address=0x2fffe, size=2, access='write', value=8, exact=True)
        monitors = [bytes.fromhex(m[6:]).decode() for m in t.commands if m.startswith('qRcmd,')]
        assert 'watch range 2fffc 4 2 6 1 12345678 ffffffff 0 0' in monitors
        assert 'watch range 1000 10 7 7 0 0 ffffffff 0 0' in monitors
        assert 'watch add 2fffe 2 2 6 1 8 ffff 0 0' in monitors
        for args in (dict(size=8, value=1), dict(access='I', sources=['copper']), dict(access='R', change_only=True)):
            with pytest.raises(DebugError):
                await c.call('watch', operation='add', address=0x1000, **args)
        await c.call('watch', operation='clear')
        assert sum(m.startswith('watch remove') for m in [bytes.fromhex(x[6:]).decode() for x in t.commands if x.startswith('qRcmd,')]) == 3
    run(test)


def test_fd_tables_and_address_names(tmp_path):
    assert winuae_chipset.fd_lookup(-552)['name'] == 'OpenLibrary'
    assert winuae_chipset.fd_lookup(-198)['name'] == 'AllocMem'
    table = winuae_chipset.parse_fd('##base _XBase\n##bias 30\n##private\nHidden()()\n##public\nShown(a)(d0)\n##end\n')
    assert table == {-30: 'Hidden', -36: 'Shown'}
    assert winuae_chipset.annotate_line('00f80360 4eae fdd8  JSR (-$0228,a6)', None) == 'exec.OpenLibrary()'
    assert winuae_chipset.lookup_address(0xdff09a)['name'] == 'INTENA'
    assert winuae_chipset.lookup_address(0xbfd100)['name'] == 'ciabprb'
    assert winuae_chipset.lookup_address(0x6c)['name'].startswith('Level 3 interrupt')
    assert winuae_chipset.lookup_address(0x80)['name'] == 'TRAP #0'


def state_file(path, cpu_regs, ram):
    def chunk(name, body, compress=False):
        if compress:
            data = struct.pack('>I', len(body)) + zlib.compress(body)
            written = len(data) - 4
            header = name + struct.pack('>II', len(data) + 12, 1)
        else:
            data, written = body, len(body)
            header = name + struct.pack('>II', len(body) + 12, 0)
        return header + data + b'\0' * (4 - (written & 3))
    d, a, pc, usp, isp, sr = cpu_regs
    cpu = struct.pack('>II', 68000, 0) + struct.pack('>15I', *(d + a)) + struct.pack('>IHHIIH', pc, 0, 0, usp, isp, sr) + bytes(4)
    path.write_bytes(chunk(b'ASF ', b'\0' * 8) + chunk(b'CPU ', cpu) + chunk(b'CRAM', ram, compress=True) + b'END ' + struct.pack('>I', 12))


def test_state_files_parse_diff_and_report_saved_registers(tmp_path):
    regs = ([1, 2, 3, 4, 5, 6, 7, 8], [0x100 * i for i in range(7)], 0x10008, 0x2000, 0x3000, 0x2700)
    state_file(tmp_path / 'a.uss', regs, bytes(4096))
    changed = bytearray(4096)
    changed[1000:1004] = b'\xde\xad\xbe\xef'
    state_file(tmp_path / 'b.uss', regs, bytes(changed))
    saved = winuae_state.saved_registers(tmp_path / 'a.uss')
    assert saved['PC'] == 0x10008 and saved['D7'] == 8 and saved['A7'] == 0x3000 and saved['USP'] == 0x2000
    diff = winuae_state.diff_states(tmp_path / 'a.uss', tmp_path / 'b.uss')
    assert diff['unchanged'] == 2 and diff['changed'][0]['chunk'] == 'CRAM'
    assert diff['changed'][0]['differing_bytes'] == 4 and diff['changed'][0]['regions'][0]['start'] == 768


def test_input_sequences_and_text():
    taps = build_sequence('type_text', dict(text='Hi', layout='us'))
    assert taps[0] == [0, 0, 0, 0x60, 1] and taps[1] == [0, 0, 0, 0x25, 1]
    assert sum(step[0] for step in taps) == 12
    with pytest.raises(DebugError, match='Unsupported'):
        build_sequence('type_text', dict(text='é', layout='us'))
    steps = build_sequence('input_sequence', dict(steps=[dict(action='mouse', axis='x', delta=-5, after_frames=2),
                                                         dict(action='wait', after_frames=10)]))
    assert steps == [[2, 1, 0, 0, 0xfffffffb], [10, 3, 0, 0, 0]]


def hunk_bytes():
    longs = lambda *values: b''.join(struct.pack('>I', v) for v in values)
    return (longs(0x3f3, 0, 2, 0, 1, 2, 1) + longs(0x3e9, 2) + bytes.fromhex('4e714e7500000000') +
            longs(0x3ec, 1, 1, 4, 0) + longs(0x3f0, 2) + b'start\0\0\0' + longs(0, 0, 0x3f2) +
            longs(0x3eb, 1, 0x3f2))


def test_hunk_parse_and_relocation():
    hunks = parse_hunk(hunk_bytes())
    assert [(h.kind, h.size) for h in hunks] == [('code', 8), ('bss', 4)] and hunks[0].symbols == [('start', 0)]
    _, images = relocate(hunks, [dict(address=0x40000, capacity=8, memory='chip'),
                                 dict(address=0x40100, capacity=4, memory='fast')])
    assert images[0].hex() == '4e714e7500040100' and images[1] == bytearray(4)
    with pytest.raises(DebugError, match='overlap'):
        relocate(hunks, [dict(address=0x40000, capacity=8, memory='chip'), dict(address=0x40004, capacity=4, memory='chip')])


def elf_bytes():
    strtab = b'\0main\0'
    shstrtab = b'\0.text\0.symtab\0.strtab\0.shstrtab\0'
    text = bytes.fromhex('4e714e714e714e75')
    symtab = bytes(16) + struct.pack('>IIIBBH', 1, 0x1004, 4, 0x12, 0, 1)
    body = text + symtab + strtab + shstrtab
    offsets = [52, 52 + len(text), 52 + len(text) + len(symtab), 52 + len(text) + len(symtab) + len(strtab)]
    shoff = 52 + len(body)
    shoff += (4 - shoff % 4) % 4
    sections = [bytes(40),
                struct.pack('>10I', 1, 1, 6, 0x1000, offsets[0], len(text), 0, 0, 2, 0),
                struct.pack('>10I', 7, 2, 0, 0, offsets[1], len(symtab), 3, 1, 4, 16),
                struct.pack('>10I', 15, 3, 0, 0, offsets[2], len(strtab), 0, 0, 1, 0),
                struct.pack('>10I', 23, 3, 0, 0, offsets[3], len(shstrtab), 0, 0, 1, 0)]
    header = (b'\x7fELF\x01\x02\x01' + bytes(9) + struct.pack('>HHIIIIIHHHHHH', 2, 4, 1, 0x1000, 0, shoff, 0, 52, 32, 0, 40, 5, 4))
    data = header + body
    return data + bytes(shoff - len(data)) + b''.join(sections)


def test_elf_and_hunk_symbol_tables():
    elf = parse_symbols(elf_bytes())
    assert elf['format'] == 'elf32-m68k' and elf['sections'][0]['name'] == '.text'
    assert elf['symbols'] == [dict(name='main', section=1, offset=4, size=4)]
    assert parse_symbols(hunk_bytes())['symbols'][0]['name'] == 'start'


def test_planar_png_is_valid():
    png = planar_png(8, 1, [b'\xf0'], 2, [0x000000, 0xffffff])
    assert png[:8] == b'\x89PNG\r\n\x1a\n'
    rows = zlib.decompress(png[png.index(b'IDAT') + 4:png.index(b'IEND') - 8])
    assert rows[:4] == b'\x00\xff\xff\xff' and rows[13:16] == b'\x00\x00\x00'


def test_memory_search_spans_chunks():
    async def test(c, t):
        t.memory[0x7fe:0x802] = b'\x12\x34\x56\x78'
        t.memory[0x900:0x902] = b'\x12\x34'
        result = await c.call('memory_search', address=0, length=0x1000, hex='1234')
        assert result['matches'] == [0x7fe, 0x900]
        limited = await c.call('memory_search', address=0, length=0x1000, hex='1234', max_matches=1)
        assert limited['limit_reached'] and limited['next_address'] == 0x7ff
    run(test)


def test_winuae_launch_arguments_and_floppies(tmp_path):
    cfg = SimpleNamespace(winuae_debug_enabled=True, winuae_debug_port=2346)
    assert emulator.winuae_launch_args(cfg)[:8] == ['-s', 'use_gui=no', '-s', 'debugging_features=gdbserver',
                                                     '-s', 'gdb_port=2346', '-s', 'cpu_threaded=false']
    app = tmp_path / 'WinUAE.app' / 'Contents' / 'MacOS'
    app.mkdir(parents=True)
    (app / 'WinUAE').write_text('')
    assert emulator.winuae_executable(str(tmp_path / 'WinUAE.app')) == str(app / 'WinUAE')
    manager = emulator.EmulatorManager(binary=str(app / 'WinUAE'))
    disk = tmp_path / 'game.adf'
    disk.write_bytes(bytes(901120))
    manager.set_floppy(0, str(disk))
    manager.set_floppy(1, None)
    assert manager.kind == 'winuae' and manager.floppies == {0: str(disk.resolve()), 1: ''}

    async def disk_action():
        d = WinuaeDebugger(enabled=True)
        d.emulator = manager
        result = await d.call('disk', operation='insert', drive=2, file=str(disk))
        assert not result['restarted'] and result['floppies'][2] == str(disk.resolve())
        with pytest.raises(DebugError, match='existing disk image'):
            await d.call('disk', operation='insert', drive=0, file=str(tmp_path / 'missing.adf'))
    asyncio.run(disk_action())


def test_cross_origin_requests_are_refused():
    import httpx
    from starlette.applications import Starlette
    from starlette.routing import Route
    from amiga_devbench.winuae_api import make_winuae_endpoint

    async def body():
        app = Starlette(routes=[Route('/api/winuae/{action}', make_winuae_endpoint(WinuaeDebugger()), methods=['GET', 'POST'])])
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url='http://localhost:3000') as c:
            assert (await c.post('/api/winuae/lookup', json={'address': 4}, headers={'Origin': 'http://evil.example'})).status_code == 403
            same = await c.post('/api/winuae/lookup', json={'address': 4}, headers={'Origin': 'http://localhost:3000'})
            assert same.status_code == 200 and (await c.post('/api/winuae/lookup', json={'address': 4})).status_code == 200
    asyncio.run(body())


def test_bad_fd_offsets_are_debug_errors():
    async def body():
        d = WinuaeDebugger()
        with pytest.raises(DebugError, match='offset'):
            await d.call('fd', operation='lookup', offset='abc')
        assert (await d.call('fd', operation='lookup', offset='-0552'))['name'] == 'OpenLibrary'
    asyncio.run(body())


def test_dwarf_source_without_address_lets_the_helper_use_the_pc(tmp_path, monkeypatch):
    from amiga_devbench import winuae_dwarf
    elf = tmp_path / 'linked.elf'
    elf.write_bytes(elf_bytes())
    seen = {}

    async def helper(d, request):
        seen.update(request)
        return {}
    monkeypatch.setattr(winuae_dwarf, 'run_helper', helper)

    async def test(c, t):
        await c.call('source', file=str(elf), mappings=[dict(section='.text', address=0x1000, size=8)])
        assert 'address' not in seen and seen['registers']['PC'] == 0x10000
    run(test)
