# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins

"""FS-UAE emulator process manager."""

from __future__ import annotations

import asyncio
import logging
import os
import shutil
import signal
import subprocess
import time
from pathlib import Path
from typing import Any

from .state import EventBus

logger = logging.getLogger(__name__)


# Locations searched when [emulator] binary = "auto". Ordered: patched-build
# first (so users with both installed get the debugger features), then stock
# fs-uae from PATH.
_PATCHED_FSUAE_CANDIDATES = [
    "$AMIGA_MCP_FSUAE_BIN",                    # explicit override
    "~/.amiga-devbench/fs-uae",                # installer-managed location
    "/tmp/fsuae-src/fs-uae",                   # default of fsuae_remote_patch/build.sh
    "~/code/fsuae_remote_patch/fs-uae",        # common dev checkout
]


def _expand(path: str) -> str:
    """Expand $VAR and ~ in a candidate path."""
    return os.path.expanduser(os.path.expandvars(path))


def _looks_patched(binary: str) -> bool | None:
    """Best-effort check: does this fs-uae binary include the RPC patch?

    Returns True/False if we could tell, None if inconclusive. We probe by
    grepping the binary for the RPC service banner — cheap and reliable.
    """
    try:
        p = Path(binary)
        if not p.exists() or not os.access(binary, os.X_OK):
            return None
        # The patched binary embeds the literal "fs-uae-rpc v1" service string.
        # Read at most 50 MB (real binary is ~20 MB) to bound the probe.
        with open(binary, "rb") as f:
            blob = f.read(50 * 1024 * 1024)
        return b"fs-uae-rpc" in blob
    except Exception:
        return None


def discover_fsuae_binary(configured: str = "auto") -> tuple[str, bool]:
    """Pick an fs-uae binary path. Returns (path, is_patched_likely).

    If `configured` is anything other than the literal "auto", return it as-is
    after a best-effort patched-detection probe (so logs still tell the user
    whether it's the debugger build). When "auto", scan the candidates above,
    preferring the first one that looks patched; otherwise fall back to stock
    fs-uae on PATH; otherwise the (likely stale) default.
    """
    if configured and configured.lower() != "auto":
        return configured, bool(_looks_patched(configured))

    for cand in _PATCHED_FSUAE_CANDIDATES:
        expanded = _expand(cand)
        if not expanded or expanded == cand and "$" in cand:  # unexpanded var
            continue
        if Path(expanded).is_file() and os.access(expanded, os.X_OK):
            if _looks_patched(expanded):
                return expanded, True

    # Fall back to whatever `fs-uae` is on PATH (likely stock from brew/apt).
    on_path = shutil.which("fs-uae")
    if on_path:
        return on_path, bool(_looks_patched(on_path))

    # Last-resort: the original hardcoded default. Will fail later in start()
    # with a clear "binary not found" error.
    return "/opt/homebrew/bin/fs-uae", False


class EmulatorManager:
    """Manages the FS-UAE emulator process lifecycle."""

    def __init__(
        self,
        binary: str = "/opt/homebrew/bin/fs-uae",
        config_file: str = "",
        event_bus: EventBus | None = None,
        extra_env: dict[str, str] | None = None,
    ) -> None:
        # Resolve "auto" sentinel up front so .get_status() reports the
        # real path and _find_external_pid() matches the actual binary name.
        resolved, is_patched = discover_fsuae_binary(binary)
        self._configured_binary = binary
        self._binary = resolved
        self._is_patched = is_patched
        self._config_file = config_file
        self._event_bus = event_bus
        self._extra_env = dict(extra_env) if extra_env else {}
        self._process: asyncio.subprocess.Process | None = None
        self._monitor_task: asyncio.Task | None = None
        self._started_at: float | None = None
        if (binary or "").lower() == "auto":
            tag = "patched (debugger build)" if is_patched else "stock"
            logger.info("Emulator auto-discovery: %s — %s", resolved, tag)
        elif is_patched:
            logger.info("Emulator: %s (patched debugger build detected)", resolved)

    def set_extra_env(self, env: dict[str, str]) -> None:
        """Replace the env-var overlay merged into the FS-UAE subprocess.

        Stock fs-uae ignores unknown vars; the patched build picks up
        FSUAE_RPC_PORT / FSUAE_RPC_PAUSE_AT_BOOT / FSUAE_GDB_PORT here.
        """
        self._extra_env = dict(env)

    @property
    def is_running(self) -> bool:
        if self._process is not None and self._process.returncode is None:
            return True
        # Check if emulator is running externally (started outside devbench)
        return self._find_external_pid() is not None

    @property
    def pid(self) -> int | None:
        if self._process and self._process.returncode is None:
            return self._process.pid
        return self._find_external_pid()

    def _find_external_pid(self) -> int | None:
        """Check if the emulator binary is running as an external process."""
        binary_name = Path(self._binary).name
        try:
            import subprocess
            result = subprocess.run(
                ["pgrep", "-x", binary_name],
                capture_output=True, text=True, timeout=2
            )
            if result.returncode == 0 and result.stdout.strip():
                return int(result.stdout.strip().split()[0])
        except Exception:
            pass
        return None

    @property
    def uptime(self) -> float | None:
        if self._started_at and self.is_running:
            return time.time() - self._started_at
        return None

    def get_status(self) -> dict[str, Any]:
        return {
            "running": self.is_running,
            "pid": self.pid,
            "uptime": round(self.uptime, 1) if self.uptime else None,
            "binary": self._binary,
            "configured_binary": self._configured_binary,
            "patched": self._is_patched,
            "config": self._config_file,
        }

    async def start(self) -> bool:
        """Start FS-UAE. Returns True on success."""
        if self.is_running:
            logger.warning("Emulator already running (pid %s)", self.pid)
            return True

        # Validate binary exists
        if not Path(self._binary).exists():
            logger.error("Emulator binary not found: %s", self._binary)
            return False

        # Config file is optional. FS-UAE takes a .fs-uae config path
        # argument; QEMU launcher scripts don't. If empty, we invoke
        # the binary with no arg; if provided, it must exist on disk.
        config_arg: list[str] = []
        if self._config_file:
            config_path = Path(self._config_file).expanduser()
            if not config_path.exists():
                logger.error("Emulator config not found: %s", config_path)
                return False
            config_arg = [str(config_path)]
            logger.info("Starting emulator: %s %s", self._binary, config_path)
        else:
            logger.info("Starting emulator: %s", self._binary)

        env = os.environ.copy()
        env.update(self._extra_env)
        if self._extra_env:
            logger.info("Emulator env overlay: %s",
                        ", ".join(f"{k}={v}" for k, v in self._extra_env.items()))

        try:
            self._process = await asyncio.create_subprocess_exec(
                self._binary, *config_arg,
                stdout=asyncio.subprocess.DEVNULL,
                stderr=asyncio.subprocess.DEVNULL,
                # Create new process group so we can cleanly kill it
                preexec_fn=os.setsid,
                env=env,
            )
            self._started_at = time.time()
            logger.info("Emulator started (pid %d)", self._process.pid)

            if self._event_bus:
                self._event_bus.publish("emulator_status", self.get_status())

            # Start monitor task
            self._monitor_task = asyncio.ensure_future(self._monitor())
            return True

        except Exception as e:
            logger.error("Failed to start emulator: %s", e)
            return False

    async def stop(self) -> bool:
        """Stop FS-UAE gracefully. Returns True if stopped."""
        if not self.is_running:
            return True

        pid = self._process.pid
        logger.info("Stopping emulator (pid %d)", pid)

        try:
            # Send SIGTERM to the process group
            os.killpg(os.getpgid(pid), signal.SIGTERM)
        except (OSError, ProcessLookupError):
            pass

        # Wait up to 5 seconds for graceful shutdown
        try:
            await asyncio.wait_for(self._process.wait(), timeout=5.0)
        except asyncio.TimeoutError:
            logger.warning("Emulator didn't stop gracefully, force killing")
            try:
                os.killpg(os.getpgid(pid), signal.SIGKILL)
            except (OSError, ProcessLookupError):
                pass
            try:
                await asyncio.wait_for(self._process.wait(), timeout=2.0)
            except asyncio.TimeoutError:
                pass

        self._process = None
        self._started_at = None

        if self._monitor_task:
            self._monitor_task.cancel()
            self._monitor_task = None

        if self._event_bus:
            self._event_bus.publish("emulator_status", self.get_status())

        logger.info("Emulator stopped")
        return True

    async def restart(self) -> bool:
        """Stop then start the emulator."""
        await self.stop()
        await asyncio.sleep(1.0)  # Brief pause between stop/start
        return await self.start()

    async def _monitor(self) -> None:
        """Background task that detects unexpected emulator exit."""
        try:
            if self._process:
                await self._process.wait()
                if self._started_at:  # Was running, now exited
                    rc = self._process.returncode
                    logger.warning("Emulator exited unexpectedly (rc=%s)", rc)
                    self._started_at = None
                    if self._event_bus:
                        self._event_bus.publish("emulator_status", {
                            **self.get_status(),
                            "crashed": rc != 0,
                            "exitCode": rc,
                        })
        except asyncio.CancelledError:
            pass

    def read_config(self) -> str:
        """Read the FS-UAE config file contents.

        Returns "" when no config file is configured (empty _config_file);
        without that guard Path("") resolves to CWD and .exists() returns
        True — .read_text() then dies with IsADirectoryError. QEMU
        profiles like qemu-os4 launch a shell script that takes no config,
        so an empty value is a legitimate "no config" state.
        """
        if not self._config_file:
            return ""
        config_path = Path(self._config_file).expanduser()
        if config_path.exists() and config_path.is_file():
            return config_path.read_text()
        return ""

    def write_config(self, content: str) -> None:
        """Write new content to the FS-UAE config file.

        No-op when no config file is configured — matches read_config's
        empty-string return. Prevents the web UI's Config → Save from
        landing at CWD/`.tmp` on a QEMU-style profile.
        """
        if not self._config_file:
            logger.info("write_config: skipped (no emulator_config set)")
            return
        config_path = Path(self._config_file).expanduser()
        # Atomic write
        tmp_path = config_path.with_suffix(".tmp")
        tmp_path.write_text(content)
        tmp_path.rename(config_path)
