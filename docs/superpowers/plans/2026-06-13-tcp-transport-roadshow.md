# TCP/RoadShow Transport for amiga-bridge — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a runtime-selectable TCP/IP transport (Amiga listens, host connects) to the `amiga-bridge` daemon via `bsdsocket.library` (RoadShow), keeping the existing serial path intact.

**Architecture:** A thin `transport.c` dispatch layer routes `transport_*` calls to either the unchanged serial backend (`serial_io.c`) or a new TCP backend (`net_io.c`). `main.c` parses a CLI arg (`SERIAL` default, or `TCP [port]`) to pick the mode. Async socket readiness is delivered to the existing `Wait()` loop via a `bsdsocket` SIGIO signal; the line protocol above the seam is untouched.

**Tech Stack:** m68k AmigaOS C (Bebbo `amigadev/crosstools` toolchain, built via Docker), `bsdsocket.library` (RoadShow/AmiTCP/emulator emulation), existing Python `amiga-devbench` host (config-only change).

**Spec:** `docs/superpowers/specs/2026-06-13-tcp-transport-roadshow-design.md`

**Verification note:** Amiga binaries cannot be unit-tested with a host test runner. The per-task "verify" step is a Docker cross-compile (the failure mode we care about is bad headers/API calls). Behavioral verification happens in Tasks 8–9 via the emulator's `bsdsocket` emulation and (finally) real hardware.

**Build command used throughout:**
```bash
docker run --rm -v "$(pwd):/work" -w /work amigadev/crosstools:m68k-amigaos make -C amiga-bridge daemon
```
On Windows PowerShell, substitute `${PWD}` for `$(pwd)`. Equivalent: `make bridge`.

---

## File Structure

| File | Responsibility | Change |
|---|---|---|
| `amiga-bridge/include/bridge_internal.h` | Shared decls | Modify: add `transport_*` + `net_*` decls, mode constants, `SocketBase` extern |
| `amiga-bridge/src/transport.c` | Dispatch serial vs TCP | **Create** |
| `amiga-bridge/src/net_io.c` | TCP/bsdsocket backend (listen/accept/recv/send) | **Create** |
| `amiga-bridge/src/serial_io.c` | Serial backend | Unchanged |
| `amiga-bridge/src/main.c` | Arg parse, transport lifecycle, READY edge | Modify |
| `amiga-bridge/src/protocol_handler.c` | Send path | Modify (1 line) |
| `amiga-bridge/src/crash_handler.c` | Crash dump gate | Modify (1 line) |
| `amiga-bridge/Makefile` | Build | Modify: add 2 sources |
| `devbench.toml` | Host transport config | Modify |
| `docs/` | Real-hardware guide note | Modify |

---

## Task 1: Declare the transport interface

**Files:**
- Modify: `amiga-bridge/include/bridge_internal.h` (near the existing `serial_*` block, lines 12–18, and the `g_serial_connected` extern at line 287)

- [ ] **Step 1: Add transport + net declarations and mode constants**

In `bridge_internal.h`, immediately AFTER the existing serial block (currently lines 12–18, ending with `BOOL serial_is_open(void);`), insert:

```c
/* ─── Transport selection ─── */
#define TRANSPORT_SERIAL 0
#define TRANSPORT_TCP    1

extern int g_transport_mode;

/* Transport dispatch layer (transport.c) — main.c and callers use these */
int   transport_open(int mode, ULONG param);   /* param: serial baud OR tcp port */
void  transport_close(void);
int   transport_write(const char *buf, int len);
void  transport_start_read(void);
int   transport_check_read(char *out_byte);
ULONG transport_get_signal(void);
BOOL  transport_is_open(void);

/* TCP/bsdsocket backend (net_io.c) */
int   net_open(ULONG port);
void  net_close(void);
int   net_write(const char *buf, int len);
int   net_check_read(char *out_byte);
ULONG net_get_signal(void);
BOOL  net_is_open(void);
```

- [ ] **Step 2: Verify the header still compiles cleanly with the daemon**

Run:
```bash
docker run --rm -v "$(pwd):/work" -w /work amigadev/crosstools:m68k-amigaos make -C amiga-bridge daemon
```
Expected: build still SUCCEEDS (no callers changed yet; new decls are unused but harmless). If it fails, the header edit has a syntax error — fix it.

- [ ] **Step 3: Commit**

```bash
git add amiga-bridge/include/bridge_internal.h
git commit -m "bridge: declare transport dispatch + net backend interface"
```

---

## Task 2: Create the transport dispatch layer

**Files:**
- Create: `amiga-bridge/src/transport.c`

- [ ] **Step 1: Write `transport.c`**

```c
/*
 * transport.c - Transport dispatch for AmigaBridge daemon.
 *
 * Routes the daemon's I/O to either the serial backend (serial_io.c) or the
 * TCP/bsdsocket backend (net_io.c), chosen at startup via g_transport_mode.
 */
#include <exec/types.h>

#include "bridge_internal.h"

int g_transport_mode = TRANSPORT_SERIAL;

int transport_open(int mode, ULONG param)
{
    g_transport_mode = mode;
    if (mode == TRANSPORT_TCP)
        return net_open(param);
    return serial_open(param);
}

void transport_close(void)
{
    if (g_transport_mode == TRANSPORT_TCP)
        net_close();
    else
        serial_close();
}

int transport_write(const char *buf, int len)
{
    if (g_transport_mode == TRANSPORT_TCP)
        return net_write(buf, len);
    return serial_write(buf, len);
}

void transport_start_read(void)
{
    if (g_transport_mode == TRANSPORT_TCP)
        return;                 /* no-op: TCP socket is always drainable */
    serial_start_read();
}

int transport_check_read(char *out_byte)
{
    if (g_transport_mode == TRANSPORT_TCP)
        return net_check_read(out_byte);
    return serial_check_read(out_byte);
}

ULONG transport_get_signal(void)
{
    if (g_transport_mode == TRANSPORT_TCP)
        return net_get_signal();
    return serial_get_signal();
}

BOOL transport_is_open(void)
{
    if (g_transport_mode == TRANSPORT_TCP)
        return net_is_open();
    return serial_is_open();
}
```

- [ ] **Step 2: Commit (build verification deferred to Task 4, when the Makefile + net_io exist)**

`transport.c` references `net_*` symbols that don't exist yet, so it cannot link until Task 3. Commit the source now; it is added to the build in Task 4.

```bash
git add amiga-bridge/src/transport.c
git commit -m "bridge: add transport dispatch layer (serial vs tcp)"
```

---

## Task 3: Create the TCP/bsdsocket backend

**Files:**
- Create: `amiga-bridge/src/net_io.c`

- [ ] **Step 1: Write `net_io.c`**

```c
/*
 * net_io.c - TCP/IP transport for AmigaBridge daemon via bsdsocket.library
 *            (RoadShow / AmiTCP / Miami / emulator bsdsocket emulation).
 *
 * The Amiga is the TCP SERVER: it listens; the host amiga-devbench connects in.
 * Socket readiness is delivered to the main Wait() loop via a SIGIO signal from
 * the stack. All sockets are non-blocking; reads are drained into a buffer and
 * handed up one byte at a time so the existing line assembler is reused.
 */
#include <exec/types.h>
#include <proto/exec.h>
#include <proto/socket.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <bsdsocket/socketbasetags.h>
#include <string.h>

#include "bridge_internal.h"

struct Library *SocketBase = NULL;

static LONG  listen_sock = -1;
static LONG  client_sock = -1;
static BYTE  io_sigbit   = -1;
static ULONG io_sigmask  = 0;

static char  rx_buf[512];
static int   rx_len = 0;
static int   rx_pos = 0;

static void set_nonblocking(LONG s)
{
    LONG one = 1;
    IoctlSocket(s, FIONBIO, (char *)&one);
}

static void drop_client(void)
{
    if (client_sock >= 0) { CloseSocket(client_sock); client_sock = -1; }
    rx_len = rx_pos = 0;
    ui_add_log("TCP: client disconnected");
}

static void try_accept(void)
{
    struct sockaddr_in ca;
    LONG calen = sizeof(ca);
    LONG s = accept(listen_sock, (struct sockaddr *)&ca, &calen);
    if (s >= 0) {
        if (client_sock >= 0) CloseSocket(client_sock);   /* drop stale peer */
        client_sock = s;
        set_nonblocking(client_sock);
        rx_len = rx_pos = 0;
        ui_add_log("TCP: client connected");
    }
}

int net_open(ULONG port)
{
    struct sockaddr_in sa;
    LONG one = 1;

    SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (!SocketBase) {
        ui_add_log("ERR: no bsdsocket.library (TCP/IP stack not running?)");
        return -1;
    }

    /* Ask the stack to signal us on socket I/O readiness */
    io_sigbit = AllocSignal(-1);
    if (io_sigbit == -1) {
        ui_add_log("ERR: AllocSignal failed");
        CloseLibrary(SocketBase);
        SocketBase = NULL;
        return -1;
    }
    io_sigmask = 1UL << io_sigbit;
    SocketBaseTags(SBTM_SETVAL_SIGIO,  (ULONG)io_sigmask,
                   SBTM_SETVAL_SIGURG, (ULONG)io_sigmask,
                   TAG_END);

    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        ui_add_log("ERR: socket() failed");
        net_close();
        return -1;
    }
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one));

    memset(&sa, 0, sizeof(sa));
    sa.sin_family      = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port        = htons((UWORD)port);

    if (bind(listen_sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        ui_add_log("ERR: bind() failed");
        net_close();
        return -1;
    }
    if (listen(listen_sock, 1) < 0) {
        ui_add_log("ERR: listen() failed");
        net_close();
        return -1;
    }
    set_nonblocking(listen_sock);

    ui_add_log("TCP: listening");
    return 0;
}

void net_close(void)
{
    if (client_sock >= 0) { CloseSocket(client_sock); client_sock = -1; }
    if (listen_sock >= 0) { CloseSocket(listen_sock); listen_sock = -1; }
    if (io_sigbit != -1)  { FreeSignal(io_sigbit); io_sigbit = -1; io_sigmask = 0; }
    if (SocketBase)       { CloseLibrary(SocketBase); SocketBase = NULL; }
    rx_len = rx_pos = 0;
}

ULONG net_get_signal(void)
{
    return io_sigmask;
}

BOOL net_is_open(void)
{
    return (client_sock >= 0) ? TRUE : FALSE;
}

int net_check_read(char *out_byte)
{
    LONG n;

    if (!SocketBase) return 0;

    /* Serve buffered bytes first */
    if (rx_pos < rx_len) {
        *out_byte = rx_buf[rx_pos++];
        return 1;
    }

    /* No peer yet: try to accept one */
    if (client_sock < 0) {
        if (listen_sock >= 0) try_accept();
        if (client_sock < 0) return 0;
    }

    /* Refill from the client socket (non-blocking) */
    n = recv(client_sock, rx_buf, sizeof(rx_buf), 0);
    if (n > 0) {
        rx_len = (int)n;
        rx_pos = 0;
        *out_byte = rx_buf[rx_pos++];
        return 1;
    } else if (n == 0) {
        drop_client();               /* peer closed */
        return 0;
    } else {
        if (Errno() != EWOULDBLOCK) drop_client();
        return 0;
    }
}

int net_write(const char *buf, int len)
{
    int sent  = 0;
    int guard = 0;

    if (client_sock < 0) return -1;

    while (sent < len) {
        LONG n = send(client_sock, (APTR)(buf + sent), len - sent, 0);
        if (n > 0) {
            sent += (int)n;
        } else if (n < 0 && Errno() == EWOULDBLOCK) {
            if (++guard > 1000) { drop_client(); return sent; }
            Delay(1);                /* ~20ms: let the send buffer drain */
        } else {
            drop_client();
            return sent;
        }
    }
    return sent;
}
```

- [ ] **Step 2: Commit (build verification in Task 4)**

```bash
git add amiga-bridge/src/net_io.c
git commit -m "bridge: add TCP/bsdsocket backend (net_io)"
```

---

## Task 4: Add new sources to the build and verify compile

**Files:**
- Modify: `amiga-bridge/Makefile` (the `DAEMON_SRCS` list, lines 7–16)

- [ ] **Step 1: Add `transport.c` and `net_io.c` to `DAEMON_SRCS`**

Change the `DAEMON_SRCS` assignment so its final line reads (append the two new files to the existing list — keep all current entries):

```make
DAEMON_SRCS = src/main.c src/serial_io.c src/ipc_manager.c \
              src/client_registry.c src/protocol_handler.c \
              src/system_inspector.c src/fs_access.c \
              src/process_launcher.c src/gfx_inspector.c \
              src/crash_handler.c src/snoop.c \
              src/audio_inspector.c src/intuition_inspector.c \
              src/input_inject.c src/font_browser.c \
              src/chipwrite_logger.c src/pool_tracker.c \
              src/clipboard_bridge.c src/arexx_bridge.c \
              src/debugger.c src/transport.c src/net_io.c
```

- [ ] **Step 2: Compile the daemon**

Run:
```bash
docker run --rm -v "$(pwd):/work" -w /work amigadev/crosstools:m68k-amigaos make -C amiga-bridge daemon
```
Expected: SUCCEEDS and links `amiga-bridge`.

Likely failure modes and fixes (the toolchain ships BSD socket headers, but names can differ):
- `bsdsocket/socketbasetags.h: No such file` → remove that include; the `SBTM_*` tags also come via `<proto/socket.h>`. Re-run.
- `FIONBIO` / `EWOULDBLOCK` undefined → add `#include <sys/ioctl.h>` and `#include <errno.h>` to `net_io.c`.
- `Errno` implicit declaration → it is a bsdsocket library function exposed by `<proto/socket.h>`; ensure that include is present (it is).
- Unresolved socket symbols at link → the Bebbo toolchain resolves `bsdsocket` calls through the `SocketBase` inlines in `<proto/socket.h>`; no extra `-l` is normally required. If link fails, add `-lnet` to `LDFLAGS` in the Makefile and re-run.

Fix whichever applies, re-run until the build succeeds.

- [ ] **Step 3: Commit**

```bash
git add amiga-bridge/Makefile amiga-bridge/src/net_io.c
git commit -m "bridge: build transport.c and net_io.c into the daemon"
```

---

## Task 5: Wire transport selection into main.c

**Files:**
- Modify: `amiga-bridge/src/main.c` — top-of-`main` arg parse; the `serial_open` block (lines 265–276); the startup READY block (lines 308–311); the signal build (line 317); the serial-drain block (lines 360–368); the close call (line 467). Confirm `<string.h>` and `<stdlib.h>` are included near the top; add them if missing.

- [ ] **Step 1: Add arg parsing at the start of `main()`**

Immediately after the local declarations in `main()` (after line 229, `int i;`), insert:

```c
    /* Transport selection from CLI args:
     *   amiga-bridge            -> serial (115200 baud), current behavior
     *   amiga-bridge TCP        -> TCP server on default port 2345
     *   amiga-bridge TCP <port> -> TCP server on <port>
     */
    int   sel_mode  = TRANSPORT_SERIAL;
    ULONG sel_param = 115200;            /* serial baud */
    if (argc >= 2 && (strcmp(argv[1], "TCP") == 0 || strcmp(argv[1], "tcp") == 0)) {
        ULONG tcp_port = 2345;
        if (argc >= 3) {
            long p = atol(argv[2]);
            if (p > 0 && p < 65536) tcp_port = (ULONG)p;
        }
        sel_mode  = TRANSPORT_TCP;
        sel_param = tcp_port;
    }
```

- [ ] **Step 2: Replace the `serial_open` block with `transport_open`**

Replace the existing block (lines 265–276):

```c
    /* Open serial device - use high baud for FS-UAE PTY */
    if (serial_open(115200) != 0) {
        printf("  Serial: FAILED\n");
        ui_add_log("ERR: Cannot open serial");
        g_serial_connected = FALSE;
    } else {
        printf("  Serial: OK\n");
        g_serial_connected = TRUE;
        ui_add_log("Serial opened");
        /* Start first async read */
        serial_start_read();
    }
```

with:

```c
    /* Open the selected transport (serial.device or TCP/bsdsocket) */
    if (transport_open(sel_mode, sel_param) != 0) {
        printf("  Transport: FAILED\n");
        ui_add_log("ERR: Cannot open transport");
        g_serial_connected = FALSE;
    } else {
        printf("  Transport: OK\n");
        /* Serial: device is open now -> connected. TCP: listening, but no peer
         * yet -> connected becomes TRUE on accept (rising edge in main loop). */
        g_serial_connected = transport_is_open();
        ui_add_log(sel_mode == TRANSPORT_TCP ? "TCP listening" : "Serial opened");
        transport_start_read();
    }
```

- [ ] **Step 3: Remove the startup READY block (moved to a rising edge)**

Delete the block at lines 308–311:

```c
    /* Send READY to host */
    if (g_serial_connected) {
        protocol_send_raw("READY|1.0");
    }
```

(READY is now sent the moment a peer is present — see Step 6. For serial this fires on the first loop iteration, preserving current behavior.)

- [ ] **Step 4: Source the signal from the transport layer**

Change line 317 from:

```c
    serialSig = serial_get_signal();
```

to:

```c
    serialSig = transport_get_signal();   /* serial port sig OR tcp SIGIO */
```

- [ ] **Step 5: Make the drain block transport-driven (so TCP can accept)**

Replace the serial-drain block (lines 360–368):

```c
        /* Check serial data */
        if (g_serial_connected && (received & serialSig)) {
            char ch;
            while (serial_check_read(&ch)) {
                handle_serial_byte(ch);
                /* Restart async read for next byte */
                serial_start_read();
            }
        }
```

with (note: the gate drops `g_serial_connected` so TCP `accept()` runs even before a peer exists):

```c
        /* Drain the transport (serial bytes, or TCP accept + recv) */
        if (received & serialSig) {
            char ch;
            while (transport_check_read(&ch)) {
                handle_serial_byte(ch);
                transport_start_read();
            }
        }
```

- [ ] **Step 6: Track peer state and send READY on the rising edge**

Immediately AFTER the drain block from Step 5, insert:

```c
        /* Maintain link-connected state; greet a freshly connected peer */
        {
            BOOL now_conn = transport_is_open();
            if (now_conn && !g_serial_connected) {
                g_serial_connected = TRUE;
                protocol_send_raw("READY|1.0");
            } else if (!now_conn && g_serial_connected) {
                g_serial_connected = FALSE;
            }
        }
```

- [ ] **Step 7: Use `transport_close` on shutdown**

Change line 467 from:

```c
    serial_close();
```

to:

```c
    transport_close();
```

- [ ] **Step 8: Ensure `<string.h>` and `<stdlib.h>` are included**

Check the includes near the top of `main.c`. If `<string.h>` (for `strcmp`) or `<stdlib.h>` (for `atol`) is absent, add it alongside the other system includes.

- [ ] **Step 9: Compile the daemon**

Run:
```bash
docker run --rm -v "$(pwd):/work" -w /work amigadev/crosstools:m68k-amigaos make -C amiga-bridge daemon
```
Expected: SUCCEEDS. If `strcmp`/`atol` are reported implicit, complete Step 8.

- [ ] **Step 10: Commit**

```bash
git add amiga-bridge/src/main.c
git commit -m "bridge: select transport via CLI arg; READY on peer connect"
```

---

## Task 6: Update the two external call sites

**Files:**
- Modify: `amiga-bridge/src/protocol_handler.c:96`
- Modify: `amiga-bridge/src/crash_handler.c:188`

- [ ] **Step 1: Route the send path through the transport**

In `protocol_handler.c`, change line 96 from:

```c
    serial_write(sendbuf, len + 1);
```

to:

```c
    transport_write(sendbuf, len + 1);
```

- [ ] **Step 2: Route the crash-dump gate through the transport**

In `crash_handler.c`, change line 188 from:

```c
    if (serial_is_open()) {
```

to:

```c
    if (transport_is_open()) {
```

- [ ] **Step 3: Compile the daemon**

Run:
```bash
docker run --rm -v "$(pwd):/work" -w /work amigadev/crosstools:m68k-amigaos make -C amiga-bridge daemon
```
Expected: SUCCEEDS.

- [ ] **Step 4: Commit**

```bash
git add amiga-bridge/src/protocol_handler.c amiga-bridge/src/crash_handler.c
git commit -m "bridge: route send + crash-dump gate through transport layer"
```

---

## Task 7: Host config + real-hardware docs

**Files:**
- Modify: `devbench.toml`
- Modify: `docs/` (add a short "TCP/RoadShow transport" note; create `docs/tcp-transport.md`)

- [ ] **Step 1: Document the TCP host config in `devbench.toml`**

In `devbench.toml`, under the `[serial]` section, add a comment block documenting the real-hardware/TCP usage (leave the emulator defaults working; this is documentation so users can switch by editing host/port):

```toml
[serial]
mode = "tcp"
host = "127.0.0.1"   # real Amiga: set to the Amiga's LAN IP (run `amiga-bridge TCP 2345` there)
port = 2345          # must match the daemon's TCP port; emulator-over-serial users keep their PTY/TCP bridge port
```

(If your emulator workflow currently relies on a different port, keep it; the TCP-network transport simply points `host` at the Amiga and `port` at the daemon's listen port.)

- [ ] **Step 2: Add `docs/tcp-transport.md`**

Create `docs/tcp-transport.md`:

```markdown
# TCP/RoadShow Transport (network instead of serial)

The `amiga-bridge` daemon can talk to `amiga-devbench` over TCP/IP using the
Amiga's `bsdsocket.library` (RoadShow, AmiTCP, Miami, or an emulator's bsdsocket
emulation) instead of a serial cable. The Amiga listens; devbench connects.

## On the Amiga
1. Have a TCP/IP stack running (RoadShow recommended; `bsdsocket.library` must be available).
2. Run the daemon in TCP mode:

       run >NIL: DH0:Dev/amiga-bridge TCP 2345

   No args = serial (unchanged). `TCP` alone = port 2345. `TCP <port>` = custom port.
3. Note the Amiga's IP (e.g. RoadShow: `ShowNetStatus` or your router's DHCP table).

## On the host
Edit `devbench.toml`:

    [serial]
    mode = "tcp"
    host = "<amiga-ip>"
    port = 2345

    [emulator]
    auto_start = false

Then start devbench and verify with the `amiga_ping` MCP tool.

## Testing without real hardware
WinUAE and FS-UAE provide a built-in `bsdsocket.library` emulation that maps
Amiga sockets to host sockets. Enable it, run `amiga-bridge TCP 2345` inside the
emulator, and point `devbench.toml` at `127.0.0.1:2345`.
```

- [ ] **Step 3: Commit**

```bash
git add devbench.toml docs/tcp-transport.md
git commit -m "docs: document TCP/RoadShow transport + host config"
```

---

## Task 8: Emulator integration test (bsdsocket emulation)

**Files:** none (manual/behavioral verification)

- [ ] **Step 1: Build and deploy the daemon into the emulator**

Build: `make bridge`. Copy `amiga-bridge/amiga-bridge` into the emulator's shared volume so the Amiga can run it.

- [ ] **Step 2: Enable `bsdsocket.library` emulation in the emulator**

WinUAE: Host → "bsdsocket.library (uaenet)" enabled. FS-UAE: `bsdsocket_library = 1`. This makes the emulated Amiga's sockets use the host network.

- [ ] **Step 3: Run the daemon in TCP mode inside the emulator**

In the Amiga Shell:
```
run >NIL: DH0:Dev/amiga-bridge TCP 2345
```
Expected: the bridge window logs `TCP: listening`.

- [ ] **Step 4: Point devbench at it and start**

In `devbench.toml` set `[serial] host = "127.0.0.1"`, `port = 2345`, `[emulator] auto_start = false`. Run:
```bash
python -m amiga_devbench
```
Expected: bridge window logs `TCP: client connected`; devbench shows connected; you see heartbeats.

- [ ] **Step 5: Functional checks via MCP**

From Claude Code call, in order: `amiga_ping`, `amiga_sysinfo`, `amiga_list_clients`.
Expected: ping round-trips; sysinfo returns real values; no errors.

- [ ] **Step 6: Reconnect check**

Stop the daemon (close its window / CTRL-C) and relaunch `amiga-bridge TCP 2345`.
Expected: devbench's reconnect loop re-attaches and re-reads `READY|1.0`; tools work again without restarting devbench.

- [ ] **Step 7: Record results**

If all checks pass, note it in the PR/commit description. If a check fails, debug with the systematic-debugging skill before proceeding to real hardware.

---

## Task 9: Real-hardware validation (RoadShow) — final

**Files:** none (manual)

- [ ] **Step 1: Run on the real Amiga**

With RoadShow up on a real Amiga, copy the daemon over and run:
```
run >NIL: DH0:Dev/amiga-bridge TCP 2345
```

- [ ] **Step 2: Connect devbench to the Amiga's LAN IP**

Set `devbench.toml` `[serial] host = "<amiga-lan-ip>"`, `port = 2345`. Start devbench.

- [ ] **Step 3: Verify the full chain**

`amiga_ping` + `amiga_sysinfo` succeed against the physical Amiga. Confirm stability over a few minutes of heartbeats. Done.

---

## Self-Review

- **Spec coverage:** transport seam (Tasks 1–2, 5–6); net backend listen/accept/recv/send + SIGIO async + non-blocking + single-client + buffered drain (Task 3); READY-on-accept rising edge (Task 5 Steps 3,6); CLI arg selection with serial default (Task 5 Step 1); bsdsocket-missing error handling without fallback (Task 3 `net_open`); send-all with EWOULDBLOCK bound (Task 3 `net_write`); Makefile + build validation incl. header/link fallbacks (Task 4); host config-only change (Task 7); emulator bsdsocket + reconnect testing (Task 8); real-hardware (Task 9). All spec sections map to a task.
- **Placeholder scan:** no TBD/TODO; all code blocks complete; `<amiga-ip>` is an intentional user-supplied value in docs/config, not a code placeholder.
- **Type consistency:** `transport_*`/`net_*` signatures in Task 1 match their definitions in Tasks 2–3 and call sites in Tasks 5–6; `g_transport_mode` (Task 2) matches the extern (Task 1); `g_serial_connected` reused with generalized "peer present" semantics consistently in Task 5; mode constants `TRANSPORT_SERIAL`/`TRANSPORT_TCP` used identically in Tasks 1, 2, 5.
```
