<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Chris Collins <chris@hitorro.com> -->

# Amiga DevBench

Develop Amiga software from a modern computer. DevBench provides a web
dashboard and MCP tools for building programs, transferring files, running
AmigaDOS commands and inspecting an emulated or real Amiga.

**New here? [Install and verify DevBench](docs/quickstart.md).**
The guide works from an existing checkout and starts with a simulator, so
you can verify the host installation before setting up an Amiga.

## What you need

- **Host server:** Python 3.10+ and Git, on macOS, Linux or Windows.
- **A real target:** an emulator with your own ROM/OS, or an Amiga, running
  the `amiga-bridge` guest program. The simulator needs neither.
- **Compiling Amiga programs:** Docker and the appropriate cross-compiler
  image. Docker is not needed just to install the Python server or inspect
  a target with an existing bridge.

## Setup in three commands

```sh
scripts/setup.sh     # install everything that can be installed (68k, os4 or both)
scripts/doctor.sh    # check every prerequisite; each problem comes with its fix
scripts/start.sh     # start the emulators + devbenches, wait until the Amigas answer
```

The Kickstart ROM and the AmigaOS 3.x / 4.1 installs are commercial, so
`doctor.sh` keeps reporting them until they're in place; it says where each
goes. The scripts are bash: on Windows, use WSL (or `scripts/install.ps1`).
See **[SETUP.md](SETUP.md)** for the one-page guide.

The current server is in `amiga-devbench/` (Python). The `mcp-server/`
TypeScript project and `amiga-debug-lib/` are deprecated.

## Start here

| I want to… | Read |
|---|---|
| Set up and start everything with the scripts | [SETUP.md](SETUP.md) |
| Install from a checkout and verify it works | [Installation guide](docs/quickstart.md) |
| Connect Codex CLI or Claude Code | [MCP client setup](docs/using-the-mcp.md) |
| Use WinUAE or FS-UAE | [Dual-emulator setup](docs/winuae-and-fsuae.md) |
| Connect an emulator or physical Amiga | [Target setup](docs/targets.md) |
| Set up AmigaOS 4.1 on QEMU | [AmigaOS 4 setup](docs/amigaos4-setup.md) |
| Choose between guest and emulator debugging | [Debugger guide](docs/debuggers.md) |
| Find a tool or API | [Documentation index](docs/README.md) |

## How it fits together

```text
Codex / Claude Code ── MCP over HTTP ──┐
                                     ├── DevBench ── serial/TCP ── amiga-bridge
Web browser ───────── HTTP ───────────┘                              (Amiga)
```

The dashboard is at `http://localhost:3000/`; the MCP endpoint is
`http://localhost:3000/mcp`. `scripts/start.sh` runs one DevBench per target:
AmigaOS 4 on port 3000 and classic 68k on port 3001. DevBench must stay
running while clients use it.

Most guest tools work through the bridge. Emulator-level `amiga_fsuae_*`
tools separately require [patched FS-UAE](https://github.com/geekychris/fsuae_remote_patch).
Installing the Python server does not install an emulator, ROM or AmigaOS.

[Browse the examples](docs/examples.md) · [Architecture](docs/architecture.md) ·
[Scripted installer](docs/installer.md) · [MIT license](LICENSE)
