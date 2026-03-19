---
name: debugger_progress
description: Remote debugger implementation status and known issues as of 2026-03-18
type: project
---

## Debugger Status (Phase 2 + Trace)

### Working
- Attach/detach to tasks by name (FindTask + cli_Module code base)
- Software breakpoints (TRAP #15) with CacheClearU for 68060
- Trace exception handler for stepping past breakpoints
- Signal-based pause/resume (SIGBREAKF_CTRL_E/F) - zero blocking
- Break button (manual pause)
- Continue works reliably (10/10 rounds in automated tests)
- Source view with gutter breakpoints
- Symbol loading from deployed binary (native m68k-amigaos-nm/objdump)
- Code base relocation (cli_Module for CLI programs)
- Variable inspection (globals via GETVAR)
- Web UI debugger tab with source, registers, breakpoints, variables
- GDB RSP server on port 2159

### Known Issues
1. **First-hit-wins not working**: with 2 BPs (168+172), always reports 172 even though 168 comes first in execution. The `tst.w _dbg_trap_hit / bne .Lskip_save` asm check may not work because TRAP handler trashes D0 for the CacheClearU call before the check.
2. **Ball frozen after detach**: CTRL_F signal may arrive before target enters Wait(CTRL_F) in ab_poll().
3. **Local stack variables**: STABS LSYM parsing added to symbols.py but display incomplete.

### Architecture
- TRAP #15 handler: saves regs, unpatches BP, sets T1 bit, RTEs to original instruction
- Trace handler: fires after one instruction, repatches ALL BPs, RTEs (target continues)
- dbg_poll(): checks flag, sends DBGSTOP over serial, Signal(CTRL_E) to pause target
- ab_poll(): checks CTRL_E, enters Wait(CTRL_F) to pause
- Continue: Signal(CTRL_F) to resume target
- No IPC messages for debug flow (eliminated all blocking)

**Why:** Eliminates IPC blocking that caused timeouts, memory leaks, and daemon unresponsiveness.
**How to apply:** When fixing the first-hit-wins issue, check that register D0 isn't trashed before the `tst.w _dbg_trap_hit` check in the asm handler. The handler saves D0 to dbg_saved_regs but then uses D0 for the BP search loop.
