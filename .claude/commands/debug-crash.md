# Debug an Amiga Crash

Investigate why an Amiga program crashed. Collects crash info, recent logs, process state, and memory diagnostics.

## Arguments
- $ARGUMENTS: Context about the crash (e.g., "game froze after landing", "guru meditation on startup"). If empty, do a general investigation.

## Steps

1. **Check Amiga status**:
   Use `mcp__amiga-dev__amiga_ping` to verify the Amiga is responsive.

2. **Get last crash info**:
   Use `mcp__amiga-dev__amiga_last_crash` to get details about the most recent crash (if available).

3. **Check system info**:
   Use `mcp__amiga-dev__amiga_sysinfo` to check free memory, CPU load.

4. **List running processes**:
   Use `mcp__amiga-dev__amiga_proc_list` to see what's running.

5. **Check bridge logs**:
   Use `mcp__amiga-dev__amiga_watch_logs` with a short duration to see recent messages.

6. **Take a screenshot**:
   Use `mcp__amiga-dev__amiga_screenshot` to see the current screen state.

7. **Analyze** and report findings:
   - If out of memory: check chip/fast allocation
   - If Guru Meditation: decode the error code (format: `0000000X.YYYYYYYY` where X=error type, Y=address)
   - If no crash but frozen: check for Forbid()/Disable() imbalance or WaitPort() deadlock
   - If visual corruption: check double-buffer synchronization or blitter contention

## Common Amiga crash causes
- **Buffer overflow** — No memory protection, overwrites random memory
- **Forbid()/Permit() imbalance** — Freezes multitasking
- **WaitPort() on dead client** — Blocks forever
- **TypeOfMem() failure** — Writing to non-RAM address
- **Stack overflow** — Default 4KB stack is tiny, use `-stacksize` or launch with `stack 16000`
- **Missing library** — OpenLibrary() returns NULL, code dereferences it
- **Chip RAM exhaustion** — Audio/graphics samples must be in chip RAM
