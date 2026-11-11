# SPDX-License-Identifier: MIT
"""Settings -> Save writes only what the form changed, into the file the
configuration came from, and never settings given on the command line."""
import textwrap
import tomllib
from argparse import Namespace

import pytest

from amiga_devbench.config import (ConfigSaveError, apply_cli_overrides, load_config, patch_toml,
                                   save_changes)

LOCAL = textwrap.dedent("""\
    # my local setup
    active_profile = "winuae"

    [profiles.winuae]
    description = "WinUAE"   # the one I use
    deploy_dir = "~/guests/winuae"
    winuae_debug_port = 2346

    [serial]
    port = 1234  # bridge

    [emulator]
    auto_start = false
""")


def arguments(tmp_path, **given):
    """The command line's arguments, as __main__ parses them."""
    values = dict(config=None, project_root=str(tmp_path), profile=None, mode=None, serial_host=None,
                  serial_port=1234, pty_path="/tmp/amiga-serial", port=3000, deploy_dir=None,
                  log_level="INFO", simulator=False)
    return Namespace(**{**values, **given})


def config(tmp_path, **given):
    path = tmp_path / "devbench.local.toml"
    path.write_text(LOCAL)
    args = arguments(tmp_path, config=str(path), **given)
    cfg = load_config(config_path=args.config, project_root=args.project_root)
    apply_cli_overrides(cfg, args)
    return cfg, path


def test_patch_keeps_comments_and_other_tables():
    text = patch_toml(LOCAL, ("serial",), {"port": 4321, "host": "10.0.0.2"})
    assert "# my local setup" in text and 'description = "WinUAE"   # the one I use' in text
    assert "port = 4321  # bridge" in text
    data = tomllib.loads(text)
    assert data["serial"] == {"port": 4321, "host": "10.0.0.2"}
    assert data["profiles"]["winuae"]["winuae_debug_port"] == 2346


def test_patch_adds_a_missing_table():
    data = tomllib.loads(patch_toml(LOCAL, ("profiles", "qemu os4"), {"mode": "tcp"}))
    assert data["profiles"]["qemu os4"] == {"mode": "tcp"}


def test_saves_into_the_loaded_file_and_the_active_profile(tmp_path):
    cfg, path = config(tmp_path)
    assert cfg.config_path == str(path)
    written = save_changes(cfg, {"deploy_dir": "/new/guest", "crash_handler_auto_enable": False})
    assert written == str(path) and not (tmp_path / "devbench.toml").exists()
    data = tomllib.loads(path.read_text())
    assert data["profiles"]["winuae"]["deploy_dir"] == "/new/guest"      # the profile wins over [paths]
    assert data["bridge"] == {"crash_handler_auto_enable": False}         # no profile key: its table
    assert "paths" not in data
    assert cfg.profiles["winuae"]["deploy_dir"] == "/new/guest"


def test_without_a_profile_settings_go_into_their_table(tmp_path):
    path = tmp_path / "devbench.toml"
    path.write_text("[serial]\nport = 1234\n")
    cfg = load_config(config_path=str(path))
    save_changes(cfg, {"serial_host": "amiga.local"})
    assert tomllib.loads(path.read_text())["serial"] == {"port": 1234, "host": "amiga.local"}


def test_refuses_what_it_cannot_patch(tmp_path):
    path = tmp_path / "devbench.toml"
    path.write_text('serial = { port = 1234 }\n')       # an inline table
    cfg = load_config(config_path=str(path))
    with pytest.raises(ConfigSaveError, match="by hand"):
        save_changes(cfg, {"serial_port": 4321})
    assert path.read_text() == 'serial = { port = 1234 }\n'


def test_endpoint_saves_only_what_the_form_changed(tmp_path):
    pytest.importorskip("httpx")
    from starlette.testclient import TestClient
    from amiga_devbench.server import create_app

    cfg, path = config(tmp_path, serial_port=5555)
    client = TestClient(create_app(arguments(tmp_path, serial_port=5555), cfg))          # without its lifespan: nothing connects
    form = client.get("/api/config").json()
    assert form["serial"]["port"] == 5555                 # the command line's value
    form["paths"]["deploy_dir"] = "/other"
    reply = client.post("/api/config", json={k: form[k] for k in ("serial", "emulator", "paths", "bridge")}).json()
    assert reply == {"status": "saved", "path": str(path), "changed": ["deploy_dir"]}
    data = tomllib.loads(path.read_text())
    assert data["serial"] == {"port": 1234}               # --serial-port was not saved
    assert data["profiles"]["winuae"]["deploy_dir"] == "/other"
    assert not (tmp_path / "devbench.toml").exists()
    again = client.post("/api/config", json={"paths": {"deploy_dir": "/other"}}).json()
    assert again["changed"] == []
