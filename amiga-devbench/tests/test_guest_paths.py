# SPDX-License-Identifier: MIT
"""The guest's name for the deploy folder, from a WinUAE .uae's mounts."""
from types import SimpleNamespace

from amiga_devbench.config import load_config
from amiga_devbench.guest_paths import amiga_deploy_dir, launch_command, uae_folders


def setup(tmp_path, mounts, deploy, config_name='DevBench.uae', **extra):
    guest = tmp_path / 'guest'
    (guest / 'Dev').mkdir(parents=True, exist_ok=True)
    config = tmp_path / config_name
    config.write_text('\n'.join(m.format(guest=guest, tmp=tmp_path) for m in mounts) + '\n')
    return SimpleNamespace(deploy_dir=str(deploy(guest)), emulator_config=str(config),
                           amiga_deploy_dir='', **extra)


def test_the_boot_folder_of_devbenchs_winuae_guest(tmp_path):
    cfg = setup(tmp_path, ['filesystem2=rw,DH0:DevBench:{guest},0'], lambda g: g)
    assert amiga_deploy_dir(cfg) == 'DH0:'
    assert launch_command(cfg, 'hello', 'DH2:Dev') == 'DH0:hello'


def test_a_subfolder_and_the_innermost_mount(tmp_path):
    cfg = setup(tmp_path, ['filesystem2=rw,DH1:Work:{tmp},0', 'uaehf0=dir,rw,DH2:Dev:{guest},0'],
                lambda g: g / 'Dev')
    assert amiga_deploy_dir(cfg) == 'DH2:Dev'
    assert launch_command(cfg, 'hello', 'DH2:Dev') == 'DH2:Dev/hello'


def test_old_filesystem_lines_name_the_volume_and_spaces_are_quoted(tmp_path):
    cfg = setup(tmp_path, ['filesystem=rw,My Work:{guest}'], lambda g: g / 'Dev')
    assert launch_command(cfg, 'hello', 'DH2:Dev') == '"My Work:Dev/hello"'


def test_unknown_folders_and_fs_uae_keep_the_default(tmp_path):
    outside = setup(tmp_path, ['filesystem2=rw,DH0:DevBench:{guest},0'], lambda g: g.parent)
    assert amiga_deploy_dir(outside) is None
    assert launch_command(outside, 'hello', 'Dropbox:Dev') == 'Dropbox:Dev/hello'
    fsuae = setup(tmp_path, ['hard_drive_0 = {guest}'], lambda g: g, config_name='AmiKit.fs-uae')
    assert launch_command(fsuae, 'hello', 'DH2:Dev') == 'DH2:Dev/hello'
    assert launch_command(None, 'hello', 'DH2:Dev') == 'DH2:Dev/hello'


def test_amiga_deploy_dir_setting_wins(tmp_path):
    cfg = setup(tmp_path, ['filesystem2=rw,DH0:DevBench:{guest},0'], lambda g: g)
    cfg.amiga_deploy_dir = 'Work:Games'
    assert launch_command(cfg, 'hello', 'DH2:Dev') == 'Work:Games/hello'


def test_hardfiles_are_not_folders():
    assert uae_folders('hardfile2=rw,DH0:/disks/wb.hdf,32,1,2,512,0,,uae\n'
                       'uaehf1=hdf,rw,DH1:/disks/work.hdf,32,1,2,512,0,,uae\n') == []


def test_read_from_paths_and_profiles(tmp_path):
    path = tmp_path / 'devbench.toml'
    path.write_text('[paths]\namiga_deploy_dir = "DH2:Dev"\n\n'
                    '[profiles.winuae]\namiga_deploy_dir = "DH0:"\n')
    assert load_config(config_path=str(path)).amiga_deploy_dir == 'DH2:Dev'
    path.write_text(path.read_text().replace('[paths]', 'active_profile = "winuae"\n\n[paths]', 1))
    assert load_config(config_path=str(path)).amiga_deploy_dir == 'DH0:'
