<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Chris Collins <chris@hitorro.com> -->

# Choosing a debugger

[Documentation index](README.md) · [Install DevBench](quickstart.md)

## Three debuggers: when to use which

DevBench exposes three independent debuggers, each with its own UI tab, MCP tool family, and HTTP API: the bridge debugger, and native debuggers for patched FS-UAE and for WinUAE. They're complementary — pick the one that matches what you're trying to inspect.

### Bridge debugger — "what is my app doing?"

**UI tab:** `Debugger` &nbsp;&nbsp; **MCP tools:** `amiga_debug_attach`, `amiga_set_breakpoint`, `amiga_step`, … &nbsp;&nbsp; **API prefix:** `/api/debugger/*`

Talks to the `amiga-bridge` daemon running in Amiga RAM, over the serial link (TCP or PTY). Source-level: knows about your symbols, lines, locals, call stack. Drives one of your processes via `Launch & Attach`.

**Best for:**
- Stepping through your own C functions line by line
- Reading local variables and walking the call stack of your task
- Breaking at function names or source lines
- Watching variables you registered with `ab_register_var()`

**Limitations:**
- Requires the bridge daemon to be running and your app to be attached
- Bridge dies if the OS hangs / crashes / hasn't booted yet
- Can't see inside Kickstart ROM or other tasks
- No hardware-level watchpoints (limited to bridge-instrumented stops)

### FS-UAE native debugger — "what is the emulator doing?"

**UI tab:** `FS-UAE` &nbsp;&nbsp; **MCP prefix:** `amiga_fsuae_*` &nbsp;&nbsp; **API prefix:** `/api/fsuae/*`

Talks to the patched [fs-uae build](https://github.com/geekychris/fsuae_remote_patch) over HTTP. Emulator-level: pauses the 68k CPU itself, with breakpoints and address-specific memory watchpoints. No bridge daemon needed.

**Best for:**
- "Who keeps clobbering `$DFF180`?" — hardware-style watchpoints with R/W/I and mustchange
- Inspecting Kickstart ROM, exception vectors, CIA registers
- Pre-boot debugging (set BP at reset vector, single-step from instruction zero)
- Debugging when the OS has hung — bridge is dead but fs-uae is fine
- Decoding chipset state (DMACON / INTENA / BPLCONx / copper pointers / beam pos)

**Limitations:**
- Requires the patched fs-uae build (Linux + macOS only; Windows compiles to a no-op stub)
- No source-line mapping — addresses only
- Stock fs-uae from Homebrew/apt doesn't expose this API; the tab stays hidden

### Side-by-side

| Capability | Bridge | FS-UAE | WinUAE |
|---|---|---|---|
| Pause / step / continue | ✓ | ✓ | ✓ (plus step over, step out, range stepping) |
| Source-level (file:line, locals, backtrace) | ✓ | — | ✓ with DWARF from a linked ELF file |
| Function-name breakpoints (via symbol load) | ✓ | by address only | ✓ |
| Hardware-style watchpoints (R/W/I, mustchange) | — | ✓ | ✓ (plus DMA sources and an access log) |
| Read Kickstart ROM | — | ✓ | ✓ |
| Read/write 68k regs (D0-D7, A0-A7, PC, SR, USP, ISP) | partial (task ctx) | ✓ (live CPU) | ✓ (live CPU, plus MSP) |
| Memory map (chip/fast/ROM/IO/unmapped) | — | ✓ | ✓ |
| Chipset register snapshot | partial (inspector) | ✓ | ✓ (plus Copper lists) |
| State snapshot save/load (.uss) | — | ✓ | ✓ (saved at the next frame start) |
| Exec tasks, loaded segments, program-entry and exception stops | — | — | ✓ |
| Works when OS has crashed | — | ✓ | ✓ |
| Works without bridge running | — | ✓ | ✓ |
| Works on Windows | ✓ | — (patched build is mac/linux only) | ✓ (WinUAE 6.1.0 or newer) |

You can mix them: set a hardware watchpoint with a native debugger to find *where* something happens, then attach the bridge debugger to step through the surrounding source. The bridge debugger runs side-by-side with either native debugger without interfering.

---

## WinUAE native debugging

The additional [WinUAE backend](winuae-debugger.md) provides native CPU, memory,
breakpoint, watch, chipset, checkpoint, input, process and exception controls
through the WinUAE tab and MCP, covering everything the FS-UAE backend offers.
It uses WinUAE's GDB server directly and works without a running guest bridge.
