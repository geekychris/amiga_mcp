"""Amiga copper list decoder.

Decodes raw copper list data into human-readable instructions.
"""

from __future__ import annotations

import logging
from typing import Any

logger = logging.getLogger(__name__)

# Custom chip register names (offset from $DFF000)
# Only the most common ones - add more as needed
CUSTOM_REGS: dict[int, str] = {
    0x0002: "DMACONR",
    0x0004: "VPOSR",
    0x0006: "VHPOSR",
    0x000A: "JOY0DAT",
    0x000C: "JOY1DAT",
    0x0010: "ADKCONR",
    0x0012: "POT0DAT",
    0x001C: "INTENA",
    0x001E: "INTREQ",
    0x0020: "DSKPTH",
    0x0022: "DSKPTL",
    0x0024: "DSKLEN",
    0x002A: "VPOSW",
    0x002C: "VHPOSW",
    0x002E: "COPCON",
    0x0040: "BLTCON0",
    0x0042: "BLTCON1",
    0x0044: "BLTAFWM",
    0x0046: "BLTALWM",
    0x0048: "BLTCPTH",
    0x004A: "BLTCPTL",
    0x004C: "BLTBPTH",
    0x004E: "BLTBPTL",
    0x0050: "BLTAPTH",
    0x0052: "BLTAPTL",
    0x0054: "BLTDPTH",
    0x0056: "BLTDPTL",
    0x0058: "BLTSIZE",
    0x0060: "BLTCMOD",
    0x0062: "BLTBMOD",
    0x0064: "BLTAMOD",
    0x0066: "BLTDMOD",
    0x0080: "COP1LCH",
    0x0082: "COP1LCL",
    0x0084: "COP2LCH",
    0x0086: "COP2LCL",
    0x0088: "COPJMP1",
    0x008A: "COPJMP2",
    0x008E: "DIWSTRT",
    0x0090: "DIWSTOP",
    0x0092: "DDFSTRT",
    0x0094: "DDFSTOP",
    0x0096: "DMACON",
    0x009A: "INTENA",
    0x009C: "INTREQ",
    0x009E: "ADKCON",
    0x00A0: "AUD0LCH",
    0x00A2: "AUD0LCL",
    0x00A4: "AUD0LEN",
    0x00A6: "AUD0PER",
    0x00A8: "AUD0VOL",
    0x00AA: "AUD0DAT",
    0x00B0: "AUD1LCH",
    0x00B2: "AUD1LCL",
    0x00C0: "AUD2LCH",
    0x00C2: "AUD2LCL",
    0x00D0: "AUD3LCH",
    0x00D2: "AUD3LCL",
    0x00E0: "BPL1PTH",
    0x00E2: "BPL1PTL",
    0x00E4: "BPL2PTH",
    0x00E6: "BPL2PTL",
    0x00E8: "BPL3PTH",
    0x00EA: "BPL3PTL",
    0x00EC: "BPL4PTH",
    0x00EE: "BPL4PTL",
    0x00F0: "BPL5PTH",
    0x00F2: "BPL5PTL",
    0x00F4: "BPL6PTH",
    0x00F6: "BPL6PTL",
    0x0100: "BPLCON0",
    0x0102: "BPLCON1",
    0x0104: "BPLCON2",
    0x0106: "BPLCON3",
    0x0108: "BPL1MOD",
    0x010A: "BPL2MOD",
    0x0110: "BPL1DAT",
    0x0112: "BPL2DAT",
    0x0114: "BPL3DAT",
    0x0116: "BPL4DAT",
    0x0118: "BPL5DAT",
    0x011A: "BPL6DAT",
    0x0120: "SPR0PTH",
    0x0122: "SPR0PTL",
    0x0124: "SPR1PTH",
    0x0126: "SPR1PTL",
    0x0128: "SPR2PTH",
    0x012A: "SPR2PTL",
    0x012C: "SPR3PTH",
    0x012E: "SPR3PTL",
    0x0130: "SPR4PTH",
    0x0132: "SPR4PTL",
    0x0134: "SPR5PTH",
    0x0136: "SPR5PTL",
    0x0138: "SPR6PTH",
    0x013A: "SPR6PTL",
    0x013C: "SPR7PTH",
    0x013E: "SPR7PTL",
    0x0140: "SPR0POS",
    0x0142: "SPR0CTL",
    0x0144: "SPR0DATA",
    0x0146: "SPR0DATB",
    0x0148: "SPR1POS",
    0x014A: "SPR1CTL",
    0x014C: "SPR1DATA",
    0x014E: "SPR1DATB",
    0x0150: "SPR2POS",
    0x0152: "SPR2CTL",
    0x0154: "SPR2DATA",
    0x0156: "SPR2DATB",
    0x0158: "SPR3POS",
    0x015A: "SPR3CTL",
    0x015C: "SPR3DATA",
    0x015E: "SPR3DATB",
    0x0160: "SPR4POS",
    0x0162: "SPR4CTL",
    0x0164: "SPR4DATA",
    0x0166: "SPR4DATB",
    0x0168: "SPR5POS",
    0x016A: "SPR5CTL",
    0x016C: "SPR5DATA",
    0x016E: "SPR5DATB",
    0x0170: "SPR6POS",
    0x0172: "SPR6CTL",
    0x0174: "SPR6DATA",
    0x0176: "SPR6DATB",
    0x0178: "SPR7POS",
    0x017A: "SPR7CTL",
    0x017C: "SPR7DATA",
    0x017E: "SPR7DATB",
}

# Color registers COLOR00-COLOR31
for i in range(32):
    CUSTOM_REGS[0x0180 + i * 2] = f"COLOR{i:02d}"


def reg_name(offset: int) -> str:
    """Get register name for a custom chip offset."""
    if offset in CUSTOM_REGS:
        return CUSTOM_REGS[offset]
    return f"REG_{offset:04X}"


def decode_copper_instruction(word1: int, word2: int) -> dict[str, Any]:
    """Decode a single copper instruction (2 words).

    Returns a dict with instruction details.
    """
    # Check for end-of-list marker
    if word1 == 0xFFFF and word2 == 0xFFFE:
        return {"type": "END", "text": "END (WAIT $FFFF,$FFFE)"}

    is_move = (word1 & 1) == 0 and (word2 & 1) == 0

    if (word1 & 1) == 0:
        # MOVE instruction
        register = word1 & 0x01FE  # bits 8-1
        value = word2
        rname = reg_name(register)
        addr = 0xDFF000 + register
        text = f"MOVE #{value:04X}, {rname} (${addr:06X})"
        return {
            "type": "MOVE",
            "register": register,
            "regName": rname,
            "value": value,
            "address": addr,
            "text": text,
        }
    else:
        # WAIT or SKIP
        is_skip = (word2 & 1) == 1

        vpos = (word1 >> 8) & 0xFF
        hpos = (word1 >> 1) & 0x7F
        ve_mask = (word2 >> 8) & 0x7F
        he_mask = (word2 >> 1) & 0x7F
        bfd = (word2 >> 15) & 1

        if is_skip:
            text = f"SKIP VP>={vpos} HP>={hpos} (VE=${ve_mask:02X} HE=${he_mask:02X} BFD={bfd})"
            return {
                "type": "SKIP",
                "vpos": vpos,
                "hpos": hpos,
                "veMask": ve_mask,
                "heMask": he_mask,
                "bfd": bfd,
                "text": text,
            }
        else:
            text = f"WAIT VP>={vpos} HP>={hpos} (VE=${ve_mask:02X} HE=${he_mask:02X} BFD={bfd})"
            return {
                "type": "WAIT",
                "vpos": vpos,
                "hpos": hpos,
                "veMask": ve_mask,
                "heMask": he_mask,
                "bfd": bfd,
                "text": text,
            }


def decode_copper_list(hex_data: str, base_addr: int = 0) -> list[dict[str, Any]]:
    """Decode a full copper list from hex data.

    Args:
        hex_data: Hex-encoded copper list data.
        base_addr: Base address of the copper list in memory.

    Returns:
        List of decoded instruction dicts.
    """
    instructions = []
    data = bytes.fromhex(hex_data)

    for i in range(0, len(data) - 3, 4):
        word1 = (data[i] << 8) | data[i + 1]
        word2 = (data[i + 2] << 8) | data[i + 3]
        addr = base_addr + i

        inst = decode_copper_instruction(word1, word2)
        inst["offset"] = i
        inst["address"] = addr
        inst["raw"] = f"{word1:04X} {word2:04X}"
        instructions.append(inst)

        if inst["type"] == "END":
            break

    return instructions


def format_copper_list(instructions: list[dict[str, Any]]) -> str:
    """Format decoded copper instructions into a readable string."""
    lines = [f"Copper List ({len(instructions)} instructions):"]
    lines.append(f"{'Addr':>8s}  {'Raw':>9s}  Instruction")
    lines.append("-" * 60)

    for inst in instructions:
        addr = inst.get("address", 0)
        raw = inst.get("raw", "")
        text = inst.get("text", "?")
        lines.append(f"{addr:08X}  {raw}  {text}")

    return "\n".join(lines)
