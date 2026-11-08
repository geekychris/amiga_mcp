# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

"""The per-user runtime directory for PID and lock files."""

import getpass
import os
import stat
import tempfile


def private_dir() -> str:
    """A per-user directory only its owner can use, for PID and lock files."""
    d = os.path.join(tempfile.gettempdir(), f"amiga-devbench-{getpass.getuser()}")
    os.makedirs(d, mode=0o700, exist_ok=True)
    st = os.lstat(d)
    if not stat.S_ISDIR(st.st_mode) or (hasattr(os, "getuid") and st.st_uid != os.getuid()):
        raise RuntimeError(f"{d} is not a directory of ours; refusing to keep PID files there")
    if os.name != "nt":
        os.chmod(d, 0o700)
    return d
