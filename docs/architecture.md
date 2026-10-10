<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Chris Collins <chris@hitorro.com> -->

# Architecture

[Documentation index](README.md) · [Install DevBench](quickstart.md)

## System Architecture

### High-Level Data Flow

```mermaid
sequenceDiagram
    participant CC as Claude Code
    participant MCP as MCP Server
    participant WEB as Web API
    participant SER as Serial Connection
    participant BRG as amiga-bridge (Amiga)
    participant APP as Client App (Amiga)

    CC->>MCP: amiga_ping()
    MCP->>SER: send("PING")
    SER->>BRG: PING\n
    BRG->>SER: PONG|2|1649344|6459000\n
    SER->>MCP: event("pong", data)
    MCP->>CC: "Amiga alive - clients: 2"

    Note over BRG,APP: IPC via Exec MsgPort
    CC->>MCP: amiga_get_var("ball_speed")
    MCP->>SER: GETVAR|ball_speed
    BRG->>APP: ABMSG_VAR_GET (IPC)
    APP->>BRG: reply with value
    BRG->>SER: VAR|ball_speed|i32|10
    SER->>MCP: event("var", data)
    MCP->>CC: "ball_speed = 10"
```

### Component Layers

```mermaid
graph TB
    subgraph "Layer 4 — User Interfaces"
        CC[Claude Code<br/>MCP Client]
        WEB[Web Dashboard<br/>localhost:3000]
    end

    subgraph "Layer 3 — Host Server"
        MCP_TOOLS[MCP Tools<br/>45+ tools]
        WEB_API[Web API<br/>80+ endpoints]
        SSE[SSE Event Stream]
        BUILDER[Builder<br/>Docker wrapper]
        DEPLOYER[Deployer<br/>Shared folder copy]
    end

    subgraph "Layer 2 — Transport"
        PROTO[Protocol Parser<br/>Line-based, pipe-delimited]
        SERIAL[Serial Connection<br/>TCP or PTY mode]
        EVENTBUS[EventBus<br/>Async pub/sub]
        STATE[AmigaState<br/>In-memory cache]
    end

    subgraph "Layer 1 — Amiga Side"
        DAEMON[amiga-bridge daemon<br/>Owns serial.device]
        IPC[IPC Manager<br/>MsgPort message routing]
        SYSINFO[System Inspector<br/>Tasks, libs, memory]
        FS[Filesystem Access<br/>Read, write, list]
        PROC[Process Launcher<br/>Launch, break, stop]
        CLIENT_REG[Client Registry<br/>Track connected apps]
    end

    subgraph "Layer 0 — Client Apps"
        LIB[libbridge.a<br/>Client library]
        APP1[hello_world]
        APP2[bouncing_ball]
        APP3[system_monitor]
    end

    CC --> MCP_TOOLS
    WEB --> WEB_API
    WEB --> SSE
    MCP_TOOLS --> EVENTBUS
    WEB_API --> EVENTBUS
    SSE --> EVENTBUS
    MCP_TOOLS --> SERIAL
    WEB_API --> SERIAL
    EVENTBUS --> STATE
    SERIAL --> PROTO
    PROTO --> DAEMON
    DAEMON --> IPC
    DAEMON --> SYSINFO
    DAEMON --> FS
    DAEMON --> PROC
    DAEMON --> CLIENT_REG
    IPC --> LIB
    LIB --> APP1
    LIB --> APP2
    LIB --> APP3
```

### Amiga-Side IPC Architecture

```mermaid
graph LR
    subgraph "amiga-bridge daemon"
        MAIN[Main Loop<br/>Wait on signals] --> SER_IO[Serial I/O<br/>Async read/write]
        MAIN --> IPC_MGR[IPC Manager<br/>AMIGABRIDGE port]
        MAIN --> WIN[Status Window<br/>Diagnostic output]
    end

    subgraph "Client App 1"
        C1[ab_init] -->|FindPort| IPC_MGR
        C1_LOG[ab_log] -->|PutMsg| IPC_MGR
        C1_VAR[ab_push_var] -->|PutMsg| IPC_MGR
        C1_POLL[ab_poll] -->|GetMsg| IPC_MGR
    end

    subgraph "Client App 2"
        C2[ab_init] -->|FindPort| IPC_MGR
    end

    SER_IO -->|serial.device| SERIAL_HW[Serial Port<br/>TCP/PTY to Host]

    style MAIN fill:#e07020,color:#fff
    style SER_IO fill:#4a90d9,color:#fff
    style IPC_MGR fill:#44cc44,color:#000
```

The daemon owns the serial port exclusively. Client apps never touch serial —
they communicate via AmigaOS MsgPort IPC (`FindPort("AMIGABRIDGE")` + `PutMsg/GetMsg`).

---

## Component Deep Dives

### amiga-bridge (Amiga Daemon)

The heart of the Amiga side. A single process that:

1. **Opens serial.device** with async I/O (SendIO/CheckIO) for non-blocking reads
2. **Creates MsgPort "AMIGABRIDGE"** for client app registration
3. **Runs a Wait() loop** on serial + IPC + window signals
4. **Routes messages** between serial (host) and IPC (client apps)
5. **Provides system inspection** without requiring a client app (task list, memory, etc.)

| Source File | Responsibility |
|---|---|
| `main.c` | Event loop, status window, signal handling |
| `serial_io.c` | serial.device open/close, async read/write |
| `ipc_manager.c` | MsgPort creation, message routing |
| `client_registry.c` | Track active clients by name/ID |
| `protocol_handler.c` | Parse host commands, format responses (~1400 lines) |
| `system_inspector.c` | Task/lib/device/volume listing, memory inspection |
| `fs_access.c` | Directory listing, file read/write, rename, copy, protect, comment, checksum (CRC32), append |
| `process_launcher.c` | Launch processes (async), path validation, CTRL-C, process tracking (up to 16), signal delivery |

**Key design decisions:**

- All large buffers are `static` to avoid 4KB default stack overflow
- Uses `Forbid()/Permit()` for task list iteration (minimal critical sections)
- Volatile byte-by-byte reads for memory inspection (CopyMem returns zeros in FS-UAE for some regions)
- Process launcher validates path with `Lock()` before launch, suppresses requesters with `pr_WindowPtr = -1`
- Custom chip registers ($DFF000) blocked from reads (byte access corrupts word-only registers)

### amiga-devbench (Host Server)

A single Python application that combines:

```mermaid
graph TB
    subgraph "amiga-devbench process"
        UVICORN[Uvicorn ASGI Server<br/>Port 3000]
        STARLETTE[Starlette Routes<br/>Web API + Static Files]
        FASTMCP[FastMCP<br/>MCP Tools]
        SERIAL_CONN[SerialConnection<br/>TCP or PTY]
        EVENT_BUS[EventBus<br/>Async pub/sub]
        STATE_MGR[AmigaState<br/>Logs, vars, clients]
        BUILDER_MOD[Builder<br/>Docker cross-compile]
        DEPLOYER_MOD[Deployer<br/>Copy to shared folder]
    end

    UVICORN --> STARLETTE
    UVICORN --> FASTMCP
    STARLETTE --> EVENT_BUS
    FASTMCP --> EVENT_BUS
    SERIAL_CONN --> EVENT_BUS
    EVENT_BUS --> STATE_MGR

    style UVICORN fill:#e07020,color:#fff
```

#### UI

See [Web UI Reference](web-ui.md#web-ui-reference) for detailed documentation with screenshots of all 8 tabs.

| Module | Purpose |
|---|---|
| `server.py` | Starlette app, all HTTP/SSE endpoints, PID file singleton |
| `mcp_tools.py` | 45+ MCP tool definitions using FastMCP |
| `protocol.py` | `parse_message()` and `format_command()` — protocol codec |
| `serial_conn.py` | `SerialConnection` class — PTY creation, TCP connect, auto-reconnect |
| `state.py` | `AmigaState` (log buffer, var cache) + `EventBus` (async queue-based pub/sub) |
| `builder.py` | `Builder` class — wraps `docker run` for cross-compilation |
| `deployer.py` | `Deployer` class — copies binaries to AmiKit shared folder |
| `simulator.py` | Fake Amiga that speaks the bridge protocol (for testing without emulator) |
| `__main__.py` | CLI entry point with argparse |

**Connection modes:**

- **PTY mode** (default): Creates a pseudo-terminal at `/tmp/amiga-serial`, FS-UAE opens it as its serial port
- **TCP mode** (`--serial-host`): Connects to FS-UAE's TCP serial port (e.g., `tcp://0.0.0.0:1234`)

### libbridge.a (Client Library)

Static library that Amiga apps link against. Provides a simple API for:

- Registering with the daemon
- Logging (printf-style, 4 severity levels)
- Registering variables for remote inspection/modification
- Registering hooks (functions the host can call)
- Registering memory regions (named areas the host can read)
- Polling for incoming commands
- Sending heartbeats

**Resource limits per client:** 32 variables, 16 hooks, 8 memory regions

**Message pool:** 4 pre-allocated `BridgeMsg` structures (no malloc per call)

---
