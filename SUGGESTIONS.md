# Amiga DevBench — Feature Suggestions

Ideas for extending the development environment, organized by impact and feasibility.

---

## High-Impact, Feasible Features

### 1. Breakpoints & Single-Step
The single biggest missing debugging capability.
- Patch code in memory with `TRAP #0` (or `ILLEGAL`) to set breakpoints
- Bridge daemon installs a trap handler that captures all registers + stack, sends them to host, then waits for a "continue" or "step" command
- Single-step via the 68020's trace bit in the SR (set T0 bit, RTE, catch the next trace exception)
- Web UI shows source-context disassembly with clickable gutter to set/clear breakpoints
- Combined with STABS debug info from the cross-compiler, you'd get source-level breakpoints

### 2. Symbol Table / Debug Info Loading ✅ Implemented
Parse STABS/DWARF debug info from cross-compiled binaries on the host:
- Disassembler shows function names and source lines instead of raw addresses
- Crash reports show symbolic stack traces
- Memory inspector can show struct field names
- Breakpoints can be set by function name or source line
- `m68k-amigaos-objdump --stabs` extracts this, or parse ELF/a.out directly in Python

### 3. Audio / Paula Inspector ✅ Implemented
Sound is central to Amiga development. Paula has 4 DMA channels:
- Read audio channel registers: period, volume, sample pointer, length for each channel
- Visualize waveforms currently playing (read sample data from chip RAM)
- Show which channels are active (DMACONR bits)
- Log audio interrupts
- Could stream audio samples to the host for playback

### 4. Blitter Monitor
The blitter is the most complex and crash-prone part of Amiga graphics programming:
- Intercept `OwnBlitter()`/`DisownBlitter()` via SetFunction
- Log blit operations: source/dest/mask pointers, dimensions, minterms
- Visualize source and destination rectangles in a bitmap preview
- Track blitter wait times (how long CPU blocks on `WaitBlit()`)
- Detect common bugs: blitting from/to non-chip RAM, incorrect modulos

### 5. Intuition Inspector ✅ Implemented
Inspect the live GUI object tree:
- Walk `IntuitionBase->FirstScreen` → screen list with details (dimensions, depth, title, ViewPort mode)
- For each screen, walk its window list (position, size, flags, IDCMP, gadget list)
- For each window, enumerate gadgets (type, position, state, text)
- IDCMP message snooping — log what events each window receives
- Click a window/gadget in the web UI to highlight it on the Amiga

### 6. Input Injection / Remote Control ✅ Implemented
Send keyboard and mouse events to the Amiga programmatically:
- `input.device` to inject InputEvents via DoIO
- Enables automated UI testing — script a sequence of clicks and keystrokes
- Key press, key release, mouse move, mouse click, raw key codes
- Record and replay input sequences for regression testing

### 7. ARexx Bridge
Many Amiga apps have ARexx ports. If the bridge daemon creates its own ARexx port:
- Send ARexx commands to any app that has an ARexx port
- Query app state through ARexx
- Script complex multi-app workflows from Claude Code

### 8. DMA Timeline Visualizer
Show DMA bandwidth usage per scan line:
- Read DMACONR to know which DMA channels are active
- Calculate theoretical bandwidth: bitplane DMA, sprite DMA, copper, disk, audio
- Show a visual timeline of which chip has the bus on each scan line
- Detect "DMA crunch" situations where the CPU is starved

### 9. Hot Code Reload
Patch running code without restarting:
- `UnLoadSeg()` old code, `LoadSeg()` new code
- Bridge daemon manages: stop client, swap code segment, restart
- Dramatically faster iteration than full restart for UI tweaks

### 10. Automated Test Harness ✅ Implemented
Lightweight test framework for the Amiga side:
- Client library: `ab_test_begin()`, `ab_test_assert()`, `ab_test_end()`
- Results stream back to host as structured test events (TEST protocol messages)
- Web UI shows pass/fail with failure details
- MCP tool returns structured test results for CI integration
- Combine with input injection for integration tests

---

## Medium-Impact / Nice-to-Have

| Feature | What it does |
|---|---|
| **Assign Manager** | View/create/modify AmigaOS logical assigns (SYS:, LIBS:, FONTS:, etc.) |
| **Font Browser** | List installed fonts, preview rendering at different sizes |
| **Locale/Catalog Inspector** | View localized strings for multi-language apps |
| **Custom Chip Write Logger** | Snoop writes to $DFF000 range — see what your code programs into hardware |
| **Memory Pool Tracker** | Track `CreatePool`/`AllocPooled`/`FreePooled` for pool-based allocation |
| **Startup-Sequence Editor** | Edit S:Startup-Sequence and S:User-Startup remotely |
| **Preferences Editor** | Read/write Workbench prefs (screenmode, palette, font) remotely |
| **Visual Diff for Screenshots** | Take two screenshots and highlight pixel differences — regression testing |
| **CLI History & Aliases** | Persistent command history and aliases in the Shell tab |
| **Clipboard Bridge** | Share clipboard between host and Amiga via `clipboard.device` |
