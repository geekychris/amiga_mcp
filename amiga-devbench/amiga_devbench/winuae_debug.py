# SPDX-License-Identifier: MIT
"""WinUAE native debugger. One RSP connection shared by HTTP and MCP.

Connection is explicit: WinUAE pauses the CPU on GDB attach. Status reads
never attach or interrupt. Inspection and mutation require a paused CPU. No guest bridge or subprocess is needed.

Breakpoint stops are processed here before stop observers see them, which
provides hit counts, skip counts and one-shot breakpoints. Feature modules
register further operations with @action; all share this connection.
"""
from __future__ import annotations

import asyncio
import json
import re
from dataclasses import dataclass
from typing import Any, Awaitable, Callable

REGISTERS = tuple([f'D{i}' for i in range(8)] + [f'A{i}' for i in range(8)] + ['SR', 'PC'])
STACK_POINTERS = ('USP', 'ISP', 'MSP')
STOP = re.compile(r'(?:S[0-9a-fA-F]{2}|T[0-9a-fA-F]{2}.*)\Z')
# Raw Amiga key codes of the keyboard reset combination.
KEY_CTRL, KEY_LALT, KEY_LAMIGA, KEY_RAMIGA = 0x63, 0x64, 0x66, 0x67


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


def boolean(value: Any, name: str) -> bool:
    if value in (0, 1, '0', '1'):
        return bool(int(value))
    if not isinstance(value, bool):
        raise DebugError(f'{name} must be true or false')
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


@dataclass
class Action:
    func: Callable[..., Awaitable[Any]]
    locked: bool
    stopped: bool
    capability: str | None


ACTIONS: dict[str, Action] = {}
# Called after a checkpoint restore, which clears all remote debugger state.
REARM: list[Callable[['WinuaeDebugger'], Awaitable[None]]] = []


def action(name: str, *, locked: bool = True, stopped: bool = True, capability: str | None = None):
    """Register an operation. Locked operations run with the connection lock
    held and must not wait for a stop; stopped ones require a paused CPU."""
    def register(func):
        ACTIONS[name] = Action(func, locked, stopped, capability)
        return func
    return register


@dataclass
class Point:
    kind: int
    address: int
    size: int
    hits: int = 0
    skip: int = 0
    oneshot: bool = False
    symbol: str | None = None

    def describe(self) -> dict:
        return dict(kind=self.kind, address=self.address, size=self.size, hits=self.hits,
                    skip=self.skip, oneshot=self.oneshot, symbol=self.symbol)


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
        self._processing = 0
        self._interrupting = False
        self._run_mode: str | None = None
        self._no_ack = False
        self._last_packet = b''
        self._retries = 0
        self._packet_size = 4096
        self.execution = 'disconnected'
        self.last_stop: str | None = None
        self.last_error: str | None = None
        self.capabilities: dict = {}
        self._points: dict[tuple[int, int, int], Point] = {}
        self._exception_mask = 0
        # Per-session state owned by feature modules, cleared on disconnect.
        self.state: dict[str, Any] = {}
        # The emulator manager, when DevBench can launch WinUAE itself.
        self.emulator: Any = None

    @property
    def connected(self) -> bool:
        return self._writer is not None and not self._writer.is_closing()

    def commands(self) -> list[str]:
        return self.capabilities.get('commands', [])

    def snapshot(self) -> dict:
        return dict(enabled=self.enabled, connected=self.connected, host=self.host,
                    port=self.port, execution=self.execution, last_stop=self.last_stop,
                    last_error=self.last_error, capabilities=self.capabilities,
                    breakpoints=[p.describe() for _, p in sorted(self._points.items())])

    def _closed(self, message: str | None = None) -> None:
        if self._writer:
            self._writer.close()
        self._writer = None
        self.execution = 'disconnected'
        self.last_error = message
        self.capabilities = {}
        self._points.clear()  # WinUAE removes session-owned points on disconnect.
        self._exception_mask = 0
        self.state.clear()
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
                    self._stopped(reply)
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

    def _stopped(self, reply: str) -> None:
        # Breakpoint stops need the PC, which a plain S05 reply lacks. Only a
        # resumed run can hit a breakpoint; a single step stops by itself.
        signal = reply[1:3] == '05' and 'watch' not in reply and 'winuae-' not in reply
        if signal and self._run_mode != 'step' and any(k[0] == 0 for k in self._points):
            self._processing += 1
            asyncio.get_running_loop().create_task(self._process_stop(reply))
        else:
            self._signal_stop(reply)

    async def _process_stop(self, reply: str) -> None:
        resumed = False
        try:
            async with self._lock:
                if self.connected:
                    pc = int(await self._request('p11'), 16)
                    point = self._points.get((0, pc, 2))
                    if point:
                        point.hits += 1
                        if point.skip and self._run_mode == 'continue' and not self._interrupting:
                            point.skip -= 1
                            self._send('c')
                            resumed = True
                        elif point.oneshot:
                            await self._ok(f'z0,{pc:x},2')
                            del self._points[(0, pc, 2)]
        except (DebugError, ValueError):
            pass  # A failed request has closed the connection and woken observers.
        finally:
            self._processing -= 1
            if not resumed:
                self._signal_stop(reply)

    def _signal_stop(self, reply: str) -> None:
        self._stop_generation += 1
        self.execution = 'stopped'
        self.last_stop = reply
        self._interrupting = False
        self._run_mode = None
        self._stop.set()

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

    def require(self, capability: str) -> None:
        if capability not in self.commands():
            raise DebugError(f'This WinUAE build does not support {capability}')

    async def monitor_text(self, command: str, capability: str | None = None) -> str | None:
        """Run a monitor command; return its text, or None for a bare OK."""
        if capability:
            self.require(capability)
        reply = await self._request('qRcmd,' + command.encode('utf-8').hex())
        if reply == 'OK':
            return None
        try:
            return bytes.fromhex(reply).decode('utf-8')
        except (ValueError, UnicodeError):
            raise DebugError('Invalid WinUAE monitor response') from None

    async def _monitor(self, command: str, capability: str | None = None) -> Any:
        text = await self.monitor_text(command, capability)
        if text is None:
            return None
        try:
            return json.loads(text)
        except ValueError:
            raise DebugError('Invalid WinUAE monitor response') from None

    # Guest access helpers for feature modules. Callers hold _lock.

    async def read_bytes(self, address: int, size: int) -> bytes:
        if address + size > 0x100000000:
            raise DebugError('Memory range wraps the address space')
        result = bytearray()
        chunk_size = min(4096, (self._packet_size - 40) // 2)
        for offset in range(0, size, chunk_size):
            length = min(chunk_size, size - offset)
            reply = await self._request(f'm{address + offset:x},{length:x}')
            if not re.fullmatch(f'[0-9a-fA-F]{{{length * 2}}}', reply):
                raise DebugError('Truncated memory response')
            result += bytes.fromhex(reply)
        return bytes(result)

    async def write_bytes(self, address: int, data: bytes, check: bool = True) -> None:
        if address + len(data) > 0x100000000:
            raise DebugError('Memory range wraps the address space')
        if check:
            result = await self._monitor(f'memory-check {address:x} {len(data):x}', 'memory-check')
            if not isinstance(result, dict) or result.get('writable_ram') is not True:
                raise DebugError('The complete write range must be physical RAM')
        chunk_size = min(4096, (self._packet_size - 40) // 2)
        for offset in range(0, len(data), chunk_size):
            part = data[offset:offset + chunk_size]
            await self._ok(f'M{address + offset:x},{len(part):x}:' + part.hex())

    async def registers(self) -> dict[str, int]:
        reply = await self._request('g')
        if not re.fullmatch('[0-9a-fA-F]{144}', reply):
            raise DebugError('Unexpected m68k register layout')
        return dict(zip(REGISTERS, (int(reply[i:i + 8], 16) for i in range(0, 144, 8))))

    async def set_register(self, name: str, value: int) -> None:
        await self._ok(f'P{REGISTERS.index(name):x}={value:08x}')

    def _stack_modes(self) -> dict[str, int]:
        """SR S/M bits that select each stack pointer as A7."""
        modes = {'USP': 0, 'ISP': 0x2000}
        if self.capabilities.get('cpu_model', 68000) in (68020, 68030, 68040):
            modes['MSP'] = 0x3000
        return modes

    async def stack_pointers(self, regs: dict[str, int]) -> dict[str, int]:
        """Read the inactive stack pointers by switching SR, which WinUAE
        applies through MakeFromSR, then restore the original SR."""
        sr, modes = regs['SR'], self._stack_modes()
        current = 'USP' if not sr & 0x2000 else 'MSP' if sr & 0x1000 and 'MSP' in modes else 'ISP'
        result = {current: regs['A7']}
        switched = False
        try:
            for name, bits in modes.items():
                if name != current:
                    switched = True
                    await self.set_register('SR', (sr & ~0x3000) | bits)
                    result[name] = int(await self._request('p0f'), 16)
        finally:
            if switched:
                await self.set_register('SR', sr)
        return {name: result[name] for name in modes}

    async def set_stack_pointer(self, name: str, value: int) -> None:
        modes = self._stack_modes()
        if name not in modes:
            raise DebugError(f'{name} does not exist on this CPU')
        sr = (await self.registers())['SR']
        try:
            await self.set_register('SR', (sr & ~0x3000) | modes[name])
            await self.set_register('A7', value)
        finally:
            await self.set_register('SR', sr)

    def _run(self, command: str, mode: str) -> None:
        self._stop.clear()
        self.last_stop = None
        self.execution = 'running'
        self._run_mode = mode
        self._send(command)

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
        if self._stop_generation != generation or self._processing:
            return state  # A newer or pending asynchronous stop takes precedence.
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

    async def rearm(self) -> None:
        """Reinstall session state after a checkpoint restore cleared it."""
        points = list(self._points.values())
        self._points.clear()
        for point in points:
            await self._ok(f'Z{point.kind},{point.address:x},{point.size:x}')
            self._points[(point.kind, point.address, point.size)] = point
        if self._exception_mask:
            await self._monitor(f'exception-mask {self._exception_mask:016x}', 'exception-mask')
        for hook in REARM:
            await hook(self)

    async def call(self, action_name: str, **args: Any) -> Any:
        """Validated operations shared by the web routes and MCP tools."""
        entry = ACTIONS.get(action_name)
        if entry is None:
            raise DebugError(f'Unknown WinUAE operation: {action_name}')
        if not entry.locked:
            return await entry.func(self, **args)
        async with self._lock:
            if entry.stopped:
                self._paused()
            if entry.capability:
                self.require(entry.capability)
            return await entry.func(self, **args)


async def _step_once(d: WinuaeDebugger, command: str, mode: str, timeout: float) -> str:
    """Start one run command and wait for its stop without holding the lock."""
    async with d._lock:
        d._paused()
        d._run(command, mode)
    result = await d.wait_stopped(timeout)
    if result['timed_out']:
        raise DebugError('The CPU did not stop in time; it is still running')
    return d.last_stop or ''


def _stepped(reply: str) -> bool:
    """Whether a stop is the plain end of a step, not a watch or event."""
    return reply[1:3] == '05' and 'watch' not in reply and 'winuae-' not in reply


@action('connect', stopped=False)
async def _connect_action(d: WinuaeDebugger) -> dict:
    return await d._connect()


@action('disconnect', locked=False, stopped=False)
async def _disconnect(d: WinuaeDebugger) -> dict:
    await d.aclose()
    return d.snapshot()


@action('status', stopped=False)
async def _status(d: WinuaeDebugger) -> dict:
    if d.connected:
        await d._execution()
    return d.snapshot()


@action('wait', locked=False, stopped=False)
async def _wait(d: WinuaeDebugger, timeout: float = 10) -> dict:
    return await d.wait_stopped(timeout)


@action('pause', locked=False, stopped=False)
async def _pause(d: WinuaeDebugger) -> dict:
    async with d._lock:
        if not d.connected:
            raise DebugError('Connect to the WinUAE debugger first')
        if d.execution == 'stopped':
            return dict(d.snapshot(), timed_out=False)
        d._interrupting = True
        d._writer.write(b'\x03')
    result = await d.wait_stopped(d.timeout)
    if result['timed_out']:
        raise DebugError('Interrupt timed out; WinUAE has not reported a stop')
    return result


@action('resume')
async def _resume(d: WinuaeDebugger) -> dict:
    d._run('c', 'continue')
    return d.snapshot()


@action('step', locked=False, stopped=False)
async def _step(d: WinuaeDebugger, count: int = 1, wait: bool = False) -> dict:
    """One instruction returns at once; several step until done or a watch,
    breakpoint or exception stop interrupts them."""
    count = integer(count, 'count', 1000, 1)
    if count == 1 and not boolean(wait, 'wait'):
        async with d._lock:
            d._paused()
            d._run('s', 'step')
        return d.snapshot()
    done = 0
    while done < count:
        reply = await _step_once(d, 's', 'step', d.timeout)
        done += 1
        if not _stepped(reply):
            break
    return dict(d.snapshot(), steps=done)


@action('step_over', capability='step-over')
async def _step_over(d: WinuaeDebugger) -> dict:
    d._stop.clear()
    d.last_stop = None
    d.execution = 'running'
    d._run_mode = 'over'
    try:
        await d._monitor('step-over')
    except DebugError:
        if d.connected:
            d.execution = 'stopped'
            d._stop.set()
        raise
    return d.snapshot()


async def _next_address(d: WinuaeDebugger, pc: int) -> int:
    """Address after the instruction at pc, from the native disassembler."""
    text = await d.monitor_text(f'disasm {pc:x} 2', 'disasm') or ''
    addresses = [int(m[1], 16) for m in re.finditer(r'^\s*([0-9A-Fa-f]{8})\s', text, re.M)]
    if len(addresses) < 2 or addresses[0] != pc:
        raise DebugError('Cannot determine the next instruction address')
    return addresses[1]


@action('step_out', locked=False, stopped=False)
async def _step_out(d: WinuaeDebugger, max_instructions: int = 20000, timeout: float = 20) -> dict:
    """Run until the current subroutine returns. Calls and traps in it are
    stepped over, so nested returns do not end the step."""
    limit = integer(max_instructions, 'max_instructions', 1000000, 1)
    if not isinstance(timeout, (int, float)) or not 0 < timeout <= 60:
        raise DebugError('timeout must be greater than zero and at most 60 seconds')
    deadline = asyncio.get_running_loop().time() + timeout
    for executed in range(1, limit + 1):
        async with d._lock:
            d._paused()
            pc = (await d.registers())['PC']
            opcode = int.from_bytes(await d.read_bytes(pc, 2), 'big')
            call = (opcode & 0xffc0) == 0x4e80 or (opcode & 0xff00) == 0x6100 or (opcode & 0xfff0) == 0x4e40
            returns = opcode in (0x4e73, 0x4e74, 0x4e75, 0x4e77)
            target = await _next_address(d, pc) if call else None
        if call:
            d.require('step-over')
            async with d._lock:
                d._paused()
                await _step_over(d)
            result = await d.wait_stopped(max(0.1, deadline - asyncio.get_running_loop().time()))
            if result['timed_out']:
                raise DebugError('The called subroutine has not returned; the CPU is still running')
            async with d._lock:
                if (await d.registers())['PC'] != target:
                    return dict(d.snapshot(), returned=False, instructions=executed, reason='stopped inside a call')
        else:
            reply = await _step_once(d, 's', 'step', d.timeout)
            if not _stepped(reply):
                return dict(d.snapshot(), returned=False, instructions=executed, reason=reply)
            if returns:
                return dict(d.snapshot(), returned=True, instructions=executed)
        if asyncio.get_running_loop().time() > deadline:
            break
    return dict(d.snapshot(), returned=False, instructions=executed, reason='instruction or time budget exhausted')


@action('range_step')
async def _range_step(d: WinuaeDebugger, start: Any = None, end: Any = None) -> dict:
    """Step while the PC stays in [start, end), using GDB range stepping."""
    first, last = integer(start, 'start'), integer(end, 'end', 0x100000000)
    if last < first:
        raise DebugError('end must not be below start')
    if 'r' not in (await d._request('vCont?')).split(';'):
        raise DebugError('This WinUAE build does not support range stepping')
    d._run(f'vCont;r{first:x},{last:x}', 'range')
    return d.snapshot()


@action('registers')
async def _registers(d: WinuaeDebugger, register: str | None = None, value: Any = None,
                     stack_pointers: bool = True) -> dict:
    if register is not None:
        name = str(register).upper()
        if name in REGISTERS:
            number = integer(value, 'register value', 0xffff if name == 'SR' else 0xffffffff)
            if name in ('PC', 'A7') and number & 1:
                raise DebugError(f'{name} must be even')
            await d.set_register(name, number)
        elif name in STACK_POINTERS:
            number = integer(value, 'register value')
            if number & 1:
                raise DebugError(f'{name} must be even')
            await d.set_stack_pointer(name, number)
        else:
            raise DebugError('Register must be D0-D7, A0-A7, SR, PC, USP, ISP or MSP')
    regs = await d.registers()
    if boolean(stack_pointers, 'stack_pointers'):
        regs.update(await d.stack_pointers(regs))
    return regs


@action('read_memory')
async def _read_memory(d: WinuaeDebugger, address: Any = None, size: Any = 256) -> dict:
    address = integer(address, 'address')
    size = integer(size, 'size', 65536, 1)
    if address + size > 0x100000000:
        raise DebugError('Memory range wraps the address space')
    return dict(address=address, size=size, hex=(await d.read_bytes(address, size)).hex())


@action('write_memory')
async def _write_memory(d: WinuaeDebugger, address: Any = None, hex_data: Any = '') -> dict:
    address = integer(address, 'address')
    if not isinstance(hex_data, str) or not re.fullmatch(r'(?:[0-9a-fA-F]{2}){1,4096}', hex_data):
        raise DebugError('Supply 1..4096 bytes as pairs of hex digits')
    data = bytes.fromhex(hex_data)
    if address + len(data) > 0x100000000:
        raise DebugError('Memory range wraps the address space')
    await d.write_bytes(address, data)
    return dict(address=address, size=len(data))


@action('breakpoint')
async def _breakpoint(d: WinuaeDebugger, operation: str = 'list', address: Any = None,
                      kind: str = 'execute', size: Any = 1, skip: Any = 0, oneshot: Any = False,
                      symbol: str | None = None, project: str | None = None) -> list:
    if operation == 'list':
        return d.snapshot()['breakpoints']
    if operation == 'clear':
        for key in list(d._points):
            await d._ok(f'z{key[0]},{key[1]:x},{key[2]:x}')
            del d._points[key]
        return []
    if operation not in ('add', 'remove'):
        raise DebugError('Breakpoint operation must be add, remove, clear or list')
    code = {'execute': 0, 'write': 2, 'read': 3, 'access': 4}.get(kind)
    if code is None:
        raise DebugError('Breakpoint kind must be execute, read, write or access')
    if symbol:
        if code != 0:
            raise DebugError('Symbol breakpoints are execute breakpoints')
        from . import symbols
        hit = symbols.lookup_function_address(str(symbol), project)
        if hit is None:
            raise DebugError(f"No symbol '{symbol}' in the loaded projects; load symbols first")
        address = hit[1]
    address = integer(address, 'address')
    length = 2 if code == 0 else integer(size, 'size', 65536, 1)
    if address + length > 0x100000000 or (code == 0 and address & 1):
        raise DebugError('Invalid breakpoint range or instruction alignment')
    key = code, address, length
    if operation == 'remove':
        if key not in d._points:
            raise DebugError('This session does not own that breakpoint')
        await d._ok(f'z{code},{address:x},{length:x}')
        del d._points[key]
        return d.snapshot()['breakpoints']
    skip = integer(skip, 'skip', 1000000)
    oneshot = boolean(oneshot, 'oneshot')
    if code and (skip or oneshot):
        raise DebugError('Skip counts and one-shot apply to execute breakpoints')
    if key not in d._points:
        await d._ok(f'Z{code},{address:x},{length:x}')
    d._points[key] = Point(code, address, length, skip=skip, oneshot=oneshot,
                           symbol=str(symbol) if symbol else None)
    return d.snapshot()['breakpoints']


@action('inspect')
async def _inspect(d: WinuaeDebugger, subject: str = 'exception', process: Any = 0) -> Any:
    if subject == 'capabilities':
        return await d._monitor('capabilities')
    if subject in ('tasks', 'exception', 'memory-map'):
        return await d._monitor(subject, subject)
    if subject == 'segments':
        process = integer(process, 'process address')
        if process & 3:
            raise DebugError('Process address must be aligned to four bytes')
        return await d._monitor('segments' + (f' {process:x}' if process else ''), 'segments')
    raise DebugError('Inspection must be tasks, segments, exception, memory-map or capabilities')


@action('process_break')
async def _process_break(d: WinuaeDebugger, operation: str = 'status', name: Any = '', address: Any = 0) -> Any:
    if operation in ('clear', 'status'):
        return await d._monitor('process-break ' + operation, 'process-break')
    if operation == 'name':
        if not isinstance(name, str) or not 1 <= len(name) <= 255 or any(not 32 <= ord(c) < 127 for c in name):
            raise DebugError('Program name must contain 1..255 printable ASCII characters')
        return await d._monitor('process-break name ' + name, 'process-break')
    if operation == 'address':
        address = integer(address, 'process address', minimum=4)
        if address & 3:
            raise DebugError('Process address must be aligned to four bytes')
        return await d._monitor(f'process-break address {address:x}', 'process-break')
    raise DebugError('Process breakpoint operation must be status, name, address or clear')


@action('exception_mask')
async def _exception_mask(d: WinuaeDebugger, vectors: Any = None) -> Any:
    if not isinstance(vectors, list) or len(vectors) > 62:
        raise DebugError('Supply a list of exception vectors (2..63); [] disables stops')
    mask = 0
    for vector in vectors:
        mask |= 1 << integer(vector, 'exception vector', 63, 2)
    await d._monitor(f'exception-mask {mask:016x}', 'exception-mask')
    d._exception_mask = mask
    return await d._monitor('exception', 'exception')


@action('reset', locked=False, stopped=False)
async def _reset(d: WinuaeDebugger, hard: Any = False, resume: Any = True, timeout: float = 30) -> dict:
    """Reset through WinUAE's keyboard emulation: Ctrl-Amiga-Amiga is a soft
    reset, and holding Alt as well a hard reset. WinUAE detaches GDB clients
    on reset, so reconnect afterwards and resume unless asked not to."""
    hard, resume = boolean(hard, 'hard'), boolean(resume, 'resume')
    if not isinstance(timeout, (int, float)) or not 0 < timeout <= 60:
        raise DebugError('timeout must be greater than zero and at most 60 seconds')
    keys = ([KEY_LALT] if hard else []) + [KEY_CTRL, KEY_LAMIGA, KEY_RAMIGA]
    async with d._lock:
        if not d.connected:
            raise DebugError('Connect to the WinUAE debugger first')
        d.require('input')
        # Like FS-UAE breakpoints, session points survive the reset.
        saved = dict(d._points), d._exception_mask, dict(d.state)
        try:
            for state in (1, 0):
                for key in (keys if state else reversed(keys)):
                    await d.monitor_text(f'input event 0 0 {key:x} {state}')
            if d.execution == 'stopped':
                d._run('c', 'continue')
        except DebugError:
            if d.connected:
                raise  # Otherwise the reset has already closed the connection.
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    while d.connected and loop.time() < deadline:
        await asyncio.sleep(0.1)
    if d.connected:
        raise DebugError('WinUAE did not reset; is input recording or playback active?')
    while True:
        try:
            await d.call('connect')
            break
        except DebugError:
            if loop.time() > deadline:
                raise DebugError('WinUAE reset, but its GDB server did not come back in time') from None
            await asyncio.sleep(0.25)
    async with d._lock:
        d._points.update(saved[0])
        d._exception_mask = saved[1]
        d.state.update(saved[2])
        await d.rearm()
    if resume:
        await d.call('resume')
    return dict(d.snapshot(), reset='hard' if hard else 'soft')


from . import winuae_features  # noqa: E402,F401  (registers feature actions)
