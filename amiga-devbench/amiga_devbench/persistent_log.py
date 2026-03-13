"""Persistent append-only log file for devbench events.

Writes all bridge events, protocol traffic, and system events to a single
log file in key=value format for easy searching with grep, awk, lnav, etc.

Log file: {project_root}/logs/devbench.log
"""

from __future__ import annotations

import asyncio
import json
import logging
import os
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from .state import EventBus

logger = logging.getLogger(__name__)

# Events that are too noisy or binary to log in full
_SKIP_FIELDS = {"hexData", "scrdata", "bitmapData", "diffData", "sampleData"}


def _escape_val(v: Any) -> str:
    """Escape a value for key=value format. Quotes strings containing spaces."""
    if v is None:
        return ""
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return str(v)
    s = str(v)
    # Replace newlines and pipes (protocol delimiter)
    s = s.replace("\n", "\\n").replace("\r", "")
    if " " in s or "=" in s or '"' in s or "\t" in s:
        s = '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'
    return s


def _flatten(data: dict[str, Any], prefix: str = "") -> list[tuple[str, str]]:
    """Flatten a dict into key=value pairs."""
    pairs = []
    for k, v in data.items():
        if k in _SKIP_FIELDS:
            # Log presence and size but not the data itself
            if isinstance(v, str):
                pairs.append((prefix + k + "_len", str(len(v))))
            continue
        if k == "type":
            continue  # Already captured as event=
        full_key = prefix + k if not prefix else prefix + k
        if isinstance(v, dict):
            pairs.extend(_flatten(v, full_key + "."))
        elif isinstance(v, list):
            if len(v) <= 10 and all(isinstance(x, str) for x in v):
                pairs.append((full_key, ",".join(v)))
            else:
                pairs.append((full_key + "_count", str(len(v))))
        else:
            pairs.append((full_key, _escape_val(v)))
    return pairs


class PersistentLog:
    """Append-only structured log file."""

    def __init__(self, log_dir: str | Path) -> None:
        self._log_dir = Path(log_dir)
        self._log_dir.mkdir(parents=True, exist_ok=True)
        self._log_path = self._log_dir / "devbench.log"
        self._file = open(self._log_path, "a", buffering=1)  # line-buffered
        self._task: asyncio.Task | None = None
        logger.info("Persistent log: %s", self._log_path)

    @property
    def path(self) -> Path:
        return self._log_path

    def write_event(self, event: str, data: Any = None) -> None:
        """Write a single event line to the log file."""
        ts = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"
        parts = [f"ts={ts}", f"event={event}"]

        if isinstance(data, dict):
            for k, v in _flatten(data):
                parts.append(f"{k}={v}")
        elif data is not None:
            parts.append(f"data={_escape_val(data)}")

        line = " ".join(parts)
        try:
            self._file.write(line + "\n")
        except Exception:
            logger.exception("Failed to write persistent log")

    def write_tx(self, line: str) -> None:
        """Log an outgoing protocol line."""
        ts = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"
        # Parse the pipe-delimited command
        parts_raw = line.split("|", 1)
        cmd_type = parts_raw[0] if parts_raw else "?"
        payload = parts_raw[1] if len(parts_raw) > 1 else ""
        log_line = f"ts={ts} event=tx cmd={cmd_type}"
        if payload:
            # Truncate very long hex payloads
            if len(payload) > 200:
                log_line += f" payload={_escape_val(payload[:200])}... payload_len={len(payload)}"
            else:
                log_line += f" payload={_escape_val(payload)}"
        try:
            self._file.write(log_line + "\n")
        except Exception:
            logger.exception("Failed to write persistent log")

    def write_rx(self, line: str) -> None:
        """Log an incoming protocol line."""
        ts = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"
        parts_raw = line.split("|", 1)
        msg_type = parts_raw[0] if parts_raw else "?"
        payload = parts_raw[1] if len(parts_raw) > 1 else ""
        log_line = f"ts={ts} event=rx msg_type={msg_type}"
        if payload:
            if len(payload) > 200:
                log_line += f" payload={_escape_val(payload[:200])}... payload_len={len(payload)}"
            else:
                log_line += f" payload={_escape_val(payload)}"
        try:
            self._file.write(log_line + "\n")
        except Exception:
            logger.exception("Failed to write persistent log")

    def write_api(self, method: str, path: str, status: int = 200,
                  duration_ms: float = 0.0, error: str | None = None) -> None:
        """Log an API request."""
        ts = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"
        line = f"ts={ts} event=api method={method} url={_escape_val(path)} status={status} duration_ms={duration_ms:.1f}"
        if error:
            line += f" error={_escape_val(error)}"
        try:
            self._file.write(line + "\n")
        except Exception:
            logger.exception("Failed to write persistent log")

    async def subscribe_to_bus(self, bus: EventBus) -> None:
        """Subscribe to all events and log them. Runs until cancelled."""
        async with bus.subscribe("*") as queue:
            while True:
                try:
                    evt, data = await asyncio.wait_for(queue.get(), timeout=60.0)
                    self.write_event(evt, data)
                except asyncio.TimeoutError:
                    continue
                except asyncio.CancelledError:
                    break

    def start(self, bus: EventBus) -> None:
        """Start background task to log all bus events."""
        self._task = asyncio.ensure_future(self.subscribe_to_bus(bus))
        # Write startup marker
        self.write_event("devbench_start", {"pid": os.getpid()})

    def stop(self) -> None:
        """Stop the background logging task and close the file."""
        if self._task and not self._task.done():
            self._task.cancel()
        self.write_event("devbench_stop")
        try:
            self._file.close()
        except Exception:
            pass
