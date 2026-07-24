# Amiga DevBench - Cross-Development Environment

## Project Overview
Amiga cross-development environment with MCP server integration for Claude Code.
Compiles C code on macOS/Linux/Windows via Docker, deploys to Amiga emulators,
and provides real-time debug monitoring over serial/TCP.

**Two target architectures supported:**
- **Classic 68k** (AmigaOS 3.x) on FS-UAE / AmiKit — the original path
- **PowerPC OS4** (AmigaOS 4.1) on QEMU sam460ex — newer path, see
  [`docs/amigaos4-setup.md`](docs/amigaos4-setup.md) and
  [`docs/quickstart.md`](docs/quickstart.md)

Switch between them with `python3 -m amiga_devbench --profile <name>`
(profiles live in `devbench.toml`). The web UI header shows the active
profile + target arch as a coloured badge.

## Architecture (Current)
- **amiga-bridge/**: Amiga-side daemon + client library (libbridge.a). Owns serial.device, IPC via MsgPorts.
  - `amiga-bridge/client/`: Client library apps link against (`ab_init`, `ab_log`, `ab_poll`, etc.)
- **amiga-devbench/**: Host-side Python server (MCP + web UI + serial protocol).
  - Run: `python3 -m amiga_devbench` or `make start`
  - Serves web UI at http://localhost:3000/, MCP at /mcp
- **examples/**: Git submodule — sample Amiga programs (games, demos, tools) using bridge client lib. Lives at [github.com/geekychris/amiga_games](https://github.com/geekychris/amiga_games). Clone with `--recurse-submodules` or run `git submodule update --init` after cloning.
- **docker/**: Dockerfile for cross-compilation environment.
- **scripts/**: Build, deploy, and test scripts.

### Related repos (separate from this checkout)
- **[github.com/geekychris/python-amigaos4](https://github.com/geekychris/python-amigaos4)** — CPython 3.12 port for AmigaOS 4.1 PPC. Uses the same walkero cross-compile toolchain; will eventually integrate with the bridge client for scriptable Amiga automation from devbench.

### Deprecated (do NOT use)
- `amiga-debug-lib/` — Old C library, replaced by amiga-bridge client lib.
- `mcp-server/` — Old TypeScript MCP server, replaced by amiga-devbench.

## Build Requirements

**Classic 68k target:**
- Docker with `amigadev/crosstools:m68k-amigaos` image
- Python 3.10+ for devbench (`pip install -e amiga-devbench`)
- AmiKit or FS-UAE with serial port exposed as TCP (default: `127.0.0.1:1234`)

**PowerPC OS4 target (additional):**
- Docker with `walkero/amigagccondocker:os4-gcc11-{arm64,amd64}` (host-arch-dependent)
- QEMU with sam460ex machine (`brew install qemu`)
- `lha` for extracting the OS4 install ISO from Hyperion (`brew install lha`)
- Python `amitools` (via pip, for reading/writing HDF hardfiles from macOS)
- AmigaOS 4.1 Final Edition — commercial, from Hyperion Entertainment

**One-shot install of all Docker images (both arches + gdb-multiarch):**
```
scripts/install-toolchains.sh
```

## Build Commands

**Classic 68k (default):**
```bash
make setup        # One-time: pip install devbench
make start        # Start devbench (reads devbench.toml)
make bridge       # Build amiga-bridge daemon + libbridge.a (68k)
make examples     # Build example apps via Docker (68k)
make all          # Build everything (68k)
make clean        # Clean all build artifacts
```

**PowerPC OS4:**
```bash
scripts/install-toolchains.sh                     # one-time: docker pulls
scripts/build-bridge-ppc.sh                       # build amiga-bridge daemon + libbridge (PPC)
scripts/build-example-ppc.sh hello_world          # build a single example (PPC)
scripts/build-example-ppc.sh void_trader          # ditto (needs OS4-porting per example)

# Devbench with the OS4 profile
python3 -m amiga_devbench --profile qemu-os4

# Boot the OS4 emulator (QEMU sam460ex)
scripts/start-qemu-os4.sh                         # normal boot
scripts/start-qemu-os4.sh --install               # boot install CD (first time)
scripts/start-qemu-os4.sh --gdb                   # boot + open GDB stub on TCP 1234

# Deploy into the dev HDF (works while OS4 is running — auto diskchange)
scripts/deploy-os4.sh path/to/binary target-name

# Debug PPC binary via QEMU's GDB stub (from --gdb launch)
scripts/gdb-os4.sh amiga-bridge/amiga-bridge      # gdb-multiarch attached
```

## Amiga C Conventions (classic 68k)
- Always use `-noixemul` flag (no Unix emulation, pure AmigaOS)
- Target 68020 with `-m68020`
- Include paths: `-I../../amiga-bridge/include`
- Link with: `-L../../amiga-bridge -lbridge -lamiga`
- Use `%ld` with `(long)` cast for printf (amiga.lib `%d` reads 16-bit WORD)
- Use `(long)` cast for all integer format specifiers

## Amiga C Conventions (PPC OS4)
- Compile with `-mcrt=newlib -O2 -mcpu=440 -Wall -D__PPC__ -D__USE_INLINE__ -D__USE_OLD_TIMEVAL__`
- Link with `-mcrt=newlib -L../../amiga-bridge -lbridge -lauto` (drop `-noixemul`, `-lamiga`)
- `-D__USE_INLINE__` pulls in inline4/*.h so classic call names
  (`GetMsg`, `OpenWindow`, etc.) work as macros that dispatch to
  `IExec->GetMsg()` etc. Without it, you get "implicit declaration" wall.
- `-D__USE_OLD_TIMEVAL__` keeps `struct timerequest` fields `tr_node`
  / `tr_time` / `tv_secs` / `tv_micro` intact (OS4 renamed the base
  struct to `TimeRequest`/`TimeVal` to avoid POSIX collision).
- **Never** declare `struct ExecBase *SysBase`, `struct IntuitionBase
  *IntuitionBase`, `struct GfxBase *GfxBase`, or `struct DosLibrary
  *DOSBase` on PPC — OS4's `<proto/*>` already declares them as
  `struct Library *`. Gate any such declarations with `#ifndef __PPC__`.
- **Do not use `Delay(1)`** as a frame-pacing primitive — triggers DSI
  under our current -lauto / newlib setup. Root cause TBD; use
  `WaitTOF()` for VBlank sync or timer.device IORequest for reliable
  small delays.
- **No direct chip register access** (`custom.color[]`, `custom.dmacon`,
  `$DFF000`). sam460ex has no chip RAM. Use `graphics.library`
  (`WritePixel`, `RectFill`, `Draw`) or CGX APIs.
- **No inline `__asm("dN")` register captures** — 68k-only. If a
  source file uses these, gate it out of the PPC build (see how the
  amiga-bridge Makefile splits `DAEMON_SRCS_PORTABLE` vs
  `DAEMON_SRCS_68K_ONLY`).
- Same `%ld` + `(long)` cast rule applies — OS4 sprintf isn't any
  more forgiving of int-vs-long mixups than classic amiga.lib.

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

**Classic 68k (AmiKit shared folder):**
`/Applications/AmiKit.app/Contents/SharedSupport/prefix/drive_c/AmiKit/Dropbox/Dev/`
Amiga sees this as `DH2:Dev/`.

**PowerPC OS4 (dev HDF via amitools):**
`~/AmigaOS4/amigaos4-dev.hdf` — devbench + `scripts/deploy-os4.sh`
write straight into the raw hardfile via `xdftool`. OS4 sees this as
`DH1:` (labelled `DevDrive:` after `init-dev-hdf.sh`).

**⚠️ Concurrency:** the deploy writes to the raw HDF from macOS while
OS4 (inside QEMU) has the same file open as a block device. Both sides
maintain their own cache of the filesystem, so a write from macOS
during OS4 operation can corrupt the on-disk state. `deploy-os4.sh`
detects an attached QEMU with `pgrep` and refuses by default; set
`FORCE=1` only when you accept the risk. The `diskchange DH1:` nudge
the script fires after a write only refreshes OS4's directory
listing — it does **not** make the write atomic or safe against a
concurrent read from OS4.

The `qemu-os4` profile in `devbench.toml` already points `deploy_dir`
at that HDF; MCP's `amiga_deploy` tool and the web UI's Deploy button
detect the `.hdf` suffix and shell out to `deploy-os4.sh` transparently.

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
3. Clone the repo (with submodules): `git clone --recurse-submodules <repo-url> && cd amiga_mcp` — or after cloning, run `git submodule update --init` to fetch the `examples/` submodule
4. Install devbench: `pip install -e amiga-devbench`
5. Pull cross-compiler: `docker pull amigadev/crosstools:m68k-amigaos`
6. Build everything: `make all` (or use Docker directly on Windows: `docker run --rm -v %cd%:/work -w /work amigadev/crosstools:m68k-amigaos make -C amiga-bridge`)
7. Install [FS-UAE](https://fs-uae.net/) or [WinUAE](https://www.winuae.net/), configure serial as TCP `127.0.0.1:1234`
8. Edit `devbench.toml` — set `deploy_dir` to your emulator's shared folder path
9. Start devbench: `python -m amiga_devbench`
10. Configure Claude Code MCP (see above), then use `amiga_build_deploy_run` to iterate
