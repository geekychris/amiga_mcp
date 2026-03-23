# Linux Setup Guide for Amiga DevBench

Help a Linux user get the Amiga development environment running.

## Arguments
- $ARGUMENTS: Specific issue or step they're stuck on. If empty, walk through the full setup.

## Prerequisites

### 1. Docker
```bash
# Ubuntu/Debian
sudo apt-get update && sudo apt-get install docker.io docker-compose
sudo usermod -aG docker $USER
# Log out and back in for group to take effect

# Fedora
sudo dnf install docker docker-compose
sudo systemctl enable --now docker
sudo usermod -aG docker $USER

# Arch
sudo pacman -S docker docker-compose
sudo systemctl enable --now docker
sudo usermod -aG docker $USER
```
Verify: `docker run hello-world`

Pull cross-compiler: `docker pull amigadev/crosstools:m68k-amigaos`

### 2. Python 3.10+
```bash
# Ubuntu/Debian
sudo apt-get install python3 python3-pip python3-venv

# Fedora
sudo dnf install python3 python3-pip

# Arch
sudo pacman -S python python-pip
```
Verify: `python3 --version`

### 3. Git
```bash
sudo apt-get install git   # or dnf/pacman equivalent
```

### 4. Amiga Emulator
**FS-UAE** (recommended on Linux):
```bash
# Ubuntu (PPA)
sudo add-apt-repository ppa:fengestad/stable
sudo apt-get update && sudo apt-get install fs-uae fs-uae-launcher

# Flatpak (any distro)
flatpak install flathub net.fsuae.FS-UAE

# Or build from source: https://fs-uae.net/download
```

**Alternative**: RetroArch with PUAE core (available in most repos).

## Setup Steps

### 1. Clone and install
```bash
git clone <repo-url>
cd amiga_mcp
pip install -e amiga-devbench
# Or in a venv:
python3 -m venv .venv && source .venv/bin/activate && pip install -e amiga-devbench
```

### 2. Build bridge and examples
```bash
make bridge
make examples
# Or build a single project:
docker run --rm -v "$(pwd)":/work -w /work amigadev/crosstools:m68k-amigaos make -C examples/hello_world
```

### 3. Configure emulator serial port
Add to your FS-UAE config file (`~/.config/fs-uae/Configurations/MyConfig.fs-uae`):
```
serial_port = tcp://0.0.0.0:1234
```

### 4. Set up shared folder
Add to FS-UAE config:
```
hard_drive_2 = /home/youruser/amiga-shared
hard_drive_2_label = Dev
```
Create the directory: `mkdir -p ~/amiga-shared`

The Amiga will see this as `DH2:` (or `Dev:`).

### 5. Configure devbench.toml
Edit `devbench.toml` in the project root:
```toml
[serial]
host = "127.0.0.1"
port = 1234

[paths]
deploy_dir = "/home/youruser/amiga-shared"
```

### 6. Start devbench
```bash
python3 -m amiga_devbench
# Or: make start
```
Open http://localhost:3000 in a browser for the web dashboard.

### 7. Deploy and run
```bash
cp examples/hello_world/hello_world ~/amiga-shared/
# On the Amiga CLI: DH2:hello_world
```

### 8. Claude Code MCP config
Add to `~/.claude/settings.json` or project `.mcp.json`:
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

## Common Linux Issues
- **Docker permission denied**: Run `sudo usermod -aG docker $USER` then log out/in
- **Port 3000 in use**: Change port in `devbench.toml` under `[server]` section, or kill the conflicting process
- **FS-UAE no display**: Install SDL2 (`sudo apt-get install libsdl2-dev`)
- **Serial connection refused**: Start the emulator BEFORE devbench. Check firewall isn't blocking port 1234
- **make: command not found**: `sudo apt-get install build-essential`
- **Kickstart ROM missing**: FS-UAE needs Amiga ROM files — see https://fs-uae.net/docs/kickstarts
- **No sound in emulator**: Check PulseAudio/PipeWire is running, FS-UAE may need `--audio-driver=pulse`
