# Setup

Amiga DevBench drives two kinds of Amiga, each with its own emulator and its
own devbench (web UI + MCP server):

| Target | Emulator | Devbench |
|---|---|---|
| **Classic Amiga · 68k** (AmigaOS 3.x) | FS-UAE | http://localhost:3001/ |
| **AmigaOS 4.1 · PowerPC** | QEMU (sam460ex) | http://localhost:3000/ |

You can set up either one or both. Everything below takes `68k`, `os4` or
`all` (the default).

## 1. Install

```sh
git clone --recurse-submodules https://github.com/geekychris/amiga_mcp.git
cd amiga_mcp
scripts/setup.sh            # or: scripts/setup.sh 68k / scripts/setup.sh os4   (make setup)
```

`setup.sh` installs everything that can be installed automatically:

- Python 3.10+, Docker (and starts it), and the `examples` submodule;
- devbench itself (`pip install -e amiga-devbench`);
- the cross-compilers: Docker images for 68k and PowerPC;
- **68k:** FS-UAE; builds the bridge daemon and copies it into the folder
  the Amiga sees as `DH2:`;
- **OS4:** QEMU, lha, amitools; builds software OpenGL (OSMesa) and the
  bridge daemon, and copies it onto the OS4 dev disk.

It's safe to re-run, and it finishes by running the check in step 2.
It works on macOS (Homebrew) and Debian/Ubuntu (apt); on Windows, use
`scripts/install.ps1`.

**What it can't install for you** (they're commercial):

- **68k:** a Kickstart 3.x ROM and an AmigaOS 3.x system disk (e.g. from
  Amiga Forever or AmiKit). Set their paths in the FS-UAE config named in
  `devbench.toml` (`[emulator] config`). `AmiKit-Debug.fs-uae` in the repo
  root is a starting point.
- **OS4:** AmigaOS 4.1 Final Edition (Hyperion). Install it once with
  `scripts/start-qemu-os4.sh --install`, following
  [docs/amigaos4-setup.md](docs/amigaos4-setup.md).

On each Amiga, start the bridge daemon at boot. `setup.sh` copies the 68k
daemon into the folder `deploy_dir` names in `devbench.toml` (the
`local-fsuae` profile). The emulator must mount that folder; with the
sample FS-UAE config it's `DH2:Dev`. Use whatever Amiga path that folder
has on yours. The OS4 daemon goes onto the dev disk, `DH1:`. Add these
lines to the startup sequence:

- **68k**, in `S:Startup-Sequence` or `S:User-Startup`:
  ```
  If EXISTS DH2:Dev/amiga-bridge
    Run >NIL: DH2:Dev/amiga-bridge
  EndIf
  ```
- **OS4**, in `S:User-Startup`:
  ```
  Run >NIL: DH1:amiga-bridge
  ```

## 2. Check

```sh
scripts/doctor.sh           # or: scripts/doctor.sh 68k / os4               (make doctor)
```

This checks every prerequisite and changes nothing. Each line is ✓ (fine),
! (optional, a warning) or ✗ (missing), and every ✗ comes with the exact
fix. It exits 0 when the chosen target is ready.

## 3. Start

```sh
scripts/start.sh            # both                                          (make start)
scripts/start.sh 68k        # classic Amiga only                            (make start-68k)
scripts/start.sh os4        # AmigaOS 4 only                                (make start-os4)
scripts/start.sh status     # what's running                                (make status)
scripts/start.sh stop       # stop emulators and devbenches                 (make stop)
```

(`make start` used to start a single devbench on the active profile; that's
now `make devbench`.)

`start.sh` starts each devbench, which starts its emulator. It then waits
until the Amiga has booted and its bridge answers, and prints the URLs.
Logs are written to `~/.amiga-devbench/logs/`.

Each devbench's web UI says which Amiga it drives: an orange **AmigaOS 4 ·
PowerPC** banner or a blue **Classic Amiga · 68k** one. Its Dashboard has
Start / Restart / Stop for the emulator.

## 4. Connect Claude Code

```json
{
  "mcpServers": {
    "amiga-os4": { "type": "streamable-http", "url": "http://localhost:3000/mcp" },
    "amiga-68k": { "type": "streamable-http", "url": "http://localhost:3001/mcp" }
  }
}
```

Keep only the entry for the target you use if you don't need both.

## Building and running something

```sh
scripts/build-example-68k.sh spectral_keep      # classic 68k
scripts/build-example-ppc.sh spectral_keep      # AmigaOS 4
```

To build every 68k example, use `make examples`. See
[examples/README.md](examples/README.md) for what's there, and CLAUDE.md
for the build conventions and the deploy paths.

## Other scripts

`start.sh`, `setup.sh` and `doctor.sh` are the front door. The lower-level
scripts they use, or that predate them, still work:

- `install-toolchains.sh`: just the Docker images.
- `start-qemu-os4.sh`: QEMU on its own. It has `--install`, `--gdb`, and
  `AUDIO=1` for the sound card.
- `start-fsuae.sh`, `start-all.sh`, `devbench-start.sh`: older starters
  for a single FS-UAE setup.
- `install.sh`: a `curl | bash` installer that clones the repo first.
