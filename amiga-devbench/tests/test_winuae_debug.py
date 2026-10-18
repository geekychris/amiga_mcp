"""Exercise real asynchronous RSP sockets, including unsolicited stops."""
import asyncio
import json
from contextlib import asynccontextmanager

import pytest
from amiga_devbench.winuae_debug import WinuaeDebugger, DebugError, decode_packet


def packet(text):
    data = text.encode()
    return b'$' + data + f'#{sum(data) % 256:02x}'.encode()


class Target:
    def __init__(self):
        self.commands = []
        self.running = True
        self.regs = [0] * 17 + [0x10000]
        self.memory = bytearray(0x1000)
        self.connections = []
        self.points = set()
        self.interleave = False
        self.delay = False
        self.bad_checksum = False
        self.readonly = False
        self.mask = '0000000000000000'
        self.caps = dict(protocol=1, commands=['execution-status', 'disasm', 'memory-check', 'exception-mask', 'exception', 'step-over', 'tasks', 'segments', 'process-break'])

    async def serve(self, reader, writer):
        self.connections.append(writer)
        last = b''
        try:
            while True:
                ch = await reader.readexactly(1)
                if ch == b'+':
                    continue
                if ch == b'-':
                    writer.write(last)
                    continue
                if ch == b'\x03':
                    self.commands.append('INTERRUPT')
                    self.running = False
                    writer.write(packet('T02thread:1;'))
                    continue
                assert ch == b'$'
                raw = (await reader.readuntil(b'#'))[:-1]
                assert int(await reader.readexactly(2), 16) == sum(raw) % 256
                cmd = raw.decode()
                self.commands.append(cmd)
                reply = 'OK'
                if cmd == 'qSupported':
                    reply = 'PacketSize=100;QStartNoAckMode+'
                elif cmd == 'D':
                    self.points.clear()
                    self.running = True
                elif cmd.startswith('qRcmd,'):
                    monitor = bytes.fromhex(cmd[6:]).decode()
                    if monitor == 'capabilities': value = self.caps
                    elif monitor == 'execution-status':
                        if self.delay:
                            await asyncio.sleep(.3)
                        value = dict(execution='running' if self.running else 'stopped', debugger_stopped=not self.running)
                    elif monitor.startswith('memory-check '): value = {'writable_ram': not self.readonly}
                    elif monitor == 'exception': value = {'mask': self.mask, 'last': None}
                    elif monitor.startswith('exception-mask '):
                        self.mask = monitor.split()[1]; value = None
                    elif monitor.startswith('disasm '): value = '00010000  4e71  NOP'
                    elif monitor == 'step-over':
                        self.running = False
                        writer.write(packet('T05thread:1;'))
                        value = None
                    elif monitor == 'tasks': value = [{'address': 0x100, 'name': 'CLI'}]
                    else: value = None
                    reply = 'OK' if value is None else (value if isinstance(value, str) else json.dumps(value)).encode().hex()
                elif cmd == 'g':
                    reply = ''.join(f'{x:08x}' for x in self.regs)
                    if self.interleave:
                        writer.write(packet('T05watch:100;'))
                elif cmd.startswith('p'): reply = f'{self.regs[int(cmd[1:],16)]:08x}'
                elif cmd.startswith('P'):
                    r, v = cmd[1:].split('='); self.regs[int(r,16)] = int(v,16)
                elif cmd.startswith('m'):
                    a,n = [int(x,16) for x in cmd[1:].split(',')];reply = self.memory[a:a+n].hex()
                elif cmd.startswith('M'):
                    location, data = cmd[1:].split(':');a,n = [int(x,16) for x in location.split(',')]
                    self.memory[a:a+n] = bytes.fromhex(data)
                elif cmd.startswith('Z'): self.points.add(cmd[1:])
                elif cmd.startswith('z'): self.points.remove(cmd[1:])
                elif cmd == 'c':
                    self.running = True
                    continue
                elif cmd == 's':
                    self.regs[-1] += 2
                    reply = 'T05thread:1;'
                last = packet(reply)
                if self.bad_checksum:
                    self.bad_checksum = False
                    writer.write(last[:-2] + b'ff')
                else:
                    # TCP fragmentation at arbitrary framing boundaries.
                    writer.write(last[:2]); await writer.drain(); await asyncio.sleep(0)
                    writer.write(last[2:])
                if cmd == 'D': break
        except (asyncio.IncompleteReadError, ConnectionError):
            pass
        finally:
            writer.close()


@asynccontextmanager
async def session(target=None, timeout=.2):
    target = target or Target()
    server = await asyncio.start_server(target.serve, '127.0.0.1', 0)
    client = WinuaeDebugger(port=server.sockets[0].getsockname()[1], enabled=True, timeout=timeout)
    try:
        yield client, target
    finally:
        await client.aclose()
        server.close(); await server.wait_closed()
        for writer in target.connections: writer.close()


def test_connect_observation_and_explicit_pause():
    async def run():
        async with session() as (c,t):
            assert (await c.call('connect'))['execution'] == 'running'
            assert (await c.call('status'))['connected']
            with pytest.raises(DebugError, match='Pause'):
                await c.call('registers')
            assert 'INTERRUPT' not in t.commands and '?' not in t.commands
            await c.call('pause')
            assert (await c.call('registers'))['PC'] == 0x10000
            await c.call('disconnect')
            assert t.running and not c.connected
    asyncio.run(run())


def test_async_stop_does_not_consume_register_or_monitor_reply():
    async def run():
        async with session() as (c,t):
            await c.call('connect'); await c.call('pause')
            t.interleave = True
            assert (await c.call('registers'))['PC'] == 0x10000
            assert c.last_stop == 'T05watch:100;'
            await c.call('step_over')
            assert (await c.call('wait', timeout=.1))['execution'] == 'stopped'
            assert (await c.call('disassemble'))['text'].endswith('NOP')
    asyncio.run(run())


def test_wait_timeout_preserves_execution_and_does_not_block_pause():
    async def run():
        async with session() as (c,t):
            await c.call('connect')
            assert (await c.call('wait', timeout=.01))['timed_out']
            assert t.running and 'INTERRUPT' not in t.commands
            waiting = asyncio.create_task(c.call('wait', timeout=.15))
            await c.call('pause')
            assert not (await waiting)['timed_out']
            await c.call('step')
            assert not (await c.call('wait', timeout=.15))['timed_out']
            assert (await c.call('registers'))['PC'] == 0x10002
    asyncio.run(run())


def test_resume_outside_devbench_clears_the_old_stop():
    async def run():
        async with session() as (c,t):
            await c.call('connect'); await c.call('pause')
            t.running = True  # resumed from the WinUAE GUI
            assert (await c.call('status'))['execution'] == 'running'
            t.commands.clear()
            assert (await c.call('pause'))['execution'] == 'stopped'
            assert 'INTERRUPT' in t.commands
    asyncio.run(run())


def test_timeout_disconnects_before_a_late_response_can_be_reused():
    async def run():
        async with session(timeout=.03) as (c,t):
            await c.call('connect')
            t.delay = True
            with pytest.raises(DebugError, match='timed out'):
                await c.call('status')
            assert not c.connected
            t.delay = False
            assert (await c.call('connect'))['connected']
            await c.call('pause')
            assert (await c.call('registers'))['PC'] == 0x10000
    asyncio.run(run())


def test_disconnect_wakes_stop_waiters():
    async def run():
        async with session() as (c,t):
            await c.call('connect')
            waiter = asyncio.create_task(c.call('wait', timeout=.15))
            await asyncio.sleep(0)
            t.connections[-1].close()
            with pytest.raises(DebugError): await waiter
            assert c.execution == 'disconnected'
    asyncio.run(run())


def test_checksum_retransmit_and_bounded_decode():
    async def run():
        t = Target(); t.bad_checksum = True
        async with session(t) as (c,t):
            assert (await c.call('connect'))['connected']
    asyncio.run(run())
    assert decode_packet(b'0* ') == '0000'
    assert decode_packet(b'}\x03}]') == '#}'
    with pytest.raises(DebugError): decode_packet(b'* ')
    with pytest.raises(DebugError): decode_packet(b'}')


def test_chunked_memory_write_registers_points_and_exception_mask():
    async def run():
        async with session() as (c,t):
            await c.call('connect'); await c.call('pause')
            data = bytes(range(256)).hex()
            await c.call('write_memory', address='0x100', hex_data=data)
            assert (await c.call('read_memory', address='$100', size=256))['hex'] == data
            await c.call('registers', register='D0', value='$deadbeef')
            assert (await c.call('registers'))['D0'] == 0xdeadbeef
            await c.call('breakpoint', operation='add', address='0x100', kind='write', size=4)
            assert (await c.call('breakpoint'))[0]['size'] == 4
            await c.call('breakpoint', operation='remove', address='0x100', kind='write', size=4)
            assert not t.points
            assert (await c.call('exception_mask', vectors=[2,3,4]))['mask'] == '000000000000001c'
    asyncio.run(run())


def test_bad_input_and_unsupported_features_do_not_write_state():
    async def run():
        async with session() as (c,t):
            await c.call('connect'); await c.call('pause')
            for action, args in [
                ('read_memory', dict(address='0xffffffff', size=2)),
                ('read_memory', dict(address=True)),
                ('write_memory', dict(address=0, hex_data='0')),
                ('registers', dict(register='PC', value=1)),
                ('breakpoint', dict(operation='add', address=1)),
                ('exception_mask', dict(vectors=[1])),
                ('process_break', dict(operation='name', name='bad\nname')),
            ]:
                before = list(t.commands)
                with pytest.raises(DebugError): await c.call(action, **args)
                assert t.commands == before
            t.readonly = True
            with pytest.raises(DebugError): await c.call('write_memory', address=0, hex_data='ff')
            assert t.memory[0] == 0
            c.capabilities['commands'].remove('step-over')
            with pytest.raises(DebugError): await c.call('step_over')
            assert c.execution == 'stopped'
    asyncio.run(run())


def test_disabled_backend_never_connects():
    async def run():
        c = WinuaeDebugger()
        assert not (await c.call('status'))['connected']
        with pytest.raises(DebugError, match='Enable'): await c.call('connect')
    asyncio.run(run())
