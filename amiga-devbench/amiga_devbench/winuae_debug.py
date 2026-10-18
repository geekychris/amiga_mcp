# SPDX-License-Identifier: MIT
"""WinUAE native debugger. One RSP connection shared by HTTP and MCP.

Connection is explicit: WinUAE pauses the CPU on GDB attach. Status reads
never attach or interrupt. Inspection and mutation require a paused CPU. No guest bridge or subprocess is needed.
"""
from __future__ import annotations

import asyncio
import json
import re
from typing import Any

REGISTERS = tuple([f'D{i}' for i in range(8)] + [f'A{i}' for i in range(8)] + ['SR', 'PC'])
STOP = re.compile(r'(?:S[0-9a-fA-F]{2}|T[0-9a-fA-F]{2}.*)\Z')


class DebugError(Exception):
    pass


def integer(value: Any, name: str, maximum: int = 0xffffffff, minimum: int = 0) -> int:
    if isinstance(value, str):
        text = value.strip()
        try:
            value = int(text[1:], 16) if text.startswith('$') else int(text, 16 if text.lower().startswith('0x') else 10)
        except ValueError:
            raise DebugError(f'{name} must be an integer (hex requires 0x or $)') from None
    if type(value) is not int or not minimum <= value <= maximum:
        raise DebugError(f'{name} must be in {minimum}..{maximum}')
    return value


def decode_packet(data: bytes) -> str:
    """Decode RSP quoting/RLE after checking the wire checksum."""
    result = bytearray()
    i = 0
    while i < len(data):
        ch = data[i]
        if ch == 125:
            i += 1
            if i == len(data):
                raise DebugError('Truncated RSP escape')
            result.append(data[i] ^ 32)
        elif ch == 42:
            i += 1
            if not result or i == len(data) or not 32 <= data[i] <= 126:
                raise DebugError('Invalid RSP repeat')
            result.extend([result[-1]] * (data[i] - 29))
        else:
            result.append(ch)
        if len(result) > 65536:
            raise DebugError('RSP response too large')
        i += 1
    return result.decode('ascii')


class WinuaeDebugger:
    def __init__(self, host: str = '127.0.0.1', port: int = 2345,
                 enabled: bool = False, timeout: float = 5.0):
        self.host, self.port, self.enabled = host, port, enabled
        self.timeout = timeout
        self._lock = asyncio.Lock()
        self._writer: asyncio.StreamWriter | None = None
        self._reader_task: asyncio.Task | None = None
        self._pending: asyncio.Future | None = None
        self._stop = asyncio.Event()
        self._stop_generation = 0
        self._no_ack = False
        self._last_packet = b''
        self._retries = 0
        self._packet_size = 4096
        self.execution = 'disconnected'
        self.last_stop: str | None = None
        self.last_error: str | None = None
        self.capabilities: dict = {}
        self._breakpoints: set[tuple[int, int, int]] = set()

    @property
    def connected(self) -> bool:
        return self._writer is not None and not self._writer.is_closing()

    def snapshot(self) -> dict:
        return dict(enabled=self.enabled, connected=self.connected, host=self.host,
                    port=self.port, execution=self.execution, last_stop=self.last_stop,
                    last_error=self.last_error, capabilities=self.capabilities,
                    breakpoints=[dict(kind=k, address=a, size=n)
                                 for k, a, n in sorted(self._breakpoints)])

    def _closed(self, message: str | None = None) -> None:
        if self._writer:
            self._writer.close()
        self._writer = None
        self.execution = 'disconnected'
        self.last_error = message
        self.capabilities = {}
        self._breakpoints.clear()  # WinUAE removes session-owned points on disconnect.
        self._stop.set()
        if self._pending and not self._pending.done():
            self._pending.set_exception(DebugError(message or 'Debugger disconnected'))

    async def _close(self) -> None:
        writer, task = self._writer, self._reader_task
        self._closed()
        if task and task is not asyncio.current_task():
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass
        self._reader_task = None
        if writer:
            try:
                await writer.wait_closed()
            except OSError:
                pass

    async def aclose(self) -> None:
        async with self._lock:
            # D releases session-owned breakpoints and resumes a stopped guest.
            try:
                if self.connected:
                    await self._ok('D')
            except DebugError:
                pass
            finally:
                await self._close()

    async def _read(self, reader: asyncio.StreamReader) -> None:
        bad_checksums = 0
        try:
            while True:
                ch = await reader.readexactly(1)
                if ch == b'+':
                    continue
                if ch == b'-':
                    self._retries += 1
                    if self._no_ack or self._retries > 3 or not self._last_packet:
                        raise DebugError('RSP packet rejected')
                    self._writer.write(self._last_packet)
                    await self._writer.drain()
                    continue
                if ch != b'$':
                    raise DebugError('Invalid RSP frame')
                data = (await reader.readuntil(b'#'))[:-1]
                checksum = await reader.readexactly(2)
                if len(data) > 65536 or not re.fullmatch(b'[0-9a-fA-F]{2}', checksum):
                    raise DebugError('Invalid RSP checksum')
                if sum(data) % 256 != int(checksum, 16):
                    bad_checksums += 1
                    if self._no_ack or bad_checksums > 3:
                        raise DebugError('RSP checksum mismatch')
                    self._writer.write(b'-')
                    continue
                bad_checksums = 0
                if not self._no_ack:
                    self._writer.write(b'+')
                reply = decode_packet(data)
                if re.fullmatch(r'O(?:[0-9a-fA-F]{2})+', reply):
                    continue  # Console packets are not command responses.
                if STOP.fullmatch(reply):
                    self._stop_generation += 1
                    self.execution = 'stopped'
                    self.last_stop = reply
                    self._stop.set()
                    continue  # An async stop must never consume a monitor reply.
                if self._pending and not self._pending.done():
                    self._pending.set_result(reply)
                elif re.fullmatch(r'E[0-9a-fA-F]{2}', reply):
                    raise DebugError(f'WinUAE run command failed: {reply}')
                else:
                    raise DebugError('Unexpected RSP reply')
        except asyncio.CancelledError:
            raise
        except (OSError, EOFError, ValueError, asyncio.IncompleteReadError,
                asyncio.LimitOverrunError, DebugError) as exc:
            self._closed(str(exc) or 'Debugger disconnected')

    def _send(self, command: str) -> None:
        if not self.connected:
            raise DebugError('Connect to the WinUAE debugger first')
        data = command.encode('ascii')
        if len(data) > self._packet_size:
            raise DebugError('RSP request exceeds target packet size')
        self._last_packet = b'$' + data + f'#{sum(data) % 256:02x}'.encode()
        self._retries = 0
        self._writer.write(self._last_packet)

    async def _request(self, command: str) -> str:
        # All callers hold _lock; never leave an ambiguous reply on timeout.
        future = asyncio.get_running_loop().create_future()
        self._pending = future
        try:
            self._send(command)
            reply = await asyncio.wait_for(future, self.timeout)
            if re.fullmatch(r'E[0-9a-fA-F]{2}', reply):
                raise DebugError(f'WinUAE rejected the request ({reply})')
            if reply == '':
                raise DebugError('Command is not supported by this WinUAE build')
            return reply
        except (asyncio.TimeoutError, asyncio.CancelledError) as exc:
            self._closed('RSP request interrupted; reconnect before retrying')
            if isinstance(exc, asyncio.CancelledError):
                raise
            raise DebugError('WinUAE request timed out; connection closed') from None
        finally:
            self._pending = None

    async def _ok(self, command: str) -> None:
        if await self._request(command) != 'OK':
            raise DebugError('WinUAE did not acknowledge the change')

    async def _monitor(self, command: str, capability: str | None = None) -> Any:
        if capability and capability not in self.capabilities.get('commands', []):
            raise DebugError(f'This WinUAE build does not support {capability}')
        reply = await self._request('qRcmd,' + command.encode('utf-8').hex())
        if reply == 'OK':
            return None
        try:
            return json.loads(bytes.fromhex(reply).decode('utf-8'))
        except (ValueError, UnicodeError):
            raise DebugError('Invalid WinUAE monitor response') from None

    async def _connect(self) -> dict:
        if not self.enabled:
            raise DebugError('Enable [winuae_debug] in your DevBench configuration first')
        if self.connected:
            return self.snapshot()
        await self._close()
        self._no_ack = False
        self._packet_size = 4096
        self.last_stop = None
        self._stop.clear()
        try:
            reader, self._writer = await asyncio.wait_for(
                asyncio.open_connection(self.host, self.port, limit=65540), self.timeout)
            self._reader_task = asyncio.create_task(self._read(reader))
            supported = await self._request('qSupported')
            size = re.search(r'(?:^|;)PacketSize=([0-9a-fA-F]+)', supported)
            if size:
                self._packet_size = min(65536, max(64, int(size[1], 16)))
            if 'QStartNoAckMode+' in supported.split(';'):
                if await self._request('QStartNoAckMode') != 'OK':
                    raise DebugError('Invalid no-ack negotiation')
                self._no_ack = True
            caps = await self._monitor('capabilities')
            if not isinstance(caps, dict) or caps.get('protocol') != 1 or 'execution-status' not in caps.get('commands', []):
                raise DebugError('The endpoint is not a supported WinUAE debugger')
            self.capabilities = caps
            await self._execution()
            return self.snapshot()
        except (OSError, asyncio.TimeoutError, DebugError) as exc:
            await self._close()
            self.last_error = str(exc)
            raise DebugError(f'Cannot connect to WinUAE at {self.host}:{self.port}: {exc}') from None

    async def _execution(self) -> dict:
        generation = self._stop_generation
        state = await self._monitor('execution-status', 'execution-status')
        if not isinstance(state, dict) or state.get('execution') not in ('running', 'stopped', 'host-paused'):
            raise DebugError('Invalid WinUAE execution status')
        if self._stop_generation != generation:
            return state  # A newer asynchronous stop takes precedence over the sampled reply.
        self.execution = state['execution']
        if state.get('debugger_stopped'):
            self.execution = 'stopped'
            self._stop.set()
        elif self.execution != 'stopped':
            self._stop.clear()  # Resumed elsewhere, e.g. from the WinUAE GUI.
        return state

    def _paused(self) -> None:
        if not self.connected:
            raise DebugError('Connect to the WinUAE debugger first')
        if self.execution != 'stopped':
            raise DebugError('Pause WinUAE before inspecting or changing native state')

    async def wait_stopped(self, timeout: float) -> dict:
        if not isinstance(timeout, (int, float)) or not 0 < timeout <= 60:
            raise DebugError('Stop timeout must be greater than zero and at most 60 seconds')
        if not self.connected:
            raise DebugError('Connect to the WinUAE debugger first')
        try:
            await asyncio.wait_for(self._stop.wait(), timeout)
        except asyncio.TimeoutError:
            return dict(self.snapshot(), timed_out=True)  # Never interrupt on timeout.
        if not self.connected:
            raise DebugError(self.last_error or 'Debugger disconnected')
        return dict(self.snapshot(), timed_out=False)

    async def call(self, action: str, **args: Any) -> Any:
        """Validated operations shared by the web routes and MCP tools."""
        if action == 'wait':
            return await self.wait_stopped(args.get('timeout', 10))
        if action == 'disconnect':
            await self.aclose()
            return self.snapshot()
        async with self._lock:
            if action == 'connect':
                return await self._connect()
            if action == 'status':
                if self.connected:
                    await self._execution()
                return self.snapshot()
            if action == 'pause':
                if not self.connected:
                    raise DebugError('Connect to the WinUAE debugger first')
                self._writer.write(b'\x03')
                # Waiting here is bounded; ordinary stop observers do not hold _lock.
                result = await self.wait_stopped(self.timeout)
                if result['timed_out']:
                    raise DebugError('Interrupt timed out; WinUAE has not reported a stop')
                return result
            self._paused()
            if action in ('resume', 'step', 'step_over'):
                self._stop.clear()
                self.last_stop = None
                self.execution = 'running'
                if action == 'step_over':
                    try:
                        await self._monitor('step-over', 'step-over')
                    except DebugError:
                        if self.connected:
                            self.execution = 'stopped'
                            self._stop.set()
                        raise
                else:
                    self._send('c' if action == 'resume' else 's')
                return self.snapshot()
            if action == 'registers':
                if 'register' in args:
                    name = str(args['register']).upper()
                    if name not in REGISTERS:
                        raise DebugError('Register must be D0-D7, A0-A7, SR or PC')
                    value = integer(args.get('value'), 'register value', 0xffff if name == 'SR' else 0xffffffff)
                    if name in ('PC', 'A7') and value & 1:
                        raise DebugError(f'{name} must be even')
                    await self._ok(f'P{REGISTERS.index(name):x}={value:08x}')
                reply = await self._request('g')
                if not re.fullmatch('[0-9a-fA-F]{144}', reply):
                    raise DebugError('Unexpected m68k register layout')
                return dict(zip(REGISTERS, (int(reply[i:i+8], 16) for i in range(0, 144, 8))))
            if action in ('read_memory', 'write_memory'):
                address = integer(args.get('address'), 'address')
                if action == 'write_memory':
                    data = args.get('hex_data', '')
                    if not isinstance(data, str) or not re.fullmatch(r'(?:[0-9a-fA-F]{2}){1,4096}', data):
                        raise DebugError('Supply 1..4096 bytes as pairs of hex digits')
                    size = len(data) // 2
                else:
                    size = integer(args.get('size', 256), 'size', 65536, 1)
                if address + size > 0x100000000:
                    raise DebugError('Memory range wraps the address space')
                if action == 'write_memory':
                    check = await self._monitor(f'memory-check {address:x} {size:x}', 'memory-check')
                    if not isinstance(check, dict) or check.get('writable_ram') is not True:
                        raise DebugError('The complete write range must be physical RAM')
                result = ''
                chunk_size = min(4096, (self._packet_size - 40) // 2)
                for offset in range(0, size, chunk_size):
                    length = min(chunk_size, size-offset)
                    if action == 'read_memory':
                        reply = await self._request(f'm{address+offset:x},{length:x}')
                        if not re.fullmatch(f'[0-9a-fA-F]{{{length*2}}}', reply):
                            raise DebugError('Truncated memory response')
                        result += reply
                    else:
                        await self._ok(f'M{address+offset:x},{length:x}:' + data[offset*2:(offset+length)*2])
                return dict(address=address, size=size, **({'hex': result} if action == 'read_memory' else {}))
            if action == 'disassemble':
                address = args.get('address', 'pc')
                if address == 'pc':
                    address = int(await self._request('p11'), 16)
                address = integer(address, 'address')
                count = integer(args.get('count', 20), 'count', 100, 1)
                # Native disassembler returns plain text, not JSON.
                if 'disasm' not in self.capabilities.get('commands', []):
                    raise DebugError('Native disassembly is unavailable')
                reply = await self._request('qRcmd,' + f'disasm {address:x} {count}'.encode().hex())
                return dict(address=address, text=bytes.fromhex(reply).decode('utf-8'))
            if action == 'breakpoint':
                operation = args.get('operation', 'list')
                if operation == 'list':
                    return self.snapshot()['breakpoints']
                if operation not in ('add', 'remove'):
                    raise DebugError('Breakpoint operation must be add, remove or list')
                kind = {'execute': 0, 'write': 2, 'read': 3, 'access': 4}.get(args.get('kind', 'execute'))
                if kind is None:
                    raise DebugError('Breakpoint kind must be execute, read, write or access')
                address = integer(args.get('address'), 'address')
                size = 2 if kind == 0 else integer(args.get('size', 1), 'size', 65536, 1)
                if address + size > 0x100000000 or (kind == 0 and address & 1):
                    raise DebugError('Invalid breakpoint range or instruction alignment')
                key = kind, address, size
                if operation == 'remove' and key not in self._breakpoints:
                    raise DebugError('This session does not own that breakpoint')
                await self._ok(f'{"Z" if operation == "add" else "z"}{kind},{address:x},{size:x}')
                if operation == 'add':
                    self._breakpoints.add(key)
                else:
                    self._breakpoints.remove(key)
                return self.snapshot()['breakpoints']
            if action == 'inspect':
                subject = args.get('subject', 'exception')
                if subject in ('tasks', 'exception', 'memory-map'):
                    return await self._monitor(subject, subject)
                if subject == 'segments':
                    process = integer(args.get('process', 0), 'process address')
                    if process & 3:
                        raise DebugError('Process address must be aligned to four bytes')
                    return await self._monitor('segments' + (f' {process:x}' if process else ''), 'segments')
                raise DebugError('Inspection must be tasks, segments, exception or memory-map')
            if action == 'process_break':
                operation = args.get('operation', 'status')
                if operation in ('clear', 'status'):
                    return await self._monitor('process-break ' + operation, 'process-break')
                if operation == 'name':
                    name = args.get('name', '')
                    if not isinstance(name, str) or not 1 <= len(name) <= 255 or any(not 32 <= ord(c) < 127 for c in name):
                        raise DebugError('Program name must contain 1..255 printable ASCII characters')
                    return await self._monitor('process-break name ' + name, 'process-break')
                if operation == 'address':
                    address = integer(args.get('address'), 'process address', minimum=4)
                    if address & 3:
                        raise DebugError('Process address must be aligned to four bytes')
                    return await self._monitor(f'process-break address {address:x}', 'process-break')
                raise DebugError('Process breakpoint operation must be status, name, address or clear')
            if action == 'exception_mask':
                vectors = args.get('vectors')
                if not isinstance(vectors, list) or len(vectors) > 62:
                    raise DebugError('Supply a list of exception vectors (2..63); [] disables stops')
                mask = 0
                for vector in vectors:
                    mask |= 1 << integer(vector, 'exception vector', 63, 2)
                await self._monitor(f'exception-mask {mask:016x}', 'exception-mask')
                return await self._monitor('exception', 'exception')
            raise DebugError(f'Unknown WinUAE operation: {action}')
