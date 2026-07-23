# AmigaOS 4.1 Setup Guide

Step-by-step install of AmigaOS 4.1 Final Edition on QEMU sam460ex,
matched to the scripts in this repo. Written against the reality of a
working macOS setup — the aspirational full plan lives in
[`amigaos4-ppc-plan.md`](amigaos4-ppc-plan.md).

## Prerequisites

- **macOS** (Apple Silicon or Intel). Windows / Linux paths differ.
- **QEMU** with `qemu-system-ppc` and the `sam460ex` machine:
  ```
  brew install qemu
  qemu-system-ppc --version           # 11.x tested
  qemu-system-ppc -machine help | grep sam460ex
  ```
- **lha** for extracting the install ISO:
  ```
  brew install lha
  ```
- **Docker** (only needed later for the PPC cross-compiler).
- **AmigaOS 4.1 Final Edition** — purchased from
  [Hyperion Entertainment](https://www.hyperion-entertainment.com/).
  Ships as `.iso.lha` or ISO files; both work.

## Asset layout

Scripts assume `$HOME/AmigaOS4/` contains:

```
~/AmigaOS4/
├── amigaos4-system.hdf        # 2 GB target for OS install
├── amigaos4-dev.hdf           # 512 MB shared dev drive
└── AmigaOS4.1-FE.iso          # Install CD
```

If your purchased assets live elsewhere (e.g. `~/amiga/AmigaOS4/`),
symlink instead of copying — the scripts follow symlinks:

```
ln -s ~/amiga/AmigaOS4 ~/AmigaOS4
```

If the ISO is `.iso.lha`, extract and symlink to the expected name:

```
cd ~/AmigaOS4
lha xw=. ~/path/to/Sam460InstallCD-*.iso.lha
ln -s Sam460InstallCD-*.iso AmigaOS4.1-FE.iso
```

If the HDF containers don't exist yet, `start-qemu-os4.sh` creates
sparse 2 GB and 512 MB images on first run.

## First-time install

### 1. Boot the install CD

```
bash scripts/start-qemu-os4.sh --install
```

What the script does:

- Attaches `amigaos4-system.hdf` on IDE slot 0 (target)
- Attaches `AmigaOS4.1-FE.iso` on IDE slot 1 (source)
- Sets `-boot d` to boot from the CD
- 1 GB RAM (512 MB wedged mid-boot in testing)
- Uses `-nic none` (sam460ex doesn't emulate rtl8139)
- Serial port exposed on TCP `127.0.0.1:2346`
- Cocoa display with `zoom-to-fit=on` and auto-resize to 960×720

QEMU auto-loads its bundled `u-boot-sam460.bin` firmware for sam460ex
— no separate U-Boot download is needed. Boot to installer takes
~2 minutes.

### 2. Complete the Workbench installer

The install CD's Workbench comes up with a welcome window offering
four icons. **The "Booting" text under the boing wallpaper is the CD's
background image — not a live boot indicator.** The four icons are:

1. **Set locale / keymap** — optional, skip if English default is fine
2. **Media Toolbox** — partition + format the target HDD (required)
3. **AmigaOS 4.1 installation utility** — copies the OS to HDD
4. Explore CD contents

Order: **#2 → #3**.

**In Media Toolbox:**
- Select the first HDD (the 2 GB `amigaos4-system.hdf`).
- Install RDB (Rigid Disk Block).
- Create one partition covering the whole disk — name it `DH0:`.
- Set filesystem to **Smart FileSystem (SFS)**.
- Format the partition.

**In the AmigaOS 4.1 installation utility:**
- Choose `DH0:` as target.
- Accept defaults for locale, keyboard, and packages unless you want
  to prune.
- Copy takes ~10–20 min under QEMU emulation.

### 3. Boot from HDD

Shutdown OS4 from the Workbench menu once install is done. Then
relaunch **without** `--install`:

```
bash scripts/start-qemu-os4.sh
```

This attaches both HDDs (system + dev), drops the CD, and lets U-Boot
autoboot from the newly-installed system partition. The first HDD boot
lands you at the **First Boot Wizard** — a series of icons that
configure network, USB, screen, and user preferences.

Complete or close the wizard to reach the standard Workbench.

### 4. Fix the graphics resolution

sam460ex under QEMU boots at 640×480×8-bit by default, which looks
dithered when Cocoa's zoom-to-fit scales it. From Workbench:

1. Open **Workbench** disk → **Prefs** drawer → **ScreenMode**
2. Choose a higher resolution + depth (e.g. **1024×768 × 24-bit** if
   offered)
3. Click **Save** — the screen flashes and returns at the new mode

If only 8-bit modes are offered, the video driver isn't loaded — see
"Video driver troubleshooting" below.

## Applying updates

The purchased bundle usually includes cumulative updates:

- `AmigaOS4.1FinalEditionUpdate1.lha`
- `AmigaOS4.1FinalEditionUpdate2-53.14.lha`
- `AmigaOS4.1FinalEditionUpdate3-53.34.lha`

Route them into OS4 via the dev HDD:

1. On macOS host, mount `amigaos4-dev.hdf` (see "Mounting the dev HDF"
   below).
2. Copy the `.lha` files into it.
3. Unmount / eject on macOS.
4. Boot OS4 (`start-qemu-os4.sh`).
5. In OS4: open the dev drive, extract each update in order (`LhA x
   Update1.lha`) and run its installer icon.

## Mounting the dev HDF from macOS

The 512 MB `amigaos4-dev.hdf` is raw disk. macOS doesn't understand
SFS or FFS natively, but you can either:

- **Use `xdftool`** (from amitools) to write to it as an Amiga
  filesystem: `pip install amitools`
- **Or share via QEMU's built-in TFTP / SMB** (network file server
  setup — heavier)
- **Or use a plain FAT32 partition** on the dev HDD when partitioning
  in Media Toolbox: OS4 reads FAT via `crossdostype`, and macOS reads
  it natively via `hdiutil attach`

The FAT32 route is the smoothest for iterating on binaries — treat the
dev HDD like a USB stick both sides read.

## Video driver troubleshooting

If ScreenMode only shows 8-bit modes, the emulated video card driver
isn't loaded. sam460ex QEMU emulates the **SiliconMotion SM501**. On
OS4:

- Check `SYS:Devs/Monitors/` for `SM501` or `RadeonHD`
- If missing, install from the OS4 CD's `Extras` drawer

## Devbench wiring

For classic 68k the serial TCP is `127.0.0.1:1234`; for OS4 it's
`127.0.0.1:2346`. Use the split config:

```
python3 -m amiga_devbench --config devbench-ppc.toml
```

The web UI runs on the same port (3000). MCP endpoint at `/mcp`.

## Fresh start

If you want to wipe everything and try again:

```
rm ~/AmigaOS4/amigaos4-system.hdf ~/AmigaOS4/amigaos4-dev.hdf
bash scripts/start-qemu-os4.sh --install
```

The script creates fresh sparse HDF containers when they're missing.

## Known gotchas

- **HDF must be a sparse file** on APFS. `du -sh` reports actual
  bytes; a fresh 2 GB HDF is only 16 KB on disk until you write to it.
- **`-boot d` sticks with `--install`**. Even after installing, if you
  relaunch with `--install`, U-Boot keeps booting the CD. Drop
  `--install` to boot from HDD.
- **sam460ex has only one IDE bus** with two slots. `-cdrom` implicitly
  maps to bus 1 which doesn't exist — the script uses
  `-drive if=ide,index=1,media=cdrom` explicitly.
- **rtl8139 NIC is not supported** on sam460ex. Networking needs the
  built-in EMAC or a supported PCI card; the script disables NIC with
  `-nic none` to keep the boot clean.
- **CPU stays at 100 % during boot** — normal. QEMU emulates PPC on
  Apple Silicon or x86 without hardware assist. First boot to
  Workbench is ~2 min.
