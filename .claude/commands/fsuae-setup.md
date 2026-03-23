# Set Up FS-UAE Emulator for Amiga DevBench

Configure FS-UAE (or AmiKit) to work with the Amiga DevBench development environment.

## Arguments
- $ARGUMENTS: Platform or specific issue (e.g., "mac", "linux", "windows", "serial not connecting", "amikit"). If empty, detect platform and walk through full setup.

## Two paths: AmiKit (easy) or FS-UAE standalone

### Path A: AmiKit (macOS, recommended)
AmiKit bundles everything (Workbench, ROMs, pre-configured drives). Easiest way to get started.

1. **Install AmiKit**: Download from https://www.amikit.amiga.sk/
2. **Run the configure script**:
   ```bash
   ./scripts/configure-amikit.sh
   ```
   This patches AmiKit's WinUAE configs to enable serial-over-TCP on port 1234 and creates the shared Dev folder.

3. **Restart AmiKit** — the serial port is now active.

4. **Configure devbench.toml**:
   ```toml
   [serial]
   mode = "tcp"
   host = "127.0.0.1"
   port = 1234

   [emulator]
   config = "/Users/YOU/Documents/FS-UAE/Configurations/AmiKit-Debug.fs-uae"
   auto_start = true

   [paths]
   deploy_dir = "/Applications/AmiKit.app/Contents/SharedSupport/prefix/drive_c/AmiKit/Dropbox/Dev"
   ```

5. **Start devbench**: `make start`

### Path B: FS-UAE Standalone (any platform)

#### Step 1: Install FS-UAE
- **macOS**: `brew install --cask fs-uae`
- **Linux**: `sudo apt-get install fs-uae` or Flatpak: `flatpak install flathub net.fsuae.FS-UAE`
- **Windows**: Download from https://fs-uae.net/download

#### Step 2: Get Kickstart ROM
FS-UAE needs Amiga Kickstart ROM files. Legal options:
- **Amiga Forever** (commercial): https://www.amigaforever.com/ — includes all ROMs
- **Cloanto Amiga OS ROMs** — bundled with Amiga Forever
- Place ROM files in `~/Documents/FS-UAE/Kickstarts/` (macOS/Linux) or `%USERPROFILE%\Documents\FS-UAE\Kickstarts\` (Windows)

Required ROM: Kickstart 3.1 (A1200) — file usually named `kick31.rom` or `amiga-os-310-a1200.rom`

#### Step 3: Create a hard drive directory
```bash
mkdir -p ~/Documents/FS-UAE/Hard\ Drives/System
mkdir -p ~/amiga-shared
```
You'll need a Workbench 3.1 install on the System drive (or use AmiKit which provides this).

#### Step 4: Create FS-UAE config file

There's a sample config at the project root: `AmiKit-Debug.fs-uae`. Copy and customize it:

```bash
mkdir -p ~/Documents/FS-UAE/Configurations/
cp AmiKit-Debug.fs-uae ~/Documents/FS-UAE/Configurations/DevBench.fs-uae
```

Then edit the copy — the **critical settings** are:

```ini
[fs-uae]
# CPU and memory
amiga_model = A1200
cpu = 68060
chip_memory = 2048
fast_memory = 8192

# Kickstart ROM path (CHANGE THIS)
kickstart_file = /path/to/your/kick31.rom

# Hard drives (CHANGE THESE)
hard_drive_0 = /path/to/System               # Bootable Workbench
hard_drive_0_label = System

hard_drive_2 = /path/to/amiga-shared         # Shared folder for deploying binaries
hard_drive_2_label = Dev

# SERIAL PORT — this is the key setting for DevBench
serial_port = tcp://0.0.0.0:1234

# Display
window_width = 800
window_height = 600
fullscreen = 0

# Mouse integration (don't capture mouse)
mouse_integration = 1
automatic_input_grab = 0
```

#### Step 5: Configure devbench.toml
```toml
[serial]
mode = "tcp"
host = "127.0.0.1"
port = 1234

[emulator]
binary = "fs-uae"     # or full path like /usr/bin/fs-uae
config = "/path/to/Documents/FS-UAE/Configurations/DevBench.fs-uae"
auto_start = true

[paths]
deploy_dir = "/path/to/amiga-shared"
```

#### Step 6: Test
```bash
# Start devbench (will auto-start emulator if configured)
make start

# Or start emulator manually first, then devbench
fs-uae ~/Documents/FS-UAE/Configurations/DevBench.fs-uae &
python3 -m amiga_devbench
```

Open http://localhost:3000 — the Dashboard should show the emulator status and the connection indicator should go from red → yellow → green once the bridge daemon starts on the Amiga.

## Web UI Config

Once devbench is running, you can edit the FS-UAE config directly from the browser:
1. Go to http://localhost:3000
2. Click the **Settings** tab
3. The **Config** sub-tab shows devbench.toml settings (serial, emulator path, deploy dir)
4. Below that is a **full FS-UAE config editor** with Save & Restart Emulator button

## Serial Connection: How It Works

```
FS-UAE                          DevBench
 |                                |
 | serial_port=tcp://0.0.0.0:1234|
 | (listens on port 1234)        |
 |                                |
 |<------TCP connect-------------|  (devbench connects as client)
 |                                |
 | serial.device ←→ TCP socket   |
 |                                |
 Amiga bridge daemon              Python server
 (reads/writes serial.device)     (reads/writes TCP socket)
```

The Amiga program uses `serial.device` normally. FS-UAE tunnels that over TCP. DevBench connects to that TCP port.

## Troubleshooting

- **Connection refused on port 1234**: FS-UAE must be running BEFORE devbench tries to connect. Check that `serial_port = tcp://0.0.0.0:1234` is in the FS-UAE config.
- **Yellow indicator (serial connected but no bridge)**: The bridge daemon needs to be started on the Amiga side: `DH2:Dev/amiga-bridge` (or `AK2:Dev/amiga-bridge` on AmiKit)
- **"No Kickstart ROM"**: FS-UAE can't find the ROM. Check `kickstart_file` path.
- **Emulator starts but black screen**: Workbench not installed on DH0. You need a bootable system drive.
- **Shared folder not visible on Amiga**: Check `hard_drive_2` path exists and the label matches what you expect (`Dev:` or `DH2:`).
