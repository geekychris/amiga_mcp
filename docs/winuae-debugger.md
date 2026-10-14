# WinUAE native debugger

[Documentation index](README.md) · [WinUAE and FS-UAE setup](winuae-and-fsuae.md)

DevBench's **WinUAE** tab and `amiga_winuae_*` MCP tools control WinUAE's
native GDB debugger. They share one connection and work without a guest
bridge, including when AmigaOS has crashed. The existing **Debugger** tab
uses the guest bridge; the **FS-UAE** tab uses patched FS-UAE's HTTP API.

## Enable it

Use WinUAE 6.1.0 or newer, which has a built-in GDB server on Windows,
macOS and Linux. In a separate `.uae` configuration, enable:

```ini
debugging_features=gdbserver
gdb_port=2346
cpu_threaded=false
```

WinUAE does not start its GDB server with the threaded CPU, so
`cpu_threaded` must stay off; otherwise DevBench cannot connect. The same
settings can also be given on the WinUAE command line, for example
`-s debugging_features=gdbserver -s gdb_port=2346`.

Use a free port. Only one GDB client can use a WinUAE instance at a time;
disconnect any existing GDB/MCP debugger before connecting DevBench.

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
show these keys; edit the file. The default port, 2345, is also WinUAE's default. These examples use 2346,
so that another GDB client can keep 2345; set the same port in WinUAE.

Leave the global setting disabled, and set `winuae_debug_enabled = false`
in other profiles. The endpoint is read at startup; restart DevBench after
changing it. This port is separate from the guest bridge, DevBench HTTP
(port 3000), and DevBench's guest-debugger GDB server (port 2159).

## In the browser

1. Launch WinUAE with the debug configuration, then start DevBench using
   the matching local config/profile and `--no-emulator`.
2. Open <http://localhost:3000/> and select **WinUAE**.
3. Click **Connect & pause**. WinUAE pauses on GDB attach. DevBench does
   not attach automatically at startup or when displaying status.
4. Inspect registers/disassembly, memory, tasks, loaded segments or the
   last native exception. Use **Resume**, **Pause**, **Step** or **Step over**.
5. **Disconnect** releases this connection's debug points and resumes
   execution. Closing a browser tab does not disconnect: MCP may still
   be using the shared session. Stopping DevBench closes the session.

Inspection and edits require a stopped CPU. They never silently interrupt
running emulation. Addresses and register values accept decimal, `0x` hex
or `$` hex. Memory access is physical; writes accept RAM only, rejecting
ROM, I/O and ranges the emulator cannot certify. Per request, reads are
limited to 64 KiB and writes to 4 KiB; writes are not transactional.

The breakpoint table contains this connection's execute and CPU data
watchpoints. It does not claim to enumerate locally installed debugger
breakpoints. Unsupported native monitor features are disabled in the UI
and reported as errors by tools.

To stop at program entry, pause, enter the AmigaDOS command name and click
**Arm entry stop**. Resume, then launch that program in the guest. Loaded
segments accepts an Exec Process address; `0` means the current process.
It can fail if the current task is not a DOS process or its structures are
corrupt, for example while Kickstart waits for a boot disk.

Exception stops accept vectors 2–63. For example, `2,3,4,5,8,10,11` covers
several common CPU faults. **Disable stops** clears the mask. This changes
native emulator debugging, not AmigaOS exception vectors. **Last exception**
reports the captured fault context; it may be empty before a fault occurs.

## MCP tools

| Tool | Purpose |
|---|---|
| `amiga_winuae_control` | Status, connect, disconnect, pause, resume, step, step over, wait for stop |
| `amiga_winuae_registers` | Read registers or write one |
| `amiga_winuae_read_memory` | Read physical memory |
| `amiga_winuae_write_memory` | Write physical RAM |
| `amiga_winuae_disassemble` | Use the emulator's disassembler |
| `amiga_winuae_breakpoint` | Add/remove/list session breakpoints and watchpoints |
| `amiga_winuae_inspect` | Tasks, segments, exception context and memory map |
| `amiga_winuae_process_break` | Program-entry stop by command name or Process address |
| `amiga_winuae_exception_stops` | Replace the exception-stop mask |

`step` and `step_over` start execution. Use `control(action="wait")` to
observe the next stop. A wait timeout leaves execution running; use `pause`
if you intend to interrupt. A command-reply timeout instead closes the
connection, avoiding reuse of a late reply for an unrelated request.

This integration does not yet expose the separate
[WinUAE MCP server](https://github.com/axewater/mcp-winuae-emu)'s
DWARF/source features, conditional, DMA and value watchpoints, history,
guest output, input automation or checkpoints. WinUAE provides them through
the same GDB server.
FS-UAE's existing tools and UI remain separate and retain their behavior.

The client implements the [GDB remote serial protocol](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Overview.html)
using Python's asynchronous sockets; no Node.js service or native host module
is required. It has been tested live with WinUAE on macOS.
