"""FS-UAE emulator process manager."""

from __future__ import annotations

import asyncio
import logging
import os
import signal
import time
from pathlib import Path
from typing import Any

from .state import EventBus

logger = logging.getLogger(__name__)


class EmulatorManager:
    """Manages the FS-UAE emulator process lifecycle."""

    def __init__(
        self,
        binary: str = "/opt/homebrew/bin/fs-uae",
        config_file: str = "",
        event_bus: EventBus | None = None,
    ) -> None:
        self._binary = binary
        self._config_file = config_file
        self._event_bus = event_bus
        self._process: asyncio.subprocess.Process | None = None
        self._monitor_task: asyncio.Task | None = None
        self._started_at: float | None = None

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

        # Validate config exists
        config_path = Path(self._config_file).expanduser()
        if not config_path.exists():
            logger.error("Emulator config not found: %s", config_path)
            return False

        logger.info("Starting emulator: %s %s", self._binary, config_path)

        try:
            self._process = await asyncio.create_subprocess_exec(
                self._binary, str(config_path),
                stdout=asyncio.subprocess.DEVNULL,
                stderr=asyncio.subprocess.DEVNULL,
                # Create new process group so we can cleanly kill it
                preexec_fn=os.setsid,
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
        """Read the FS-UAE config file contents."""
        config_path = Path(self._config_file).expanduser()
        if config_path.exists():
            return config_path.read_text()
        return ""

    def write_config(self, content: str) -> None:
        """Write new content to the FS-UAE config file."""
        config_path = Path(self._config_file).expanduser()
        # Atomic write
        tmp_path = config_path.with_suffix(".tmp")
        tmp_path.write_text(content)
        tmp_path.rename(config_path)
