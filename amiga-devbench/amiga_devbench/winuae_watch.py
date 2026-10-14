# SPDX-License-Identifier: MIT
"""WinUAE watchpoints, DMA watches, register conditions and PC history.

Watches use WinUAE's remote memwatch commands. Ranges, instruction fetches,
value matches and change-only writes need `watch range`; without it, plain
CPU read/write ranges fall back to standard GDB watchpoints.
"""
from __future__ import annotations

from typing import Any

from .winuae_debug import REARM, DebugError, WinuaeDebugger, action, boolean, integer

# memwatch source masks from WinUAE's debug.h, grouped as in its console.
SOURCES = {'cpu': 0x6, 'blitter': 0x1f8, 'copper': 0x200, 'disk': 0x400,
           'audio': 0x7800, 'bitplane': 0x7f8000, 'sprite': 0x7f800000}
CPU_FETCH = 0x1
ACCESS = {'read': 1, 'write': 2, 'access': 3, 'fetch': 4}
# Condition registers and operators in WinUAE's BREAKPOINT_REG/CMP order.
CONDITION_REGISTERS = [f'D{i}' for i in range(8)] + [f'A{i}' for i in range(8)] + ['PC', 'USP', 'MSP', 'ISP', 'VBR', 'SR']
CONDITION_OPERATORS = ['eq', 'ne', 'le', 'ge', 'lt', 'gt']


def access_bits(access: Any) -> int:
    """Accept read/write/access/fetch or FS-UAE style R, W, I letters."""
    if isinstance(access, str) and access in ACCESS:
        return ACCESS[access]
    if isinstance(access, str) and access and set(access.upper()) <= set('RWI'):
        return sum(bit for letter, bit in (('R', 1), ('W', 2), ('I', 4)) if letter in access.upper())
    raise DebugError('access must be read, write, access, fetch or a combination of R, W and I')


def source_mask(sources: Any, access: int) -> int:
    names = ['cpu'] if sources is None else sources
    if not isinstance(names, list) or not names or any(n not in SOURCES for n in names):
        raise DebugError(f'sources must be a non-empty list of {", ".join(SOURCES)}')
    mask = 0
    for name in names:
        mask |= SOURCES[name]
    if access & 4:
        if 'cpu' not in names:
            raise DebugError('Instruction fetches are CPU accesses; include the cpu source')
        mask |= CPU_FETCH
        if not access & 3:
            mask &= ~SOURCES['cpu']  # A fetch-only watch must not match data accesses.
    return mask


def _watches(d: WinuaeDebugger) -> dict[int, str]:
    return d.state.setdefault('watches', {})


async def _add_watch(d: WinuaeDebugger, command: str) -> int:
    reply = await d._monitor(command, 'watch')
    ident = integer(reply.get('id') if isinstance(reply, dict) else None, 'watch id', 255)
    _watches(d)[ident] = command
    return ident


@action('watch', capability='watch')
async def _watch(d: WinuaeDebugger, operation: str = 'list', id: Any = None, address: Any = None,
                 size: Any = 1, access: Any = 'write', sources: Any = None, value: Any = None,
                 mask: Any = None, change_only: Any = False, log_only: Any = False,
                 exact: Any = False) -> Any:
    """Watch memory. A range matches any overlapping access, like FS-UAE and
    the WinUAE console; exact matches one complete aligned 1, 2 or 4 byte
    access. Values are compared under mask; change_only fires only on writes
    that change RAM; log_only records into the access log without stopping."""
    if operation in ('last', 'events', 'clear-events'):
        return await d._monitor(f'watch {operation}')
    if operation == 'list':
        data = [dict(kind=('write', 'read', 'access')[k - 2], address=a, size=s, gdb=True)
                for (k, a, s) in sorted(d._points) if k]
        return dict(watches=await d._monitor('watch list'), gdb_watchpoints=data)
    if operation == 'remove':
        ident = integer(id, 'id', 255)
        if ident not in _watches(d):
            raise DebugError('This session does not own that watch')
        await d.monitor_text(f'watch remove {ident:x}')
        del _watches(d)[ident]
        return dict(removed=ident)
    if operation == 'clear':
        for ident in list(_watches(d)):
            await d.monitor_text(f'watch remove {ident:x}')
            del _watches(d)[ident]
        for key in [k for k in d._points if k[0]]:
            await d._ok(f'z{key[0]},{key[1]:x},{key[2]:x}')
            del d._points[key]
        return dict(watches=[], gdb_watchpoints=[])
    if operation != 'add':
        raise DebugError('Watch operation must be add, remove, clear, list, last, events or clear-events')
    start = integer(address, 'address')
    length = integer(size, 'size', 65536, 1)
    bits = access_bits(access)
    source = source_mask(sources, bits)
    change, log, exact = boolean(change_only, 'change_only'), boolean(log_only, 'log_only'), boolean(exact, 'exact')
    if change and bits != 2:
        raise DebugError('change_only requires write access to RAM')
    if mask is not None and value is None:
        raise DebugError('mask requires value')
    if value is not None and length not in (1, 2, 4):
        raise DebugError('WinUAE compares values at the watch width; value needs size 1, 2 or 4')
    limit = (1 << (8 * length)) - 1 if length < 4 else 0xffffffff
    number = integer(value, 'value', limit) if value is not None else 0
    value_mask = integer(mask, 'mask', limit) if mask is not None else limit
    args = f'{start:x} {length:x} {bits:x} {source:x} {int(value is not None)} {number:x} {value_mask:x} {int(change)} {int(log)}'
    if exact:
        if length not in (1, 2, 4) or start % length or bits & 4:
            raise DebugError('Exact watches need an aligned 1, 2 or 4 byte read, write or access')
        ident = await _add_watch(d, 'watch add ' + args)
    elif 'watch-range' in d.commands():
        ident = await _add_watch(d, 'watch range ' + args)
    elif bits & 4 or value is not None or change or log or source != SOURCES['cpu']:
        raise DebugError('Fetch, value, change-only, log-only and DMA source ranges need a WinUAE build with watch-range')
    else:
        # Older builds: a plain CPU data range is a standard GDB watchpoint.
        kind = {1: 3, 2: 2, 3: 4}[bits]
        if start + length > 0x100000000:
            raise DebugError('Memory range wraps the address space')
        from .winuae_debug import Point
        await d._ok(f'Z{kind},{start:x},{length:x}')
        d._points[(kind, start, length)] = Point(kind, start, length)
        return dict(gdb=True, kind=kind, address=start, size=length)
    return dict(id=ident)


@action('dma_watch', capability='dma-watch')
async def _dma_watch(d: WinuaeDebugger, operation: str = 'list', id: Any = None, address: Any = None,
                     length: Any = 1, mode: str = 'write', sources: Any = None) -> Any:
    """Watch a range for accesses by DMA sources only."""
    owned = d.state.setdefault('dma_watches', {})
    if operation == 'list':
        return await d._monitor('dma-watch list')
    if operation == 'remove':
        ident = integer(id, 'id', 255)
        if ident not in owned:
            raise DebugError('This session does not own that DMA watch')
        await d.monitor_text(f'dma-watch remove {ident:x}')
        del owned[ident]
        return await d._monitor('dma-watch list')
    if operation != 'add':
        raise DebugError('DMA watch operation must be add, remove or list')
    start = integer(address, 'address')
    size = integer(length, 'length', 65536, 1)
    bits = ACCESS.get(mode)
    names = sources or [n for n in SOURCES if n != 'cpu']
    if bits is None or bits == 4:
        raise DebugError('mode must be read, write or access')
    if not isinstance(names, list) or not names or any(n not in SOURCES or n == 'cpu' for n in names):
        raise DebugError('sources must be DMA groups: ' + ', '.join(n for n in SOURCES if n != 'cpu'))
    source = 0
    for name in names:
        source |= SOURCES[name]
    command = f'dma-watch add {start:x} {size:x} {bits:x} {source:x}'
    ident = integer((await d.monitor_text(command) or '').strip(), 'DMA watch id', 255)
    owned[ident] = command
    return dict(id=ident)


@action('condition', capability='condition')
async def _condition(d: WinuaeDebugger, operation: str = 'list', id: Any = None, register: Any = None,
                     operator: Any = 'eq', value: Any = 0, mask: Any = 0xffffffff, signed: Any = False) -> Any:
    """Stop when a register comparison holds, checked at every instruction."""
    owned = d.state.setdefault('conditions', {})
    if operation == 'list':
        return await d._monitor('condition list')
    if operation == 'remove':
        ident = integer(id, 'id', 255)
        if ident not in owned:
            raise DebugError('This session does not own that condition')
        await d.monitor_text(f'condition remove {ident:x}')
        del owned[ident]
        return dict(removed=ident)
    if operation != 'add':
        raise DebugError('Condition operation must be add, remove or list')
    name = str(register).upper()
    if name not in CONDITION_REGISTERS or name == 'PC':
        raise DebugError('Condition register must be D0-D7, A0-A7, USP, MSP, ISP, VBR or SR; use breakpoints for PC')
    if operator not in CONDITION_OPERATORS:
        raise DebugError('operator must be one of ' + ', '.join(CONDITION_OPERATORS))
    command = 'condition add ' + ' '.join(f'{n:x}' for n in (
        CONDITION_REGISTERS.index(name), CONDITION_OPERATORS.index(operator),
        integer(value, 'value'), integer(mask, 'mask'), int(boolean(signed, 'signed'))))
    reply = await d._monitor(command)
    ident = integer(reply.get('id') if isinstance(reply, dict) else None, 'condition id', 255)
    owned[ident] = command
    return dict(id=ident)


@action('history', capability='history')
async def _history(d: WinuaeDebugger, operation: str = 'read', count: Any = 32) -> Any:
    """Read the debugger's PC history, or turn recording on or off."""
    if operation not in ('read', 'on', 'off'):
        raise DebugError('History operation must be read, on or off')
    if operation != 'read':
        await d.monitor_text(f'history {operation}')
    return await d._monitor(f'history read {integer(count, "count", 128, 1):x}')


async def _rearm(d: WinuaeDebugger) -> None:
    """Restored state has no remote watches; reinstall this session's."""
    for key in ('watches', 'dma_watches', 'conditions'):
        owned = d.state.get(key, {})
        d.state[key] = {}
        for command in owned.values():
            if key == 'dma_watches':
                d.state[key][integer((await d.monitor_text(command) or '').strip(), 'DMA watch id', 255)] = command
            else:
                reply = await d._monitor(command)
                d.state[key][integer(reply['id'], 'id', 255)] = command


REARM.append(_rearm)
