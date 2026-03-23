# Amiga DevBench - Cross-Development Environment

## Project Overview
Amiga cross-development environment with MCP server integration for Claude Code.
Compiles C code on macOS/Linux/Windows via Docker, deploys to AmiKit emulator,
and provides real-time debug monitoring over serial/TCP.

## Architecture (Current)
- **amiga-bridge/**: Amiga-side daemon + client library (libbridge.a). Owns serial.device, IPC via MsgPorts.
  - `amiga-bridge/client/`: Client library apps link against (`ab_init`, `ab_log`, `ab_poll`, etc.)
- **amiga-devbench/**: Host-side Python server (MCP + web UI + serial protocol).
  - Run: `python3 -m amiga_devbench` or `make start`
  - Serves web UI at http://localhost:3000/, MCP at /mcp
- **examples/**: Sample Amiga programs (games, demos, tools) using bridge client lib.
- **docker/**: Dockerfile for cross-compilation environment.
- **scripts/**: Build, deploy, and test scripts.

### Deprecated (do NOT use)
- `amiga-debug-lib/` — Old C library, replaced by amiga-bridge client lib.
- `mcp-server/` — Old TypeScript MCP server, replaced by amiga-devbench.

## Build Requirements
- Docker with `amigadev/crosstools:m68k-amigaos` image
- Python 3.10+ for devbench (`pip install -e amiga-devbench`)
- AmiKit or FS-UAE with serial port exposed as TCP (default: `127.0.0.1:1234`)

## Build Commands
```bash
make setup        # One-time: pip install devbench
make start        # Start devbench (reads devbench.toml)
make bridge       # Build amiga-bridge daemon + libbridge.a
make examples     # Build example apps via Docker
make all          # Build everything
make clean        # Clean all build artifacts
```

## Amiga C Conventions
- Always use `-noixemul` flag (no Unix emulation, pure AmigaOS)
- Target 68020 with `-m68020`
- Include paths: `-I../../amiga-bridge/include`
- Link with: `-L../../amiga-bridge -lbridge -lamiga`
- Use `%ld` with `(long)` cast for printf (amiga.lib `%d` reads 16-bit WORD)
- Use `(long)` cast for all integer format specifiers

## Bridge Client API
```c
#include "bridge_client.h"
ab_init("app_name");                                    // Register with daemon
ab_register_var("name", AB_TYPE_I32, &variable);        // Expose variable
ab_register_hook("name", "description", callback_fn);   // Register remote hook
ab_register_memregion("name", ptr, size);               // Expose memory region
AB_I("format %ld", (long)val);                          // Log info message
ab_poll();                                              // Process bridge messages (call each frame)
ab_cleanup();                                           // Disconnect
```

## Bridge Protocol
Line-based text protocol over serial/TCP, pipe-delimited fields.

### Amiga → Host
- `CLOG|client|level|tick|message` — Client log message
- `CVAR|client|name|type|value` — Variable report
- `HB|tick|free_chip|free_fast|num_clients` — Heartbeat
- `LISTCLIENTS|id|name|...` — Client enumeration
- `HOOK_RESULT|client|hook|status|result` — Hook call result

### Host → Amiga
- `GETVAR|client|name` — Read variable
- `SETVAR|client|name|value` — Write variable
- `CALLHOOK|client|hook|args` — Call registered hook
- `READMEMREG|client|name` — Read memory region
- `SCRIPT|script_text` — Execute AmigaDOS script
- `STOP|client` — Stop a client process

## Emulator Setup
- Sample FS-UAE config: `AmiKit-Debug.fs-uae` in project root (copy and customize)
- Key setting: `serial_port = tcp://0.0.0.0:1234` enables serial-over-TCP
- Emulator config path goes in `devbench.toml` under `[emulator] config = "..."`
- Once running, edit FS-UAE config from web UI: Settings tab → FS-UAE Config Editor
- AmiKit users: run `./scripts/configure-amikit.sh` for automatic setup
- See `/fsuae-setup` skill for detailed walkthrough

## Deploy
Binaries deploy to AmiKit shared folder:
`/Applications/AmiKit.app/Contents/SharedSupport/prefix/drive_c/AmiKit/Dropbox/Dev/`
Amiga sees this as `DH2:Dev/`

## Testing Without Emulator
```bash
python3 -m amiga_devbench --simulator   # Starts with fake Amiga on TCP 1234
```

## Claude Code MCP Config
```json
{
  "mcpServers": {
    "amiga-dev": {
      "type": "streamable-http",
      "url": "http://localhost:3000/mcp"
    }
  }
}
```

## Windows Quick Start
1. Install [Docker Desktop for Windows](https://www.docker.com/products/docker-desktop/)
2. Install Python 3.10+ from [python.org](https://www.python.org/downloads/)
3. Clone the repo: `git clone <repo-url> && cd amiga_mcp`
4. Install devbench: `pip install -e amiga-devbench`
5. Pull cross-compiler: `docker pull amigadev/crosstools:m68k-amigaos`
6. Build everything: `make all` (or use Docker directly on Windows: `docker run --rm -v %cd%:/work -w /work amigadev/crosstools:m68k-amigaos make -C amiga-bridge`)
7. Install [FS-UAE](https://fs-uae.net/) or [WinUAE](https://www.winuae.net/), configure serial as TCP `127.0.0.1:1234`
8. Edit `devbench.toml` — set `deploy_dir` to your emulator's shared folder path
9. Start devbench: `python -m amiga_devbench`
10. Configure Claude Code MCP (see above), then use `amiga_build_deploy_run` to iterate
