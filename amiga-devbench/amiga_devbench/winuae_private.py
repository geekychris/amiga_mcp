# SPDX-License-Identifier: MIT
"""A private WinUAE for one MCP session.

The session gets a folder with a copy of the guest's boot folder and a
.uae configuration that boots that copy. The copy's S/Startup-Sequence
starts the guest bridge on the session's own port (the bridge listens
through WinUAE's bsdsocket emulation, so the port is the guest's choice).
Every other writable mount of the user's configuration is mounted
read-only and floppy images are left out, so two emulators never write to
the same disk, and serial ports are dropped so they cannot collide with
the user's emulator.

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
import time
from dataclasses import dataclass
from pathlib import Path

from .rundir import pid_alive, private_dir

BRIDGE_LINE = re.compile(r'(?im)^(\s*\S*amiga-bridge\s+TCP\s+)(\d+)\b')
MOUNT_KEY = re.compile(r'(?i)^(filesystem2?|hardfile2?|uaehf\d+)$')
SERIAL_KEY = re.compile(r'(?i)^(\w+\.)?serial_port$')
FLOPPY_KEY = re.compile(r'(?i)^floppy\d+$')     # an inserted image, written back by WinUAE
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
    # The copy's S/Startup-Sequence is rewritten: a symlink there would lead
    # the rewrite into the original guest, so make both real in the copy.
    s_dir = folder / 'S'
    if s_dir.is_symlink():
        target = s_dir.resolve()
        s_dir.unlink()
        shutil.copytree(target, s_dir, symlinks=True)
    startup = s_dir / 'Startup-Sequence'
    text = startup.read_text(errors='replace')
    if startup.is_symlink():
        startup.unlink()
    startup.write_text(BRIDGE_LINE.sub(lambda m: f'{m[1]}{bridge_port}', text))

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
        elif FLOPPY_KEY.match(key) and line.partition('=')[2].strip():
            lines[number] = f'; private session: no floppy image ({line})'
    private = session / 'DevBench-private.uae'
    private.write_text('\n'.join(lines) + '\n')
    return Guest(private, folder, boot)


# ── Session bookkeeping ──────────────────────────────────────────────

# The launcher holds an exclusive lock on its session's owner.lock while it
# runs, and the system drops it when the launcher exits however it ends: a
# lock that can be taken marks an orphaned session, even if the launcher's
# PID has been reused since.
_held: dict[Path, object] = {}


def _try_lock(f) -> bool:
    try:
        if os.name == 'nt':
            import msvcrt
            f.seek(0)
            msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(f.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        return True
    except OSError:
        return False


def new_session() -> Path:
    path = sessions_dir() / f'{os.getpid()}-{os.urandom(3).hex()}'
    path.mkdir(mode=0o700)
    lock = open(path / 'owner.lock', 'a+b')
    if not _try_lock(lock):
        lock.close()
        raise OSError(f'Cannot lock {path / "owner.lock"}')
    _held[path] = lock
    (path / 'owner.json').write_text(json.dumps(dict(launcher=os.getpid())))
    return path


def release_session(session: Path) -> None:
    """Drop this launcher's lock and remove the session folder."""
    lock = _held.pop(session, None)
    if lock is not None:
        lock.close()
    shutil.rmtree(session, ignore_errors=True)


def _orphaned(session: Path) -> bool:
    lock_path = session / 'owner.lock'
    if lock_path.exists():
        try:
            with open(lock_path, 'a+b') as f:
                return _try_lock(f)     # closing the file releases it again
        except OSError:
            return False
    # A session from before owner.lock, or one still being created
    try:
        launcher = json.loads((session / 'owner.json').read_text()).get('launcher')
    except (OSError, ValueError, AttributeError):
        launcher = None
    if not isinstance(launcher, int):
        prefix = session.name.split('-')[0]
        launcher = int(prefix) if prefix.isdigit() else None
    return launcher is None or not pid_alive(launcher)


def session_processes(session: Path) -> list[int]:
    """Processes whose command line names the session folder: its DevBench
    and the WinUAE that DevBench started with the session's configuration."""
    if os.name == 'nt':
        folder = str(session).replace("'", "''")
        command = ['powershell', '-NoProfile', '-NonInteractive', '-Command',
                   "Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -and "
                   f"$_.CommandLine.Contains('{folder}') }} | ForEach-Object {{ $_.ProcessId }}"]
    else:
        command = ['pgrep', '-f', '--', re.sub(r'([.^$*+?()\[\]{}|\\])', r'\\\1', str(session))]
    try:
        found = subprocess.run(command, capture_output=True, text=True, timeout=15)
    except (OSError, subprocess.SubprocessError):
        return []
    return [int(p) for p in found.stdout.split() if p.isdigit() and int(p) != os.getpid()]


def stop_session(session: Path, timeout: float = 10.0) -> bool:
    """Stop the session's processes; True once none of them runs."""
    pids = session_processes(session)
    for pid in pids:
        try:
            os.kill(pid, signal.SIGTERM)    # TerminateProcess on Windows
        except OSError:
            pass
    deadline = time.monotonic() + timeout
    while any(pid_alive(p) for p in pids) and time.monotonic() < deadline:
        time.sleep(0.2)
    for pid in pids:
        if pid_alive(pid) and os.name != 'nt':
            try:
                os.kill(pid, signal.SIGKILL)
            except OSError:
                pass
    if pids:
        time.sleep(0.2)
    return not any(pid_alive(p) for p in pids)


def sweep() -> list[str]:
    """Remove the sessions of launchers that are gone, stopping what they
    left running. A session whose processes won't stop is kept for the next
    sweep."""
    removed = []
    for session in sessions_dir().iterdir():
        if session in _held or not _orphaned(session):
            continue
        if not stop_session(session):
            continue
        shutil.rmtree(session, ignore_errors=True)
        removed.append(session.name)
    return removed
