# SPDX-License-Identifier: MIT
"""A private WinUAE for one MCP session.

The session gets a folder with a copy of the guest's boot folder and a
.uae configuration that boots that copy. The copy's S/Startup-Sequence
starts the guest bridge on the session's own port (the bridge listens
through WinUAE's bsdsocket emulation, so the port is the guest's choice).
Every other writable mount of the user's configuration is mounted
read-only, so two emulators never write to the same disk, and serial
ports are dropped so they cannot collide with the user's emulator.

The launcher (mcp_stdio --private) starts its own DevBench for the session,
which starts this WinUAE as its child; both stop when the session ends.
Sessions whose launcher died are cleaned up by the next private launch.
"""
from __future__ import annotations

import json
import os
import re
import shutil
import signal
import socket
import subprocess
from dataclasses import dataclass
from pathlib import Path

from .rundir import private_dir

BRIDGE_LINE = re.compile(r'(?im)^(\s*\S*amiga-bridge\s+TCP\s+)(\d+)\b')
MOUNT_KEY = re.compile(r'(?i)^(filesystem2?|hardfile2?|uaehf\d+)$')
SERIAL_KEY = re.compile(r'(?i)^(\w+\.)?serial_port$')
MAX_GUEST_BYTES = 256 << 20
# Ports a private session never takes: the defaults of the user's own setup.
RESERVED_PORTS = frozenset((1234, 2345, 2346, 3000, 3001))


class PrivateError(Exception):
    """A private session cannot be set up; the message says why."""


def free_port() -> int:
    while True:
        with socket.socket() as s:
            s.bind(('127.0.0.1', 0))
            port = s.getsockname()[1]
        if port not in RESERVED_PORTS:
            return port


def sessions_dir() -> Path:
    path = Path(private_dir()) / 'winuae-sessions'
    path.mkdir(mode=0o700, exist_ok=True)
    return path


def _mount(key: str, value: str) -> tuple[int, str | None]:
    """(index of the rw/ro field, directory path or None) of a mount line."""
    fields = value.split(',')
    key = key.lower()
    if key.startswith('uaehf'):
        access, kind, location = 1, fields[0].lower(), ','.join(fields[2:])
    else:
        access, location = 0, ','.join(fields[1:])
        kind = 'dir' if key.startswith('filesystem') else 'hdf'
    if kind != 'dir':
        return access, None
    if key == 'filesystem':                       # rw,VOLUME:path
        path = location.split(':', 1)[1] if ':' in location else ''
    else:                                         # rw,DEVICE:VOLUME:path,bootpri
        path = location.rsplit(',', 1)[0] if ',' in location else location
        path = path.split(':', 2)[2] if path.count(':') >= 2 else ''
    return access, path or None


def _starts_bridge(folder: Path) -> bool:
    startup = folder / 'S' / 'Startup-Sequence'
    try:
        return bool(BRIDGE_LINE.search(startup.read_text(errors='replace')))
    except OSError:
        return False


def _size(folder: Path) -> int:
    total = 0
    for root, _dirs, files in os.walk(folder):
        for name in files:
            try:
                total += os.lstat(os.path.join(root, name)).st_size
            except OSError:
                pass
    return total


@dataclass
class Guest:
    config: Path        # the session's .uae
    folder: Path        # its copy of the boot folder (the deploy folder)
    source: Path        # the boot folder it copies


def prepare_guest(config: str | os.PathLike, session: Path, bridge_port: int) -> Guest:
    """Copy the boot folder and write the session's configuration."""
    config = Path(config).expanduser()
    try:
        lines = config.read_text(errors='replace').splitlines()
    except OSError as exc:
        raise PrivateError(f'Cannot read the WinUAE configuration {config}: {exc}') from None
    mounts = []
    for number, line in enumerate(lines):
        key, sep, value = line.partition('=')
        if sep and MOUNT_KEY.match(key.strip()):
            access, path = _mount(key.strip(), value)
            mounts.append((number, key.strip(), value, access, path))
    boot = next((Path(p).expanduser() for *_, p in mounts if p and _starts_bridge(Path(p).expanduser())), None)
    if boot is None:
        raise PrivateError(
            f'{config} mounts no folder whose S/Startup-Sequence starts "amiga-bridge TCP <port>". '
            "A private session needs DevBench's WinUAE guest, which boots from such a folder, "
            'so that its copy can run the bridge on a port of its own.')
    if _size(boot) > MAX_GUEST_BYTES:
        raise PrivateError(f'The guest folder {boot} is larger than {MAX_GUEST_BYTES >> 20} MiB; '
                           'a private session copies it')
    folder = session / 'guest'
    shutil.copytree(boot, folder, symlinks=True)
    startup = folder / 'S' / 'Startup-Sequence'
    startup.write_text(BRIDGE_LINE.sub(lambda m: f'{m[1]}{bridge_port}', startup.read_text(errors='replace')))

    for number, key, value, access, path in mounts:
        fields = value.split(',')
        if path and Path(path).expanduser() == boot:
            lines[number] = f'{key}={value.replace(path, str(folder))}'
        elif fields[access].strip().lower() == 'rw':
            fields[access] = 'ro'
            lines[number] = f'{key}={",".join(fields)}'
    for number, line in enumerate(lines):
        key = line.partition('=')[0].strip()
        if SERIAL_KEY.match(key):
            lines[number] = f'; private session: no serial port ({line})'
    private = session / 'DevBench-private.uae'
    private.write_text('\n'.join(lines) + '\n')
    return Guest(private, folder, boot)


# ── Session bookkeeping ──────────────────────────────────────────────

def new_session() -> Path:
    path = sessions_dir() / f'{os.getpid()}-{os.urandom(3).hex()}'
    path.mkdir(mode=0o700)
    (path / 'owner.json').write_text(json.dumps(dict(launcher=os.getpid())))
    return path


def _alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def session_processes(session: Path) -> list[int]:
    """Processes whose command line names the session folder: its DevBench
    and the WinUAE that DevBench started with the session's configuration."""
    pattern = re.sub(r'([.^$*+?()\[\]{}|\\])', r'\\\1', str(session))
    try:
        found = subprocess.run(['pgrep', '-f', '--', pattern], capture_output=True, text=True, timeout=2)
    except (OSError, subprocess.SubprocessError):
        return []
    return [int(p) for p in found.stdout.split() if p.isdigit() and int(p) != os.getpid()]


def stop_session(session: Path) -> None:
    for pid in session_processes(session):
        try:
            os.kill(pid, signal.SIGTERM)
        except OSError:
            pass


def sweep() -> list[str]:
    """Remove the sessions of launchers that are gone, stopping what they left running."""
    removed = []
    for session in sessions_dir().iterdir():
        try:
            launcher = json.loads((session / 'owner.json').read_text()).get('launcher')
        except (OSError, ValueError, AttributeError):
            launcher = None
        if not isinstance(launcher, int):
            prefix = session.name.split('-')[0]     # written before owner.json
            launcher = int(prefix) if prefix.isdigit() else None
        if launcher is not None and _alive(launcher):
            continue
        stop_session(session)
        shutil.rmtree(session, ignore_errors=True)
        removed.append(session.name)
    return removed
