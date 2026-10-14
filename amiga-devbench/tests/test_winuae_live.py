"""Live tests against a real WinUAE with its GDB server.

Skipped unless WINUAE_PATH (the executable) and WINUAE_ROM (a Kickstart
ROM) are set; WINUAE_CPU_MODEL selects the CPU (default 68020). Each test
module run launches its own emulator on a free port.
"""
import asyncio
import base64
import os
import socket
import struct
import subprocess
import tempfile
from pathlib import Path

import pytest

from amiga_devbench import winuae_state
from amiga_devbench.winuae_debug import DebugError, WinuaeDebugger

EXE, ROM = os.environ.get('WINUAE_PATH'), os.environ.get('WINUAE_ROM')
CPU = os.environ.get('WINUAE_CPU_MODEL', '68020')
pytestmark = pytest.mark.skipif(not (EXE and ROM), reason='set WINUAE_PATH and WINUAE_ROM for live WinUAE tests')

# 10000 moveq #1,d0 / 10002 addq.l #1,d1 / 10004 bsr.w 1000c / 10008 bra.s 10000
# 1000a nop / 1000c addq.l #1,d2 / 1000e rts
LOOP = bytes.fromhex('70015281610000066' + '0f64e7152824e75')


def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


@pytest.fixture(scope='module')
def emulator(tmp_path_factory):
    folder = tmp_path_factory.mktemp('winuae')
    port = free_port()
    log = open(folder / 'winuae.log', 'w')
    process = subprocess.Popen(
        [EXE, '-s', 'use_gui=no', '-s', f'kickstart_rom_file={ROM}', '-s', f'cpu_model={CPU}',
         '-s', 'chipmem_size=4', '-s', f'cpu_compatible={"true" if CPU == "68000" else "false"}',
         '-s', 'cachesize=0', '-s', 'debugging_features=gdbserver', '-s', f'gdb_port={port}'],
        cwd=folder, stdout=log, stderr=log,
        env=dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', QT_QPA_PLATFORM='offscreen'))
    yield port, folder
    process.terminate()
    try:
        process.wait(5)
    except subprocess.TimeoutExpired:
        process.kill()


def run(emulator, test, monkeypatch=None):
    port, folder = emulator

    async def body():
        d = WinuaeDebugger(port=port, enabled=True, timeout=10)
        for _ in range(100):
            try:
                await d.call('connect')
                break
            except DebugError:
                await asyncio.sleep(0.2)
        else:
            raise AssertionError('WinUAE GDB server did not start')
        try:
            if (await d.call('status'))['execution'] != 'stopped':
                await d.call('pause')
            await test(d, folder)
        finally:
            await d.call('disconnect')
    asyncio.run(body())


async def start_loop(d):
    """Boot Kickstart briefly, then run the loop in supervisor mode."""
    await d.call('resume')
    await asyncio.sleep(1.5)
    await d.call('pause')
    await d.call('write_memory', address=0x10000, hex_data=LOOP.hex())
    for name, value in (('SR', 0x2700), ('A7', 0x30000), ('PC', 0x10000), ('D0', 0), ('D1', 0), ('D2', 0)):
        await d.call('registers', register=name, value=value)


async def resume_and_wait(d, timeout=5):
    await d.call('resume')
    result = await d.call('wait', timeout=timeout)
    if result['timed_out']:
        await d.call('pause')
    return result


def test_steps_stack_pointers_and_breakpoints(emulator):
    async def test(d, folder):
        await start_loop(d)
        regs = await d.call('registers')
        assert regs['ISP'] == 0x30000 and 'USP' in regs and (('MSP' in regs) == (CPU in ('68020', '68030', '68040')))
        await d.call('registers', register='USP', value=0x28000)
        regs = await d.call('registers')
        assert regs['USP'] == 0x28000 and regs['SR'] == 0x2700 and regs['A7'] == 0x30000
        result = await d.call('step', count=3)
        assert result['steps'] == 3 and (await d.call('registers'))['PC'] == 0x1000c
        result = await d.call('step_out')
        assert result['returned'] and (await d.call('registers'))['PC'] == 0x10008
        d2 = (await d.call('registers'))['D2']
        await d.call('breakpoint', operation='add', address=0x1000c, skip=2)
        assert not (await resume_and_wait(d))['timed_out']
        regs = await d.call('registers')
        point = (await d.call('breakpoint'))[0]
        assert regs['PC'] == 0x1000c and regs['D2'] == d2 + 2 and point['hits'] == 3 and point['skip'] == 0
        await d.call('breakpoint', operation='clear')
        await d.call('breakpoint', operation='add', address=0x10002, oneshot=True)
        assert not (await resume_and_wait(d))['timed_out']
        assert (await d.call('registers'))['PC'] == 0x10002 and await d.call('breakpoint') == []
        with pytest.raises(DebugError, match='No symbol'):
            await d.call('breakpoint', operation='add', symbol='no_such_function')
        await d.call('registers', register='PC', value=0x10000)
        await d.call('range_step', start=0x10000, end=0x10004)
        assert not (await d.call('wait', timeout=5))['timed_out']
        assert (await d.call('registers'))['PC'] == 0x10004
    run(emulator, test)


def test_watches_conditions_and_history(emulator):
    async def test(d, folder):
        await start_loop(d)
        added = await d.call('watch', operation='add', address=0x1000c, size=4, access='I')
        listed = (await d.call('watch'))['watches']
        assert listed[0]['id'] == added['id'] and listed[0]['range']
        if CPU == '68000':
            # Faster CPU modes fetch from RAM without going through memwatch.
            stop = await resume_and_wait(d)
            assert not stop['timed_out'] and 'watch' in stop['last_stop']
            last = await d.call('watch', operation='last')
            assert last['access'] == 4 and 0x1000c <= last['address'] < 0x10010
        await d.call('watch', operation='clear')
        with pytest.raises(DebugError, match='size 1, 2 or 4'):
            await d.call('watch', operation='add', address=0x2fff8, size=8, access='W', value=1)
        # bsr pushes the return address 0x10008 at 0x2fffc; chip RAM is 16
        # bits wide, so this is two word writes on every CPU.
        await d.call('watch', operation='add', address=0x2fffc, size=4, access='W', value=0x12345678)
        assert (await resume_and_wait(d, 1))['timed_out']
        await d.call('watch', operation='clear')
        await d.call('watch', operation='add', address=0x2fffe, size=2, access='write', value=0x0008, exact=True)
        assert not (await resume_and_wait(d))['timed_out']
        assert (await d.call('watch', operation='last'))['value'] & 0xffff == 0x0008
        await d.call('watch', operation='clear')
        dma = await d.call('dma_watch', operation='add', address=0x25000, length=16, mode='read', sources=['copper'])
        assert any(w['id'] == dma['id'] for w in await d.call('dma_watch'))
        await d.call('dma_watch', operation='remove', id=dma['id'])
        target = (await d.call('registers'))['D2'] + 3
        condition = await d.call('condition', operation='add', register='D2', operator='eq', value=target)
        assert not (await resume_and_wait(d))['timed_out']
        assert (await d.call('registers'))['D2'] == target
        await d.call('condition', operation='remove', id=condition['id'])
        await d.call('history', operation='on')
        await d.call('step', count=5)
        assert (await d.call('history', count=4))
        await d.call('history', operation='off')
    run(emulator, test)


def test_chipset_views_and_library_annotation(emulator, tmp_path):
    async def test(d, folder):
        await start_loop(d)
        custom = await d.call('custom')
        assert 'DMACON' in custom['registers'] and isinstance(custom['decoded']['dmacon'], list)
        assert custom['beam']['vpos'] is not None
        copper = await d.call('copper', count=8)
        assert copper['instructions']
        stack = await d.call('stack', depth=4)
        assert stack['sp'] == 0x30000 and len(stack['words']) == 4
        await d.call('step', count=3)  # into the subroutine
        stack = await d.call('stack', depth=2)
        assert stack['words'][0]['value'] == 0x10008 and stack['words'][0]['kind'] == 'return'
        assert (await d.call('lookup', address=0xdff096))['name'] == 'DMACON'
        assert (await d.call('lookup', address=0xbfe001))['name'] == 'ciaapra'
        assert (await d.call('lookup', address=0x10))['name'] == 'Illegal instruction'
        assert (await d.call('fd', operation='lookup', offset=-552))['name'] == 'OpenLibrary'
        fd = tmp_path / 'test_lib.fd'
        fd.write_text('##base _TestBase\n##bias 30\n##public\nFirst()()\nSecond(a)(d0)\n##end\n')
        await d.call('fd', operation='load', path=str(fd), library='test.library')
        assert (await d.call('fd', operation='lookup', offset=-36, library='test'))['name'] == 'Second'
        await d.call('write_memory', address=0x10100, hex_data='4eaefdd84e75')
        listing = await d.call('disassemble', address=0x10100, count=1, library='exec')
        assert listing['lines'][0]['annotation'] == 'exec.OpenLibrary()'
    run(emulator, test)


def test_checkpoints_slots_diff_and_autosnap(emulator, tmp_path, monkeypatch):
    monkeypatch.setattr(winuae_state, 'SNAPSHOT_DIR', tmp_path / 'snapshots')

    async def test(d, folder):
        await start_loop(d)
        await d.call('breakpoint', operation='add', address=0x1000c)
        saved = await d.call('checkpoint', operation='save', path=str(tmp_path / 'one.uss'))
        assert Path(saved['path']).stat().st_size > 1000 and saved['suspended_breakpoints'] == 1
        await d.call('snapshots', operation='save', slot='slot-1')
        await d.call('snapshots', operation='label', slot='slot-1', label='loop running')
        listing = await d.call('snapshots')
        assert listing['labels']['slot-1'] == 'loop running' and listing['slots'][0]['size'] > 0
        restored = await d.call('checkpoint', operation='restore', path=str(tmp_path / 'one.uss'))
        for name in ('PC', 'D1', 'D2', 'A7', 'SR'):
            assert restored['registers'][name] == saved['registers'][name]
        # A second restore straight from the restored stop.
        again = await d.call('checkpoint', operation='restore', path=str(tmp_path / 'one.uss'))
        assert again['registers']['PC'] == saved['registers']['PC']
        assert (await d.call('breakpoint'))[0]['address'] == 0x1000c
        assert not (await resume_and_wait(d))['timed_out']
        assert (await d.call('registers'))['PC'] == 0x1000c
        diff = await d.call('snapshot_diff', a=str(tmp_path / 'one.uss'), b='slot-1')
        assert 'CPU' in [c['chunk'] for c in diff['changed']] or diff['unchanged']
        await d.call('breakpoint', operation='clear')
        await d.call('resume')
        await d.call('autosnap', interval=1, ring_size=2)
        await asyncio.sleep(2.6)
        status = await d.call('autosnap', interval=0)
        assert status['saves'] >= 1 and not status['last_error']
    run(emulator, test)


def test_input_output_screenshot_and_memory_tools(emulator, tmp_path):
    async def test(d, folder):
        await start_loop(d)
        await d.call('input', event='key', code=0x40, pressed=True)
        assert (await d.call('input'))['held']
        await d.call('input', event='release')
        await d.call('key_tap', codes=[0x40])
        await d.call('input', event='release')
        await d.call('type_text', text='hi', layout='us')
        await d.call('input', event='release')
        await d.call('guest_output', operation='on')
        assert 'records' in await d.call('guest_output')
        await d.call('guest_output', operation='off')
        shot = await d.call('screenshot', path=str(tmp_path / 'screen.png'))
        assert base64.b64decode(shot['png_base64'])[:4] == b'\x89PNG'
        found = await d.call('memory_search', address=0x10000, length=0x100, hex='5282')
        assert found['matches'] == [0x1000c]
        bitmap = await d.call('bitmap', width=16, height=2, planes=[0x10000], palette=[0, 0xffffff])
        assert base64.b64decode(bitmap['png_base64'])[:4] == b'\x89PNG'
        assert (await d.call('capture', ranges=[dict(address=0x10000, length=16)]))['memory'][0]['hex'] == LOOP.hex()
        raw = tmp_path / 'raw.bin'
        raw.write_bytes(bytes(range(64)))
        assert (await d.call('load', file=str(raw), address=0x18000))['verified']
        await d.call('exception_mask', vectors=[4])
        await d.call('write_memory', address=0x10000, hex_data='4afc')
        await d.call('registers', register='PC', value=0x10000)
        assert not (await resume_and_wait(d))['timed_out']
        report = await d.call('postmortem')
        assert report['diagnosis_context'] == 'fault' and report['fault']['vector'] == 4
        await d.call('exception_mask', vectors=[])
    run(emulator, test)


def hunk_file(path):
    longs = lambda *values: b''.join(struct.pack('>I', v) for v in values)
    name = b'start\0\0\0'
    path.write_bytes(
        longs(0x3f3, 0, 2, 0, 1, 2, 1) +
        longs(0x3e9, 2) + bytes.fromhex('4e714e7500000000') + longs(0x3ec, 1, 1, 4, 0) +
        longs(0x3f0, 2) + name + longs(0, 0, 0x3f2) +
        longs(0x3ea, 1, 0x12345678, 0x3f2))


def test_hunk_symbols_and_dwarf_validation(emulator, tmp_path):
    async def test(d, folder):
        await start_loop(d)
        exe = tmp_path / 'test.exe'
        hunk_file(exe)
        info = await d.call('hunk_inspect', file=str(exe))
        assert [h['kind'] for h in info['hunks']] == ['code', 'data'] and info['symbols'][0]['name'] == 'start'
        placements = [dict(address=0x40000, capacity=8, memory='chip'), dict(address=0x40100, capacity=4, memory='chip')]
        await d.call('hunk_load', file=str(exe), placements=placements)
        loaded = await d.call('read_memory', address=0x40000, size=8)
        assert loaded['hex'] == '4e714e7500040100'
        listing = await d.call('symbols', file=str(exe))
        assert listing['format'] == 'hunk' and listing['symbols'][0]['name'] == 'start'
        read = await d.call('symbol_read', file=str(exe), symbol='start', format='u16', count=2,
                            mappings=[dict(section=0, address=0x40000, size=8)])
        assert read['values'] == [0x4e71, 0x4e75]
        with pytest.raises(DebugError, match='linked ELF32'):
            await d.call('source', file=str(exe), mappings=[dict(section=0, address=0x40000, size=8)])
    run(emulator, test)


def test_soft_and_hard_reset_keep_session_points(emulator):
    async def test(d, folder):
        await start_loop(d)
        await d.call('breakpoint', operation='add', address=0xf80002)
        result = await d.call('reset', hard=False, resume=False)
        assert result['reset'] == 'soft' and (await d.call('breakpoint'))[0]['address'] == 0xf80002
        await d.call('breakpoint', operation='clear')
        result = await d.call('reset', hard=True, resume=True)
        assert result['reset'] == 'hard' and result['connected']
    run(emulator, test)
