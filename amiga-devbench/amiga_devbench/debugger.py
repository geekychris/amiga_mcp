"""Host-side debugger state management for Amiga remote debugging."""

from __future__ import annotations

import logging
from dataclasses import dataclass, field
from typing import Any

logger = logging.getLogger(__name__)

# 68k SR flag bits
SR_FLAGS = {
    15: "T",   # Trace
    13: "S",   # Supervisor
    12: "M",   # Master/Interrupt
    4:  "X",   # Extend
    3:  "N",   # Negative
    2:  "Z",   # Zero
    1:  "V",   # Overflow
    0:  "C",   # Carry
}

REG_NAMES_D = [f"D{i}" for i in range(8)]
REG_NAMES_A = [f"A{i}" for i in range(8)]


def decode_sr_flags(sr: int) -> str:
    """Decode SR value into flag string like 'T S -- X N Z V C'."""
    parts = []
    for bit, name in sorted(SR_FLAGS.items(), reverse=True):
        if sr & (1 << bit):
            parts.append(name)
        else:
            parts.append("-")
    return " ".join(parts)


@dataclass
class Breakpoint:
    """A breakpoint set on the Amiga."""
    id: int
    address: int
    enabled: bool
    original_word: int
    symbol: str | None = None
    source_file: str | None = None
    source_line: int | None = None


@dataclass
class BacktraceFrame:
    """A single frame in a backtrace."""
    depth: int
    pc: int
    symbol: str | None = None
    source_file: str | None = None
    source_line: int | None = None


@dataclass
class DebuggerState:
    """Current state of the remote debugger."""
    attached: bool = False
    stopped: bool = False
    target_name: str = ""
    pc: int = 0
    sr: int = 0
    stop_reason: str = ""
    code_base: int = 0  # Code segment load address (for relocation)

    # Registers: D0-D7 (0-7), A0-A7 (8-15), PC (16), SR (17)
    regs: list[int] = field(default_factory=lambda: [0] * 18)
    prev_regs: list[int] = field(default_factory=lambda: [0] * 18)

    breakpoints: list[Breakpoint] = field(default_factory=list)
    backtrace: list[BacktraceFrame] = field(default_factory=list)

    warnings: list[str] = field(default_factory=list)

    def reset(self) -> None:
        """Reset all state."""
        self.attached = False
        self.stopped = False
        self.target_name = ""
        self.pc = 0
        self.sr = 0
        self.stop_reason = ""
        self.regs = [0] * 18
        self.prev_regs = [0] * 18
        self.breakpoints = []
        self.backtrace = []
        self.warnings = []
        self.code_base = 0

    def update_from_stop(self, msg: dict[str, Any]) -> None:
        """Update state from a DBGSTOP message."""
        self.stopped = True
        self.stop_reason = msg.get("reason", "unknown")
        self.pc = msg.get("pc", 0)
        self.sr = msg.get("sr", 0)

        # Save previous registers for change highlighting
        self.prev_regs = list(self.regs)

        # Update registers
        dregs = msg.get("dataRegs", [])
        aregs = msg.get("addrRegs", [])
        for i, v in enumerate(dregs[:8]):
            self.regs[i] = v
        for i, v in enumerate(aregs[:8]):
            self.regs[8 + i] = v
        self.regs[16] = self.pc
        self.regs[17] = self.sr

        self.warnings = msg.get("warnings", [])

    def update_from_regs(self, msg: dict[str, Any]) -> None:
        """Update state from a DBGREGS message."""
        self.prev_regs = list(self.regs)

        dregs = msg.get("dataRegs", [])
        aregs = msg.get("addrRegs", [])
        for i, v in enumerate(dregs[:8]):
            self.regs[i] = v
        for i, v in enumerate(aregs[:8]):
            self.regs[8 + i] = v
        self.regs[16] = msg.get("pc", self.pc)
        self.regs[17] = msg.get("sr", self.sr)
        self.pc = self.regs[16]
        self.sr = self.regs[17]

    def update_from_state(self, msg: dict[str, Any]) -> None:
        """Update from a DBGSTATE message."""
        self.attached = msg.get("attached", False)
        self.stopped = msg.get("stopped", False)
        self.target_name = msg.get("targetName", "")
        self.pc = msg.get("pc", 0)
        if msg.get("codeBase"):
            self.code_base = msg["codeBase"]

    def update_breakpoints(self, bps: list[dict[str, Any]]) -> None:
        """Update breakpoint list from BPLIST message."""
        self.breakpoints = []
        for bp in bps:
            self.breakpoints.append(Breakpoint(
                id=bp.get("id", 0),
                address=bp.get("address", 0),
                enabled=bp.get("enabled", False),
                original_word=bp.get("originalWord", 0),
            ))

    def update_backtrace(self, frames: list[dict[str, Any]]) -> None:
        """Update backtrace from DBGBT message."""
        self.backtrace = []
        for i, f in enumerate(frames):
            self.backtrace.append(BacktraceFrame(
                depth=i,
                pc=f.get("pc", 0),
                symbol=f.get("symbol"),
                source_file=f.get("file"),
                source_line=f.get("line"),
            ))

    def changed_regs(self) -> list[int]:
        """Return indices of registers that changed since last stop."""
        return [i for i in range(18) if self.regs[i] != self.prev_regs[i]]

    def format_regs(self) -> str:
        """Format registers for display."""
        changed = set(self.changed_regs())
        lines = []

        # Data registers
        d_parts = []
        for i in range(8):
            marker = "*" if i in changed else " "
            d_parts.append(f"D{i}={self.regs[i]:08X}{marker}")
        lines.append("  ".join(d_parts[:4]))
        lines.append("  ".join(d_parts[4:]))

        # Address registers
        a_parts = []
        for i in range(8):
            idx = 8 + i
            marker = "*" if idx in changed else " "
            a_parts.append(f"A{i}={self.regs[idx]:08X}{marker}")
        lines.append("  ".join(a_parts[:4]))
        lines.append("  ".join(a_parts[4:]))

        # PC and SR
        pc_marker = "*" if 16 in changed else " "
        sr_marker = "*" if 17 in changed else " "
        sr_val = self.regs[17]
        flags = decode_sr_flags(sr_val)
        lines.append(f"PC={self.regs[16]:08X}{pc_marker}  SR={sr_val:04X}{sr_marker}  [{flags}]")

        return "\n".join(lines)

    def format_backtrace(self) -> str:
        """Format backtrace for display."""
        if not self.backtrace:
            return "No backtrace available"
        lines = []
        for f in self.backtrace:
            sym = f" <{f.symbol}>" if f.symbol else ""
            src = f" at {f.source_file}:{f.source_line}" if f.source_file else ""
            marker = " <<" if f.depth == 0 else ""
            lines.append(f"  #{f.depth}  0x{f.pc:08X}{sym}{src}{marker}")
        return "\n".join(lines)

    def to_dict(self) -> dict[str, Any]:
        """Serialize state for JSON/API responses."""
        return {
            "attached": self.attached,
            "stopped": self.stopped,
            "targetName": self.target_name,
            "pc": self.pc,
            "sr": self.sr,
            "srFlags": decode_sr_flags(self.sr),
            "stopReason": self.stop_reason,
            "regs": {
                "data": [self.regs[i] for i in range(8)],
                "addr": [self.regs[8 + i] for i in range(8)],
                "pc": self.regs[16],
                "sr": self.regs[17],
            },
            "changedRegs": self.changed_regs(),
            "breakpoints": [
                {
                    "id": bp.id,
                    "address": bp.address,
                    "addressHex": f"{bp.address:08X}",
                    "enabled": bp.enabled,
                    "symbol": bp.symbol,
                    "file": bp.source_file,
                    "line": bp.source_line,
                }
                for bp in self.breakpoints
            ],
            "backtrace": [
                {
                    "depth": f.depth,
                    "pc": f.pc,
                    "pcHex": f"{f.pc:08X}",
                    "symbol": f.symbol,
                    "file": f.source_file,
                    "line": f.source_line,
                }
                for f in self.backtrace
            ],
            "warnings": self.warnings,
            "codeBase": self.code_base,
            "codeBaseHex": f"{self.code_base:08X}" if self.code_base else "",
        }


def annotate_with_symbols(state: DebuggerState, symbol_table: Any) -> None:
    """Annotate breakpoints and backtrace frames with symbol info."""
    if symbol_table is None:
        return

    for bp in state.breakpoints:
        sym = symbol_table.lookup_address(bp.address)
        if sym:
            bp.symbol = sym
        src = symbol_table.lookup_source_line(bp.address)
        if src:
            bp.source_file, bp.source_line = src

    for frame in state.backtrace:
        sym = symbol_table.lookup_address(frame.pc)
        if sym:
            frame.symbol = sym
        src = symbol_table.lookup_source_line(frame.pc)
        if src:
            frame.source_file, frame.source_line = src
