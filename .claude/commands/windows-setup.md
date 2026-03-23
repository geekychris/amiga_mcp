# Windows Setup Guide for Amiga DevBench

Help a Windows user get the Amiga development environment running.

## Arguments
- $ARGUMENTS: Specific issue or step they're stuck on. If empty, walk through the full setup.

## Prerequisites
1. **Docker Desktop** — https://www.docker.com/products/docker-desktop/
   - Enable WSL2 backend for best performance
   - After install: `docker pull amigadev/crosstools:m68k-amigaos`

2. **Python 3.10+** — https://www.python.org/downloads/
   - Check "Add Python to PATH" during install
   - Verify: `python --version`

3. **Git** — https://git-scm.com/download/win
   - Or use GitHub Desktop

4. **Amiga Emulator** — one of:
   - **WinUAE** (recommended): https://www.winuae.net/ — best Amiga emulation on Windows
   - **FS-UAE**: https://fs-uae.net/ — cross-platform alternative

## Setup Steps

### 1. Clone and install
```cmd
git clone <repo-url>
cd amiga_mcp
pip install -e amiga-devbench
```

### 2. Build the bridge and examples
```cmd
docker run --rm -v %cd%:/work -w /work amigadev/crosstools:m68k-amigaos make -C amiga-bridge
docker run --rm -v %cd%:/work -w /work amigadev/crosstools:m68k-amigaos make -C examples/hello_world
```
Or in PowerShell:
```powershell
docker run --rm -v ${PWD}:/work -w /work amigadev/crosstools:m68k-amigaos make -C amiga-bridge
```

### 3. Configure emulator serial port
**WinUAE**: Settings → Serial Port → set to `TCP://0.0.0.0:1234`
**FS-UAE**: Add to config file: `serial_port = tcp://0.0.0.0:1234`

### 4. Set up shared folder
**WinUAE**: Settings → Hard Drives → Add Directory → point to a folder for sharing files
**FS-UAE**: `hard_drive_2 = C:\AmigaDev\shared`

The Amiga will see this as `DH2:` or similar.

### 5. Configure devbench.toml
Edit `devbench.toml` in the project root:
```toml
[serial]
host = "127.0.0.1"
port = 1234

[paths]
deploy_dir = "C:\\path\\to\\shared\\folder"
```

### 6. Start devbench
```cmd
python -m amiga_devbench
```
Open http://localhost:3000 in a browser for the web dashboard.

### 7. Deploy to Amiga
Copy built binaries to the shared folder:
```cmd
copy examples\hello_world\hello_world C:\path\to\shared\folder\
```
On the Amiga, run from the shared drive: `DH2:hello_world`

### 8. Claude Code MCP config
Add to your Claude Code MCP settings:
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

## Common Windows issues
- **Docker not starting**: Make sure Hyper-V/WSL2 is enabled in Windows Features
- **make not found**: Install GNU Make via chocolatey (`choco install make`) or use docker commands directly
- **Permission denied**: Run terminal as Administrator, or check Docker Desktop sharing settings
- **Serial connection refused**: Make sure emulator is running BEFORE starting devbench
- **Path separators**: Use forward slashes in devbench.toml paths, or double-backslashes
