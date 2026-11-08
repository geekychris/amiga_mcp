# SPDX-License-Identifier: MIT
"""The program being debugged: its Hunk executable, the DWARF that the
m68k-amigaos toolchain stores in it, and where AmigaDOS loaded it.

The toolchain's linker (bebbo's binutils, amigaos.c) packs the DWARF
sections into one HUNK_DEBUG: for each section in the fixed order of
DWARF_SECTIONS, a big-endian length followed by the section, padded to a
longword. Empty sections keep their zero length.

DWARF addresses use the hunks laid out back to back in hunk order: hunk N
starts at the sum of the sizes of hunks 0..N-1. Code addresses are therefore
offsets into the code hunk, and a variable in the data hunk sits at the code
size plus its offset. program_elf() turns this into an ELF image with one
section per hunk at that address, which the DWARF helper reads like any
linked ELF; loaded segment N then maps section N.
"""
from __future__ import annotations

import os
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .winuae_debug import DebugError, WinuaeDebugger, integer
from .winuae_files import Hunk, HunkFile, parse_hunk_file, read_file_async

DWARF_SECTIONS = (
    '.debug_frame', '.debug_info', '.debug_abbrev', '.debug_loclists', '.debug_aranges',
    '.debug_rnglists', '.debug_line', '.debug_str', '.debug_line_str', '.debug_types',
    '.debug_macro', '.debug_ranges', '.debug_addr', '.debug_str_offsets',
)


def split_dwarf(body: bytes) -> dict[str, bytes]:
    """The DWARF sections of a toolchain debug hunk. Any other debug hunk
    (SAS/C line tables, say) does not split exactly and is rejected."""
    sections, cursor = {}, 0
    for name in DWARF_SECTIONS:
        if cursor + 4 > len(body):
            raise DebugError('The debug hunk is not m68k-amigaos DWARF')
        size = int.from_bytes(body[cursor:cursor + 4], 'big')
        start = cursor + 4
        cursor = start + ((size + 3) & ~3)
        if cursor > len(body):
            raise DebugError('The debug hunk is not m68k-amigaos DWARF')
        sections[name] = body[start:start + size]
    if any(body[cursor:]) or not sections['.debug_info']:
        raise DebugError('The debug hunk is not m68k-amigaos DWARF')
    return sections


def dwarf_bases(hunks: list[Hunk]) -> list[int]:
    bases, total = [], 0
    for hunk in hunks:
        bases.append(total)
        total += hunk.size
    return bases


def section_name(hunk: Hunk) -> str:
    """The section name parse_symbols uses for a hunk."""
    return f'{hunk.kind}:{hunk.index}'


def program_elf(hunks: list[Hunk], dwarf: dict[str, bytes]) -> bytes:
    """A linked ELF32 m68k image of the program for the DWARF helper."""
    # (name, type, flags, address, data, size): SHT_PROGBITS 1, SHT_NOBITS 8,
    # SHT_STRTAB 3; SHF_WRITE 1, SHF_ALLOC 2, SHF_EXECINSTR 4.
    sections = []
    for hunk, base in zip(hunks, dwarf_bases(hunks)):
        if hunk.kind == 'bss':
            sections.append((section_name(hunk), 8, 3, base, b'', hunk.size))
        else:
            data = hunk.data.ljust(hunk.size, b'\0')
            sections.append((section_name(hunk), 1, 6 if hunk.kind == 'code' else 3, base, data, hunk.size))
    sections += [(n, 1, 0, 0, dwarf[n], len(dwarf[n])) for n in DWARF_SECTIONS if dwarf[n]]
    names = bytearray(b'\0')
    offsets = []
    for section in sections + [('.shstrtab',)]:
        offsets.append(len(names))
        names += section[0].encode() + b'\0'
    sections.append(('.shstrtab', 3, 0, 0, bytes(names), len(names)))
    body, headers, cursor = bytearray(), [bytes(40)], 52
    for (name, kind, flags, address, data, size), name_offset in zip(sections, offsets):
        headers.append(struct.pack('>10I', name_offset, kind, flags, address, cursor, size, 0, 0,
                                   4 if flags else 1, 0))
        body += data
        cursor += len(data)
    table = (cursor + 3) & ~3
    body += bytes(table - cursor)
    header = (b'\x7fELF\x01\x02\x01' + bytes(9)
              + struct.pack('>2H5I6H', 2, 4, 1, 0, 0, table, 0, 52, 0, 0, 40, len(headers), len(headers) - 1))
    return header + bytes(body) + b''.join(headers)


@dataclass
class Program:
    path: str
    data: bytes
    file: HunkFile

    @property
    def hunks(self) -> list[Hunk]:
        return self.file.hunks

    def dwarf(self) -> dict[str, bytes]:
        for body in self.file.debug:
            try:
                return split_dwarf(body)
            except DebugError:
                continue
        raise DebugError(f'{self.path} has no DWARF; build it with -g')

    def elf(self) -> bytes:
        return program_elf(self.hunks, self.dwarf())

    def symbol(self, name: str) -> tuple[int, int] | None:
        """(hunk, offset) of a hunk symbol; C names match with or without
        their leading underscore."""
        for wanted in (name, '_' + name):
            hits = [(h.index, offset) for h in self.hunks for n, offset in h.symbols if n == wanted]
            if len(hits) > 1:
                raise DebugError(f'Symbol {name} is defined more than once')
            if hits:
                return hits[0]
        return None

    def nearest(self, hunk: int, offset: int) -> str | None:
        """name+offset of the closest symbol at or below an offset in a hunk,
        preferring program names to the linker script's labels."""
        best = None
        for name, at in self.hunks[hunk].symbols:
            if at <= offset and (best is None or (at, name not in LINKER_LABELS) > (best[1], best[0] not in LINKER_LABELS)):
                best = name, at
        if best is None:
            return None
        name = best[0][1:] if best[0].startswith('_') else best[0]
        return name if offset == best[1] else f'{name}+0x{offset - best[1]:x}'


# Labels from the m68k-amigaos linker script, not functions or variables.
LINKER_LABELS = frozenset((
    '__stext', '__etext', '__sdata', '__edata', '__bss_start', '__end', '___datadata_relocs',
    '___CTOR_LIST__', '___DTOR_LIST__', '___INIT_LIST__', '___EXIT_LIST__', '___LIB_LIST__',
))


def project_binary(project: str) -> str:
    from . import symbols
    path = symbols._find_binary(project)
    if not path:
        raise DebugError(f"No built binary for project '{project}'; build it first")
    return path


async def load_program(file: Any = None, project: Any = None) -> Program:
    if file is None and project is None:
        raise DebugError('Supply the program as file or project')
    if file is not None and project is not None:
        raise DebugError('Supply file or project, not both')
    path = str(file) if file is not None else project_binary(str(project))
    data = await read_file_async(path)
    if len(data) < 4 or int.from_bytes(data[:4], 'big') != 0x3f3:
        raise DebugError(f'{path} is not a Hunk executable')
    return Program(os.path.abspath(os.path.expanduser(path)), data, parse_hunk_file(data))


@dataclass
class Layout:
    """Where a program's hunks are loaded: loaded segment N holds hunk N."""
    program: Program
    bases: list[int]
    process: int | None

    def address(self, hunk: int, offset: int) -> int:
        return self.bases[hunk] + offset

    def locate(self, address: int) -> tuple[int, int] | None:
        for hunk, base in zip(self.program.hunks, self.bases):
            if base <= address < base + max(hunk.size, 1):
                return hunk.index, address - base
        return None

    def mappings(self) -> list[dict]:
        return [dict(section=section_name(h), segment=h.index) for h in self.program.hunks]


async def layout(d: WinuaeDebugger, program: Program, process: Any = None) -> Layout:
    """Match the program's hunks to the loaded segments of a DOS process
    (the current one by default). Each segment must hold its hunk."""
    proc = None if process is None else integer(process, 'process', minimum=4)
    if proc is not None and proc % 4:
        raise DebugError('The process address must be longword aligned')
    report = await d._monitor('segments' + (f' {proc:x}' if proc else ''), 'segments')
    segments = report.get('segments', []) if isinstance(report, dict) else []
    by_index = {s.get('index'): s for s in segments if isinstance(s, dict)}
    bases = []
    for hunk in program.hunks:
        segment = by_index.get(hunk.index)
        if segment is None or integer(segment.get('size'), 'segment size') < hunk.size:
            raise DebugError(f'The loaded segments of {"the current process" if proc is None else hex(proc)} '
                             f'do not match {Path(program.path).name} (hunk {hunk.index}); '
                             'pass the process that runs it')
        bases.append(integer(segment.get('address'), 'segment address'))
    if len(segments) != len(program.hunks):
        raise DebugError(f'The process has {len(segments)} segments but {Path(program.path).name} '
                         f'has {len(program.hunks)} hunks; pass the process that runs it')
    return Layout(program, bases, proc)
