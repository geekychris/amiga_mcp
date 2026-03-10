"""Motorola 68000/68020 disassembler for Amiga DevBench.

Decodes 68k machine code into assembly mnemonics. Designed for host-side
disassembly of memory read from the Amiga via the INSPECT protocol.
"""

from __future__ import annotations

import struct
from typing import Callable

# ─── Library Vector Offset (LVO) Tables ───

EXEC_LVOS: dict[int, str] = {
    -0x1E: "Supervisor",
    -0x24: "ExitIntr",
    -0x2A: "Schedule",
    -0x30: "Reschedule",
    -0x36: "Switch",
    -0x3C: "Dispatch",
    -0x42: "Exception",
    -0x48: "InitCode",
    -0x4E: "InitStruct",
    -0x54: "MakeLibrary",
    -0x5A: "RemLibrary",
    -0x60: "MakeFunctions",
    -0x66: "FindResident",
    -0x6C: "InitResident",
    -0x72: "Alert",
    -0x78: "Debug",
    -0x7E: "Disable",
    -0x84: "Enable",
    -0x8A: "Forbid",
    -0x90: "Permit",
    -0x96: "SetSR",
    -0x9C: "SuperState",
    -0xA2: "UserState",
    -0xA8: "SetIntVector",
    -0xAE: "AddIntServer",
    -0xB4: "RemIntServer",
    -0xBA: "Cause",
    -0xC0: "Allocate",
    -0xC6: "Deallocate",
    -0xCC: "AllocMem",
    -0xD2: "AllocAbs",
    -0xD8: "FreeMem",
    -0xDE: "AvailMem",
    -0xE4: "AllocEntry",
    -0xEA: "FreeEntry",
    -0xF0: "Insert",
    -0xF6: "AddHead",
    -0xFC: "AddTail",
    -0x102: "Remove",
    -0x108: "RemHead",
    -0x10E: "RemTail",
    -0x114: "Enqueue",
    -0x11A: "FindName",
    -0x120: "AddTask",
    -0x126: "RemTask",
    -0x12C: "FindTask",
    -0x132: "SetTaskPri",
    -0x138: "SetSignal",
    -0x13E: "SetExcept",
    -0x144: "Wait",
    -0x14A: "Signal",
    -0x150: "AllocSignal",
    -0x156: "FreeSignal",
    -0x15C: "AllocTrap",
    -0x162: "FreeTrap",
    -0x168: "AddPort",
    -0x16E: "RemPort",
    -0x174: "PutMsg",
    -0x17A: "GetMsg",
    -0x180: "ReplyMsg",
    -0x186: "WaitPort",
    -0x18C: "FindPort",
    -0x192: "AddLibrary",
    -0x198: "RemLibrary",
    -0x19E: "OldOpenLibrary",
    -0x1A4: "CloseLibrary",
    -0x1AA: "SetFunction",
    -0x1B0: "SumLibrary",
    -0x1B6: "AddDevice",
    -0x1BC: "RemDevice",
    -0x1C2: "OpenDevice",
    -0x1C8: "CloseDevice",
    -0x1CE: "DoIO",
    -0x1D4: "SendIO",
    -0x1DA: "CheckIO",
    -0x1E0: "WaitIO",
    -0x1E6: "AbortIO",
    -0x1EC: "AddResource",
    -0x1F2: "RemResource",
    -0x1F8: "OpenResource",
    -0x222: "RawDoFmt",
    -0x228: "GetCC",
    -0x22E: "TypeOfMem",
    -0x234: "Procure",
    -0x23A: "Vacate",
    -0x240: "OpenLibrary",
    -0x246: "InitSemaphore",
    -0x24C: "ObtainSemaphore",
    -0x252: "ReleaseSemaphore",
    -0x258: "AttemptSemaphore",
    -0x25E: "ObtainSemaphoreList",
    -0x264: "ReleaseSemaphoreList",
    -0x26A: "FindSemaphore",
    -0x270: "AddSemaphore",
    -0x276: "RemSemaphore",
    -0x27C: "SumKickData",
    -0x282: "AddMemList",
    -0x288: "CopyMem",
    -0x28E: "CopyMemQuick",
    -0x294: "CacheClearU",
    -0x29A: "CacheClearE",
    -0x2A0: "CacheControl",
    -0x2A6: "CreateIORequest",
    -0x2AC: "DeleteIORequest",
    -0x2B2: "CreateMsgPort",
    -0x2B8: "DeleteMsgPort",
    -0x2BE: "ObtainSemaphoreShared",
    -0x2C4: "AllocVec",
    -0x2CA: "FreeVec",
    -0x2D0: "CreatePool",
    -0x2D6: "DeletePool",
    -0x2DC: "AllocPooled",
    -0x2E2: "FreePooled",
    -0x2E8: "AttemptSemaphoreShared",
    -0x2EE: "ColdReboot",
    -0x2F4: "StackSwap",
}

DOS_LVOS: dict[int, str] = {
    -0x1E: "Open",
    -0x24: "Close",
    -0x2A: "Read",
    -0x30: "Write",
    -0x36: "Input",
    -0x3C: "Output",
    -0x42: "Seek",
    -0x48: "DeleteFile",
    -0x4E: "Rename",
    -0x54: "Lock",
    -0x5A: "UnLock",
    -0x60: "DupLock",
    -0x66: "Examine",
    -0x6C: "ExNext",
    -0x72: "Info",
    -0x78: "CreateDir",
    -0x7E: "CurrentDir",
    -0x84: "IoErr",
    -0x8A: "CreateProc",
    -0x90: "Exit",
    -0x96: "LoadSeg",
    -0x9C: "UnLoadSeg",
    -0xAE: "DeviceProc",
    -0xB4: "SetComment",
    -0xBA: "SetProtection",
    -0xC0: "DateStamp",
    -0xC6: "Delay",
    -0xCC: "WaitForChar",
    -0xD2: "ParentDir",
    -0xD8: "IsInteractive",
    -0xDE: "Execute",
    -0xE4: "AllocDosObject",
    -0xEA: "FreeDosObject",
    -0xF0: "DoPkt",
    -0xF6: "SendPkt",
    -0xFC: "WaitPkt",
    -0x102: "ReplyPkt",
    -0x108: "LockDosList",
    -0x10E: "UnLockDosList",
    -0x114: "AttemptLockDosList",
    -0x11A: "NewLoadSeg",
    -0x120: "FPutC",
    -0x126: "FGetC",
    -0x12C: "FRead",
    -0x132: "FWrite",
    -0x138: "ReadArgs",
    -0x13E: "FindArg",
    -0x144: "ReadItem",
    -0x14A: "StrToDate",
    -0x150: "DateToStr",
    -0x156: "VFWritef",
    -0x15C: "VFPrintf",
    -0x162: "VPrintf",
    -0x168: "ParsePatternNoCase",
    -0x16E: "MatchPatternNoCase",
    -0x174: "FilePart",
    -0x17A: "PathPart",
    -0x180: "AddPart",
    -0x186: "StartNotify",
    -0x18C: "EndNotify",
    -0x192: "SetVar",
    -0x198: "GetVar",
    -0x19E: "DeleteVar",
    -0x1A4: "FindVar",
    -0x1AA: "CliInitNewcli",
    -0x1B0: "CliInitRun",
    -0x1B6: "WriteChars",
    -0x1BC: "PutStr",
    -0x1C2: "SplitName",
    -0x1C8: "SameLock",
    -0x1CE: "SetMode",
    -0x1D4: "ExAll",
    -0x1DA: "ReadLink",
    -0x1E0: "MakeLink",
    -0x1E6: "ChangeMode",
    -0x1EC: "SetFileSize",
    -0x1F2: "SetIoErr",
    -0x1F8: "Fault",
    -0x1FE: "PrintFault",
    -0x204: "ErrorReport",
    -0x210: "SystemTagList",
}

INTUITION_LVOS: dict[int, str] = {
    -0x1E: "OpenIntuition",
    -0x24: "Intuition",
    -0x2A: "AddGadget",
    -0x30: "ClearDMRequest",
    -0x36: "ClearMenuStrip",
    -0x3C: "ClearPointer",
    -0x42: "CloseScreen",
    -0x48: "CloseWindow",
    -0x4E: "CloseWorkBench",
    -0x54: "CurrentTime",
    -0x5A: "DisplayAlert",
    -0x60: "DisplayBeep",
    -0x66: "DoubleClick",
    -0x6C: "DrawBorder",
    -0x72: "DrawImage",
    -0x78: "EndRequest",
    -0x7E: "GetDefPrefs",
    -0x84: "GetPrefs",
    -0x8A: "InitRequester",
    -0x90: "ItemAddress",
    -0x96: "ModifyIDCMP",
    -0x9C: "ModifyProp",
    -0xA2: "MoveScreen",
    -0xA8: "MoveWindow",
    -0xAE: "OffGadget",
    -0xB4: "OffMenu",
    -0xBA: "OnGadget",
    -0xC0: "OnMenu",
    -0xC6: "OpenScreen",
    -0xCC: "OpenWindow",
    -0xD2: "OpenWorkBench",
    -0xD8: "PrintIText",
    -0xDE: "RefreshGadgets",
    -0xE4: "RemoveGadget",
    -0xEA: "ReportMouse",
    -0xF0: "Request",
    -0xF6: "ScreenToBack",
    -0xFC: "ScreenToFront",
    -0x102: "SetDMRequest",
    -0x108: "SetMenuStrip",
    -0x10E: "SetPointer",
    -0x114: "SetWindowTitles",
    -0x11A: "ShowTitle",
    -0x120: "SizeWindow",
    -0x126: "ViewAddress",
    -0x12C: "ViewPortAddress",
    -0x132: "WindowToBack",
    -0x138: "WindowToFront",
    -0x13E: "WindowLimits",
    -0x15C: "SetPrefs",
    -0x162: "IntuiTextLength",
    -0x168: "WBenchToBack",
    -0x16E: "WBenchToFront",
    -0x174: "AutoRequest",
    -0x17A: "BeginRefresh",
    -0x180: "BuildSysRequest",
    -0x186: "EndRefresh",
    -0x18C: "FreeSysRequest",
    -0x198: "MakeScreen",
    -0x19E: "RemakeDisplay",
    -0x1A4: "RethinkDisplay",
    -0x1B0: "AllocRemember",
    -0x1B6: "FreeRemember",
    -0x1C2: "LockIBase",
    -0x1C8: "UnlockIBase",
    -0x1F2: "EasyRequestArgs",
    -0x264: "OpenScreenTagList",
    -0x25E: "OpenWindowTagList",
}

GFX_LVOS: dict[int, str] = {
    -0x1E: "BltBitMap",
    -0x24: "BltTemplate",
    -0x2A: "ClearEOL",
    -0x30: "ClearScreen",
    -0x36: "TextLength",
    -0x3C: "Text",
    -0x42: "SetFont",
    -0x48: "OpenFont",
    -0x4E: "CloseFont",
    -0x54: "AskSoftStyle",
    -0x5A: "SetSoftStyle",
    -0x60: "AddBob",
    -0x66: "AddVSprite",
    -0x6C: "DoCollision",
    -0x72: "DrawGList",
    -0x78: "InitGels",
    -0x7E: "InitMasks",
    -0x84: "RemIBob",
    -0x8A: "RemVSprite",
    -0x90: "SetCollision",
    -0x96: "SortGList",
    -0x9C: "AddAnimOb",
    -0xA2: "Animate",
    -0xA8: "GetGBuffers",
    -0xAE: "InitGMasks",
    -0xBA: "DrawEllipse",
    -0xC0: "AreaEllipse",
    -0xC6: "LoadRGB4",
    -0xCC: "InitRastPort",
    -0xD2: "InitVPort",
    -0xD8: "MrgCop",
    -0xDE: "MakeVPort",
    -0xE4: "LoadView",
    -0xEA: "WaitBlit",
    -0xF0: "SetRast",
    -0xF6: "Move",
    -0xFC: "Draw",
    -0x102: "AreaMove",
    -0x108: "AreaDraw",
    -0x10E: "AreaEnd",
    -0x114: "WaitTOF",
    -0x11A: "QBlit",
    -0x120: "InitArea",
    -0x126: "SetRGB4",
    -0x12C: "QBSBlit",
    -0x132: "BltClear",
    -0x138: "RectFill",
    -0x13E: "BltPattern",
    -0x144: "ReadPixel",
    -0x14A: "WritePixel",
    -0x150: "Flood",
    -0x156: "PolyDraw",
    -0x15C: "SetAPen",
    -0x162: "SetBPen",
    -0x168: "SetDrMd",
    -0x16E: "InitView",
    -0x174: "CBump",
    -0x17A: "CMove",
    -0x180: "CWait",
    -0x186: "VBeamPos",
    -0x18C: "InitBitMap",
    -0x192: "ScrollRaster",
    -0x198: "WaitBOVP",
    -0x19E: "GetSprite",
    -0x1A4: "FreeSprite",
    -0x1AA: "ChangeSprite",
    -0x1B0: "MoveSprite",
    -0x1B6: "LockLayerRom",
    -0x1BC: "UnlockLayerRom",
    -0x1C8: "AllocRaster",
    -0x1CE: "FreeRaster",
    -0x1D4: "AndRectRegion",
    -0x1DA: "OrRectRegion",
    -0x1E0: "NewRegion",
    -0x1E6: "ClearRectRegion",
    -0x1EC: "ClearRegion",
    -0x1F2: "DisposeRegion",
    -0x1F8: "FreeVPortCopLists",
    -0x1FE: "FreeCopList",
    -0x204: "ClipBlit",
    -0x20A: "XorRectRegion",
    -0x210: "FreeCprList",
    -0x222: "SetRGB4CM",
    -0x228: "ScrollVPort",
    -0x22E: "UCopperListInit",
    -0x234: "FreeGBuffers",
    -0x23A: "BltBitMapRastPort",
    -0x246: "GetColorMap",
    -0x24C: "FreeColorMap",
    -0x252: "GetRGB4",
    -0x258: "ScrollRasterBF",
    -0x264: "SetRGB32",
    -0x26A: "GetBitMapAttr",
    -0x270: "AllocBitMap",
    -0x276: "FreeBitMap",
}

# Combined for A6-relative calls. Key: (library_hint, offset) -> name
# We'll try Exec first since SysBase is most commonly in A6
ALL_LVOS: dict[str, dict[int, str]] = {
    "exec": EXEC_LVOS,
    "dos": DOS_LVOS,
    "intuition": INTUITION_LVOS,
    "gfx": GFX_LVOS,
}


def _lvo_name(offset: int) -> str | None:
    """Look up an LVO name by negative offset. Tries exec first."""
    for lib, table in ALL_LVOS.items():
        name = table.get(offset)
        if name:
            return f"{name} [{lib}]" if lib != "exec" else name
    return None


# ─── Condition Code Names ───

CC_NAMES = {
    0x0: "T",   0x1: "F",   0x2: "HI",  0x3: "LS",
    0x4: "CC",  0x5: "CS",  0x6: "NE",  0x7: "EQ",
    0x8: "VC",  0x9: "VS",  0xA: "PL",  0xB: "MI",
    0xC: "GE",  0xD: "LT",  0xE: "GT",  0xF: "LE",
}

SIZE_NAMES = {0: ".B", 1: ".W", 2: ".L"}
SIZE_BYTES = {0: 1, 1: 2, 2: 4}


# ─── Instruction Stream Reader ───

class _Stream:
    """Reads words from a byte buffer, tracking position."""

    def __init__(self, data: bytes, base_addr: int):
        self.data = data
        self.base = base_addr
        self.pos = 0  # byte offset

    @property
    def addr(self) -> int:
        return self.base + self.pos

    def has(self, n: int = 2) -> bool:
        return self.pos + n <= len(self.data)

    def word(self) -> int:
        """Read next 16-bit word (big-endian)."""
        if self.pos + 2 > len(self.data):
            raise _EndOfData()
        w = (self.data[self.pos] << 8) | self.data[self.pos + 1]
        self.pos += 2
        return w

    def sword(self) -> int:
        """Read next 16-bit signed word."""
        w = self.word()
        return w - 0x10000 if w >= 0x8000 else w

    def long(self) -> int:
        """Read next 32-bit long (big-endian)."""
        hi = self.word()
        lo = self.word()
        return (hi << 16) | lo

    def slong(self) -> int:
        """Read next 32-bit signed long."""
        v = self.long()
        return v - 0x100000000 if v >= 0x80000000 else v

    def hex_between(self, start: int, end: int) -> str:
        """Return hex string for bytes between two offsets."""
        return self.data[start:end].hex().upper()


class _EndOfData(Exception):
    pass


# ─── Effective Address Decoding ───

def _decode_ea(s: _Stream, mode: int, reg: int, size: int,
               pc_addr: int | None = None) -> str:
    """Decode a 68k effective address.

    mode/reg from the instruction encoding. size is 0=B, 1=W, 2=L.
    pc_addr is the address of the extension word for PC-relative modes.
    """
    if mode == 0:  # Dn
        return f"D{reg}"
    if mode == 1:  # An
        return f"A{reg}"
    if mode == 2:  # (An)
        return f"(A{reg})"
    if mode == 3:  # (An)+
        return f"(A{reg})+"
    if mode == 4:  # -(An)
        return f"-(A{reg})"
    if mode == 5:  # d16(An)
        d16 = s.sword()
        if reg == 6 and d16 < 0:
            # A6-relative negative offset - could be LVO
            lvo = _lvo_name(d16)
            if lvo:
                return f"{d16}(A{reg})  ; {lvo}"
        return f"{d16}(A{reg})"
    if mode == 6:  # d8(An,Xn.S)
        return _decode_brief_ext(s, f"A{reg}")
    if mode == 7:
        if reg == 0:  # xxx.W
            addr = s.sword()
            return f"${addr & 0xFFFF:04X}.W"
        if reg == 1:  # xxx.L
            addr = s.long()
            return f"${addr:08X}"
        if reg == 2:  # d16(PC)
            ref_addr = pc_addr if pc_addr is not None else s.addr
            d16 = s.sword()
            target = ref_addr + d16
            return f"${target:08X}(PC)"
        if reg == 3:  # d8(PC,Xn.S)
            ref_addr = pc_addr if pc_addr is not None else s.addr
            return _decode_brief_ext(s, "PC", ref_addr)
        if reg == 4:  # #imm
            if size == 0:  # byte
                val = s.word()
                return f"#${val & 0xFF:02X}"
            elif size == 1:  # word
                val = s.word()
                return f"#${val:04X}"
            else:  # long
                val = s.long()
                return f"#${val:08X}"
    return "???"


def _decode_brief_ext(s: _Stream, base: str, ref_addr: int | None = None) -> str:
    """Decode brief extension word for indexed addressing."""
    ext = s.word()
    da = "A" if ext & 0x8000 else "D"
    xreg = (ext >> 12) & 7
    wl = ".L" if ext & 0x0800 else ".W"
    disp = ext & 0xFF
    if disp >= 0x80:
        disp -= 0x100
    if ref_addr is not None and base == "PC":
        target = ref_addr + disp
        return f"${target:08X}({base},{da}{xreg}{wl})"
    return f"{disp}({base},{da}{xreg}{wl})"


def _decode_ea_from_word(s: _Stream, opword: int, size: int) -> str:
    """Extract mode/reg from bits 5-0 of opword and decode EA."""
    mode = (opword >> 3) & 7
    reg = opword & 7
    pc_addr = s.addr  # capture before extension words are read
    return _decode_ea(s, mode, reg, size, pc_addr)


def _decode_ea_dst(s: _Stream, opword: int, size: int) -> str:
    """Extract destination EA from bits 11-6 (reversed mode/reg)."""
    reg = (opword >> 9) & 7
    mode = (opword >> 6) & 7
    pc_addr = s.addr
    return _decode_ea(s, mode, reg, size, pc_addr)


# ─── MOVEM Register List ───

def _movem_reglist(mask: int, reverse: bool = False) -> str:
    """Format a MOVEM register list mask."""
    if reverse:
        # For predecrement mode, bits are reversed
        new_mask = 0
        for i in range(16):
            if mask & (1 << i):
                new_mask |= 1 << (15 - i)
        mask = new_mask

    regs: list[str] = []
    # D0-D7 (bits 0-7), A0-A7 (bits 8-15)
    for prefix, base_bit in [("D", 0), ("A", 8)]:
        i = 0
        while i < 8:
            if mask & (1 << (base_bit + i)):
                start = i
                while i < 7 and mask & (1 << (base_bit + i + 1)):
                    i += 1
                if i > start:
                    regs.append(f"{prefix}{start}-{prefix}{i}")
                else:
                    regs.append(f"{prefix}{start}")
            i += 1
    return "/".join(regs) if regs else "0"


# ─── Main Decode Dispatch ───

def _decode_one(s: _Stream) -> str:
    """Decode a single instruction. Returns mnemonic string.

    Raises _EndOfData if the stream runs out.
    """
    start_pos = s.pos
    op = s.word()

    # Top 4 bits dispatch
    top4 = (op >> 12) & 0xF

    try:
        # ── 0000: Bit manipulation / MOVEP / Immediate ──
        if top4 == 0x0:
            return _decode_0000(s, op)

        # ── 0001/0010/0011: MOVE ──
        if top4 in (0x1, 0x2, 0x3):
            return _decode_move(s, op, top4)

        # ── 0100: Miscellaneous ──
        if top4 == 0x4:
            return _decode_0100(s, op)

        # ── 0101: ADDQ/SUBQ/Scc/DBcc ──
        if top4 == 0x5:
            return _decode_0101(s, op)

        # ── 0110: Bcc/BSR/BRA ──
        if top4 == 0x6:
            return _decode_branch(s, op)

        # ── 0111: MOVEQ ──
        if top4 == 0x7:
            if op & 0x0100:
                return None  # invalid
            dreg = (op >> 9) & 7
            data = op & 0xFF
            if data >= 0x80:
                data -= 0x100
            return f"MOVEQ    #{data},D{dreg}"

        # ── 1000: OR/DIV/SBCD ──
        if top4 == 0x8:
            return _decode_1000(s, op)

        # ── 1001: SUB/SUBA/SUBX ──
        if top4 == 0x9:
            return _decode_addsub(s, op, "SUB")

        # ── 1010: A-line (unimplemented) ──
        if top4 == 0xA:
            return f"DC.W     ${op:04X}  ; A-line trap"

        # ── 1011: CMP/EOR ──
        if top4 == 0xB:
            return _decode_1011(s, op)

        # ── 1100: AND/MUL/ABCD/EXG ──
        if top4 == 0xC:
            return _decode_1100(s, op)

        # ── 1101: ADD/ADDA/ADDX ──
        if top4 == 0xD:
            return _decode_addsub(s, op, "ADD")

        # ── 1110: Shifts and rotates ──
        if top4 == 0xE:
            return _decode_shift(s, op)

        # ── 1111: F-line (coprocessor / unimplemented) ──
        if top4 == 0xF:
            return f"DC.W     ${op:04X}  ; F-line trap"

    except _EndOfData:
        raise
    except Exception:
        pass

    return None


def _decode_0000(s: _Stream, op: int) -> str:
    """Decode group 0000: ORI, ANDI, SUBI, ADDI, EORI, CMPI, BTST, BCHG, BCLR, BSET, MOVEP."""
    if op & 0x0100:
        # Bit operations with register, or MOVEP
        dreg = (op >> 9) & 7
        mode = (op >> 3) & 7
        reg = op & 7

        if mode == 1:
            # MOVEP
            opmode = (op >> 6) & 7
            disp = s.sword()
            if opmode == 4:
                return f"MOVEP.W  {disp}(A{reg}),D{dreg}"
            elif opmode == 5:
                return f"MOVEP.L  {disp}(A{reg}),D{dreg}"
            elif opmode == 6:
                return f"MOVEP.W  D{dreg},{disp}(A{reg})"
            elif opmode == 7:
                return f"MOVEP.L  D{dreg},{disp}(A{reg})"

        # BTST/BCHG/BCLR/BSET with Dn
        bit_op = (op >> 6) & 3
        names = ["BTST", "BCHG", "BCLR", "BSET"]
        ea = _decode_ea_from_word(s, op, 0)
        return f"{names[bit_op]:9s}D{dreg},{ea}"

    # Immediate operations or static bit ops
    sub_op = (op >> 9) & 7
    size_bits = (op >> 6) & 3

    if sub_op == 0:  # ORI
        if size_bits == 3:
            return None
        if op == 0x003C:
            val = s.word() & 0xFF
            return f"ORI      #${val:02X},CCR"
        if op == 0x007C:
            val = s.word()
            return f"ORI      #${val:04X},SR"
        return _decode_imm_op(s, op, "ORI", size_bits)
    if sub_op == 1:  # ANDI
        if size_bits == 3:
            return None
        if op == 0x023C:
            val = s.word() & 0xFF
            return f"ANDI     #${val:02X},CCR"
        if op == 0x027C:
            val = s.word()
            return f"ANDI     #${val:04X},SR"
        return _decode_imm_op(s, op, "ANDI", size_bits)
    if sub_op == 2:  # SUBI
        if size_bits == 3:
            return None
        return _decode_imm_op(s, op, "SUBI", size_bits)
    if sub_op == 3:  # ADDI
        if size_bits == 3:
            return None
        return _decode_imm_op(s, op, "ADDI", size_bits)
    if sub_op == 4:  # BTST/BCHG/BCLR/BSET with immediate bit number
        bit_op = (op >> 6) & 3
        names = ["BTST", "BCHG", "BCLR", "BSET"]
        bit_num = s.word() & 0xFF
        ea = _decode_ea_from_word(s, op, 0)
        return f"{names[bit_op]:9s}#{bit_num},{ea}"
    if sub_op == 5:  # EORI
        if size_bits == 3:
            return None
        if op == 0x0A3C:
            val = s.word() & 0xFF
            return f"EORI     #${val:02X},CCR"
        if op == 0x0A7C:
            val = s.word()
            return f"EORI     #${val:04X},SR"
        return _decode_imm_op(s, op, "EORI", size_bits)
    if sub_op == 6:  # CMPI
        if size_bits == 3:
            return None
        return _decode_imm_op(s, op, "CMPI", size_bits)

    return None


def _decode_imm_op(s: _Stream, op: int, name: str, size_bits: int) -> str:
    """Decode an immediate operation (ORI, ANDI, SUBI, ADDI, EORI, CMPI)."""
    sz = SIZE_NAMES[size_bits]
    if size_bits == 0:
        imm = s.word() & 0xFF
        imm_str = f"#${imm:02X}"
    elif size_bits == 1:
        imm = s.word()
        imm_str = f"#${imm:04X}"
    else:
        imm = s.long()
        imm_str = f"#${imm:08X}"
    ea = _decode_ea_from_word(s, op, size_bits)
    return f"{name}{sz:4s} {imm_str},{ea}"


def _decode_move(s: _Stream, op: int, top4: int) -> str:
    """Decode MOVE/MOVEA instructions (groups 01, 10, 11)."""
    size_map = {0x1: 0, 0x3: 1, 0x2: 2}  # top4 -> size (B, W, L)
    size = size_map[top4]
    sz = SIZE_NAMES[size]

    dst_reg = (op >> 9) & 7
    dst_mode = (op >> 6) & 7

    # Source EA
    pc_addr = s.addr
    src_mode = (op >> 3) & 7
    src_reg = op & 7
    src = _decode_ea(s, src_mode, src_reg, size, pc_addr)

    # MOVEA?
    if dst_mode == 1:
        # MOVEA - only word and long
        if size == 0:
            return None  # MOVEA.B is invalid
        dst = f"A{dst_reg}"
        return f"MOVEA{sz:3s} {src},{dst}"

    # Destination EA
    pc_addr2 = s.addr
    dst = _decode_ea(s, dst_mode, dst_reg, size, pc_addr2)

    return f"MOVE{sz:4s} {src},{dst}"


def _decode_0100(s: _Stream, op: int) -> str:
    """Decode group 0100: miscellaneous instructions."""

    # Check specific opcodes first
    if op == 0x4E70:
        return "RESET"
    if op == 0x4E71:
        return "NOP"
    if op == 0x4E72:
        val = s.word()
        return f"STOP     #${val:04X}"
    if op == 0x4E73:
        return "RTE"
    if op == 0x4E75:
        return "RTS"
    if op == 0x4E76:
        return "TRAPV"
    if op == 0x4E77:
        return "RTR"
    if op == 0x4AFC:
        return "ILLEGAL"

    # TRAP
    if (op & 0xFFF0) == 0x4E40:
        vec = op & 0xF
        return f"TRAP     #{vec}"

    # LINK
    if (op & 0xFFF8) == 0x4E50:
        reg = op & 7
        disp = s.sword()
        return f"LINK     A{reg},#${disp & 0xFFFF:04X}"

    # UNLK
    if (op & 0xFFF8) == 0x4E58:
        reg = op & 7
        return f"UNLK     A{reg}"

    # MOVE USP
    if (op & 0xFFF0) == 0x4E60:
        reg = op & 7
        if op & 8:
            return f"MOVE     USP,A{reg}"
        else:
            return f"MOVE     A{reg},USP"

    # SWAP
    if (op & 0xFFF8) == 0x4840:
        reg = op & 7
        return f"SWAP     D{reg}"

    # PEA
    if (op & 0xFFC0) == 0x4840:
        ea = _decode_ea_from_word(s, op, 2)
        return f"PEA      {ea}"

    # EXT
    if (op & 0xFFF8) == 0x4880:
        reg = op & 7
        return f"EXT.W    D{reg}"
    if (op & 0xFFF8) == 0x48C0:
        reg = op & 7
        return f"EXT.L    D{reg}"
    if (op & 0xFFF8) == 0x49C0:
        # EXTB.L (68020+)
        reg = op & 7
        return f"EXTB.L   D{reg}"

    # MOVEM
    if (op & 0xFB80) == 0x4880:
        sz_bit = (op >> 6) & 1  # 0=word, 1=long
        sz = ".W" if sz_bit == 0 else ".L"
        direction = (op >> 10) & 1  # 0=reg-to-mem, 1=mem-to-reg
        mask = s.word()
        mode = (op >> 3) & 7
        is_predec = (mode == 4)
        ea = _decode_ea_from_word(s, op, 1 + sz_bit)
        reglist = _movem_reglist(mask, reverse=is_predec)
        if direction == 0:
            return f"MOVEM{sz:3s} {reglist},{ea}"
        else:
            return f"MOVEM{sz:3s} {ea},{reglist}"

    # LEA
    if (op & 0xF1C0) == 0x41C0:
        areg = (op >> 9) & 7
        ea = _decode_ea_from_word(s, op, 2)
        return f"LEA      {ea},A{areg}"

    # CHK
    if (op & 0xF1C0) == 0x4180:
        dreg = (op >> 9) & 7
        ea = _decode_ea_from_word(s, op, 1)
        return f"CHK.W    {ea},D{dreg}"

    # JSR
    if (op & 0xFFC0) == 0x4E80:
        ea = _decode_ea_from_word(s, op, 2)
        return f"JSR      {ea}"

    # JMP
    if (op & 0xFFC0) == 0x4EC0:
        ea = _decode_ea_from_word(s, op, 2)
        return f"JMP      {ea}"

    # TST
    if (op & 0xFF00) == 0x4A00:
        size_bits = (op >> 6) & 3
        if size_bits == 3:
            # TAS
            ea = _decode_ea_from_word(s, op, 0)
            return f"TAS      {ea}"
        ea = _decode_ea_from_word(s, op, size_bits)
        return f"TST{SIZE_NAMES[size_bits]:5s}{ea}"

    # NEG
    if (op & 0xFF00) == 0x4400:
        size_bits = (op >> 6) & 3
        if size_bits == 3:
            return None
        ea = _decode_ea_from_word(s, op, size_bits)
        return f"NEG{SIZE_NAMES[size_bits]:5s}{ea}"

    # NEGX
    if (op & 0xFF00) == 0x4000:
        size_bits = (op >> 6) & 3
        if size_bits == 3:
            # MOVE from SR
            ea = _decode_ea_from_word(s, op, 1)
            return f"MOVE     SR,{ea}"
        ea = _decode_ea_from_word(s, op, size_bits)
        return f"NEGX{SIZE_NAMES[size_bits]:4s} {ea}"

    # CLR
    if (op & 0xFF00) == 0x4200:
        size_bits = (op >> 6) & 3
        if size_bits == 3:
            return None
        ea = _decode_ea_from_word(s, op, size_bits)
        return f"CLR{SIZE_NAMES[size_bits]:5s}{ea}"

    # NOT
    if (op & 0xFF00) == 0x4600:
        size_bits = (op >> 6) & 3
        if size_bits == 3:
            # MOVE to SR
            ea = _decode_ea_from_word(s, op, 1)
            return f"MOVE     {ea},SR"
        ea = _decode_ea_from_word(s, op, size_bits)
        return f"NOT{SIZE_NAMES[size_bits]:5s}{ea}"

    # NBCD
    if (op & 0xFFC0) == 0x4800:
        ea = _decode_ea_from_word(s, op, 0)
        return f"NBCD     {ea}"

    # MOVE to/from CCR
    if (op & 0xFFC0) == 0x44C0:
        ea = _decode_ea_from_word(s, op, 1)
        return f"MOVE     {ea},CCR"
    if (op & 0xFFC0) == 0x46C0:
        ea = _decode_ea_from_word(s, op, 1)
        return f"MOVE     {ea},SR"
    if (op & 0xFFC0) == 0x40C0:
        ea = _decode_ea_from_word(s, op, 1)
        return f"MOVE     SR,{ea}"

    return None


def _decode_0101(s: _Stream, op: int) -> str:
    """Decode group 0101: ADDQ, SUBQ, Scc, DBcc."""
    size_bits = (op >> 6) & 3

    if size_bits == 3:
        # Scc or DBcc
        cond = (op >> 8) & 0xF
        mode = (op >> 3) & 7
        reg = op & 7

        if mode == 1:
            # DBcc
            disp = s.sword()
            target = s.addr - 2 + disp
            cc = CC_NAMES[cond]
            return f"DB{cc:6s} D{reg},${target:08X}"

        # Scc
        ea = _decode_ea_from_word(s, op, 0)
        cc = CC_NAMES[cond]
        return f"S{cc:7s} {ea}"

    # ADDQ / SUBQ
    data = (op >> 9) & 7
    if data == 0:
        data = 8
    sz = SIZE_NAMES[size_bits]
    ea = _decode_ea_from_word(s, op, size_bits)
    if op & 0x0100:
        return f"SUBQ{sz:4s} #{data},{ea}"
    else:
        return f"ADDQ{sz:4s} #{data},{ea}"


def _decode_branch(s: _Stream, op: int) -> str:
    """Decode group 0110: BRA, BSR, Bcc."""
    cond = (op >> 8) & 0xF
    disp8 = op & 0xFF

    pc_of_ext = s.addr  # address after opword

    if disp8 == 0:
        disp = s.sword()
        target = pc_of_ext + disp
        sz = ".W"
    elif disp8 == 0xFF:
        disp = s.slong()
        target = pc_of_ext + disp
        sz = ".L"
    else:
        if disp8 >= 0x80:
            disp8 -= 0x100
        target = pc_of_ext + disp8
        sz = ".S"

    if cond == 0:
        return f"BRA{sz:5s} ${target:08X}"
    if cond == 1:
        return f"BSR{sz:5s} ${target:08X}"
    cc = CC_NAMES[cond]
    return f"B{cc}{sz:5s}${target:08X}"


def _decode_1000(s: _Stream, op: int) -> str:
    """Decode group 1000: OR, DIVU, DIVS, SBCD."""
    dreg = (op >> 9) & 7
    opmode = (op >> 6) & 7

    if opmode == 3:
        # DIVU
        ea = _decode_ea_from_word(s, op, 1)
        return f"DIVU.W   {ea},D{dreg}"
    if opmode == 7:
        # DIVS
        ea = _decode_ea_from_word(s, op, 1)
        return f"DIVS.W   {ea},D{dreg}"

    # SBCD
    if opmode == 4 and ((op >> 3) & 7) in (0, 1):
        rm = (op >> 3) & 1
        ry = op & 7
        if rm:
            return f"SBCD     -(A{ry}),-(A{dreg})"
        else:
            return f"SBCD     D{ry},D{dreg}"

    # OR
    size_bits = opmode & 3
    if size_bits == 3:
        return None
    sz = SIZE_NAMES[size_bits]
    ea = _decode_ea_from_word(s, op, size_bits)
    if opmode < 3:
        return f"OR{sz:6s}{ea},D{dreg}"
    else:
        return f"OR{sz:6s}D{dreg},{ea}"


def _decode_addsub(s: _Stream, op: int, name: str) -> str:
    """Decode groups 1001 (SUB) and 1101 (ADD)."""
    dreg = (op >> 9) & 7
    opmode = (op >> 6) & 7

    # ADDA/SUBA
    if opmode == 3:
        ea = _decode_ea_from_word(s, op, 1)
        return f"{name}A.W   {ea},A{dreg}"
    if opmode == 7:
        ea = _decode_ea_from_word(s, op, 2)
        return f"{name}A.L   {ea},A{dreg}"

    # ADDX/SUBX
    size_bits = opmode & 3
    if opmode in (4, 5, 6):
        rm = (op >> 3) & 1
        ry = op & 7
        if size_bits == 3:
            return None
        sz = SIZE_NAMES[size_bits]
        xname = f"{name}X"
        mode = (op >> 3) & 7
        if mode == 0:
            return f"{xname}{sz:4s} D{ry},D{dreg}"
        elif mode == 1:
            return f"{xname}{sz:4s} -(A{ry}),-(A{dreg})"

    # Regular ADD/SUB
    if size_bits == 3:
        return None
    sz = SIZE_NAMES[size_bits]
    ea = _decode_ea_from_word(s, op, size_bits)
    if opmode < 3:
        return f"{name}{sz:6s}{ea},D{dreg}"
    else:
        return f"{name}{sz:6s}D{dreg},{ea}"


def _decode_1011(s: _Stream, op: int) -> str:
    """Decode group 1011: CMP, CMPA, CMPM, EOR."""
    dreg = (op >> 9) & 7
    opmode = (op >> 6) & 7

    # CMPA
    if opmode == 3:
        ea = _decode_ea_from_word(s, op, 1)
        return f"CMPA.W   {ea},A{dreg}"
    if opmode == 7:
        ea = _decode_ea_from_word(s, op, 2)
        return f"CMPA.L   {ea},A{dreg}"

    # CMP (Dn direction, opmode 0-2)
    if opmode < 3:
        size_bits = opmode
        ea = _decode_ea_from_word(s, op, size_bits)
        return f"CMP{SIZE_NAMES[size_bits]:5s}{ea},D{dreg}"

    # CMPM or EOR (opmode 4-6)
    size_bits = opmode - 4
    if size_bits > 2:
        return None
    sz = SIZE_NAMES[size_bits]
    mode = (op >> 3) & 7
    ry = op & 7

    if mode == 1:
        # CMPM
        return f"CMPM{sz:4s} (A{ry})+,(A{dreg})+"

    # EOR
    ea = _decode_ea_from_word(s, op, size_bits)
    return f"EOR{sz:5s}D{dreg},{ea}"


def _decode_1100(s: _Stream, op: int) -> str:
    """Decode group 1100: AND, MUL, ABCD, EXG."""
    dreg = (op >> 9) & 7
    opmode = (op >> 6) & 7

    # MULU
    if opmode == 3:
        ea = _decode_ea_from_word(s, op, 1)
        return f"MULU.W   {ea},D{dreg}"
    # MULS
    if opmode == 7:
        ea = _decode_ea_from_word(s, op, 1)
        return f"MULS.W   {ea},D{dreg}"

    # ABCD
    if opmode == 4 and ((op >> 3) & 7) in (0, 1):
        rm = (op >> 3) & 1
        ry = op & 7
        if rm:
            return f"ABCD     -(A{ry}),-(A{dreg})"
        else:
            return f"ABCD     D{ry},D{dreg}"

    # EXG
    if opmode == 5 and ((op >> 3) & 7) == 0:
        ry = op & 7
        return f"EXG      D{dreg},D{ry}"
    if opmode == 5 and ((op >> 3) & 7) == 1:
        ry = op & 7
        return f"EXG      A{dreg},A{ry}"
    if opmode == 6 and ((op >> 3) & 7) == 1:
        ry = op & 7
        return f"EXG      D{dreg},A{ry}"

    # AND
    size_bits = opmode & 3
    if size_bits == 3:
        return None
    sz = SIZE_NAMES[size_bits]
    ea = _decode_ea_from_word(s, op, size_bits)
    if opmode < 3:
        return f"AND{sz:5s}{ea},D{dreg}"
    else:
        return f"AND{sz:5s}D{dreg},{ea}"


def _decode_shift(s: _Stream, op: int) -> str:
    """Decode group 1110: shifts and rotates."""
    size_bits = (op >> 6) & 3

    if size_bits == 3:
        # Memory shift/rotate (word only)
        direction = "L" if op & 0x0100 else "R"
        shift_type = (op >> 9) & 3
        names = ["AS", "LS", "ROX", "RO"]
        name = names[shift_type] + direction
        ea = _decode_ea_from_word(s, op, 1)
        return f"{name:9s}{ea}"

    # Register shift/rotate
    dreg = op & 7
    direction = "L" if op & 0x0100 else "R"
    shift_type = (op >> 3) & 3
    names = ["AS", "LS", "ROX", "RO"]
    name = names[shift_type] + direction
    sz = SIZE_NAMES[size_bits]

    if op & 0x0020:
        # Count in register
        creg = (op >> 9) & 7
        return f"{name}{sz:5s}D{creg},D{dreg}"
    else:
        count = (op >> 9) & 7
        if count == 0:
            count = 8
        return f"{name}{sz:5s}#{count},D{dreg}"


# ─── Public API ───

def disassemble(data: bytes, base_addr: int, count: int = 20) -> list[tuple[int, str, str]]:
    """Disassemble 68k machine code.

    Args:
        data: Raw bytes of machine code (big-endian 68k).
        base_addr: Address in Amiga memory where this code lives.
        count: Maximum number of instructions to decode.

    Returns:
        List of (address, hex_bytes, mnemonic_string) tuples.
    """
    s = _Stream(data, base_addr)
    result: list[tuple[int, str, str]] = []

    for _ in range(count):
        if not s.has(2):
            break

        addr = s.addr
        start = s.pos

        try:
            mnemonic = _decode_one(s)
        except _EndOfData:
            # Ran out of data mid-instruction - emit what we have as DC.W
            s.pos = start
            if s.has(2):
                w = s.word()
                hexstr = f"{w:04X}"
                result.append((addr, hexstr, f"DC.W     ${w:04X}"))
            break

        if mnemonic is None:
            # Unrecognized - rewind and emit DC.W
            s.pos = start
            w = s.word()
            hexstr = f"{w:04X}"
            result.append((addr, hexstr, f"DC.W     ${w:04X}"))
        else:
            hexstr = s.hex_between(start, s.pos)
            result.append((addr, hexstr, mnemonic))

    return result


def disassemble_hex(hex_data: str, base_addr: int, count: int = 20) -> list[tuple[int, str, str]]:
    """Disassemble from a hex string (as returned by INSPECT protocol)."""
    data = bytes.fromhex(hex_data)
    return disassemble(data, base_addr, count)


def format_listing(instructions: list[tuple[int, str, str]]) -> str:
    """Format disassembly output as a readable listing."""
    lines: list[str] = []
    for addr, hexbytes, mnemonic in instructions:
        # Pad hex to a consistent width (max instruction = 10 bytes = 20 hex chars)
        hex_padded = hexbytes.ljust(20)
        lines.append(f"  {addr:08X}  {hex_padded}  {mnemonic}")
    return "\n".join(lines)
