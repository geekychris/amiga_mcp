# SPDX-License-Identifier: MIT
"""Private WinUAE sessions: the guest copy, cleanup, and the launcher's
--private mode with a stand-in WinUAE."""
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

import httpx
import pytest

from amiga_devbench import winuae_private as wp
from tests.test_mcp_stdio import Client


def guest_folder(root: Path, port: int = 1234) -> Path:
    folder = root / 'guest'
    (folder / 'S').mkdir(parents=True)
    (folder / 'S' / 'Startup-Sequence').write_text(
        f'Echo "Starting bridge" >SYS:bridge-start.log\nSYS:amiga-bridge TCP {port}\n')
    (folder / 'C').mkdir()
    (folder / 'C' / 'Dir').write_bytes(b'\0\0\x03\xf3')
    return folder


def uae_config(root: Path, guest: Path) -> Path:
    work = root / 'work'
    work.mkdir()
    config = root / 'DevBench.uae'
    config.write_text('\n'.join([
        'kickstart_rom_file=/roms/kick.rom',
        f'filesystem2=rw,DH0:DevBench:{guest},0',
        f'uaehf0=dir,rw,DH0:DevBench:{guest},0',
        f'filesystem2=rw,DH1:Work:{work},-128',
        f'filesystem=rw,Old:{work}',
        'hardfile2=rw,DH2:/disks/system.hdf,32,1,2,512,0,,uae',
        'hardfile2=ro,DH3:/disks/cd.hdf,32,1,2,512,0,,uae',
        'unix.serial_port=TCP://0.0.0.0:1234',
        'gdb_port=2346',
    ]) + '\n')
    return config


def test_the_session_boots_a_copy_with_its_own_bridge_port(tmp_path):
    guest = guest_folder(tmp_path)
    config = uae_config(tmp_path, guest)
    original = config.read_text()
    session = tmp_path / 'session'
    session.mkdir()
    prepared = wp.prepare_guest(config, session, 40123)

    assert prepared.source == guest and prepared.folder == session / 'guest'
    assert 'amiga-bridge TCP 40123' in (prepared.folder / 'S' / 'Startup-Sequence').read_text()
    assert (prepared.folder / 'C' / 'Dir').read_bytes() == b'\0\0\x03\xf3'
    assert 'amiga-bridge TCP 1234' in (guest / 'S' / 'Startup-Sequence').read_text()
    assert config.read_text() == original

    lines = prepared.config.read_text().splitlines()
    assert f'filesystem2=rw,DH0:DevBench:{prepared.folder},0' in lines
    assert f'uaehf0=dir,rw,DH0:DevBench:{prepared.folder},0' in lines
    assert f'filesystem2=ro,DH1:Work:{tmp_path / "work"},-128' in lines
    assert f'filesystem=ro,Old:{tmp_path / "work"}' in lines
    assert 'hardfile2=ro,DH2:/disks/system.hdf,32,1,2,512,0,,uae' in lines
    assert 'hardfile2=ro,DH3:/disks/cd.hdf,32,1,2,512,0,,uae' in lines
    assert not any(line.startswith('unix.serial_port') for line in lines)
    assert 'kickstart_rom_file=/roms/kick.rom' in lines


def test_a_configuration_without_the_bridge_guest_is_refused(tmp_path):
    config = tmp_path / 'other.uae'
    config.write_text('hardfile2=rw,DH0:/disks/system.hdf,32,1,2,512,0,,uae\n')
    with pytest.raises(wp.PrivateError, match='amiga-bridge TCP'):
        wp.prepare_guest(config, tmp_path, 40123)


def test_sweep_stops_and_removes_sessions_of_dead_launchers(tmp_path, monkeypatch):
    monkeypatch.setattr(wp, 'sessions_dir', lambda: tmp_path)
    gone = subprocess.Popen([sys.executable, '-c', 'pass'])
    gone.wait()
    dead = tmp_path / f'{gone.pid}-aaaaaa'
    dead.mkdir()
    (dead / 'owner.json').write_text(json.dumps({'launcher': gone.pid}))
    # A process left behind, named by the session folder like WinUAE's -f.
    orphan = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)', '-f', str(dead / 'x.uae')])
    live = tmp_path / f'{os.getpid()}-bbbbbb'
    live.mkdir()
    (live / 'owner.json').write_text(json.dumps({'launcher': os.getpid()}))
    try:
        assert wp.sweep() == [dead.name]
        assert orphan.wait(10) is not None
        assert not dead.exists() and live.exists()
    finally:
        if orphan.poll() is None:
            orphan.kill()


FAKE_WINUAE = '''#!{python}
import sys, time
open({record!r}, 'w').write(' '.join(sys.argv[1:]))
time.sleep(600)
'''


def test_private_launcher_runs_its_own_devbench_and_winuae(tmp_path):
    guest = guest_folder(tmp_path)
    config = uae_config(tmp_path, guest)
    record = tmp_path / 'winuae-args'
    winuae = tmp_path / 'bin' / 'winuae'
    winuae.parent.mkdir()
    winuae.write_text(FAKE_WINUAE.format(python=sys.executable, record=str(record)))
    winuae.chmod(0o755)
    (tmp_path / 'devbench.toml').write_text('')

    client = Client(tmp_path, '--private', '--config', str(tmp_path / 'devbench.toml'),
                    '--emulator-binary', str(winuae), '--emulator-config', str(config))
    session = None
    try:
        init = client.initialize()
        assert init['serverInfo']['name'] == 'amiga-dev', init
        assert 'private WinUAE' in init['instructions']
        assert 'amiga_winuae_control' in client.tools()
        deadline = time.monotonic() + 20
        while not record.exists() and time.monotonic() < deadline:
            time.sleep(0.2)
        args = record.read_text().split()
        uae = Path(args[args.index('-f') + 1])
        session = uae.parent
        assert uae.name == 'DevBench-private.uae' and (session / 'guest').is_dir()
        gdb = next(a for a in args if a.startswith('gdb_port='))
        assert gdb != 'gdb_port=2346' and 'debugging_features=gdbserver' in args
        assert wp.session_processes(session)
        # Its settings are generated: the web UI must not save them anywhere.
        web = re.search(r'web UI (http://127\.0\.0\.1:\d+)/', init['instructions'])[1]
        assert httpx.get(f'{web}/api/config').json()['save_disabled']
        saved = httpx.post(f'{web}/api/config', json={'paths': {'deploy_dir': '/elsewhere'}}).json()
        assert 'not saved' in saved['error'] and (tmp_path / 'devbench.toml').read_text() == ''
    finally:
        client.close()
    assert session is not None and not session.exists()
    assert not wp.session_processes(session)


def test_private_without_winuae_says_what_is_missing(tmp_path):
    (tmp_path / 'devbench.toml').write_text('')
    client = Client(tmp_path, '--private', '--config', str(tmp_path / 'devbench.toml'))
    try:
        init = client.initialize()
        assert init['serverInfo']['version'] == 'offline'
        assert 'Cannot set up a private WinUAE session' in init['instructions']
        assert '--emulator-binary' in init['instructions']
    finally:
        client.close()
