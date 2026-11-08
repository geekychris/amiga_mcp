# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

"""Entry point for amiga-devbench."""

import argparse
import sys


def build_parser() -> argparse.ArgumentParser:
    """The devbench command line, shared with the MCP stdio launcher."""
    parser = argparse.ArgumentParser(
        prog="amiga-devbench",
        description="Amiga DevBench - Host-side developer tool for Amiga cross-development",
    )
    parser.add_argument(
        "--config", "-c", type=str, default=None,
        help="Path to devbench.toml config file (default: auto-discover)",
    )
    parser.add_argument(
        "--profile", type=str, default=None,
        help="Pick a named profile from [profiles.*] in devbench.toml "
             "(overrides active_profile)",
    )
    parser.add_argument(
        "--list-profiles", action="store_true",
        help="List profiles defined in devbench.toml and exit",
    )
    parser.add_argument(
        "--mode", type=str, default=None, choices=["pty", "tcp"],
        help="Transport mode (pty for host-local FS-UAE symlink, tcp for a "
             "real Amiga or a remote emulator). Overrides config/profile.",
    )
    parser.add_argument(
        "--port", type=int, default=3000,
        help="HTTP server port (default: 3000)",
    )
    parser.add_argument(
        "--serial-host", type=str, default=None,
        help="Amiga serial TCP host (default: from config; set to use TCP mode)",
    )
    parser.add_argument(
        "--serial-port", type=int, default=1234,
        help="Amiga serial TCP port (default: 1234)",
    )
    parser.add_argument(
        "--pty-path", type=str, default="/tmp/amiga-serial",
        help="PTY symlink path for FS-UAE (default: /tmp/amiga-serial)",
    )
    parser.add_argument(
        "--project-root", type=str, default=None,
        help="Project root directory (default: parent of amiga-devbench)",
    )
    parser.add_argument(
        "--deploy-dir", type=str, default=None,
        help="AmiKit shared folder for deploying binaries",
    )
    parser.add_argument(
        "--simulator", action="store_true",
        help="Start built-in Amiga simulator on serial port",
    )
    parser.add_argument(
        "--no-emulator", action="store_true",
        help="Don't auto-start the emulator even if configured",
    )
    parser.add_argument(
        "--emulator", action="store_true",
        help="Start the emulator with devbench even if auto-start is off",
    )
    parser.add_argument(
        "--emulator-binary", type=str, default=None,
        help="Emulator to manage: FS-UAE, WinUAE (or WinUAE.app) or a launcher script",
    )
    parser.add_argument(
        "--emulator-config", type=str, default=None,
        help="The emulator's configuration file (.fs-uae or .uae)",
    )
    parser.add_argument(
        "--winuae-debug-port", type=int, default=None,
        help="Enable the WinUAE native debugger on this GDB port",
    )
    parser.add_argument(
        "--log-level", type=str, default="INFO",
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
        help="Logging level (default: INFO)",
    )
    return parser


def main():
    args = build_parser().parse_args()

    # Load config and apply CLI overrides
    from .config import load_config, apply_cli_overrides
    cfg = load_config(config_path=args.config, project_root=args.project_root)

    if args.list_profiles:
        _print_profiles(cfg)
        return

    apply_cli_overrides(cfg, args)

    if args.no_emulator:
        cfg.emulator_auto_start = False
    elif args.emulator:
        cfg.emulator_auto_start = True

    from .server import run
    run(args, cfg)


def _print_profiles(cfg) -> None:
    if not cfg.profiles:
        print("No profiles defined in devbench.toml.")
        print(
            "Add a [profiles.<name>] section with mode/host/port/pty_path "
            "and set active_profile = \"<name>\" to switch transports easily."
        )
        return
    active = cfg.active_profile or "(none)"
    print(f"Profiles in {cfg.project_root}/devbench.toml (active: {active}):\n")
    for name in sorted(cfg.profiles):
        p = cfg.profiles[name]
        desc = p.get("description", "").strip() or "-"
        mode = p.get("mode", "?")
        if mode == "tcp":
            target = f"tcp {p.get('host', '?')}:{p.get('port', '?')}"
        elif mode == "pty":
            target = f"pty {p.get('pty_path', cfg.pty_path)}"
        else:
            target = mode
        marker = " *" if name == cfg.active_profile else "  "
        print(f"{marker} {name:<16} {target:<32} {desc}")


if __name__ == "__main__":
    main()
