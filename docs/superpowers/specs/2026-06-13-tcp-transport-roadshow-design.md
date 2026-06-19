# Design: TCP/RoadShow Transport for amiga-bridge

**Date:** 2026-06-13
**Status:** Approved (architecture)
**Author:** brainstorming session

## Goal

Let the `amiga-bridge` daemon talk to the host `amiga-devbench` over a TCP/IP
network connection (via the Amiga's `bsdsocket.library`, e.g. the RoadShow
stack) instead of a physical serial link. Serial remains fully supported for the
FS-UAE/emulator workflow. The transport is chosen at startup.

## Decisions (locked)

- **Scope:** Selectable transport. Serial path is preserved untouched; TCP is added.
- **Connection role:** Amiga **listens** (TCP server); `amiga-devbench` connects
  to the Amiga's IP. The host is already a TCP client, so host-side change is
  config-only.
- **Daemon config:** CLI args. `amiga-bridge` (no args) = serial, current
  behavior. `amiga-bridge TCP [port]` = TCP server (default port 2345).
- **Protocol:** Unchanged. Same line-based, pipe-delimited text protocol above
  the transport seam. `protocol_handler.c` and the 121 host MCP tools are untouched.

## Architecture

```
main.c  ──parses argv──►  transport_open(mode, port)
                              │
                  ┌───────────┴────────────┐
            serial_io.c                  net_io.c   ← NEW (bsdsocket.library / RoadShow)
        (serial.device unit0)        (listen/accept/recv/send TCP socket)
                              │
              same line protocol above the seam (protocol_handler.c unchanged)
```

A new `transport.c` holds a mode flag and a tiny dispatch layer. `serial_io.c`
is unchanged. `net_io.c` is the only substantial new code.

### The transport interface

`transport.c` exposes the same shape the daemon already relies on. Callers move
from `serial_*` to `transport_*`:

```c
int   transport_open(int mode, ULONG param);   /* mode: TRANSPORT_SERIAL | TRANSPORT_TCP */
void  transport_close(void);
int   transport_write(const char *buf, int len);
void  transport_start_read(void);
int   transport_check_read(char *out_byte);
ULONG transport_get_signal(void);
BOOL  transport_is_open(void);                 /* serial: device open; tcp: client connected */
```

For serial, `param` is the baud (115200). For TCP, `param` is the listen port.
Dispatch is a simple `switch (g_transport_mode)` per call (function-pointer vtable
is overkill for two backends).

### Call-site changes (small, enumerated)

- `main.c`: parse `argv`; call `transport_open`/`transport_close`/
  `transport_get_signal`/`transport_start_read`/`transport_check_read`. The 11
  `g_serial_connected` references keep their name (semantics generalized to
  "peer/link present") to minimize churn.
- `protocol_handler.c:96`: `serial_write` → `transport_write`.
- `crash_handler.c:188`: `serial_is_open` → `transport_is_open`.
- `bridge_internal.h`: add `transport_*` declarations and mode constants;
  keep `serial_*` declarations (still used by the serial backend).

## net_io.c design (bsdsocket / RoadShow)

### Library + async model

- `SocketBase = OpenLibrary("bsdsocket.library", 4)`. If absent, `transport_open`
  fails with a clear UI log line; **no silent fallback to serial**.
- Async I/O integrates with the existing `Wait(signals)` loop via SIGIO:
  - `AllocSignal(-1)` → a signal bit; build `sigmask = 1L << sig`.
  - `SocketBaseTags(SBTM_SETVAL_SIGIO, sigmask, SBTM_SETVAL_SIGURG, sigmask, TAG_END)`.
  - Sockets set non-blocking via `IoctlSocket(s, FIONBIO, &one)`.
  - `transport_get_signal()` returns `sigmask`. `main.c` ORs it into the Wait mask
    exactly where `serialSig` is today — the main loop is otherwise unchanged.

### Listen / accept / drain lifecycle

- On `transport_open(TCP, port)`: create listen socket, `SO_REUSEADDR`,
  `bind(INADDR_ANY, port)`, `listen(1)`, set non-blocking.
- On SIGIO wake, `transport_check_read()`:
  - If no client: `accept()`. On success, store client fd, set non-blocking,
    set connected flag. (Peer-present edge handled by main loop, see below.)
  - If client present: drain with non-blocking `recv()` into a static buffer
    (e.g. 512 bytes); hand bytes to the caller one at a time so the existing
    `handle_serial_byte()` line assembler is reused verbatim. `EWOULDBLOCK`/no
    data → return 0. `recv()==0` or hard error → close client, return to
    listening, clear connected flag.
- Single-client model: a new connect while a client exists drops the old socket
  (mirrors `scripts/amiga-serial-bridge.py`).
- `transport_start_read()` is a no-op for TCP (socket is always drainable).

### Writes

- `transport_write()` performs a send-all loop over the client socket. On
  `EWOULDBLOCK` (send buffer full), brief bounded retry (small `Delay`/`Wait`)
  until all bytes are sent or a cap is hit. Messages are small; this matches the
  blocking semantics `serial_write` (DoIO) provides today. If no client is
  connected, return -1 (caller already gates sends on `g_serial_connected`).

### READY handshake on connect

Serial sends `READY|1.0` once at startup (`main.c:309`). Over TCP there is no peer
at startup. Move the handshake to a **rising-edge** detection in the main loop:
track `prev_connected`; when `transport_is_open()` goes FALSE→TRUE, call
`protocol_send_raw("READY|1.0")`. This keeps READY logic in one place and works
for reconnects. For serial mode the edge fires once right after open — same
observable behavior as today.

### Cleanup

`transport_close()` (TCP): close client + listen sockets (`CloseSocket`), free the
signal, `CloseLibrary(SocketBase)`.

## Host side (amiga-devbench)

Config-only. `devbench.toml`:

```toml
[serial]
mode = "tcp"
host = "<amiga-ip>"   # the Amiga's address on the LAN
port = 2345           # matches `amiga-bridge TCP 2345`

[emulator]
auto_start = false    # no emulator process to launch for real hardware
```

`SerialConnection` already connects as a TCP client and has reconnect logic, so
no code change is expected. One thing to verify during testing: when the Amiga
daemon restarts (listen socket re-accepts), devbench's reconnect loop
re-establishes cleanly and re-reads `READY|1.0`.

## Build / linking

- Add `src/net_io.c` and `src/transport.c` to `DAEMON_SRCS` in `amiga-bridge/Makefile`.
- bsdsocket functions are called through `SocketBase` via `<proto/socket.h>`;
  no extra link library is expected with the Bebbo toolchain. Network headers
  (`<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`, `<proto/socket.h>`) ship
  with `amigadev/crosstools`. **Build-validation step:** confirm the include path
  resolves; add a netinclude `-I` only if the compile reports missing headers.

## Error handling

| Condition | Behavior |
|---|---|
| `bsdsocket.library` missing | `transport_open` fails; UI logs "TCP: no bsdsocket.library"; daemon stays up with link down (does not crash, does not fall back to serial) |
| `AllocSignal` / `socket` / `bind` / `listen` fail | log specific stage, link down |
| `recv()==0` / socket error | close client, return to listening |
| `send()` EWOULDBLOCK | bounded retry loop; on cap, drop client |
| New client while connected | drop old client, accept new |

## Testing strategy

1. **Emulator bsdsocket (primary, no real hardware):** WinUAE and FS-UAE provide
   a built-in `bsdsocket.library` emulation that maps Amiga sockets to host
   sockets. Run `amiga-bridge TCP 2345` inside the emulator with bsdsocket
   emulation enabled; point `devbench.toml` at `127.0.0.1:2345`; verify
   `amiga_ping`, `amiga_sysinfo`, heartbeats, and a clean reconnect after daemon
   restart.
2. **Arg parsing:** `amiga-bridge` (serial, unchanged) vs `amiga-bridge TCP` vs
   `amiga-bridge TCP 4000` select the right mode/port; the FS-UAE serial workflow
   still works with no args.
3. **Reconnect:** kill and relaunch the daemon; devbench reconnects and re-reads
   `READY|1.0`.
4. **Real hardware (final):** run on a real Amiga with RoadShow; devbench connects
   to the Amiga's LAN IP.

## Out of scope (YAGNI)

- Multiple simultaneous host clients (single-client mirrors current serial model).
- TLS/auth (LAN/dev tool; same trust model as serial-over-TCP today).
- Amiga-dials-out role and devbench-as-server (rejected during brainstorming).
- UDP, mDNS/auto-discovery of the Amiga's IP.
```
