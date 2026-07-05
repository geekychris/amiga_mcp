"""Control a remote (SSH-reachable) Amiga emulator.

Used when a profile has ``emulator_ssh`` set — for example, an AmiKit inside
Amiberry running on a Raspberry Pi that this devbench is talking to over TCP.
The interface intentionally mirrors :class:`EmulatorManager` so the two are
interchangeable at the call sites in ``server.py``.
"""

from __future__ import annotations

import asyncio
import logging
import shlex
import time
from typing import Any

from .state import EventBus

logger = logging.getLogger(__name__)

# Sync check that never blocks the event loop for long. If a remote SSH poll
# is stale by more than this, the cached is_running is treated as "unknown"
# (False) until the next background refresh.
STATUS_CACHE_TTL_SEC = 15.0

# Background poll interval for the remote emulator's status.
STATUS_POLL_INTERVAL_SEC = 10.0

# Timeouts for the different escalation stages of start().
START_CMD_TIMEOUT_SEC = 10.0
START_READY_TIMEOUT_SEC = 30.0
REBOOT_CMD_TIMEOUT_SEC = 10.0
REBOOT_READY_TIMEOUT_SEC = 120.0

DEFAULT_REBOOT_CMD = "sudo reboot"


class RemoteEmulatorController:
    """SSH-driven lifecycle wrapper for an emulator on another host.

    ``ready_probe`` — a ``host:port`` string — is the authoritative "up" signal
    (typically the bridge's own TCP port). ``status_cmd`` runs over SSH to
    detect the emulator process itself; both signals feed into
    :meth:`is_running` so a stuck emulator with a dead bridge still reports
    "not running".
    """

    def __init__(
        self,
        ssh: str,
        start_cmd: str,
        stop_cmd: str = "",
        status_cmd: str = "",
        ready_probe: str = "",
        reboot_cmd: str = DEFAULT_REBOOT_CMD,
        event_bus: EventBus | None = None,
        name: str = "remote emulator",
    ) -> None:
        self._ssh = ssh
        self._start_cmd = start_cmd
        self._stop_cmd = stop_cmd
        self._status_cmd = status_cmd
        self._ready_probe = ready_probe
        self._reboot_cmd = reboot_cmd
        self._event_bus = event_bus
        self._name = name

        self._is_running_cached = False
        self._last_probe_at: float | None = None
        self._started_at: float | None = None
        self._poll_task: asyncio.Task | None = None

    # ─── Status ───────────────────────────────────────────────────────

    @property
    def is_running(self) -> bool:
        """Cached running-state. Freshness is bounded by STATUS_CACHE_TTL_SEC."""
        if self._last_probe_at is None:
            return False
        if time.time() - self._last_probe_at > STATUS_CACHE_TTL_SEC:
            return False
        return self._is_running_cached

    @property
    def pid(self) -> int | None:  # remote PID; we don't track it
        return None

    @property
    def uptime(self) -> float | None:
        if self._started_at and self.is_running:
            return time.time() - self._started_at
        return None

    def get_status(self) -> dict[str, Any]:
        return {
            "running": self.is_running,
            "pid": None,
            "uptime": round(self.uptime, 1) if self.uptime else None,
            "binary": f"ssh://{self._ssh}",
            "configured_binary": f"ssh://{self._ssh}",
            "patched": None,
            "config": self._name,
            "remote": True,
            "ready_probe": self._ready_probe or None,
        }

    # ─── Lifecycle ────────────────────────────────────────────────────

    async def start(self) -> bool:
        """Bring the remote emulator up, escalating from start_cmd → reboot."""
        if await self._probe_running(fresh=True):
            logger.info("%s already running (probe passed)", self._name)
            self._started_at = self._started_at or time.time()
            self._start_poll_task()
            return True

        # Attempt 1: run the configured start command via SSH.
        if self._start_cmd:
            logger.info("Starting %s via SSH: %s", self._name, self._start_cmd)
            rc, _, err = await self._run_ssh(
                self._start_cmd, timeout=START_CMD_TIMEOUT_SEC
            )
            if rc != 0:
                logger.warning(
                    "%s start_cmd exited %s (stderr: %s)",
                    self._name, rc, err.strip()[:200],
                )
            if await self._wait_ready(START_READY_TIMEOUT_SEC):
                self._started_at = time.time()
                self._publish_status()
                self._start_poll_task()
                return True
            logger.warning(
                "%s did not become ready within %ss — falling back to reboot",
                self._name, START_READY_TIMEOUT_SEC,
            )

        # Attempt 2: reboot the host and wait for the ready probe to answer.
        logger.info("Rebooting %s via SSH: %s", self._name, self._reboot_cmd)
        # A reboot cuts our SSH session; ignore rc.
        await self._run_ssh(self._reboot_cmd, timeout=REBOOT_CMD_TIMEOUT_SEC)
        if await self._wait_ready(REBOOT_READY_TIMEOUT_SEC):
            self._started_at = time.time()
            self._publish_status()
            self._start_poll_task()
            return True

        logger.error("%s failed to come up after reboot", self._name)
        self._publish_status()
        return False

    async def stop(self) -> bool:
        """Stop the remote emulator via stop_cmd (if configured)."""
        if not self._stop_cmd:
            logger.warning("%s: no stop_cmd configured; cannot stop", self._name)
            return False
        logger.info("Stopping %s via SSH: %s", self._name, self._stop_cmd)
        rc, _, err = await self._run_ssh(self._stop_cmd, timeout=10.0)
        if rc != 0:
            logger.warning(
                "%s stop_cmd exited %s (stderr: %s)",
                self._name, rc, err.strip()[:200],
            )
        # Wait a moment then re-probe.
        await asyncio.sleep(1.5)
        running = await self._probe_running(fresh=True)
        if not running:
            self._started_at = None
            self._publish_status()
        return not running

    async def restart(self) -> bool:
        await self.stop()
        await asyncio.sleep(1.0)
        return await self.start()

    # ─── Probes ───────────────────────────────────────────────────────

    async def _probe_running(self, fresh: bool = False) -> bool:
        """Refresh the running-state cache and return the new value.

        Prefers ``status_cmd`` (SSH-driven, out-of-band) over a TCP connect
        to the bridge — some Amiga TCP stacks (notably Amiberry's
        bsdsocket_emu) don't like probe-and-close from third parties, so we
        only touch the bridge socket when we have no other signal.
        """
        if self._status_cmd:
            rc, _, _ = await self._run_ssh(self._status_cmd, timeout=6.0)
            self._is_running_cached = (rc == 0)
            self._last_probe_at = time.time()
            return self._is_running_cached

        # No SSH-side signal — fall back to a TCP probe. Only used when
        # status_cmd is empty, or in _wait_ready during a bring-up.
        tcp_ok = await self._tcp_ready()
        self._is_running_cached = tcp_ok
        self._last_probe_at = time.time() if fresh or tcp_ok else None
        return tcp_ok

    async def _tcp_ready(self, timeout: float = 1.0) -> bool:
        if not self._ready_probe or ":" not in self._ready_probe:
            return False
        host, _, port_s = self._ready_probe.rpartition(":")
        try:
            port = int(port_s)
        except ValueError:
            return False
        try:
            reader, writer = await asyncio.wait_for(
                asyncio.open_connection(host, port), timeout=timeout
            )
            writer.close()
            try:
                await writer.wait_closed()
            except Exception:
                pass
            return True
        except (OSError, asyncio.TimeoutError):
            return False

    async def _wait_ready(self, timeout: float) -> bool:
        """Poll until the emulator (and, if possible, the bridge) is up.

        We prefer the SSH ``status_cmd`` — it doesn't touch the emulator's
        TCP stack, so it can't disturb bsdsocket-style implementations. If
        that isn't configured we fall back to a TCP probe against the bridge
        port. We deliberately don't do BOTH; the caller (SerialConnection)
        will attempt the real connect right after this returns, and a stray
        open-then-immediate-close by us has caused Amiberry to unrecoverably
        drop its listener in testing.
        """
        deadline = time.time() + timeout
        while time.time() < deadline:
            if await self._probe_running(fresh=True):
                return True
            await asyncio.sleep(2.0)
        return False

    # ─── Background poll ─────────────────────────────────────────────

    def _start_poll_task(self) -> None:
        if self._poll_task and not self._poll_task.done():
            return
        self._poll_task = asyncio.ensure_future(self._poll_loop())

    def stop_poll_task(self) -> None:
        if self._poll_task and not self._poll_task.done():
            self._poll_task.cancel()
        self._poll_task = None

    async def _poll_loop(self) -> None:
        try:
            while True:
                await asyncio.sleep(STATUS_POLL_INTERVAL_SEC)
                prev = self._is_running_cached
                now = await self._probe_running(fresh=True)
                if now != prev:
                    self._publish_status()
        except asyncio.CancelledError:
            pass

    # ─── SSH helper ──────────────────────────────────────────────────

    async def _run_ssh(
        self, cmd: str, timeout: float
    ) -> tuple[int, str, str]:
        """Run ``cmd`` on the remote host via ``ssh``. Returns (rc, stdout, stderr).

        A timeout returns (124, "", "timeout"). Uses BatchMode so a prompt-only
        failure doesn't hang.
        """
        argv = [
            "ssh",
            "-o", "BatchMode=yes",
            "-o", "ConnectTimeout=5",
            "-o", "StrictHostKeyChecking=accept-new",
            self._ssh,
            cmd,
        ]
        try:
            proc = await asyncio.create_subprocess_exec(
                *argv,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
            )
        except FileNotFoundError:
            logger.error("ssh binary not found on PATH")
            return 127, "", "ssh not found"

        try:
            stdout_b, stderr_b = await asyncio.wait_for(
                proc.communicate(), timeout=timeout
            )
        except asyncio.TimeoutError:
            try:
                proc.kill()
            except Exception:
                pass
            return 124, "", "timeout"

        return (
            proc.returncode or 0,
            stdout_b.decode("utf-8", errors="replace") if stdout_b else "",
            stderr_b.decode("utf-8", errors="replace") if stderr_b else "",
        )

    def _publish_status(self) -> None:
        if self._event_bus:
            self._event_bus.publish("emulator_status", self.get_status())


def build_from_config(cfg: Any, event_bus: EventBus | None = None) -> RemoteEmulatorController | None:
    """Instantiate from a DevBenchConfig if remote fields are set. Else None."""
    ssh = getattr(cfg, "emulator_ssh", "") or ""
    if not ssh:
        return None
    start_cmd = getattr(cfg, "emulator_start_cmd", "") or ""
    if not start_cmd:
        # No start command means we can't bring it up; fall back to local.
        logger.warning(
            "emulator_ssh set but emulator_start_cmd empty; ignoring remote config"
        )
        return None
    return RemoteEmulatorController(
        ssh=ssh,
        start_cmd=start_cmd,
        stop_cmd=getattr(cfg, "emulator_stop_cmd", "") or "",
        status_cmd=getattr(cfg, "emulator_status_cmd", "") or "",
        ready_probe=getattr(cfg, "emulator_ready_probe", "") or "",
        reboot_cmd=getattr(cfg, "emulator_reboot_cmd", DEFAULT_REBOOT_CMD)
                    or DEFAULT_REBOOT_CMD,
        event_bus=event_bus,
        name=f"remote emulator ({ssh})",
    )
