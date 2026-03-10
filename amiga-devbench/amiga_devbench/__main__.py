"""Entry point for amiga-devbench."""

import argparse
import sys


def main():
    parser = argparse.ArgumentParser(
        prog="amiga-devbench",
        description="Amiga DevBench - Host-side developer tool for Amiga cross-development",
    )
    parser.add_argument(
        "--config", "-c", type=str, default=None,
        help="Path to devbench.toml config file (default: auto-discover)",
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
        "--log-level", type=str, default="INFO",
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
        help="Logging level (default: INFO)",
    )

    args = parser.parse_args()

    # Load config and apply CLI overrides
    from .config import load_config, apply_cli_overrides
    cfg = load_config(config_path=args.config, project_root=args.project_root)
    apply_cli_overrides(cfg, args)

    if args.no_emulator:
        cfg.emulator_auto_start = False

    from .server import run
    run(args, cfg)


if __name__ == "__main__":
    main()
