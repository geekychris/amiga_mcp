# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

"""The per-user runtime directory for PID and lock files."""

import getpass
import os
import stat
import tempfile


class NotOurDirectory(RuntimeError):
    """The runtime directory exists but isn't a directory of this user's."""


def private_dir() -> str:
    """A per-user directory only its owner can use, for PID and lock files."""
    d = os.path.join(tempfile.gettempdir(), f"amiga-devbench-{getpass.getuser()}")
    os.makedirs(d, mode=0o700, exist_ok=True)
    st = os.lstat(d)
    if not stat.S_ISDIR(st.st_mode) or (hasattr(os, "getuid") and st.st_uid != os.getuid()):
        raise NotOurDirectory(f"{d} is not a directory of ours; refusing to keep PID files there")
    if os.name != "nt":
        os.chmod(d, 0o700)
    return d


def pid_alive(pid: int) -> bool:
    """Whether a process runs, without signalling it (on Windows,
    os.kill(pid, 0) would end it)."""
    if os.name == "nt":
        import ctypes
        kernel32 = ctypes.windll.kernel32
        handle = kernel32.OpenProcess(0x00100000, False, pid)   # SYNCHRONIZE
        if not handle:
            return False
        try:
            return kernel32.WaitForSingleObject(handle, 0) == 0x102  # WAIT_TIMEOUT
        finally:
            kernel32.CloseHandle(handle)
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    except OSError:
        return False
    return True
