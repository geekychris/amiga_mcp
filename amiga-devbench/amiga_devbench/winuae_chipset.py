# SPDX-License-Identifier: MIT
"""Chipset, Copper, stack, address and library-call views for WinUAE.

Custom registers come from WinUAE's saved custom state, which includes
write-only registers such as DMACON, INTENA and BPLCON0. Library calls in
disassembly are annotated from built-in and loaded FD tables.
"""
from __future__ import annotations

import re
from pathlib import Path
from typing import Any

from . import copper, disasm
from .winuae_debug import DebugError, WinuaeDebugger, action, boolean, integer

# Custom chip registers (Amiga Hardware Reference Manual, OCS/ECS/AGA).
CUSTOM: dict[int, str] = {
    0x000: 'BLTDDAT', 0x002: 'DMACONR', 0x004: 'VPOSR', 0x006: 'VHPOSR', 0x008: 'DSKDATR',
    0x00A: 'JOY0DAT', 0x00C: 'JOY1DAT', 0x00E: 'CLXDAT', 0x010: 'ADKCONR', 0x012: 'POT0DAT',
    0x014: 'POT1DAT', 0x016: 'POTGOR', 0x018: 'SERDATR', 0x01A: 'DSKBYTR', 0x01C: 'INTENAR',
    0x01E: 'INTREQR', 0x020: 'DSKPTH', 0x022: 'DSKPTL', 0x024: 'DSKLEN', 0x026: 'DSKDAT',
    0x028: 'REFPTR', 0x02A: 'VPOSW', 0x02C: 'VHPOSW', 0x02E: 'COPCON', 0x030: 'SERDAT',
    0x032: 'SERPER', 0x034: 'POTGO', 0x036: 'JOYTEST', 0x038: 'STREQU', 0x03A: 'STRVBL',
    0x03C: 'STRHOR', 0x03E: 'STRLONG', 0x040: 'BLTCON0', 0x042: 'BLTCON1', 0x044: 'BLTAFWM',
    0x046: 'BLTALWM', 0x048: 'BLTCPTH', 0x04A: 'BLTCPTL', 0x04C: 'BLTBPTH', 0x04E: 'BLTBPTL',
    0x050: 'BLTAPTH', 0x052: 'BLTAPTL', 0x054: 'BLTDPTH', 0x056: 'BLTDPTL', 0x058: 'BLTSIZE',
    0x05A: 'BLTCON0L', 0x05C: 'BLTSIZV', 0x05E: 'BLTSIZH', 0x060: 'BLTCMOD', 0x062: 'BLTBMOD',
    0x064: 'BLTAMOD', 0x066: 'BLTDMOD', 0x070: 'BLTCDAT', 0x072: 'BLTBDAT', 0x074: 'BLTADAT',
    0x078: 'SPRHDAT', 0x07A: 'BPLHDAT', 0x07C: 'DENISEID', 0x07E: 'DSKSYNC', 0x080: 'COP1LCH',
    0x082: 'COP1LCL', 0x084: 'COP2LCH', 0x086: 'COP2LCL', 0x088: 'COPJMP1', 0x08A: 'COPJMP2',
    0x08C: 'COPINS', 0x08E: 'DIWSTRT', 0x090: 'DIWSTOP', 0x092: 'DDFSTRT', 0x094: 'DDFSTOP',
    0x096: 'DMACON', 0x098: 'CLXCON', 0x09A: 'INTENA', 0x09C: 'INTREQ', 0x09E: 'ADKCON',
    0x100: 'BPLCON0', 0x102: 'BPLCON1', 0x104: 'BPLCON2', 0x106: 'BPLCON3', 0x108: 'BPL1MOD',
    0x10A: 'BPL2MOD', 0x10C: 'BPLCON4', 0x10E: 'CLXCON2', 0x1C0: 'HTOTAL', 0x1C2: 'HSSTOP',
    0x1C4: 'HBSTRT', 0x1C6: 'HBSTOP', 0x1C8: 'VTOTAL', 0x1CA: 'VSSTOP', 0x1CC: 'VBSTRT',
    0x1CE: 'VBSTOP', 0x1D0: 'SPRHSTRT', 0x1D2: 'SPRHSTOP', 0x1D4: 'BPLHSTRT', 0x1D6: 'BPLHSTOP',
    0x1D8: 'HHPOSW', 0x1DA: 'HHPOSR', 0x1DC: 'BEAMCON0', 0x1DE: 'HSSTRT', 0x1E0: 'VSSTRT',
    0x1E2: 'HCENTER', 0x1E4: 'DIWHIGH', 0x1E6: 'BPLHMOD', 0x1E8: 'SPRHPTH', 0x1EA: 'SPRHPTL',
    0x1EC: 'BPLHPTH', 0x1EE: 'BPLHPTL', 0x1FC: 'FMODE', 0x1FE: 'NOOP',
}
for _n in range(4):
    for _i, _name in enumerate(('LCH', 'LCL', 'LEN', 'PER', 'VOL', 'DAT')):
        CUSTOM[0x0A0 + _n * 0x10 + _i * 2] = f'AUD{_n}{_name}'
for _n in range(8):
    CUSTOM[0x0E0 + _n * 4], CUSTOM[0x0E2 + _n * 4] = f'BPL{_n + 1}PTH', f'BPL{_n + 1}PTL'
    CUSTOM[0x110 + _n * 2] = f'BPL{_n + 1}DAT'
    CUSTOM[0x120 + _n * 4], CUSTOM[0x122 + _n * 4] = f'SPR{_n}PTH', f'SPR{_n}PTL'
    for _i, _name in enumerate(('POS', 'CTL', 'DATA', 'DATB')):
        CUSTOM[0x140 + _n * 8 + _i * 2] = f'SPR{_n}{_name}'
for _n in range(32):
    CUSTOM[0x180 + _n * 2] = f'COLOR{_n:02d}'

DMACON_BITS = ['AUD0EN', 'AUD1EN', 'AUD2EN', 'AUD3EN', 'DSKEN', 'SPREN', 'BLTEN', 'COPEN',
               'BPLEN', 'DMAEN', 'BLTPRI', None, None, 'BZERO', 'BBUSY', 'SETCLR']
INT_BITS = ['TBE', 'DSKBLK', 'SOFT', 'PORTS', 'COPER', 'VERTB', 'BLIT', 'AUD0',
            'AUD1', 'AUD2', 'AUD3', 'RBF', 'DSKSYN', 'EXTER', 'INTEN', 'SETCLR']
BPLCON0_BITS = ['ECSENA', 'ERSY', 'LACE', 'LPEN', 'BPU3', 'BYPASS', 'SHRES', 'UHRES',
                'GAUD', 'COLOR', 'DPF', 'HAM', None, None, None, 'HIRES']
CIA_REGISTERS = ['PRA', 'PRB', 'DDRA', 'DDRB', 'TALO', 'TAHI', 'TBLO', 'TBHI',
                 'TODLO', 'TODMID', 'TODHI', None, 'SDR', 'ICR', 'CRA', 'CRB']
VECTORS = {0: 'Reset initial SSP', 1: 'Reset initial PC', 2: 'Bus error', 3: 'Address error',
           4: 'Illegal instruction', 5: 'Zero divide', 6: 'CHK', 7: 'TRAPV',
           8: 'Privilege violation', 9: 'Trace', 10: 'Line A emulator', 11: 'Line F emulator',
           13: 'Coprocessor protocol violation', 14: 'Format error', 15: 'Uninitialized interrupt',
           24: 'Spurious interrupt', 48: 'FPU branch or set on unordered', 49: 'FPU inexact result',
           50: 'FPU divide by zero', 51: 'FPU underflow', 52: 'FPU operand error', 53: 'FPU overflow',
           54: 'FPU signaling NaN', 55: 'FPU unimplemented data type', 56: 'MMU configuration error',
           57: 'MMU illegal operation', 58: 'MMU access level violation'}
INTERRUPT_LEVELS = {1: 'TBE, DSKBLK, SOFT', 2: 'PORTS (CIA-A)', 3: 'COPER, VERTB, BLIT',
                    4: 'AUD0-AUD3', 5: 'RBF, DSKSYN', 6: 'EXTER (CIA-B)', 7: 'NMI'}
REGIONS = [(0x000000, 0x200000, 'Chip RAM'), (0x200000, 0xA00000, 'Zorro II fast RAM'),
           (0xBFD000, 0xBFE000, 'CIA-B'), (0xBFE000, 0xBFF000, 'CIA-A'),
           (0xC00000, 0xD80000, 'Slow RAM'), (0xDC0000, 0xDD0000, 'Real-time clock'),
           (0xDFF000, 0xDFF200, 'Custom chips'), (0xE80000, 0xF00000, 'Zorro II autoconfig'),
           (0xF00000, 0xF80000, 'Extended ROM'), (0xF80000, 0x1000000, 'Kickstart ROM')]


def bits(value: int, names: list) -> list[str]:
    return [name for bit, name in enumerate(names) if name and value & (1 << bit)]


def lookup_address(address: int) -> dict:
    """Name a well-known Amiga address: custom and CIA registers, vectors."""
    result: dict[str, Any] = dict(address=address)
    if 0xDFF000 <= address < 0xDFF200:
        offset = address & 0x1FE
        result.update(kind='custom', name=CUSTOM.get(offset, f'custom+${offset:03X}'), offset=offset)
    elif (address & ~0xF00) in (0xBFE001, 0xBFD000) and CIA_REGISTERS[(address >> 8) & 0xF]:
        # CIA-A is selected on odd addresses at $BFExx1, CIA-B on even ones.
        chip, name = ('CIA-A' if address & 1 else 'CIA-B'), CIA_REGISTERS[(address >> 8) & 0xF]
        result.update(kind='cia', chip=chip, name=('ciaa' if address & 1 else 'ciab') + name.lower(), register=name)
    elif address < 0x400 and address % 4 == 0:
        vector = address // 4
        name = VECTORS.get(vector)
        if 25 <= vector <= 31:
            name = f'Level {vector - 24} interrupt autovector ({INTERRUPT_LEVELS[vector - 24]})'
        elif 32 <= vector <= 47:
            name = f'TRAP #{vector - 32}'
        elif vector >= 64:
            name = f'User interrupt vector {vector}'
        result.update(kind='vector', vector=vector, name=name or f'Reserved vector {vector}')
        if address == 4:
            result['note'] = 'AbsExecBase: the ExecBase pointer after boot'
    for start, end, region in REGIONS:
        if start <= address < end:
            result['region'] = region
    return result


# ─── FD tables ───

_ALIASES = {'graphics': 'gfx'}
FD_TABLES: dict[str, dict[int, str]] = {name: dict(table) for name, table in disasm.ALL_LVOS.items()}


def library_key(name: str) -> str:
    key = name.strip().lower().removesuffix('.library')
    return _ALIASES.get(key, key)


def parse_fd(text: str) -> dict[int, str]:
    """Parse an FD file: ##bias sets the first offset, each function line
    takes the next 6-byte vector, private ones included."""
    table, bias = {}, None
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith('*'):
            continue
        if line.startswith('##'):
            words = line[2:].split()
            if words and words[0] == 'bias' and len(words) == 2:
                bias = int(words[1])
            elif words and words[0] == 'end':
                break
            continue
        match = re.match(r'([A-Za-z_][A-Za-z0-9_]*)\s*\(', line)
        if not match or bias is None:
            raise DebugError(f'Invalid FD line: {line[:80]}')
        table[-bias] = match[1]
        bias += 6
    if not table:
        raise DebugError('The FD file defines no functions')
    return table


def fd_lookup(offset: int, library: str = 'exec') -> dict:
    key = library_key(library)
    table = FD_TABLES.get(key)
    if table is None:
        raise DebugError(f'No FD table for {library}; load its .fd file')
    name = table.get(-abs(offset))
    return dict(library=key, offset=-abs(offset), name=name, found=name is not None)


@action('fd', locked=False, stopped=False)
async def _fd(d: WinuaeDebugger, operation: str = 'libraries', offset: Any = None,
              library: str = 'exec', path: str | None = None) -> Any:
    """Library vector offset tables used to annotate disassembly."""
    if operation == 'libraries':
        return [dict(library=k, functions=len(v)) for k, v in sorted(FD_TABLES.items())]
    if operation == 'lookup':
        if isinstance(offset, str):
            try:
                offset = int(offset.strip(), 10) if offset.strip().lstrip('+-').isdigit() else int(offset, 0)
            except ValueError:
                raise DebugError('offset must be a library vector offset such as -552') from None
        if type(offset) is not int or not 6 <= abs(offset) <= 0x8000:
            raise DebugError('offset must be a library vector offset such as -552')
        return fd_lookup(offset, library)
    if operation == 'list':
        key = library_key(library)
        if key not in FD_TABLES:
            raise DebugError(f'No FD table for {library}')
        return [dict(offset=o, name=n) for o, n in sorted(FD_TABLES[key].items(), reverse=True)]
    if operation == 'load':
        from .winuae_files import read_file_async
        text = (await read_file_async(path, 1024 * 1024)).decode('latin-1')
        key = library_key(library)
        FD_TABLES[key] = parse_fd(text)
        return dict(library=key, functions=len(FD_TABLES[key]))
    raise DebugError('FD operation must be libraries, lookup, list or load')


async def _library_name(d: WinuaeDebugger, base: int) -> str | None:
    """ln_Name of the Library node at base, if base looks like one."""
    try:
        node = await d.read_bytes(base, 14)
        name_ptr = int.from_bytes(node[10:14], 'big')
        if node[8] != 9 or not name_ptr:  # NT_LIBRARY
            return None
        text = await d.read_bytes(name_ptr, 32)
        return text.split(b'\0', 1)[0].decode('latin-1') or None
    except DebugError:
        return None


CALL = re.compile(r'\b(JSR|JMP)\s+\(-\$([0-9a-fA-F]+),a6\)', re.I)


def annotate_line(line: str, library: str | None) -> str | None:
    match = CALL.search(line)
    if not match:
        return None
    offset = -int(match[2], 16)
    if library:
        hit = FD_TABLES.get(library_key(library), {}).get(offset)
        return f'{library_key(library)}.{hit}()' if hit else None
    for key, table in FD_TABLES.items():
        if offset in table:
            return f'{key}.{table[offset]}()'
    return None


@action('disassemble', capability='disasm')
async def _disassemble(d: WinuaeDebugger, address: Any = 'pc', count: Any = 20, annotate: Any = True,
                       library: str | None = None, source_xref: Any = False, project: str | None = None) -> dict:
    """WinUAE's disassembler, with library calls named from FD tables and
    optional source lines from the bridge debugger's loaded symbols."""
    regs = None
    if address == 'pc':
        regs = await d.registers()
        address = regs['PC']
    address = integer(address, 'address')
    count = integer(count, 'count', 256, 1)
    text = await d.monitor_text(f'disasm {address:x} {count}') or ''
    if boolean(annotate, 'annotate') and library is None and regs is not None:
        # A6 is valid for the current instruction; use its library as a hint.
        name = await _library_name(d, regs['A6'])
        library = name if name and library_key(name) in FD_TABLES else None
    lines = []
    for line in text.splitlines():
        entry: dict[str, Any] = dict(text=line)
        match = re.match(r'\s*([0-9A-Fa-f]{8})\s', line)
        if match:
            entry['address'] = int(match[1], 16)
        if boolean(annotate, 'annotate'):
            note = annotate_line(line, library)
            if note:
                entry['annotation'] = note
        if boolean(source_xref, 'source_xref') and 'address' in entry:
            from . import symbols
            info = symbols.annotate_address_full(entry['address'], project)
            if info and (info.get('symbol') or info.get('file')):
                entry['source'] = info
        lines.append(entry)
    return dict(address=address, text=text, lines=lines, library=library)


async def custom_registers(d: WinuaeDebugger) -> dict[int, int]:
    data = await d.read_bytes(0xDFF000, 0x200)
    return {offset: int.from_bytes(data[offset:offset + 2], 'big') for offset in range(0, 0x200, 2)}


@action('custom')
async def _custom(d: WinuaeDebugger) -> dict:
    """Decode the custom chip registers and the beam position."""
    regs = await custom_registers(d)
    named = {CUSTOM[o]: v for o, v in regs.items() if o in CUSTOM}
    long = lambda o: (regs[o] << 16) | regs[o + 2]
    bplcon0 = regs[0x100]
    planes = ((bplcon0 >> 12) & 7) + (8 if bplcon0 & 0x10 else 0)
    decoded = dict(
        dmacon=bits(regs[0x002], DMACON_BITS), intena=bits(regs[0x01C], INT_BITS),
        intreq=bits(regs[0x01E], INT_BITS), bplcon0=bits(bplcon0, BPLCON0_BITS), planes=planes,
        bitplanes=[long(0x0E0 + n * 4) for n in range(8)], sprites=[long(0x120 + n * 4) for n in range(8)],
        cop1lc=long(0x080), cop2lc=long(0x084), dskpt=long(0x020),
        blitter=dict(bltcon0=regs[0x040], bltcon1=regs[0x042], apt=long(0x050), bpt=long(0x04C),
                     cpt=long(0x048), dpt=long(0x054)),
        display=dict(diwstrt=regs[0x08E], diwstop=regs[0x090], ddfstrt=regs[0x092], ddfstop=regs[0x094],
                     bpl1mod=regs[0x108], bpl2mod=regs[0x10A], fmode=regs[0x1FC]),
        colors=[regs[0x180 + n * 2] for n in range(32)],
    )
    beam = await d._monitor('execution-status')
    return dict(registers=named, decoded=decoded,
                beam={k: beam.get(k) for k in ('frame', 'vpos', 'hpos')} if isinstance(beam, dict) else None)


@action('copper')
async def _copper(d: WinuaeDebugger, address: Any = 'cop1lc', count: Any = 64) -> dict:
    """Decode a Copper list; address may be cop1lc, cop2lc or a number."""
    if address in ('cop1lc', 'cop2lc'):
        regs = await custom_registers(d)
        base = 0x080 if address == 'cop1lc' else 0x084
        address = (regs[base] << 16) | regs[base + 2]
    address = integer(address, 'address')
    count = integer(count, 'count', 4096, 1)
    data = await d.read_bytes(address, count * 4)
    instructions = copper.decode_copper_list(data.hex(), address)
    for inst in instructions:
        if inst['type'] == 'MOVE':
            name = CUSTOM.get(inst['register'], f'custom+${inst["register"]:03X}')
            inst['regName'] = name
            inst['text'] = f"MOVE #${inst['value']:04X},{name}"
        inst['address'] = address + inst['offset']
    return dict(address=address, instructions=instructions)


def call_before(before: bytes) -> bool:
    """Whether the 6 bytes before an address end in a JSR or BSR, so that
    the address is a return address."""
    word = lambda i: int.from_bytes(before[i:i + 2], 'big')
    last, middle, first = word(4), word(2), word(0)
    # 2 bytes: BSR.S, JSR (An). 4 bytes: BSR.W, JSR d16(An), d8(An,Xn),
    # abs.w, d16(PC), d8(PC,Xn). 6 bytes: BSR.L, JSR abs.l.
    short = ((last & 0xff00) == 0x6100 and last & 0xff not in (0, 0xff)) or 0x4e90 <= last <= 0x4e97
    word_sized = middle == 0x6100 or 0x4ea8 <= middle <= 0x4eb8 or middle in (0x4eba, 0x4ebb)
    return short or word_sized or first in (0x61ff, 0x4eb9)


@action('stack')
async def _stack(d: WinuaeDebugger, depth: Any = 32) -> dict:
    """Longwords from A7. Values after a JSR or BSR are tagged return; others
    that point into ROM or a loaded segment of the current process, code."""
    depth = integer(depth, 'depth', 1024, 1)
    regs = await d.registers()
    sp = regs['A7']
    data = await d.read_bytes(sp, depth * 4)
    regions = await _regions(d)
    mapped = [(r['address'], r['address'] + r['size']) for r in regions if r.get('type') in ('ram', 'rom')]
    code = [(r['address'], r['address'] + r['size']) for r in regions if r.get('type') == 'rom']
    try:
        report = await d._monitor('segments', 'segments')
        code += [(s['address'], s['address'] + s['size']) for s in report.get('segments', [])]
    except DebugError:
        pass
    words = []
    for i in range(depth):
        value = int.from_bytes(data[i * 4:i * 4 + 4], 'big')
        kind = 'data'
        if not value & 1 and any(a + 6 <= value < b for a, b in mapped) and call_before(await d.read_bytes(value - 6, 6)):
            kind = 'return'
        elif not value & 1 and any(a <= value < b for a, b in code):
            kind = 'code'
        words.append(dict(offset=i * 4, address=sp + i * 4, value=value, kind=kind))
    return dict(sp=sp, words=words)


async def _regions(d: WinuaeDebugger) -> list[dict]:
    try:
        report = await d._monitor('memory-map', 'memory-map')
    except DebugError:
        return []
    regions = report.get('regions', report) if isinstance(report, dict) else report
    return [r for r in regions if isinstance(r, dict) and 'address' in r and 'size' in r]


@action('lookup', locked=False, stopped=False)
async def _lookup(d: WinuaeDebugger, address: Any = None) -> dict:
    """Name a well-known Amiga hardware address or exception vector."""
    return lookup_address(integer(address, 'address'))
