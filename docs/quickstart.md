<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Chris Collins <chris@hitorro.com> -->

# Install DevBench from a checkout

[Documentation index](README.md) · [MCP client setup](using-the-mcp.md)

Install the Python host first, verify it with the simulator, then connect a
real target. These are separate steps: a working web page does not prove
that an Amiga is connected.

## 1. Get the source

Install Git and Python 3.10 or newer. On Linux, your distribution may package
`venv` separately (for example, `python3-venv` on Debian/Ubuntu).

```sh
git clone https://github.com/geekychris/amiga_mcp.git
cd amiga_mcp
```

**Already cloned it?** Just change into your checkout; do not run the
one-line installer to create another copy under `~/.amiga-devbench/src`.

The `examples/` submodule is optional until you want to build examples:

```sh
git submodule update --init --recursive
```

## 2. Install the host in a virtual environment

Run these commands from the repository root. Use the environment's Python
explicitly; activation and global `pip` installation are unnecessary.

**macOS / Linux:**

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -e ./amiga-devbench
.venv/bin/python -m pip check
.venv/bin/python -m amiga_devbench --help
```

**Windows PowerShell:**

```powershell
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -e ./amiga-devbench
.\.venv\Scripts\python.exe -m pip check
.\.venv\Scripts\python.exe -m amiga_devbench --help
```

For screenshots and visual comparisons, also install `Pillow` using the
same interpreter: `.venv/bin/python -m pip install Pillow` (or the Windows
interpreter path above). No Node.js or npm setup is needed.

## 3. Create your local configuration

The repository's `devbench.toml` includes author-specific paths and profiles.
Start with the minimal example instead:

```sh
cp docs/devbench.example.toml devbench.local.toml
```

On PowerShell, use `Copy-Item docs/devbench.example.toml devbench.local.toml`.
Both `.venv/` and `devbench.local.toml` are ignored by Git.

Always pass `--config devbench.local.toml` for this setup. This file is a
complete configuration, not an overlay; it avoids selecting the repository's
active profile, starting an emulator or enabling the optional LLM proxy.

## 4. Verify without an Amiga

```sh
.venv/bin/python -m amiga_devbench --config devbench.local.toml --simulator --no-emulator
```

On Windows, substitute `.\.venv\Scripts\python.exe` for `.venv/bin/python`.
Leave the terminal running and open <http://localhost:3000/>. In a second
terminal, check <http://localhost:3000/health> or run:

```sh
curl http://localhost:3000/health
```

Expect `status: "ok"` and, after the handshake, `serial.connected: true`.
The dashboard's clients and changing variables are **simulated data**, not
your emulator. The simulator does not implement every hardware feature. Its current
`PING` response is a heartbeat rather than the `PONG` expected by
`amiga_ping`, so that tool can time out even with a healthy simulator.
Stop the server with Ctrl-C before changing targets.

The current server listens on all host interfaces, not just loopback, and
provides control endpoints without an application login. Run it on a trusted
development network; do not expose port 3000 to the Internet.

## 5. Connect your MCP client

With DevBench running, follow [MCP client setup](using-the-mcp.md). For Codex:

```sh
codex mcp add amiga-dev --url http://localhost:3000/mcp
```

Tool discovery confirms the MCP server is reachable. In simulator mode,
use `amiga_log` and check recent heartbeats in `/health`. On a real target,
`amiga_ping` confirms the bridge is responding. The HTTP client registration does
not start DevBench for you.

## 6. Connect a real target

Restart without `--simulator` after setting up the bridge and editing
`[serial]` in `devbench.local.toml`:

```sh
.venv/bin/python -m amiga_devbench --config devbench.local.toml --no-emulator
```

For local WinUAE and FS-UAE, follow the [dual-emulator guide](winuae-and-fsuae.md).
Choose **one** transport:

| Connection | Amiga side | DevBench side |
|---|---|---|
| Emulator serial exposed as TCP | Run `amiga-bridge` in its serial mode; emulator forwards `serial.device` to a host TCP listener | `[serial] mode = "tcp"`, host and port of the emulator's serial listener |
| Guest TCP/IP network | Run `amiga-bridge TCP 2345` with a working guest socket library | `[serial] mode = "tcp"`, reachable guest IP and port `2345` |
| FS-UAE PTY | Run `amiga-bridge` in serial mode; FS-UAE uses the PTY symlink | `[serial] mode = "pty"`; start DevBench before FS-UAE |

These TCP endpoints carry different transports. An emulator's **GDB port is
neither of them**. If another debugger uses port 2345, choose a different
bridge port and use the same value at both ends.

You must first copy the bridge binary into the guest, using a shared folder,
disk image or an existing transfer method. MCP file transfer cannot bootstrap
a bridge that is not running yet. Start it manually before adding anything
to `S:User-Startup`. See [target setup](targets.md) and
[TCP transport](tcp-transport.md) for configuration examples.

Stock emulators can use the guest bridge. Only the `amiga_fsuae_*` tools
require patched FS-UAE; installing DevBench does not add native debugger
support to other emulators. The local emulator manager is FS-UAE-oriented;
launch other emulators yourself and use `--no-emulator`.

## Optional: build the bridge and examples

Docker must be installed **and its engine running**. Check with `docker info`.
Then, from the repository root:

```sh
docker pull amigadev/crosstools:m68k-amigaos
make bridge
git submodule update --init --recursive
make examples
```

The bridge is built at `amiga-bridge/amiga-bridge`. Do not assume that a
checked-in binary matches your CPU: this checkout may contain a PPC build.
When switching architectures, clean the bridge objects before rebuilding
(`make -C amiga-bridge clean` using the matching compiler container). Configure a shared deploy
directory or copy it manually as described above. `make examples` needs the
submodule; an empty `examples/` directory is not a successful example build.

Without host `make` (for example in PowerShell), run the bridge build inside
the container:

```powershell
docker run --rm -v "${PWD}:/work" -w /work amigadev/crosstools:m68k-amigaos make -C amiga-bridge all
```

For PPC/AmigaOS 4, use the separate [OS4 setup guide](amigaos4-setup.md).

## Update and restart

Stop DevBench, preserve any local source edits, then:

```sh
git pull --ff-only
git submodule update --init --recursive
.venv/bin/python -m pip install -e ./amiga-devbench
.venv/bin/python -m amiga_devbench --config devbench.local.toml --no-emulator
```

Use the Windows interpreter path on Windows. Rebuild the guest bridge when
its source changes. Keep using your local config rather than copying the
repository's machine-specific `devbench.toml` over it.

## Troubleshooting

| Symptom | Check |
|---|---|
| `externally-managed-environment` from pip | Use `.venv` and its Python. No `sudo pip` or `--break-system-packages` is needed. |
| `No module named amiga_devbench` | Use the same `.venv` interpreter for installation and startup. |
| Web UI works but Amiga tools time out | Check `/health`, bridge startup, transport mode and matching port. Call `amiga_connect` if the first connection happened before guest startup. HTTP readiness alone does not establish a guest connection. |
| It tries to launch someone else's FS-UAE config | Pass `--config devbench.local.toml --no-emulator`; the checked-in config has an active profile. |
| MCP connection refused | Start DevBench first; verify the URL ends in `/mcp` and its port matches `[server]`. |
| `amiga_fsuae_*` reports unavailable | Use patched FS-UAE for those tools, or use the guest bridge tools instead. |
| Build cannot reach Docker | Start Docker Desktop/your Docker engine and retry `docker info`. |
| Bridge build reports `unknown type name '_sfdc_vararg'` | The stock m68k image has a socket-header incompatibility. For a classic 32-bit m68k build, use the explicit container command below as a temporary workaround. |
| No examples are built | Initialize the `examples/` submodule. |
| Address already in use | Stop the conflicting process or change the relevant port in the local config and client. |

Use one DevBench process at a time: the current CLI uses a shared PID file
and may stop an older instance when starting a new one.

Temporary workaround for the stock m68k image's socket header (macOS/Linux):

```sh
docker run --rm -v "$PWD:/work" -w /work amigadev/crosstools:m68k-amigaos \
  make -C amiga-bridge 'CC=m68k-amigaos-gcc -D_sfdc_vararg=ULONG'
```

This defines the missing tag-argument type for the 32-bit m68k ABI. It does
not apply to the PPC build. Prefer fixing the toolchain header for a
permanent solution.
