# SPDX-License-Identifier: MIT
"""DWARF source lines, variables and backtraces for WinUAE guests.

winuae_dwarf_helper.py parses a linked m68k ELF file with pyelftools in an
isolated subprocess. It never connects to the target or runs guest code;
its memory reads go through this session's connection, within fixed time
and read budgets.
"""
from __future__ import annotations

import asyncio
import base64
import json
import sys
from pathlib import Path
from typing import Any

from .winuae_debug import REGISTERS, DebugError, WinuaeDebugger, action, integer
from .winuae_diagnostics import matching_fault
from .winuae_files import parse_symbols, read_file_async, section_base

HELPER = Path(__file__).with_name('winuae_dwarf_helper.py')
TIME_LIMIT, READ_LIMIT, BYTE_LIMIT, OUTPUT_LIMIT = 15, 512, 65536, 1024 * 1024


async def dwarf_tool(d: WinuaeDebugger, kind: str, args: dict) -> dict:
    data = await read_file_async(args.get('file'))
    parsed = parse_symbols(data)
    if parsed['format'] != 'elf32-m68k' or int.from_bytes(data[16:18], 'big') != 2:
        raise DebugError('DWARF tools need a linked ELF32 m68k executable')
    mappings = args.get('mappings')
    if not isinstance(mappings, list) or not 1 <= len(mappings) <= 256:
        raise DebugError('Supply explicit runtime section mappings')
    expression = args.get('expression')
    if kind == 'variable' and (not isinstance(expression, str) or len(expression) > 256):
        raise DebugError('Supply a variable expression of at most 256 characters')
    max_frames = integer(args.get('max_frames', 32), 'max_frames', 64, 1)
    address = None if args.get('address') is None else integer(args['address'], 'address')
    context = args.get('context', 'current')
    if context not in ('current', 'fault'):
        raise DebugError('context must be current or fault')
    process = None if args.get('process') is None else integer(args['process'], 'process', minimum=4)
    if process is not None and process % 4:
        raise DebugError('The process address must be aligned')
    if d.capabilities.get('mmu_model'):
        raise DebugError('DWARF inspection needs an MMU-disabled target; remote memory reads are physical')
    registers = await d.registers()
    if context == 'fault':
        last = matching_fault(d.last_stop, await d._monitor('exception', 'exception'))
        if not last:
            raise DebugError('No exception snapshot matches the current stop')
        registers = dict(zip(REGISTERS, last['registers']), PC=last['instruction_pc'])
    report = await d._monitor('segments' + (f' {process:x}' if process else ''), 'segments') if any(
        isinstance(m, dict) and m.get('address') is None for m in mappings) else None
    segments = report.get('segments', []) if isinstance(report, dict) else []
    runtime = []
    for mapping in mappings:
        if not isinstance(mapping, dict):
            raise DebugError('Invalid section mapping')
        found = [s for s in parsed['sections'] if mapping.get('section') in (s['index'], s['name'])]
        if len(found) != 1:
            raise DebugError('A mapping must name one allocated ELF section')
        section = found[0]
        base, _, offset = await section_base(d, parsed, section, [mapping], process, segments)
        current = dict(section=section['index'], linked=section['address'], address=base + offset, size=section['size'])
        for other in runtime:
            if (other['section'] == current['section']
                    or (other['address'] < current['address'] + current['size'] and current['address'] < other['address'] + other['size'])
                    or (other['linked'] < current['linked'] + current['size'] and current['linked'] < other['linked'] + other['size'])):
                raise DebugError('Ambiguous or overlapping section mappings')
        runtime.append(current)
    request = dict(action=kind, elf=base64.b64encode(data).decode(), mappings=runtime, registers=registers,
                   expression=expression, max_frames=max_frames)
    if address is not None:
        request['address'] = address  # Otherwise the helper uses the PC.
    result = await run_helper(d, request)
    return dict(context=context, **result)


async def run_helper(d: WinuaeDebugger, request: dict) -> dict:
    try:
        process = await asyncio.create_subprocess_exec(
            # -E ignores PYTHON* variables but, unlike -I, keeps user-site
            # packages, where pip --user installs pyelftools.
            sys.executable, '-E', str(HELPER), stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE, limit=OUTPUT_LIMIT)
    except (OSError, NotImplementedError) as exc:  # Windows selector loops lack subprocesses.
        raise DebugError(f'Cannot start the DWARF helper: {exc!r}') from None
    reads = read_bytes = output = 0
    try:
        async def converse() -> dict:
            nonlocal reads, read_bytes, output
            process.stdin.write((json.dumps(request) + '\n').encode())
            await process.stdin.drain()
            while True:
                line = await process.stdout.readline()
                output += len(line)
                if not line:
                    error = (await process.stderr.read())[-4096:].decode('utf-8', 'replace')
                    raise DebugError(f'DWARF helper failed: {error.strip() or "no result"}')
                if output > OUTPUT_LIMIT:
                    raise DebugError('DWARF output budget exceeded')
                message = json.loads(line)
                if message.get('error'):
                    raise DebugError(str(message['error']))
                if 'read' in message:
                    start = integer(message['read'].get('address'), 'address')
                    size = integer(message['read'].get('length'), 'length', 4096, 1)
                    reads += 1
                    read_bytes += size
                    if reads > READ_LIMIT or read_bytes > BYTE_LIMIT:
                        raise DebugError('DWARF memory budget exceeded')
                    hex_data = (await d.read_bytes(start, size)).hex()
                    process.stdin.write((json.dumps({'hex': hex_data}) + '\n').encode())
                    await process.stdin.drain()
                elif isinstance(message.get('result'), dict):
                    return message['result']
                else:
                    raise DebugError('Malformed DWARF helper response')
        return await asyncio.wait_for(converse(), TIME_LIMIT)
    except asyncio.TimeoutError:
        raise DebugError(f'DWARF analysis exceeded {TIME_LIMIT} seconds') from None
    except (ValueError, BrokenPipeError, ConnectionResetError) as exc:
        raise DebugError(f'DWARF helper failed: {exc}') from None
    finally:
        if process.returncode is None:
            process.kill()
        await process.wait()


@action('source')
async def _source(d: WinuaeDebugger, **args: Any) -> dict:
    """Source file, line and function for the PC or an address (DWARF)."""
    return await dwarf_tool(d, 'source', args)


@action('variable')
async def _variable(d: WinuaeDebugger, **args: Any) -> dict:
    """Read a C variable expression with .member, ->member and [index]."""
    return await dwarf_tool(d, 'variable', args)


@action('backtrace')
async def _backtrace(d: WinuaeDebugger, **args: Any) -> dict:
    """Unwind up to 64 frames from DWARF call-frame information."""
    return await dwarf_tool(d, 'backtrace', args)

