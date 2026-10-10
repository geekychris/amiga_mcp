<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Chris Collins <chris@hitorro.com> -->

# Scripted installer reference

[Documentation index](README.md) · [Install DevBench](quickstart.md)

The examples below describe specific setups. For a new installation, start
with the [checkout installation guide](quickstart.md) and use your own paths
and a separate local configuration.

## One-line install

Brew-style installer — fetches the source, installs the Python host server, pulls the m68k Docker cross-compiler, builds the bridge daemon + examples, and launches the web UI on http://localhost:3000. Re-running pulls the latest commit and rebuilds in place.

**macOS / Linux:**

```sh
curl -fsSL https://raw.githubusercontent.com/geekychris/amiga_mcp/main/scripts/install.sh | bash
```

**Windows (PowerShell):**

```powershell
iwr -useb https://raw.githubusercontent.com/geekychris/amiga_mcp/main/scripts/install.ps1 | iex
```

The installer requires `git`, `python>=3.10`, and Docker. On mac/Linux you can opt into auto-install of missing CLI tools with `AMIGA_MCP_AUTO_INSTALL=1`. Docker Desktop on macOS/Windows needs a GUI install regardless.

**What it does NOT install:** FS-UAE itself (you need a Kickstart ROM — see [FS-UAE Emulator Setup](targets.md#fs-uae-emulator-setup)). The optional [patched fs-uae fork](https://github.com/geekychris/fsuae_remote_patch) with the HTTP debugger is opt-in via `AMIGA_MCP_BUILD_PATCHED=1` (Linux + macOS). DevBench works fine with stock fs-uae either way; the patched build unlocks the **FS-UAE** tab in the Web UI and the `amiga_fsuae_*` MCP tools.

**Knobs** (env vars before invoking):

| Variable | Default | Meaning |
|---|---|---|
| `AMIGA_MCP_SRC` | `$HOME/.amiga-devbench/src` | Where to clone the repo |
| `AMIGA_MCP_REF` | `main` | Git ref to check out |
| `AMIGA_MCP_REPO` | `https://github.com/geekychris/amiga_mcp.git` | Remote |
| `AMIGA_MCP_BUILD` | `1` | Build examples via Docker (`0` to skip) |
| `AMIGA_MCP_START` | `1` | Launch web UI in background (`0` to install only) |
| `AMIGA_MCP_OPEN` | `1` | Open browser when ready (`0` to suppress) |
| `AMIGA_MCP_AUTO_INSTALL` | `0` | (mac/Linux) `1` to brew/apt/dnf install missing deps |
| `AMIGA_MCP_BUILD_PATCHED` | `0` | (mac/Linux) `1` to clone+build the patched fs-uae fork into `~/.amiga-devbench/fs-uae`. Combine with `AMIGA_MCP_AUTO_INSTALL=1` to also install the ~10 system libs it needs. Takes ~10 min. |

### How devbench picks the fs-uae binary

`[emulator] binary` in `devbench.toml` accepts the literal `"auto"` (default). When `auto`, devbench searches in order:

1. `$AMIGA_MCP_FSUAE_BIN` env var
2. `~/.amiga-devbench/fs-uae` (installed by `AMIGA_MCP_BUILD_PATCHED=1`)
3. `/tmp/fsuae-src/fs-uae` (default output path of the patched fork's `build.sh`)
4. `~/code/fsuae_remote_patch/fs-uae` (common dev checkout)
5. `fs-uae` on `PATH` (stock build from Homebrew / apt / etc.)

It prefers the patched build when found (probed by scanning the binary for the `fs-uae-rpc` service string). Set `binary` to an explicit path to pin a specific build. Check `/api/emulator/status` (`patched: true|false`) or the devbench startup log to confirm which one was selected.

The web UI runs in the background under `$HOME/.amiga-devbench/run/devbench.pid` with logs in `$HOME/.amiga-devbench/logs/`. Stop with `kill $(cat ~/.amiga-devbench/run/devbench.pid)` (or `Stop-Process` on Windows).

---
