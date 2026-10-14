# SPDX-License-Identifier: MIT
"""Bounded memory search, planar bitmap decoding, register and memory
captures, postmortem reports and raw binary loading for WinUAE."""
from __future__ import annotations

import base64
import re
import struct
import time
import zlib
from pathlib import Path
from typing import Any

from .winuae_debug import REGISTERS, DebugError, WinuaeDebugger, action, integer
from .winuae_files import read_file_async

MAX_CAPTURE = 256 * 1024


def memory_range(address: Any, length: Any, maximum: int = MAX_CAPTURE) -> tuple[int, int]:
    start, size = integer(address, 'address'), integer(length, 'length', maximum, 1)
    if start + size > 0x100000000:
        raise DebugError('Address range wraps the 32-bit address space')
    return start, size


def hex_bytes(value: Any, maximum: int = 4096) -> bytes:
    if not isinstance(value, str) or not re.fullmatch(r'(?:[0-9a-fA-F]{2})+', value) or len(value) > maximum * 2:
        raise DebugError(f'Expected 1..{maximum} bytes as an even-length hexadecimal string')
    return bytes.fromhex(value)


@action('memory_search')
async def _memory_search(d: WinuaeDebugger, address: Any = None, length: Any = None, hex: Any = None,
                         max_matches: Any = 256, alignment: Any = 1) -> dict:
    """Find exact bytes in a range of up to 16 MiB, including overlapping
    matches and matches across read boundaries."""
    start, size = memory_range(address, length, 16 * 1024 * 1024)
    pattern = hex_bytes(hex)
    if len(pattern) > size:
        raise DebugError('The pattern is longer than the search range')
    limit = integer(max_matches, 'max_matches', 4096, 1)
    align = integer(alignment, 'alignment', 4096, 1)
    matches: list[int] = []
    carry = b''
    for offset in range(0, size, 65536):
        chunk = await d.read_bytes(start + offset, min(65536, size - offset))
        data = carry + chunk
        base = start + offset - len(carry)
        at = data.find(pattern)
        while at >= 0:
            found = base + at
            if found % align == 0:
                matches.append(found)
                if len(matches) == limit:
                    return dict(address=start, length=size, matches=matches, limit_reached=True,
                                next_address=found + 1, remaining=start + size - found - 1)
            at = data.find(pattern, at + 1)
        carry = data[max(0, len(data) - len(pattern) + 1):]
    return dict(address=start, length=size, matches=matches, limit_reached=False, next_address=None, remaining=0)


def planar_png(width: int, height: int, planes: list[bytes], stride: int, palette: list[int]) -> bytes:
    """Encode indexed planar pixels (least significant plane first) as RGB PNG."""
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            color = 0
            for p, plane in enumerate(planes):
                color |= ((plane[y * stride + (x >> 3)] >> (7 - (x & 7))) & 1) << p
            rows += palette[color].to_bytes(3, 'big')

    def chunk(name: bytes, body: bytes) -> bytes:
        return struct.pack('>I', len(body)) + name + body + struct.pack('>I', zlib.crc32(name + body))
    header = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) + chunk(b'IDAT', zlib.compress(bytes(rows))) + chunk(b'IEND', b'')


@action('bitmap')
async def _bitmap(d: WinuaeDebugger, width: Any = None, height: Any = None, planes: Any = None,
                  palette: Any = None, row_stride: Any = None) -> dict:
    """Decode 1..8 bitplanes from explicit addresses to a PNG image.
    HAM, EHB and display-mode inference are intentionally not supported."""
    w, h = integer(width, 'width', 2048, 1), integer(height, 'height', 2048, 1)
    if w * h > 262144:
        raise DebugError('The bitmap exceeds 262144 pixels')
    row_bytes = (w + 7) // 8
    stride = integer(row_stride if row_stride is not None else (w + 15) // 16 * 2, 'row_stride', 65536, row_bytes)
    if not isinstance(planes, list) or not 1 <= len(planes) <= 8:
        raise DebugError('Supply 1..8 plane addresses, least significant plane first')
    if not isinstance(palette, list) or len(palette) != 2 ** len(planes):
        raise DebugError('Supply exactly 2^planes RGB palette values such as 0xff8800')
    colors = [integer(c, 'palette color', 0xffffff) for c in palette]
    length = (h - 1) * stride + row_bytes
    if length * len(planes) > 2 * 1024 * 1024:
        raise DebugError('The bitmap reads exceed 2 MiB')
    data = [await d.read_bytes(*memory_range(p, length, 2 * 1024 * 1024)) for p in planes]
    png = planar_png(w, h, data, stride, colors)
    return dict(width=w, height=h, png_base64=base64.b64encode(png).decode())


async def capture(d: WinuaeDebugger, ranges: Any = None) -> dict:
    ranges = ranges or []
    if not isinstance(ranges, list) or len(ranges) > 16:
        raise DebugError('Supply at most 16 memory ranges')
    spans = [memory_range(r.get('address') if isinstance(r, dict) else None,
                          r.get('length') if isinstance(r, dict) else None) for r in ranges]
    if sum(size for _, size in spans) > MAX_CAPTURE:
        raise DebugError('The capture exceeds 256 KiB')
    memory = [dict(address=a, length=n, hex=(await d.read_bytes(a, n)).hex()) for a, n in spans]
    return dict(format='winuae-snapshot-v1', captured_at=time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
                stop_reply=d.last_stop, registers=await d.registers(), memory=memory)


@action('capture')
async def _capture(d: WinuaeDebugger, ranges: Any = None) -> dict:
    """Registers, the stop reason and up to 256 KiB of memory ranges."""
    return await capture(d, ranges)


def matching_fault(stop: str | None, exception: Any) -> dict | None:
    """The exception snapshot, if it belongs to the current stop."""
    vector = re.search(r'(?:^|;)winuae-exception:([0-9a-fA-F]+);', (stop or '')[3:])
    pc = re.search(r'(?:^|;)winuae-faultpc:([0-9a-fA-F]+);', (stop or '')[3:])
    last = exception.get('last') if isinstance(exception, dict) else None
    if (vector and pc and isinstance(last, dict) and last.get('vector') == int(vector[1], 16)
            and last.get('instruction_pc') == int(pc[1], 16) and isinstance(last.get('registers'), list)
            and len(last['registers']) == 18 and all(type(n) is int for n in last['registers'])):
        return last
    return None


async def postmortem(d: WinuaeDebugger) -> dict:
    snapshot = await capture(d)
    errors: dict[str, str] = {}

    async def optional(name: str, coroutine):
        try:
            return await coroutine
        except DebugError as exc:
            errors[name] = str(exc)
            return None
    exception = await optional('exception', d._monitor('exception', 'exception'))
    last = matching_fault(snapshot['stop_reply'], exception)
    fault_regs = dict(zip(REGISTERS, last['registers'])) if last else None
    context = fault_regs or snapshot['registers']
    pc = last['instruction_pc'] if last else context['PC']
    # The exception snapshot precedes the stack frame; current registers
    # already describe the handler entry. Both contexts are kept.
    stack_base = context['A7']
    return dict(
        format='winuae-postmortem-v1', snapshot=snapshot, diagnosis_context='fault' if last else 'current',
        fault=dict(last, registers=fault_regs) if last else None, last_exception=exception,
        disassembly=await optional('disassembly', d.monitor_text(f'disasm {pc:x} 12', 'disasm')),
        stack=await optional('stack', _stack_bytes(d, stack_base)),
        segments=await optional('segments', d._monitor('segments', 'segments')),
        guest_output=await optional('guest_output', d._monitor('guest-output read', 'guest-output')),
        errors=errors)


async def _stack_bytes(d: WinuaeDebugger, base: int) -> dict:
    return dict(address=base, length=256, hex=(await d.read_bytes(base, min(256, 0x100000000 - base))).hex())


@action('postmortem')
async def _postmortem(d: WinuaeDebugger, file: Any = None, mappings: Any = None, max_frames: Any = 32) -> dict:
    """A bounded crash report: fault and current registers, disassembly,
    stack bytes, segments and guest output. With a linked ELF file and
    mappings, it also contains a DWARF backtrace."""
    report = await postmortem(d)
    if file is not None:
        from .winuae_dwarf import dwarf_tool
        try:
            report['backtrace'] = await dwarf_tool(d, 'backtrace', dict(
                file=file, mappings=mappings, max_frames=max_frames, context=report['diagnosis_context']))
        except DebugError as exc:
            report['errors']['backtrace'] = str(exc)
            report['backtrace'] = None
    return report


@action('load')
async def _load(d: WinuaeDebugger, file: Any = None, address: Any = None) -> dict:
    """Copy a raw binary into RAM and verify it. Hunk executables are
    rejected; use hunk_load, which relocates them."""
    path = Path(str(file or '')).expanduser()
    data = await read_file_async(path)
    if data[:4] == b'\0\0\x03\xf3':
        raise DebugError('Hunk executables need hunk_load with explicit RAM placements')
    start = integer(address, 'address')
    if not data or start + len(data) > 0x100000000:
        raise DebugError('The file is empty or does not fit the address space')
    await d.write_bytes(start, data)
    if await d.read_bytes(start, len(data)) != data:
        raise DebugError('Verification failed after writing the file')
    return dict(path=str(path), address=start, size=len(data), verified=True)
