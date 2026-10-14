# WinUAE native debugger

[Documentation index](README.md) · [WinUAE and FS-UAE setup](winuae-and-fsuae.md)

DevBench's **WinUAE** tab and `amiga_winuae_*` MCP tools control WinUAE's
native GDB debugger. They share one connection and work without a guest
bridge, including when AmigaOS has crashed or a game has taken over the
machine. The existing **Debugger** tab uses the guest bridge; the **FS-UAE**
tab uses patched FS-UAE's HTTP API.

The WinUAE backend covers everything the FS-UAE backend offers (see
[FS-UAE equivalents](#fs-uae-equivalents)), and adds DMA and value
watches, register conditions, PC history, guest input, guest debug output,
crash reports, Hunk loading, symbol reads and DWARF source inspection.

## Enable it

Use WinUAE 6.1.0 or newer, which has a built-in GDB server on Windows,
macOS and Linux. Range and instruction-fetch watchpoints need a WinUAE
build whose capabilities list `watch-range`; without it, plain CPU
read/write ranges fall back to standard GDB watchpoints.

In `devbench.local.toml`:

```toml
[winuae_debug]
enabled = true
host = "127.0.0.1"
port = 2346
```

For the separate profiles in the dual-emulator guide, put these fields in
`[profiles.winuae]` instead:

```toml
winuae_debug_enabled = true
winuae_debug_host = "127.0.0.1"
winuae_debug_port = 2346
```

`enabled` must be a boolean and `port` an integer; other types stop DevBench
at startup with an error naming the key. The web UI's config editor does not
show these keys; edit the file. The default port, 2345, is also WinUAE's
default. These examples use 2346, so that another GDB client can keep 2345;
set the same port in WinUAE.

Leave the global setting disabled, and set `winuae_debug_enabled = false`
in other profiles. The endpoint is read at startup; restart DevBench after
changing it. This port is separate from the guest bridge, DevBench HTTP
(port 3000), and DevBench's guest-debugger GDB server (port 2159). Only one
GDB client can use a WinUAE instance at a time.

### Let DevBench launch WinUAE

Point the emulator manager at WinUAE, as an executable or a macOS
`WinUAE.app`, with a `.uae` configuration:

```toml
[emulator]
binary = "/Applications/WinUAE.app"
config = "~/Amiga/DevBench.uae"
```

DevBench then starts WinUAE with `-f <config>`, `use_gui=no` and, when
`[winuae_debug]` is enabled, `debugging_features=gdbserver`, the GDB port
and `cpu_threaded=false`, because WinUAE does not start its GDB server with
the threaded CPU. On Windows it also turns off the pause when WinUAE loses
focus. The configuration file is not modified. The emulator controls and
`amiga_emulator_*` tools start, stop and restart it, and floppy changes
restart it with the new disks. A WinUAE started elsewhere counts as
DevBench's only if it uses the same configuration file, so Stop never ends
an unrelated WinUAE. Recognizing such an instance uses `pgrep`, so it works
on macOS and Linux; on Windows, DevBench manages only a WinUAE it started.

### Or launch WinUAE yourself

In a separate `.uae` configuration, enable:

```ini
debugging_features=gdbserver
gdb_port=2346
cpu_threaded=false
```

The same settings can also be given on the WinUAE command line, for
example `-s debugging_features=gdbserver -s gdb_port=2346`. Start DevBench
with the matching local config or profile and `--no-emulator`.

## In the browser

Open <http://localhost:3000/> and select **WinUAE**. **Connect &amp; pause**
attaches; WinUAE pauses on GDB attach. DevBench does not attach
automatically at startup or when displaying status. The header holds run
control: Resume, Pause, Step, Step ×100, Step over, Step out, and soft and
hard reset. **Disconnect** releases this connection's debug points and
resumes execution. Closing a browser tab does not disconnect: MCP may still
be using the shared session.

| Sub-tab | Contents |
|---|---|
| CPU &amp; breakpoints | Disassembly with library-call names and source lines, editable registers including USP, ISP and MSP, breakpoints with skip counts, one-shot and symbol breakpoints and hit counts, watchpoints with the last hit and access log |
| Memory | Byte, word, longword and ASCII views with pointer following, RAM writes, the memory map, the stack with return-address tagging, and memory search |
| Chipset | Decoded custom registers and beam position, Copper lists, screenshots and bitplane decoding |
| State &amp; symbols | Snapshot slots with labels, state files at any path, the automatic snapshot ring, snapshot comparison, address lookup and library offset tables |
| Watches &amp; stops | DMA watches, register conditions, PC history, program-entry and exception stops, task, exception and segment inspection, crash reports and guest debug output |
| Input | Keyboard, text, mouse and joystick input |
| Files &amp; disks | Floppy images, Hunk inspection and loading, raw loading, symbol reads and DWARF source, variables and backtraces |

Inspection and edits require a stopped CPU. They never silently interrupt
running emulation. Addresses and register values accept decimal, `0x` hex
or `$` hex. Memory access is physical; writes accept RAM only, rejecting
ROM, I/O and ranges the emulator cannot certify. Per request, reads are
limited to 64 KiB and writes to 4 KiB; writes are not transactional.

## Behavior to know

- **Breakpoints** belong to this connection. Skip counts apply while
  resuming; DevBench counts hits by reading the PC at each breakpoint stop
  and continues silently while a skip count runs. They survive checkpoint
  restores and resets: DevBench reinstalls them, along with watches,
  conditions, exception stops and guest output capture.
- **Watchpoints**: a range matches any overlapping access, like FS-UAE and
  WinUAE console watchpoints. `exact` matches one complete aligned 1, 2 or
  4 byte access; chip RAM is 16 bits wide, so a longword write there is two
  word accesses. WinUAE compares values at the width of the watch, so a
  value condition needs 1, 2 or 4 bytes. Instruction fetches are seen when
  the CPU emulation fetches through the memory banks, such as the
  compatible 68000; faster CPU modes fetch from RAM directly.
- **Checkpoints**: state files are only valid at the start of a frame, so a
  save resumes the CPU, saves at the next frame start and stops again; the
  CPU ends up to one frame past the original stop. This session's
  breakpoints are lifted for that run, so they cannot cancel the save, and
  the result holds the registers stored in the file. Normal WinUAE
  savestate limits apply: disk writes are not rolled back.
- **Resets** use WinUAE's keyboard reset (Ctrl-Amiga-Amiga, with Alt for a
  hard reset). WinUAE detaches GDB clients on reset, so DevBench
  reconnects and resumes unless asked not to.
- **USP, ISP and MSP** are read and written by switching SR's stack bits
  briefly; SR itself is restored. MSP exists on the 68020, 68030 and 68040.
- **Input** goes through WinUAE's input handlers. A stopped guest sees it
  after resuming; frame-timed sequences make taps reliable.
- **Loaded segments** accept an Exec Process address; `0` means the current
  process. They can fail if the current task is not a DOS process, for
  example while Kickstart waits for a boot disk.
- **DWARF** tools need a linked ELF32 m68k file with retained relocations
  (`-Wl,--emit-relocs`), explicit section mappings and the dwarf extra:
  `pip install amiga-devbench[dwarf]` (pyelftools 0.32). They run in an
  isolated helper process with fixed time and read budgets.

## MCP tools

| Tool | Purpose |
|---|---|
| `amiga_winuae_control` | Status, connect, disconnect, pause, resume, step (count), step over, step out, wait for stop, soft or hard reset |
| `amiga_winuae_range_step` | Step while the PC stays in a range |
| `amiga_winuae_registers` | Read registers or write one, including USP, ISP and MSP |
| `amiga_winuae_read_memory` / `amiga_winuae_write_memory` | Physical memory reads, RAM writes |
| `amiga_winuae_memory_search` | Find bytes in a range of up to 16 MiB |
| `amiga_winuae_disassemble` | Native disassembly with library-call names and source lines |
| `amiga_winuae_breakpoint` | Session breakpoints and watchpoints with skip, one-shot, symbols and hit counts |
| `amiga_winuae_watch` | Range, fetch, exact, value and change-only watches and their access log |
| `amiga_winuae_dma_watch` | Watches for DMA sources only |
| `amiga_winuae_condition` | Stop on register comparisons |
| `amiga_winuae_history` | PC history |
| `amiga_winuae_process_break` | Program-entry stop by command name or Process address |
| `amiga_winuae_exception_stops` | Replace the exception-stop mask |
| `amiga_winuae_inspect` | Tasks, segments, last exception, memory map, capabilities |
| `amiga_winuae_custom` / `amiga_winuae_copper` | Decoded custom registers, Copper lists |
| `amiga_winuae_stack` | Stack longwords with return-address and code tagging |
| `amiga_winuae_lookup` | Names of custom and CIA registers, vectors and regions |
| `amiga_winuae_fd` | Library offset tables; load `.fd` files |
| `amiga_winuae_screenshot` / `amiga_winuae_bitmap` | Display screenshots, bitplane decoding (returned as images) |
| `amiga_winuae_capture` / `amiga_winuae_postmortem` | Register and memory captures, crash reports |
| `amiga_winuae_guest_output` | Guest debug messages (uaelib function 86) |
| `amiga_winuae_checkpoint` | Save or restore state files and slots |
| `amiga_winuae_snapshots` / `amiga_winuae_autosnap` / `amiga_winuae_snapshot_diff` | Slots and labels, the automatic ring, chunk-level comparison |
| `amiga_winuae_input` / `amiga_winuae_input_sequence` / `amiga_winuae_key_tap` / `amiga_winuae_type_text` | Guest input |
| `amiga_winuae_load` / `amiga_winuae_disk` | Raw loads and floppy images |
| `amiga_winuae_hunk` | Inspect Hunk executables or relocate them into reserved RAM |
| `amiga_winuae_symbols` / `amiga_winuae_symbol_read` | Hunk and ELF symbols, symbol reads through segments or mappings |
| `amiga_winuae_source` / `amiga_winuae_variable` / `amiga_winuae_backtrace` | DWARF source lines, variables and backtraces |

`step` with one instruction, `step_over`, `range_step` and resume return
at once; use `control(action="wait")` to observe the next stop. A wait
timeout leaves execution running; use `pause` if you intend to interrupt. A
command-reply timeout instead closes the connection, avoiding reuse of a
late reply for an unrelated request.

## FS-UAE equivalents

| FS-UAE | WinUAE |
|---|---|
| `amiga_fsuae_status`, `_cpu_state`, `_pause`, `_resume` | `amiga_winuae_control` |
| `amiga_fsuae_step` (`n`, `mode=over/out`) | `amiga_winuae_control` (`step` with `count`, `step_over`, `step_out`) |
| `amiga_fsuae_reset` (`hard`) | `amiga_winuae_control` (`reset`, `hard`) |
| `amiga_fsuae_cpu`, `_set_register` | `amiga_winuae_registers` |
| `amiga_fsuae_mem_read`, `_mem_write`, `_memmap` | `amiga_winuae_read_memory`, `_write_memory`, `_inspect` (`memory-map`) |
| `amiga_fsuae_stack` | `amiga_winuae_stack` |
| `amiga_fsuae_disasm` (`annotate`, `library`) | `amiga_winuae_disassemble` |
| `amiga_fsuae_breakpoint_add`, `_list`, `_clear`, `_by_symbol` | `amiga_winuae_breakpoint` |
| `amiga_fsuae_watchpoint_add`, `_list`, `_last`, `_clear` | `amiga_winuae_watch` |
| `amiga_fsuae_custom` | `amiga_winuae_custom` |
| `amiga_fsuae_symbol_lookup` | `amiga_winuae_lookup` |
| `amiga_fsuae_fd_lookup`, `_fd_load`, `_fd_libraries` | `amiga_winuae_fd` |
| `amiga_fsuae_state_save`, `_state_load` | `amiga_winuae_checkpoint` |
| FS-UAE tab snapshot slots, ring and diff | `amiga_winuae_snapshots`, `_autosnap`, `_snapshot_diff` |

The client implements the [GDB remote serial protocol](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Overview.html)
using Python's asynchronous sockets; no Node.js service or native host module
is required. It has been tested live with WinUAE on macOS, with the 68000
and the 68020; `tests/test_winuae_live.py` runs those tests when
`WINUAE_PATH` and `WINUAE_ROM` are set.
