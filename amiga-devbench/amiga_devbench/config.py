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

    # Profiles — named bundles of transport + emulator settings.
    # Keys are profile names, values are dicts with any subset of:
    #   mode ("pty"|"tcp"), host, port, pty_path,
    #   auto_start_emulator, emulator_config, emulator_binary,
    #   deploy_dir, description
    # active_profile picks one; CLI --profile overrides.
    profiles: dict[str, dict[str, Any]] = field(default_factory=dict)
    active_profile: str = ""

    # Serial
    serial_mode: str = "tcp"
    serial_host: str = "127.0.0.1"
    serial_port: int = 1234
    pty_path: str = "/tmp/amiga-serial"

    # Emulator. `binary = "auto"` searches ~/.amiga-devbench/fs-uae,
    # /tmp/fsuae-src/fs-uae (patched-build defaults), then falls back to
    # `fs-uae` on PATH. Set an explicit path to override.
    emulator_binary: str = "auto"
    emulator_config: str = "~/Documents/FS-UAE/Configurations/AmiKit-Debug.fs-uae"
    emulator_auto_start: bool = False

    # Remote emulator (SSH-driven). When emulator_ssh is set, devbench uses a
    # RemoteEmulatorController instead of spawning a local FS-UAE subprocess.
    # Typically set on a profile like [profiles.pi-amikit].
    emulator_ssh: str = ""              # e.g. "chris@amiga.local"
    emulator_start_cmd: str = ""        # shell command on the remote host
    emulator_stop_cmd: str = ""
    emulator_status_cmd: str = ""       # exit 0 if running
    emulator_ready_probe: str = ""      # "host:port" — TCP up = emulator ready
    emulator_reboot_cmd: str = "sudo reboot"

    # Server
    server_port: int = 3000
    log_level: str = "INFO"

    # Paths
    project_root: str = ""
    deploy_dir: str = ""

    # Bridge options
    crash_handler_auto_enable: bool = True

    # Target architecture — decides which toolchain Docker image is used
    # for cross-compilation and which serial/deploy defaults apply.
    #   "m68k" — classic AmigaOS 3.x on 680x0 (default)
    #   "ppc"  — AmigaOS 4.1 on PowerPC (sam460ex under QEMU)
    arch: str = "m68k"
    # Docker image for cross-compilation. Empty = pick the arch default.
    docker_image: str = ""

    # GDB RSP server
    gdb_port: int = 2159

    # FS-UAE Remote Debug HTTP RPC (patched fs-uae build)
    # See https://github.com/geekychris/fsuae_remote_patch
    # `enabled = "auto"` probes /v1/ping on startup; "on" forces; "off" disables.
    fsuae_rpc_enabled: str = "auto"
    fsuae_rpc_port: int = 8765
    fsuae_rpc_pause_at_boot: bool = False
    fsuae_gdb_port: int = 0  # 0 = disabled; nonzero enables in-emulator GDB stub
    # When true, devbench listens for bridge `crash` events and pauses
    # fs-uae (via /v1/pause) so the CPU state is frozen for inspection.
    fsuae_auto_pause_on_crash: bool = True
    # Auto-snapshot ring buffer — OFF BY DEFAULT.
    # When > 0, devbench saves a .uss snapshot every N seconds to a rotating
    # ring of slots (auto-0.uss, auto-1.uss, ...). Lets you "rewind" by loading
    # an earlier snap. Each save is ~19MB and takes ~100-300ms — opt in only
    # when you actually want the feature, since it does perceptibly affect
    # emulator throughput while the save is happening.
    fsuae_auto_snapshot_interval_s: int = 0  # 0 = off
    fsuae_auto_snapshot_ring_size: int = 5

    # Simulator mode
    simulator: bool = False

    # LLM proxy — bridges amiterm's `ask` builtin to a remote inference
    # server (Ollama-compatible by default). Opt in per-project.
    llm_enabled: bool = False
    llm_host: str = "spark.hitorro.com"
    llm_port: int = 11434
    llm_scheme: str = "http"
    llm_model: str = "nemotron"
    llm_system_prompt: str = (
        "You are an assistant embedded in an Amiga terminal. Prefer short, "
        "concrete answers. When you need to inspect the machine, use the "
        "run_command tool with an AmigaDOS command; do not fabricate output."
    )
    llm_client_name: str = "amiterm"
    llm_hook_name: str = "llm_event"

    def resolve_paths(self) -> None:
        """Expand ~ and resolve relative paths."""
        self.emulator_config = str(Path(self.emulator_config).expanduser())
        if self.project_root:
            self.project_root = str(Path(self.project_root).resolve())
        if self.deploy_dir:
            self.deploy_dir = str(Path(self.deploy_dir).expanduser())
        # If emulator_binary is a relative path (e.g. "scripts/start-qemu-os4.sh"
        # from the qemu-os4 profile), anchor it to project_root. Otherwise leave
        # things like "auto", absolute paths, or bare command names alone.
        if (
            self.emulator_binary
            and self.emulator_binary != "auto"
            and "/" in self.emulator_binary
            and not Path(self.emulator_binary).is_absolute()
            and self.project_root
        ):
            self.emulator_binary = str(Path(self.project_root) / self.emulator_binary)


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
        # Profiles (if any) overlay on top of the flat sections. This means the
        # flat [serial]/[emulator] still act as defaults; a named profile that
        # doesn't set (say) emulator_config inherits it from the flat section.
        if cfg.active_profile:
            apply_profile(cfg, cfg.active_profile)

    cfg.resolve_paths()
    return cfg


def apply_profile(cfg: DevBenchConfig, name: str) -> None:
    """Overlay the named profile's fields onto cfg.

    Raises KeyError if the profile is not defined. Callers should validate the
    name against cfg.profiles first if they want a friendlier error.
    """
    if name not in cfg.profiles:
        available = ", ".join(sorted(cfg.profiles)) or "(none)"
        raise KeyError(
            f"Profile '{name}' not found in devbench.toml. Available: {available}"
        )
    p = cfg.profiles[name]
    # Transport
    if "mode" in p:
        cfg.serial_mode = str(p["mode"]).lower()
    if "host" in p:
        cfg.serial_host = str(p["host"])
    if "port" in p:
        cfg.serial_port = int(p["port"])
    if "pty_path" in p:
        cfg.pty_path = str(p["pty_path"])
    # Emulator (local subprocess)
    if "auto_start_emulator" in p:
        cfg.emulator_auto_start = bool(p["auto_start_emulator"])
    if "emulator_config" in p:
        cfg.emulator_config = str(p["emulator_config"])
    if "emulator_binary" in p:
        cfg.emulator_binary = str(p["emulator_binary"])
    # Emulator (remote SSH-driven)
    for key in (
        "emulator_ssh", "emulator_start_cmd", "emulator_stop_cmd",
        "emulator_status_cmd", "emulator_ready_probe", "emulator_reboot_cmd",
    ):
        if key in p:
            setattr(cfg, key, str(p[key]))
    # Paths
    if "deploy_dir" in p:
        cfg.deploy_dir = str(p["deploy_dir"])
    # Arch + build settings
    if "arch" in p:
        cfg.arch = str(p["arch"]).lower()
    if "docker_image" in p:
        cfg.docker_image = str(p["docker_image"])
    # Remember which profile was applied
    cfg.active_profile = name


def _apply_toml(cfg: DevBenchConfig, data: dict[str, Any]) -> None:
    """Apply TOML data to config object."""
    # Profiles + which one is active. The overlay happens in load_config after
    # the flat sections are applied, so profile fields win.
    if "active_profile" in data:
        cfg.active_profile = str(data["active_profile"])
    if "profiles" in data and isinstance(data["profiles"], dict):
        cfg.profiles = dict(data["profiles"])

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

    build = data.get("build", {})
    if "arch" in build:
        cfg.arch = str(build["arch"]).lower()
    if "docker_image" in build:
        cfg.docker_image = str(build["docker_image"])

    llm = data.get("llm", {})
    if "enabled" in llm:       cfg.llm_enabled = bool(llm["enabled"])
    if "host" in llm:          cfg.llm_host = str(llm["host"])
    if "port" in llm:          cfg.llm_port = int(llm["port"])
    if "scheme" in llm:        cfg.llm_scheme = str(llm["scheme"])
    if "model" in llm:         cfg.llm_model = str(llm["model"])
    if "system_prompt" in llm: cfg.llm_system_prompt = str(llm["system_prompt"])
    if "client_name" in llm:   cfg.llm_client_name = str(llm["client_name"])
    if "hook_name" in llm:     cfg.llm_hook_name = str(llm["hook_name"])

    rpc = data.get("fsuae_rpc", {})
    if "enabled" in rpc:
        val = rpc["enabled"]
        if isinstance(val, bool):
            cfg.fsuae_rpc_enabled = "on" if val else "off"
        else:
            cfg.fsuae_rpc_enabled = str(val).lower()
    if "port" in rpc:
        cfg.fsuae_rpc_port = int(rpc["port"])
    if "pause_at_boot" in rpc:
        cfg.fsuae_rpc_pause_at_boot = bool(rpc["pause_at_boot"])
    if "gdb_port" in rpc:
        cfg.fsuae_gdb_port = int(rpc["gdb_port"])
    if "auto_pause_on_crash" in rpc:
        cfg.fsuae_auto_pause_on_crash = bool(rpc["auto_pause_on_crash"])
    if "auto_snapshot_interval_s" in rpc:
        cfg.fsuae_auto_snapshot_interval_s = int(rpc["auto_snapshot_interval_s"])
    if "auto_snapshot_ring_size" in rpc:
        cfg.fsuae_auto_snapshot_ring_size = int(rpc["auto_snapshot_ring_size"])


def apply_cli_overrides(cfg: DevBenchConfig, args: Any) -> None:
    """Override config with CLI arguments (CLI takes precedence)."""
    # --profile overrides any active_profile from the config file. Apply this
    # first so subsequent --host/--port/--mode land on top of the profile.
    profile_arg = getattr(args, "profile", None)
    if profile_arg:
        apply_profile(cfg, profile_arg)
    if getattr(args, "mode", None):
        cfg.serial_mode = args.mode
    if getattr(args, "serial_host", None):
        cfg.serial_host = args.serial_host
        # Only force tcp when --mode wasn't explicit — otherwise user wins.
        if not getattr(args, "mode", None):
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
        '[fsuae_rpc]',
        '# Remote-debug HTTP API exposed by the patched fs-uae build',
        '# (see https://github.com/geekychris/fsuae_remote_patch). Stock fs-uae',
        '# ignores these env vars, so leaving this enabled is safe either way.',
        f'enabled = "{cfg.fsuae_rpc_enabled}"  # "auto" | "on" | "off"',
        f'port = {cfg.fsuae_rpc_port}',
        f'pause_at_boot = {"true" if cfg.fsuae_rpc_pause_at_boot else "false"}',
        f'gdb_port = {cfg.fsuae_gdb_port}  # 0 disables the in-emulator GDB stub',
        f'auto_pause_on_crash = {"true" if cfg.fsuae_auto_pause_on_crash else "false"}',
        f'# Auto-snapshot ring buffer — OFF by default. Set to e.g. 30 for a snap every 30s.',
        f'# Each save is ~19MB / ~200ms and DOES affect emulator throughput while saving.',
        f'auto_snapshot_interval_s = {cfg.fsuae_auto_snapshot_interval_s}  # 0 disables',
        f'auto_snapshot_ring_size = {cfg.fsuae_auto_snapshot_ring_size}    # rotating slots',
        '',
    ]

    with open(path, "w") as f:
        f.write("\n".join(lines))

    return path
