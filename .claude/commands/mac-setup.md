# macOS Setup Guide for Amiga DevBench

Help a macOS user get the Amiga development environment running.

## Arguments
- $ARGUMENTS: Specific issue or step they're stuck on. If empty, walk through the full setup.

## Prerequisites

### 1. Docker Desktop
Download from https://www.docker.com/products/docker-desktop/ (Apple Silicon and Intel both supported).

After install, pull the cross-compiler:
```bash
docker pull amigadev/crosstools:m68k-amigaos
```

### 2. Python 3.10+
macOS ships with Python 3 on recent versions. Verify:
```bash
python3 --version
```
If missing, install via Homebrew:
```bash
brew install python@3.12
```

### 3. Homebrew (if not installed)
```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

### 4. Amiga Emulator
**FS-UAE**:
```bash
brew install --cask fs-uae
```

**AmiKit** (recommended — batteries-included Amiga environment):
- Download from https://www.amikit.amiga.sk/
- Includes pre-configured Workbench, utilities, and shared folder support
- Installs to `/Applications/AmiKit.app`

## Setup Steps

### 1. Clone and install
```bash
git clone <repo-url>
cd amiga_mcp
pip3 install -e amiga-devbench
```

### 2. Build everything
```bash
make all
```
This builds the bridge daemon, client library, and all examples via Docker.

### 3. Configure emulator serial port

**AmiKit**: Edit `~/Documents/FS-UAE/Configurations/AmiKit-Debug.fs-uae`:
```
serial_port = tcp://0.0.0.0:1234
```

**FS-UAE standalone**: Add to your config file:
```
serial_port = tcp://0.0.0.0:1234
```

### 4. Shared folder setup

**AmiKit** (default path):
```
/Applications/AmiKit.app/Contents/SharedSupport/prefix/drive_c/AmiKit/Dropbox/Dev/
```
The Amiga sees this as `DH2:Dev/`.

**FS-UAE standalone**: Add to config:
```
hard_drive_2 = /Users/yourname/amiga-shared
hard_drive_2_label = Dev
```

### 5. devbench.toml
The default `devbench.toml` should work on macOS with AmiKit. For custom setups, edit:
```toml
[serial]
host = "127.0.0.1"
port = 1234

[paths]
deploy_dir = "/path/to/shared/folder"
```

### 6. Start devbench
```bash
make start
# Or: python3 -m amiga_devbench
```
Web dashboard: http://localhost:3000

### 7. Deploy and run
```bash
# Deploy binary
cp examples/hello_world/hello_world /Applications/AmiKit.app/Contents/SharedSupport/prefix/drive_c/AmiKit/Dropbox/Dev/

# On the Amiga: DH2:Dev/hello_world
```

Or use the `/build-deploy-run` skill to do it all in one step.

### 8. Claude Code MCP config
Add to your Claude Code settings:
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

## Common macOS Issues
- **Xcode license not agreed**: Run `sudo xcodebuild -license` (needed for some command-line tools)
- **Docker Desktop not starting**: Check System Preferences → Privacy & Security for blocked extensions
- **Apple Silicon (M1/M2/M3)**: Docker runs the x86 cross-compiler via Rosetta — works fine but first build is slower
- **Port 3000 in use**: `lsof -i :3000` to find the process, or change port in `devbench.toml`
- **FS-UAE crashes on launch**: Try `brew reinstall --cask fs-uae` or check Gatekeeper: System Preferences → Privacy → allow FS-UAE
- **"Operation not permitted" on shared folder**: Grant Full Disk Access to Terminal in System Preferences → Privacy
- **Kickstart ROM**: AmiKit bundles everything. For standalone FS-UAE, you need Amiga ROM files
