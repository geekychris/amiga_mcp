<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Chris Collins -->

# Amiga DevBench — Quickstart (both targets)

DevBench supports two Amiga targets from a single install:

| Target | Emulator | Serial | Toolchain | Deploy |
|---|---|---|---|---|
| **Classic 68k** (AmigaOS 3.x) | FS-UAE (or AmiKit) | 127.0.0.1:1234–2345 | `amigadev/crosstools:m68k-amigaos` (Docker) | AmiKit shared folder or `Deploy` bridge tool |
| **AmigaOS 4.1 PPC** | QEMU `sam460ex` | 127.0.0.1:2346 | `walkero/amigagccondocker:os4-gcc11-*` (Docker) | Direct write into `.hdf` via `amitools` / `xdftool` |

Switch between them by picking a profile in `devbench.toml` (or with `--profile <name>`). The web UI shows an arch badge in the header — orange for `ppc`, blue for `m68k`.

## Install (one-time)

```
brew install qemu lha                                # for OS4 path
pip3 install -e amiga-devbench                       # devbench itself
scripts/install-toolchains.sh                        # docker images for both arches + gdb
scripts/install-amitools.sh                          # rdbtool / xdftool for OS4 HDF I/O
```

Everything else is per-target below.

## Path A — classic AmigaOS 3.x on FS-UAE

Existing AmiKit users have this already; new setup:

```
python3 -m amiga_devbench --profile local-fsuae
```

Web UI at `http://localhost:3000`. Build any example:

```
make -C examples/hello_world
```

Then deploy from the UI or via `amiga_deploy` MCP tool. Setup details in the top-level [`CLAUDE.md`](../CLAUDE.md).

## Path B — AmigaOS 4.1 PPC on QEMU sam460ex

Full walkthrough in [`amigaos4-setup.md`](amigaos4-setup.md). Short version:

```
# One-time: extract OS4 install CD (from your Hyperion purchase),
# init the shared dev HDF, install the OS.
ln -s ~/amiga/AmigaOS4 ~/AmigaOS4                    # if your ISOs live elsewhere
lha xw=. ~/amiga/amiga_os_4.1_sam460/Sam460InstallCD-*.iso.lha \
   -w ~/AmigaOS4/
ln -s Sam460InstallCD-*.iso ~/AmigaOS4/AmigaOS4.1-FE.iso
scripts/init-dev-hdf.sh                              # RDB + DH1 partition + format
scripts/start-qemu-os4.sh --install                  # boot the CD, install OS
# ... click through Media Toolbox + AmigaOS 4.1 installer in the QEMU window ...
scripts/start-qemu-os4.sh                            # boot the installed OS

# Every session:
python3 -m amiga_devbench --profile qemu-os4         # devbench on port 3000
scripts/build-bridge-ppc.sh                          # cross-compile the bridge daemon
scripts/build-example-ppc.sh hello_world             # cross-compile an example
scripts/deploy-os4.sh amiga-bridge/amiga-bridge amiga-bridge   # push to DH1:
scripts/deploy-os4.sh examples/hello_world/hello_world hello_world
```

Bridge daemon auto-starts on OS4 boot (we appended an entry to
`S:User-Startup` — see the "Auto-start" section below).

## Switching between profiles

Two mechanisms, both live in `devbench.toml`:

- **`active_profile`** at the top of the file — picks the default when
  `python3 -m amiga_devbench` runs with no arguments.
- **`--profile <name>`** CLI flag — overrides the default per-invocation.

Available profiles (see `devbench.toml` for the full list):

| Profile | Target | Serial |
|---|---|---|
| `local-fsuae` | Local FS-UAE, classic 68k | PTY symlink |
| `pi-amikit` | AmiKit on a Raspberry Pi over LAN | `amiga.local:2345` |
| `real-amiga` | Real Amiga hardware | LAN IP:2345 |
| **`qemu-os4`** | AmigaOS 4.1 on QEMU sam460ex | `127.0.0.1:2346` |

The web UI's header badge shows the active profile + target arch so you
can tell at a glance whether MCP tools are pointing at 68k or PPC.

## Emulator control from devbench

Each profile has an `emulator_binary` (either an FS-UAE path or a
QEMU wrapper script) and an optional `emulator_config`. The Dashboard's
**Start** / **Stop** / **Restart** buttons drive the profile's binary
directly — you don't have to launch the emulator by hand each time.

QEMU on the `qemu-os4` profile launches via `scripts/start-qemu-os4.sh`.
That script also honours env vars `SERIAL_PORT`, `OS4_DIR`, and `GDB_PORT`,
plus a `--gdb` flag if you want to attach `gdb-multiarch` for CPU-level
debugging.

## Deploy paths

`devbench.toml`'s `deploy_dir` is what MCP's `amiga_deploy` and the web
UI's Deploy button write to. Two flavours:

| Value | Semantics |
|---|---|
| A directory path (e.g. `~/Documents/AmiKit/Dev`) | Shared folder — devbench does a `shutil.copy2` |
| An `.hdf` file (e.g. `~/AmigaOS4/amigaos4-dev.hdf`) | Hardfile — devbench shells out to `scripts/deploy-os4.sh` (xdftool + optional bridge `diskchange` nudge) |

The `qemu-os4` profile ships `deploy_dir` pointing at the dev HDF, so
the standard deploy tools work transparently across both targets.

## Auto-start the bridge

Once the bridge daemon is on the target Amiga (either as an AmiKit
shortcut for classic or as `DH1:amiga-bridge` on OS4), have it start
at boot by appending to `S:User-Startup`:

```
; auto-start amiga-bridge daemon
Run >NIL: DH1:amiga-bridge     ; OS4 path
```

You can push that entry from the host via the bridge itself once it's
running the first time:

```
curl -X POST http://localhost:3000/api/dos/exec \
  -H 'Content-Type: application/json' \
  -d '{"command":"echo >>S:User-Startup \"Run >NIL: DH1:amiga-bridge\""}'
```

## Debugging OS4 with GDB

QEMU's built-in GDB stub gives CPU-level debug:

```
scripts/start-qemu-os4.sh --gdb          # opens gdb stub on TCP 1234
scripts/gdb-os4.sh examples/hello_world/hello_world
```

Inside gdb:

```
(gdb) info registers                       # PowerPC register file
(gdb) x/16i $pc                            # disassemble around program counter
(gdb) x/32wx $r1                           # dump stack
(gdb) bt                                   # backtrace (needs loaded symbols)
```

The stub sees the whole emulated machine — not process-level. Load a
symbol file (`file examples/…/binary.elf`) for named-function tracing on
that specific PPC ELF.

## What's ported to PPC so far

- `amiga-bridge` (daemon + libbridge client library) — most subsystems
  work; `snoop`, `debugger`, `crash_handler`, and `pool_tracker` are
  stubbed with "not-implemented on OS4" replies (need proper OS4 ports
  that use IExec-> interface pattern instead of 68k inline asm register
  captures).
- `examples/hello_world` — full parity, runs on both arches
- `examples/void_trader` — compiles + runs, but frame pacing is broken
  on PPC (Delay(1) and DateStamp busy-poll both trigger DSI — root
  cause TBD via GDB session). Audio subsystem stubbed silent.

Anything not in that list is still 68k-only. Portability rule of thumb:
if it uses only text protocols, RastPort/BitMap/Window APIs, and
graphics.library primitives (no inline asm, no direct chip register
access, no SetFunction patches, no MOD player), it should port with a
Makefile change and a couple of `#ifndef __PPC__` guards on library-base
declarations.
