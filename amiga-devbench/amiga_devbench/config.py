"""Configuration loader for amiga-devbench."""

from __future__ import annotations

import os
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

if sys.version_info >= (3, 11):
    import tomllib
else:
    try:
        import tomllib
    except ModuleNotFoundError:
        import tomli as tomllib  # type: ignore[no-redef]


@dataclass
class DevBenchConfig:
    """All configuration for devbench."""

    # Serial
    serial_mode: str = "tcp"
    serial_host: str = "127.0.0.1"
    serial_port: int = 1234
    pty_path: str = "/tmp/amiga-serial"

    # Emulator
    emulator_binary: str = "/opt/homebrew/bin/fs-uae"
    emulator_config: str = "~/Documents/FS-UAE/Configurations/AmiKit-Debug.fs-uae"
    emulator_auto_start: bool = False

    # Server
    server_port: int = 3000
    log_level: str = "INFO"

    # Paths
    project_root: str = ""
    deploy_dir: str = ""

    # Bridge options
    crash_handler_auto_enable: bool = True

    # GDB RSP server
    gdb_port: int = 2159

    # Simulator mode
    simulator: bool = False

    def resolve_paths(self) -> None:
        """Expand ~ and resolve relative paths."""
        self.emulator_config = str(Path(self.emulator_config).expanduser())
        if self.project_root:
            self.project_root = str(Path(self.project_root).resolve())
        if self.deploy_dir:
            self.deploy_dir = str(Path(self.deploy_dir).expanduser())


def load_config(
    config_path: str | None = None,
    project_root: str | None = None,
) -> DevBenchConfig:
    """Load config from TOML file(s). Returns defaults if no file found."""
    cfg = DevBenchConfig()

    # Determine project root
    if project_root:
        root = Path(project_root).resolve()
    else:
        # Walk up from CWD looking for devbench.toml
        root = Path.cwd()
        while root != root.parent:
            if (root / "devbench.toml").exists():
                break
            root = root.parent
        else:
            root = Path.cwd()

    cfg.project_root = str(root)

    # Load config file
    toml_path = Path(config_path) if config_path else root / "devbench.toml"
    if toml_path.exists():
        with open(toml_path, "rb") as f:
            data = tomllib.load(f)
        _apply_toml(cfg, data)

    cfg.resolve_paths()
    return cfg


def _apply_toml(cfg: DevBenchConfig, data: dict[str, Any]) -> None:
    """Apply TOML data to config object."""
    serial = data.get("serial", {})
    if "mode" in serial:
        cfg.serial_mode = serial["mode"]
    if "host" in serial:
        cfg.serial_host = serial["host"]
    if "port" in serial:
        cfg.serial_port = int(serial["port"])
    if "pty_path" in serial:
        cfg.pty_path = serial["pty_path"]

    emu = data.get("emulator", {})
    if "binary" in emu:
        cfg.emulator_binary = emu["binary"]
    if "config" in emu:
        cfg.emulator_config = emu["config"]
    if "auto_start" in emu:
        cfg.emulator_auto_start = bool(emu["auto_start"])

    srv = data.get("server", {})
    if "port" in srv:
        cfg.server_port = int(srv["port"])
    if "log_level" in srv:
        cfg.log_level = srv["log_level"]

    paths = data.get("paths", {})
    if "deploy_dir" in paths:
        cfg.deploy_dir = paths["deploy_dir"]
    if "project_root" in paths:
        cfg.project_root = paths["project_root"]

    bridge = data.get("bridge", {})
    if "crash_handler_auto_enable" in bridge:
        cfg.crash_handler_auto_enable = bool(bridge["crash_handler_auto_enable"])


def apply_cli_overrides(cfg: DevBenchConfig, args: Any) -> None:
    """Override config with CLI arguments (CLI takes precedence)."""
    if getattr(args, "serial_host", None):
        cfg.serial_host = args.serial_host
        cfg.serial_mode = "tcp"
    if getattr(args, "serial_port", None) and args.serial_port != 1234:
        cfg.serial_port = args.serial_port
    if getattr(args, "pty_path", None) and args.pty_path != "/tmp/amiga-serial":
        cfg.pty_path = args.pty_path
    if getattr(args, "port", None) and args.port != 3000:
        cfg.server_port = args.port
    if getattr(args, "project_root", None):
        cfg.project_root = str(Path(args.project_root).resolve())
    if getattr(args, "deploy_dir", None):
        cfg.deploy_dir = args.deploy_dir
    if getattr(args, "log_level", None) and args.log_level != "INFO":
        cfg.log_level = args.log_level
    if getattr(args, "simulator", False):
        cfg.simulator = True


def save_config(cfg: DevBenchConfig, path: str | None = None) -> str:
    """Save config to TOML file. Returns the path written."""
    if path is None:
        path = os.path.join(cfg.project_root, "devbench.toml")

    lines = [
        '# Amiga DevBench Configuration',
        '',
        '[serial]',
        f'mode = "{cfg.serial_mode}"',
        f'host = "{cfg.serial_host}"',
        f'port = {cfg.serial_port}',
        f'pty_path = "{cfg.pty_path}"',
        '',
        '[emulator]',
        f'binary = "{cfg.emulator_binary}"',
        f'config = "{cfg.emulator_config}"',
        f'auto_start = {"true" if cfg.emulator_auto_start else "false"}',
        '',
        '[server]',
        f'port = {cfg.server_port}',
        f'log_level = "{cfg.log_level}"',
        '',
        '[paths]',
        f'deploy_dir = "{cfg.deploy_dir}"',
        '',
        '[bridge]',
        f'crash_handler_auto_enable = {"true" if cfg.crash_handler_auto_enable else "false"}',
        '',
    ]

    with open(path, "w") as f:
        f.write("\n".join(lines))

    return path
