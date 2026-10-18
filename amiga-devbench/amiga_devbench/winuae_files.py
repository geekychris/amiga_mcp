# SPDX-License-Identifier: MIT
"""Hunk executables and Hunk/ELF symbol tables for WinUAE debugging.

Hunk format: AmigaDOS executable file format (ROM Kernel Reference Manual:
DOS, chapter 11), without overlays, external references or relative
relocations. ELF: the generic ELF ABI for 32-bit big-endian m68k files.
Nothing here relocates ELF sections implicitly or infers C types.
"""
from __future__ import annotations

import asyncio
import hashlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .winuae_debug import DebugError, WinuaeDebugger, action, integer

MAX_FILE = 16 * 1024 * 1024
HUNK_HEADER, HUNK_END = 1011, 1010


def read_file(value: Any, maximum: int = MAX_FILE) -> bytes:
    path = Path(str(value or '')).expanduser()
    if not value or not path.is_file() or path.stat().st_size > maximum:
        raise DebugError(f'Supply a regular file of at most {maximum} bytes')
    return path.read_bytes()


async def read_file_async(value: Any, maximum: int = MAX_FILE) -> bytes:
    """read_file in a worker thread, so large files do not block the loop."""
    return await asyncio.to_thread(read_file, value, maximum)


@dataclass
class Hunk:
    index: int
    size: int
    kind: str = 'bss'
    memory: str = 'any'
    data: bytes = b''
    relocations: list[tuple[int, int]] = field(default_factory=list)
    symbols: list[tuple[str, int]] = field(default_factory=list)


def parse_hunk(data: bytes) -> list[Hunk]:
    cursor = 0

    def take(n: int) -> bytes:
        nonlocal cursor
        if n < 0 or n > len(data) - cursor:
            raise DebugError(f'Truncated hunk file at offset {cursor}')
        cursor += n
        return data[cursor - n:cursor]
    u32 = lambda: int.from_bytes(take(4), 'big')
    u16 = lambda: int.from_bytes(take(2), 'big')
    if u32() != HUNK_HEADER:
        raise DebugError('Expected a HUNK_HEADER executable')
    if u32() != 0:
        raise DebugError('Resident library dependencies are unsupported')
    count, first, last = u32(), u32(), u32()
    if not count or count > 256 or first != 0 or last != count - 1:
        raise DebugError('Unsupported hunk table or overlay')
    hunks, total, symbol_count, relocation_count = [], 0, 0, 0
    for i in range(count):
        descriptor = u32()
        flags = descriptor >> 30
        if flags == 3:
            raise DebugError('Extended memory attributes are unsupported')
        size = (descriptor & 0x3fffffff) * 4
        total += size
        if total > 8 * 1024 * 1024:
            raise DebugError('Hunk allocations exceed 8 MiB')
        hunks.append(Hunk(i, size, memory={1: 'chip', 2: 'fast'}.get(flags, 'any')))
    for hunk in hunks:
        payload, offsets = False, set()
        while True:
            tag = u32() & 0x3fffffff
            if tag in (1000, 1009):  # HUNK_NAME, HUNK_DEBUG
                take(u32() * 4)
            elif tag in (1001, 1002, 1003):
                if payload:
                    raise DebugError('Duplicate hunk payload')
                size = u32() * 4
                if size > hunk.size:
                    raise DebugError('Payload exceeds the reserved hunk size')
                hunk.kind = {1001: 'code', 1002: 'data', 1003: 'bss'}[tag]
                hunk.data = b'' if tag == 1003 else take(size)
                payload = True
            elif tag in (1004, 1015, 1020):  # RELOC32, DREL32 (historical RELOC32SHORT), RELOC32SHORT
                if not payload:
                    raise DebugError('Relocations precede the hunk payload')
                number = u32 if tag == 1004 else u16
                while True:
                    n = number()
                    if not n:
                        break
                    relocation_count += n
                    if relocation_count > 262144:
                        raise DebugError('Too many relocations')
                    target = number()
                    if target >= count:
                        raise DebugError('Invalid relocation target')
                    for _ in range(n):
                        offset = number()
                        if offset % 2 or offset + 4 > hunk.size or {offset, offset - 2, offset + 2} & offsets:
                            raise DebugError('Invalid or overlapping relocation offset')
                        offsets.add(offset)
                        hunk.relocations.append((offset, target))
                if tag != 1004 and cursor % 4:
                    take(2)
            elif tag == 1008:  # HUNK_SYMBOL
                if not payload:
                    raise DebugError('Symbols precede the hunk payload')
                while True:
                    words = u32()
                    if not words:
                        break
                    symbol_count += 1
                    if symbol_count > 65536 or words > 256:
                        raise DebugError('Symbol budget exceeded')
                    name = take(words * 4).split(b'\0', 1)[0].decode('latin-1')
                    offset = u32()
                    if not name or offset > hunk.size:
                        raise DebugError('Invalid hunk symbol')
                    hunk.symbols.append((name, offset))
            elif tag == HUNK_END and payload:
                break
            else:
                raise DebugError(f'Unsupported hunk record {tag}')
    if cursor != len(data):
        raise DebugError('Trailing data or an unsupported overlay')
    return hunks


def relocate(hunks: list[Hunk], placements: Any) -> tuple[list[tuple[int, int]], list[bytearray]]:
    if not isinstance(placements, list) or len(placements) != len(hunks):
        raise DebugError('Supply one placement per hunk')
    regions = []
    for hunk, p in zip(hunks, placements):
        if not isinstance(p, dict):
            raise DebugError('Each placement is an object with address, capacity and memory')
        address = integer(p.get('address'), 'placement address')
        capacity = integer(p.get('capacity'), 'placement capacity', 8 * 1024 * 1024)
        if address % 4 or capacity < hunk.size or address + capacity > 0x100000000:
            raise DebugError('Invalid placement capacity or alignment')
        if p.get('memory') not in ('chip', 'fast'):
            raise DebugError('Placement memory must be chip or fast')
        if hunk.memory != 'any' and p['memory'] != hunk.memory:
            raise DebugError(f'Hunk {hunk.index} requires {hunk.memory} memory')
        regions.append((address, capacity))
    for i, (a, n) in enumerate(regions):
        for b, m in regions[:i]:
            if n and m and a < b + m and b < a + n:
                raise DebugError('Hunk placements overlap')
    images = []
    for hunk in hunks:
        image = bytearray(hunk.size)
        image[:len(hunk.data)] = hunk.data
        images.append(image)
    for hunk, image in zip(hunks, images):
        for offset, target in hunk.relocations:
            value = int.from_bytes(image[offset:offset + 4], 'big') + regions[target][0]
            if value > 0xffffffff:
                raise DebugError('A relocation overflows the guest address space')
            image[offset:offset + 4] = value.to_bytes(4, 'big')
    return regions, images


@action('hunk_inspect', locked=False, stopped=False)
async def _hunk_inspect(d: WinuaeDebugger, file: Any = None) -> dict:
    """Validate a Hunk executable and list its hunks and first 1024 symbols."""
    hunks = parse_hunk(await read_file_async(file))
    symbols = [dict(name=n, offset=o, hunk=h.index) for h in hunks for n, o in h.symbols]
    return dict(hunks=[dict(index=h.index, size=h.size, kind=h.kind, memory=h.memory, payload_size=len(h.data),
                            relocations=len(h.relocations), symbols=len(h.symbols)) for h in hunks],
                symbols=symbols[:1024], symbols_truncated=len(symbols) > 1024)


@action('hunk_load', capability='memory-check')
async def _hunk_load(d: WinuaeDebugger, file: Any = None, placements: Any = None) -> list:
    """Relocate a Hunk executable into RAM the caller has reserved. This is
    a debug loader: it neither allocates memory nor creates a process."""
    hunks = parse_hunk(await read_file_async(file))
    regions, images = relocate(hunks, placements)
    for i, image in enumerate(images):
        if image:
            check = await d._monitor(f'memory-check {regions[i][0]:x} {len(image):x}')
            if not isinstance(check, dict) or check.get('writable_ram') is not True or check.get('memory') != placements[i]['memory']:
                raise DebugError(f'Hunk {i} placement is not {placements[i]["memory"]} RAM')
    backups = [await d.read_bytes(regions[i][0], len(image)) if image else b'' for i, image in enumerate(images)]
    attempted = -1
    try:
        for i, image in enumerate(images):
            attempted = i
            if image:
                await d.write_bytes(regions[i][0], bytes(image), check=False)
                if await d.read_bytes(regions[i][0], len(image)) != image:
                    raise DebugError(f'Hunk {i} verification failed')
    except DebugError as exc:
        failed = []
        for i in range(attempted, -1, -1):
            if backups[i]:
                try:
                    await d.write_bytes(regions[i][0], backups[i], check=False)
                    if await d.read_bytes(regions[i][0], len(backups[i])) != backups[i]:
                        failed.append(i)
                except DebugError:
                    failed.append(i)
        note = f'rollback failed for hunks {failed}; memory may be partly modified' if failed else 'original bytes restored'
        raise DebugError(f'{exc}; {note}') from None
    return [dict(index=h.index, address=regions[h.index][0], size=h.size, kind=h.kind, memory=h.memory) for h in hunks]


def parse_symbols(data: bytes) -> dict:
    """Sections and symbols of a Hunk file or an ELF32 big-endian m68k file;
    ELF symbol values become offsets into their sections."""
    if len(data) >= 4 and int.from_bytes(data[:4], 'big') == HUNK_HEADER:
        hunks = parse_hunk(data)
        return dict(format='hunk', sections=[dict(index=h.index, name=f'{h.kind}:{h.index}', size=h.size, address=0) for h in hunks],
                    symbols=[dict(name=n, section=h.index, offset=o, size=None) for h in hunks for n, o in h.symbols])

    def at(offset: int, size: int) -> bytes:
        if offset < 0 or size < 0 or offset + size > len(data):
            raise DebugError('An ELF record extends beyond the file')
        return data[offset:offset + size]
    u16 = lambda b, o: int.from_bytes(b[o:o + 2], 'big')
    u32 = lambda b, o: int.from_bytes(b[o:o + 4], 'big')
    header = at(0, 52)
    elf_type = u16(header, 16)
    if (header[:4] != b'\x7fELF' or header[4:7] != b'\x01\x02\x01' or u16(header, 18) != 4
            or u32(header, 20) != 1 or elf_type not in (1, 2) or u16(header, 40) != 52):
        raise DebugError('Expected an ELF32 big-endian m68k relocatable or executable file')
    table, entry_size, count, names_index = u32(header, 32), u16(header, 46), u16(header, 48), u16(header, 50)
    if entry_size != 40 or not 1 <= count <= 4096 or not 0 < names_index < count:
        raise DebugError('Unsupported ELF section table')
    raw = []
    for i in range(count):
        h = at(table + i * 40, 40)
        section = dict(index=i, name_offset=u32(h, 0), type=u32(h, 4), flags=u32(h, 8), address=u32(h, 12),
                       offset=u32(h, 16), size=u32(h, 20), link=u32(h, 24), entry_size=u32(h, 36))
        if section['type'] != 8:
            at(section['offset'], section['size'])
        if section['flags'] & 0x800:
            raise DebugError('Compressed ELF sections are unsupported')
        raw.append(section)

    def string(index: int, offset: int) -> str:
        section = raw[index] if index < len(raw) else None
        if not section or section['type'] != 3 or offset >= section['size']:
            raise DebugError('Invalid ELF string reference')
        body = at(section['offset'], section['size'])
        end = body.find(b'\0', offset)
        if end < 0 or end - offset > 4096:
            raise DebugError('Unterminated or oversized ELF string')
        return body[offset:end].decode('utf-8', 'replace')
    sections = [dict(index=s['index'], name=string(names_index, s['name_offset']), size=s['size'], address=s['address'])
                for s in raw if s['flags'] & 2]
    by_index = {s['index']: s for s in sections}
    symbols, seen, entries = [], set(), 0
    for symtab in (s for s in raw if s['type'] in (2, 11)):
        if symtab['entry_size'] != 16 or symtab['size'] % 16:
            raise DebugError('Invalid ELF symbol table')
        entries += symtab['size'] // 16
        if entries > 65536:
            raise DebugError('ELF symbol budget exceeded')
        for offset in range(0, symtab['size'], 16):
            record = at(symtab['offset'] + offset, 16)
            section, kind = by_index.get(u16(record, 14)), record[12] & 15
            if not section or kind not in (0, 1, 2):
                continue
            name = string(symtab['link'], u32(record, 0))
            if not name:
                continue
            value, size = u32(record, 4), u32(record, 8)
            relative = value if elf_type == 1 else value - section['address']
            # Zero-sized NOTYPE linker markers may sit in trailing padding.
            if relative < 0 or (relative + size > section['size'] and (kind != 0 or size)):
                raise DebugError(f'Symbol {name} exceeds its section')
            key = (name, section['index'], relative, size)
            if key not in seen:
                seen.add(key)
                symbols.append(dict(name=name, section=section['index'], offset=relative, size=size or None))
    return dict(format='elf32-m68k', sections=sections, symbols=symbols)


@action('symbols', locked=False, stopped=False)
async def _symbols(d: WinuaeDebugger, file: Any = None, prefix: Any = '', limit: Any = 256) -> dict:
    """List Hunk or ELF symbols and sections of a host file."""
    data = await read_file_async(file)
    parsed = parse_symbols(data)
    if not isinstance(prefix, str) or len(prefix) > 1024:
        raise DebugError('Invalid symbol prefix')
    count = integer(limit, 'limit', 1024, 1)
    matching = [s for s in parsed['symbols'] if s['name'].startswith(prefix)]
    return dict(format=parsed['format'], sha256=hashlib.sha256(data).hexdigest(), sections=parsed['sections'],
                symbols=matching[:count], total=len(matching), truncated=len(matching) > count)


async def section_base(d: WinuaeDebugger, parsed: dict, section: dict, mappings: Any, process: int | None,
                       segments: list | None = None) -> tuple[int, int, int]:
    """Runtime base, capacity and in-segment offset of a section. Hunk
    indices map to loaded segments; ELF needs an explicit mapping."""
    if mappings is not None and (not isinstance(mappings, list) or len(mappings) > 256):
        raise DebugError('Expected at most 256 section mappings')
    matching = [m for m in (mappings or []) if isinstance(m, dict) and m.get('section') in (section['index'], section['name'])]
    if len(matching) > 1 or (not matching and parsed['format'] != 'hunk'):
        raise DebugError('ELF needs exactly one explicit mapping for this section')
    mapping = matching[0] if matching else dict(segment=section['index'])
    offset = integer(mapping.get('offset', 0), 'mapping offset')
    if mapping.get('address') is not None:
        if mapping.get('segment') is not None:
            raise DebugError('A mapping takes an address or a segment, not both')
        base, capacity = integer(mapping['address'], 'mapping address'), integer(mapping.get('size'), 'mapping size')
    else:
        index = integer(mapping.get('segment'), 'segment', 255)
        if segments is None:
            report = await d._monitor('segments' + (f' {process:x}' if process else ''), 'segments')
            segments = report.get('segments', []) if isinstance(report, dict) else []
        found = [s for s in segments if s.get('index') == index]
        if len(found) != 1:
            raise DebugError('Loaded segment not found')
        base, capacity = integer(found[0]['address'], 'segment address'), integer(found[0]['size'], 'segment size')
    if base + capacity > 0x100000000 or offset + section['size'] > capacity:
        raise DebugError('The section does not fit its mapped segment')
    return base, capacity, offset


FORMATS = {'u8': 1, 's8': 1, 'u16': 2, 's16': 2, 'u32': 4, 's32': 4}


@action('symbol_read')
async def _symbol_read(d: WinuaeDebugger, file: Any = None, symbol: Any = None, section: Any = None,
                       process: Any = None, offset: Any = 0, format: Any = 'bytes', length: Any = None,
                       count: Any = 1, mappings: Any = None) -> dict:
    """Read an exact symbol from the running program, located through loaded
    segments (Hunk) or explicit section mappings (ELF). The format is the
    caller's choice; no C types are inferred."""
    parsed = parse_symbols(await read_file_async(file))
    if not isinstance(symbol, str) or not symbol or len(symbol) > 1024:
        raise DebugError('Expected an exact symbol name')
    wanted = None if section is None else integer(section, 'section', 4095)
    candidates = [s for s in parsed['symbols'] if s['name'] == symbol and (wanted is None or s['section'] == wanted)]
    if len(candidates) != 1:
        raise DebugError('Ambiguous symbol; supply its section index' if candidates else 'Symbol not found')
    sym = candidates[0]
    sec = next(s for s in parsed['sections'] if s['index'] == sym['section'])
    if format != 'bytes' and format not in FORMATS:
        raise DebugError('format must be bytes, u8, s8, u16, s16, u32 or s32')
    element = 1 if format == 'bytes' else FORMATS[format]
    size = integer(length, 'length', 4096, 1) if format == 'bytes' else element * integer(count, 'count', 256, 1)
    start = integer(offset, 'offset')
    if sym['size'] is not None and start + size > sym['size']:
        raise DebugError('The read exceeds the symbol size')
    if sym['offset'] + start + size > sec['size']:
        raise DebugError('The read exceeds the symbol section')
    proc = None if process is None else integer(process, 'process', minimum=4)
    if proc is not None and proc % 4:
        raise DebugError('The process address must be longword aligned')
    base, _, within = await section_base(d, parsed, sec, mappings, proc)
    address = base + within + sym['offset'] + start
    if address + size > 0x100000000:
        raise DebugError('Address range wraps the 32-bit address space')
    data = await d.read_bytes(address, size)
    result = dict(symbol=sym, section=sec, address=address, length=size, format=format, hex=data.hex(),
                  type_source='explicit caller format; no DWARF inference')
    if format != 'bytes':
        result['values'] = [int.from_bytes(data[i:i + element], 'big', signed=format.startswith('s'))
                            for i in range(0, size, element)]
    return result
