# SPDX-License-Identifier: MIT
"""What the guest calls the folder DevBench deploys into.

DevBench copies programs into deploy_dir on the host; launching one needs
its Amiga path. [paths] amiga_deploy_dir sets it. Otherwise a WinUAE
configuration (.uae) says it: its mount lines pair each host folder with a
device and volume name, so a deploy_dir inside a mounted folder is
DEVICE:sub/path. Anything else keeps the AmiKit default, where the shared
folder is DH2:Dev (Dropbox:Dev).
"""
from __future__ import annotations

import re
from pathlib import Path
from typing import Any

MOUNT_KEY = re.compile(r'(?i)^(filesystem2?|uaehf\d+)$')


def uae_folders(text: str) -> list[tuple[str, Path]]:
    """(Amiga name, host folder) of each folder a .uae mounts. The name is
    the device (DH0:) where there is one, else the volume (Work:)."""
    found = []
    for line in text.splitlines():
        key, sep, value = line.partition('=')
        key = key.strip().lower()
        if not sep or not MOUNT_KEY.match(key):
            continue
        fields = value.strip().split(',')
        if key.startswith('uaehf'):
            if fields[0].lower() != 'dir' or len(fields) < 3:
                continue
            fields = fields[1:]
        if key == 'filesystem':                    # rw,VOLUME:path
            volume, _, path = ','.join(fields[1:]).partition(':')
            name = volume
        else:                                      # rw,DEVICE:VOLUME:path,bootpri
            location = ','.join(fields[1:])
            location = location.rsplit(',', 1)[0] if ',' in location else location
            parts = location.split(':', 2)
            if len(parts) < 3:
                continue
            name, volume, path = parts
            name = name or volume
        if name and path:
            found.append((name, Path(path).expanduser()))
    return found


def _quote(path: str) -> str:
    return f'"{path}"' if ' ' in path else path


def amiga_deploy_dir(cfg: Any) -> str | None:
    """The Amiga path of deploy_dir, or None when DevBench cannot tell."""
    explicit = getattr(cfg, 'amiga_deploy_dir', '')
    if explicit:
        return explicit
    deploy, config = getattr(cfg, 'deploy_dir', ''), getattr(cfg, 'emulator_config', '')
    if not deploy or not config or not config.lower().endswith('.uae'):
        return None
    try:
        text = Path(config).expanduser().read_text(errors='replace')
    except OSError:
        return None
    deploy_path = Path(deploy).expanduser().resolve()
    best = None
    for name, folder in uae_folders(text):
        try:
            relative = deploy_path.relative_to(folder.resolve())
        except ValueError:
            continue
        if best is None or len(folder.parts) > best[0]:
            best = len(folder.parts), name, relative
    if best is None:
        return None
    _, name, relative = best
    return f'{name}:' + ('' if str(relative) == '.' else relative.as_posix())


def launch_command(cfg: Any, program: str, default_dir: str) -> str:
    """The command that runs a deployed program in the guest."""
    directory = (amiga_deploy_dir(cfg) if cfg is not None else None) or default_dir
    return _quote(directory + ('' if directory.endswith((':', '/')) else '/') + program)
