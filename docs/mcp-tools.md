<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Chris Collins <chris@hitorro.com> -->

# MCP tools reference

[Documentation index](README.md) · [Install DevBench](quickstart.md)

## MCP Tools Reference

All tools are available through Claude Code when connected to the MCP server.

### Build & Deployment

| Tool | Parameters | Description |
|---|---|---|
| `amiga_build` | `project?` | Cross-compile via Docker. Omit project for all. |
| `amiga_clean` | `project?` | Clean build artifacts |
| `amiga_deploy` | `project?` | Copy binaries to AmiKit shared folder |
| `amiga_build_deploy_run` | `project`, `command?` | Build → deploy → launch in one step |

### Connection & Status

| Tool | Parameters | Description |
|---|---|---|
| `amiga_connect` | `mode?`, `host?`, `port?`, `pty_path?` | Connect to Amiga (TCP or PTY) |
| `amiga_disconnect` | — | Close serial connection |
| `amiga_ping` | — | Check if Amiga is alive, get client count and memory |

### Logging

| Tool | Parameters | Description |
|---|---|---|
| `amiga_log` | `count?`, `level?` | Get recent log messages |
| `amiga_watch_logs` | `duration_ms?`, `level?` | Stream logs in real-time |
| `amiga_watch_status` | `duration_ms?` | Stream heartbeats and variable updates |

### Memory

| Tool | Parameters | Description |
|---|---|---|
| `amiga_inspect_memory` | `address`, `size` | Hex dump at address (blocks custom chip regs) |
| `amiga_write_memory` | `address`, `hex_data` | Write hex bytes to RAM address |

### Variables

| Tool | Parameters | Description |
|---|---|---|
| `amiga_get_var` | `name` | Read a registered variable |
| `amiga_set_var` | `name`, `value` | Write a registered variable |

### Process Control

| Tool | Parameters | Description |
|---|---|---|
| `amiga_launch` | `command` | Launch a program on the Amiga |
| `amiga_dos_command` | `command` | Run an AmigaDOS command |
| `amiga_run_script` | `script` | Execute multi-line AmigaDOS script |
| `amiga_break` | `name` | Send CTRL-C break to a task |
| `amiga_stop_client` | `name` | Gracefully stop a bridge client |
| `amiga_proc_list` | — | List tracked async processes with IDs and status |
| `amiga_proc_stat` | `proc_id` | Get status of a specific tracked process |
| `amiga_signal` | `proc_id`, `signal?` | Send signal to tracked process (CTRL_C/D/E/F) |

### System Information

| Tool | Parameters | Description |
|---|---|---|
| `amiga_capabilities` | — | Query daemon version, protocol level, and supported commands |
| `amiga_list_clients` | — | List connected bridge clients |
| `amiga_list_tasks` | — | List all running tasks/processes |
| `amiga_list_libs` | — | List loaded libraries with versions |
| `amiga_lib_info` | `name` | Detailed library info: version, revision, openCnt, flags, negSize, posSize, base address, ID string |
| `amiga_dev_info` | `name` | Detailed device info: same fields as lib_info but for devices |
| `amiga_client_info` | `client` | Detailed info: vars, hooks, memregs |
| `amiga_list_assigns` | — | List all DOS assigns (logical device assignments) |
| `amiga_assign` | `name`, `path`, `mode?` | Create, replace, add to, or remove a DOS assign |

### Filesystem

| Tool | Parameters | Description |
|---|---|---|
| `amiga_list_dir` | `path?` | List directory contents |
| `amiga_read_file` | `path`, `offset?`, `size?` | Read file content (hex) |
| `amiga_write_file` | `path`, `offset`, `hex_data` | Write hex data to file |
| `amiga_rename` | `old_path`, `new_path` | Rename or move a file |
| `amiga_copy` | `src`, `dst` | Copy a file server-side (no host round-trip) |
| `amiga_append_file` | `path`, `hex_data` | Append hex-encoded data to a file |
| `amiga_checksum` | `path` | Compute CRC32 checksum and file size |
| `amiga_protect` | `path`, `bits?` | Get or set AmigaOS protection bits |
| `amiga_set_comment` | `path`, `comment` | Set file comment (filenote) |
| `amiga_tail` | `path`, `duration_ms?` | Stream live file appends (log monitoring) |

### Hooks & Memory Regions

| Tool | Parameters | Description |
|---|---|---|
| `amiga_list_hooks` | `client?` | List registered hooks |
| `amiga_call_hook` | `client`, `hook`, `args?` | Call a hook function remotely |
| `amiga_list_memregions` | `client?` | List registered memory regions |
| `amiga_read_memregion` | `client`, `region` | Read a named memory region |

### Execution

| Tool | Parameters | Description |
|---|---|---|
| `amiga_exec` | `command` | Send expression to client's command handler |

### Graphics & Hardware Inspection

| Tool | Parameters | Description |
|---|---|---|
| `amiga_screenshot` | `window?` | Capture screen or window as PNG (planar→chunky conversion) |
| `amiga_get_palette` | `screen?` | Read OCS/ECS 12-bit color palette via GetRGB4() |
| `amiga_set_palette` | `index`, `r`, `g`, `b` | Set a palette color (4-bit per channel) |
| `amiga_copper_list` | — | Read and decode copper list (MOVE/WAIT/SKIP instructions) |
| `amiga_sprites` | — | Read 8 hardware sprite channels (copper + SimpleSprites) |
| `amiga_disassemble` | `address`, `count?` | Disassemble 68k code with LVO annotation |
| `amiga_last_crash` | — | Read last crash report (registers, stack, alert code) |

### Client Profiling

| Tool | Parameters | Description |
|---|---|---|
| `amiga_list_resources` | `client` | Query client resource tracking (alloc/free, open/close) |
| `amiga_perf_report` | `client` | Query client performance data (frame timing, sections) |

### Debug & Symbols

| Tool | Parameters | Description |
|---|---|---|
| `amiga_load_symbols` | `project` | Load STABS debug symbols from a cross-compiled binary (requires `-g` flag) |
| `amiga_lookup_symbol` | `address`, `project?` | Look up function name + source file:line for a code address |
| `amiga_list_functions` | `project` | List all function symbols with source file:line mappings |
| `amiga_run_tests` | `project`, `command?` | Build, deploy, run a test suite and collect pass/fail results |

Once symbols are loaded, the disassembler annotates function entry points and source
line changes, and crash reports include symbolic register annotations and stack traces.

### Audio

| Tool | Parameters | Description |
|---|---|---|
| `amiga_audio_channels` | — | Read all 4 Paula audio channel registers (period, volume, length, pointer) |
| `amiga_audio_sample` | `address`, `size?` | Read audio sample data from chip RAM, returns hex + waveform |

### Intuition Inspection

| Tool | Parameters | Description |
|---|---|---|
| `amiga_list_screens` | — | List all Intuition screens with dimensions, depth, title, flags |
| `amiga_list_screen_windows` | `screen?` | List all windows on a screen with positions, sizes, flags |
| `amiga_list_gadgets` | `window` | List gadgets attached to a window (type, position, size, flags) |

### Input Injection

| Tool | Parameters | Description |
|---|---|---|
| `amiga_input_key` | `rawkey`, `direction?` | Inject a keyboard event via input.device (raw key code) |
| `amiga_input_mouse_move` | `dx`, `dy` | Inject a relative mouse move event |
| `amiga_input_click` | `button?`, `direction?` | Inject a mouse button press/release |

### ARexx

| Tool | Parameters | Description |
|---|---|---|
| `amiga_arexx_ports` | — | List all ARexx message ports (running ARexx-aware apps) |
| `amiga_arexx_send` | `port`, `command` | Send an ARexx command to a named port and get the result |

### Font Browser

Available via the web UI (Tools > Inspect > Font Browser). Enumerates installed Amiga
fonts via `diskfont.library` `AvailFonts()`, grouped by family with available sizes.
Click a font to view metrics: baseline, x/y size, style flags, font type.

Protocol commands: `LISTFONTS` → `FONTS|count|name:sizes|...`, `FONTINFO|name|size` → `FONTINFO|name|size|ysize|xsize|style|flags|baseline`.

### Locale/Catalog Inspector

Available via the web UI (Tools > Inspect > Locale/Catalog Inspector). Browses
installed locale catalogs under `LOCALE:Catalogs/` using the `SCRIPT` command to run
AmigaDOS `list` commands. Click a catalog directory to see its contents.

### Assign Manager

Available via the web UI (Tools > System > Assign Manager). Lists, creates, modifies,
and removes AmigaOS logical assigns (SYS:, LIBS:, FONTS:, etc.). Supports the ADD
flag for multi-directory assigns. Uses the `SCRIPT` command to run `assign` on the Amiga.

### Startup-Sequence Editor

Available via the web UI (Tools > System > Startup-Sequence Editor). Remotely edit
`S:Startup-Sequence`, `S:User-Startup`, or `S:Shell-Startup`. Uses `READFILE`/`WRITEFILE`
protocol commands. Changes take effect on next boot.

### Preferences Editor

Available via the web UI (Tools > System > Preferences Editor). Lists available
Workbench preferences files from `ENV:sys/` and reads their contents. Prefs files
are IFF binary format.

### Custom Chip Register Logger

Available via the web UI (Tools > Debug > Custom Chip Logger). Monitors 13 readable
custom chip registers at $DFF000 for changes: DMACONR, VPOSR, VHPOSR, JOY0DAT,
JOY1DAT, POT0DAT, POT1DAT, POTGOR, INTENAR, INTREQR, DSKBYTR, DENISEID, ADKCONR.

- **Start** — takes initial snapshot and begins polling from the bridge main loop (200ms interval)
- **Stop** — stops monitoring
- **Snapshot** — one-shot read of all registers

Register changes are reported in real-time via `CHIPLOGCHANGE|tick|reg:old:new|...` events.

Protocol commands: `CHIPLOGSTART`, `CHIPLOGSTOP`, `CHIPLOGSNAPSHOT` → `CHIPLOG|reg:val|...`.

### Memory Pool Tracker

Available via the web UI (Tools > Debug > Memory Pool Tracker). Tracks pool-based
memory allocation by patching `exec.library` pool functions via `SetFunction()`:
`CreatePool`, `DeletePool`, `AllocPooled`, `FreePooled`.

- **Start** — installs patches, begins tracking (up to 32 pools)
- **Stop** — restores original function vectors
- **Refresh** — lists active pools with allocation counts and total sizes

Uses `Forbid()`/`Permit()` to protect tracking data and `Disable()`/`Enable()` around
`SetFunction()` calls.

Protocol commands: `POOLSTART`, `POOLSTOP`, `POOLS` → `POOLS|count|addr:puddle:thresh:allocs:total|...`.

### Visual Diff for Screenshots

Available via the web UI (Tools > Graphics > Visual Diff). Pixel-level comparison of
two Amiga screenshots for visual regression testing.

1. **Capture A** — take baseline screenshot
2. Make changes on the Amiga
3. **Capture B** — take comparison screenshot
4. **Compare** — generates diff image with configurable threshold (0-255)

Produces a diff image (dimmed background + magenta changed pixels + yellow bounding boxes).
Reports change percentage, pixel counts, and detected change regions using 16x16 block
grid with flood-fill connected component analysis. Requires Pillow (`pip install Pillow`).

### Clipboard Bridge

Available via the web UI (Tools > Control > Clipboard Bridge). Shares clipboard text
between the host and the Amiga.

- **Get from Amiga** — reads FTXT/CHRS clipboard data via `clipboard.device` + `iffparse.library`
- **Set on Amiga** — writes text to the Amiga clipboard in IFF FTXT format
- **Copy to Host** — copies the displayed text to the host system clipboard via `navigator.clipboard`

Protocol commands: `CLIPGET` → `CLIPBOARD|length|text`, `CLIPSET|text` → `OK|CLIPBOARD|set N bytes`.

### CLI History & Aliases

Available in the web UI Shell tab. Provides persistent command history with arrow-key
navigation and named aliases (e.g., `ll` = `list LFORMAT "%n %l"`). History and aliases
are maintained server-side across sessions.

### Project Scaffolding

| Tool | Parameters | Description |
|---|---|---|
| `amiga_create_project` | `name`, `template?` | Create new example project (window/screen/headless) |
| `amiga_run` | `project`, `command?` | Deploy and launch (skip build) |

### FS-UAE Native Debugger (`amiga_fsuae_*`)

Tools that talk to the patched fs-uae HTTP RPC. These work at the *emulator* level — no bridge daemon required. All return JSON; each tool returns a clear "fsuae-rpc not available — install the patched build" error when stock fs-uae is in use, so the agent can probe without hanging.

Always call `amiga_fsuae_status` first to confirm the patched build is detected and the RPC is reachable.

| Tool | Parameters | Description |
|---|---|---|
| **Plumbing** | | |
| `amiga_fsuae_status` | — | Probe `/v1/ping`, return availability + service version |
| **Execution control** | | |
| `amiga_fsuae_pause` | — | Stop the emulator (sticky) |
| `amiga_fsuae_resume` | — | Resume; auto-rearms watchpoints |
| `amiga_fsuae_step` | `n?`, `mode?` | Step N instructions, or `mode='over'` / `mode='out'` |
| `amiga_fsuae_reset` | `hard?` | Hard (RAM-clear) or soft reset |
| `amiga_fsuae_cpu_state` | — | running / paused |
| **CPU** | | |
| `amiga_fsuae_cpu` | — | All registers (D0-D7, A0-A7, PC, SR, USP, ISP) |
| `amiga_fsuae_set_register` | `reg`, `value` | Write any register |
| **Memory** | | |
| `amiga_fsuae_mem_read` | `addr`, `length?` | Read up to 64K via fs-uae's debug accessor (works on ROM) |
| `amiga_fsuae_mem_write` | `addr`, `hex_bytes` | Write hex bytes (pause first) |
| `amiga_fsuae_memmap` | — | Region map: chip/fast/ROM/IO/unmapped |
| `amiga_fsuae_stack` | `depth?` | Read longwords from (A7) with code/data tagging |
| **Disassembly** | | |
| `amiga_fsuae_disasm` | `addr?`, `count?`, `annotate?`, `library?` | Disassemble with library-call annotation |
| **Breakpoints (CPU-level)** | | |
| `amiga_fsuae_breakpoint_add` | `addr`, `skip?`, `oneshot?` | Add BP (up to 20). Works on Kickstart ROM. |
| `amiga_fsuae_breakpoint_list` | — | List with hit counts |
| `amiga_fsuae_breakpoint_clear` | — | Remove all |
| `amiga_fsuae_breakpoint_by_symbol` | `name`, `project?`, `skip?`, `oneshot?` | **Cross-debugger:** install a CPU BP at the address of a function known to the bridge symbol tables. Requires `amiga_load_symbols` to have been called first. |
| **Watchpoints (memory)** | | |
| `amiga_fsuae_watchpoint_add` | `addr`, `size?`, `rwi?`, `mustchange?`, `val?`, `valmask?` | Hardware-style watchpoint (up to 20) |
| `amiga_fsuae_watchpoint_list` | — | List active |
| `amiga_fsuae_watchpoint_last` | — | Last hit: addr, PC, value, rwi |
| `amiga_fsuae_watchpoint_clear` | — | Remove all |
| **Chipset & symbols** | | |
| `amiga_fsuae_custom` | — | DMACON, INTENA/REQ, BPLCONx, copper/bitplane ptrs, beam pos |
| `amiga_fsuae_symbol_lookup` | `addr` | Built-in table: chipset, CIA, 68k vectors |
| `amiga_fsuae_fd_load` | `path`, `library` | Load `.fd` file for disassembler annotation |
| `amiga_fsuae_fd_lookup` | `offset`, `library?` | Translate negative offset → function name |
| `amiga_fsuae_fd_libraries` | — | List loaded FD libraries |
| **State snapshots** | | |
| `amiga_fsuae_state_save` | `path` | Save `.uss` snapshot |
| `amiga_fsuae_state_load` | `path` | Restore `.uss` snapshot |

#### Common recipes

```python
# "Who is clobbering my sprite pointer?"
amiga_fsuae_pause()
amiga_fsuae_watchpoint_add(addr="0xC0", size=4, rwi="W", mustchange=True)
amiga_fsuae_resume()
# ... let it run until WP fires ...
amiga_fsuae_watchpoint_last()  # → triggering PC + value
amiga_fsuae_disasm(addr="<that PC>", count=8, annotate=True)

# "What does Kickstart do between FC0000 and the first JSR?"
amiga_fsuae_reset(hard=True)
amiga_fsuae_pause()
amiga_fsuae_breakpoint_add(addr="0xFC0000")
amiga_fsuae_resume()
# ... pauses at first instruction of ROM ...
amiga_fsuae_step(n=1)
amiga_fsuae_cpu()
amiga_fsuae_disasm(count=16, annotate=True)

# "Stop on the 1000th call to CopyMem"
result = amiga_fsuae_fd_lookup(offset=-624)  # CopyMem
# ExecBase + offset → address (use amiga_fsuae_mem_read of $4 to get ExecBase)
amiga_fsuae_breakpoint_add(addr="<CopyMem addr>", skip=999)
amiga_fsuae_resume()
```

### REST API for fs-uae debugger

The same operations are available as HTTP endpoints for shell-script / curl use:

```sh
# Status / control
curl     http://localhost:3000/api/fsuae/status
curl -X POST http://localhost:3000/api/fsuae/pause
curl -X POST 'http://localhost:3000/api/fsuae/step?mode=over'

# Inspection
curl 'http://localhost:3000/api/fsuae/cpu'
curl 'http://localhost:3000/api/fsuae/disasm?addr=pc&count=16&annotate=1'
curl 'http://localhost:3000/api/fsuae/memmap'

# Hardware watchpoint
curl -X POST 'http://localhost:3000/api/fsuae/watchpoints?addr=0xC0&size=4&rwi=W&mustchange=1'

# Symbol / FD lookup
curl 'http://localhost:3000/api/fsuae/symbols/lookup?addr=0xDFF096'
curl 'http://localhost:3000/api/fsuae/fd/lookup?offset=-552'
```

Full list: 31 routes under `/api/fsuae/*` — see `amiga-devbench/amiga_devbench/server.py` (`api_fsuae_*` handlers) for the canonical list.

#### Snapshot slot helpers

```sh
# List slot status (size, mtime)
curl http://localhost:3000/api/fsuae/snapshot/list

# Save / load a numbered slot (1..9) under ~/.amiga-devbench/snapshots/
curl -X POST http://localhost:3000/api/fsuae/snapshot/slot/1/save
curl -X POST http://localhost:3000/api/fsuae/snapshot/slot/1/load

# Diff two snapshots (slot-N shorthand, or absolute paths)
curl 'http://localhost:3000/api/fsuae/snapshot/diff?a=slot-1&b=slot-2'
curl 'http://localhost:3000/api/fsuae/snapshot/diff?a=/tmp/before.uss&b=/tmp/after.uss'
```

The diff endpoint shells out to `tools/uss_diff.py` from the patched fork if available — searched in `~/.amiga-devbench/fsuae_remote_patch/`, `~/code/fsuae_remote_patch/`, and `/tmp/fsuae-src/`. Without it, returns a byte-summary instead.

#### Auto-snapshot ring buffer

```sh
# Status (always returns; off by default)
curl http://localhost:3000/api/fsuae/snapshot/autosnap/status

# Enable: snap every 30s into a 5-slot ring
curl -X POST 'http://localhost:3000/api/fsuae/snapshot/autosnap/set?interval=30&ring_size=5'

# Disable
curl -X POST 'http://localhost:3000/api/fsuae/snapshot/autosnap/set?interval=0'
```

Files are written as `~/.amiga-devbench/snapshots/auto-N.uss`. The `snapshot/list` endpoint reports them under an `auto:` key alongside the manual slots.

---
