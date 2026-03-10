"""Shared state and event bus for amiga-devbench."""

from __future__ import annotations

import asyncio
import logging
from collections import deque
from contextlib import asynccontextmanager
from dataclasses import dataclass, field
from typing import Any, AsyncIterator

logger = logging.getLogger(__name__)


@dataclass
class AmigaState:
    """Shared mutable state for the Amiga connection."""

    logs: deque[dict[str, Any]] = field(default_factory=lambda: deque(maxlen=1000))
    vars: dict[str, dict[str, Any]] = field(default_factory=dict)
    last_heartbeat: dict[str, Any] | None = None
    clients: list[str] = field(default_factory=list)
    connected: bool = False
    connection_mode: str = "tcp"
    last_crash: dict[str, Any] | None = None

    def add_log(self, msg: dict[str, Any]) -> None:
        self.logs.append(msg)

    def get_recent_logs(self, count: int = 100, level: str | None = None) -> list[dict[str, Any]]:
        logs = list(self.logs)
        if level:
            code = level.upper()[0] if level else None
            logs = [l for l in logs if l.get("level") == code]
        return logs[-count:]


class EventBus:
    """Async pub/sub event bus using per-subscriber queues."""

    def __init__(self) -> None:
        self._subscribers: dict[int, tuple[set[str], asyncio.Queue[tuple[str, Any]]]] = {}
        self._next_id = 0

    def publish(self, event: str, data: Any = None) -> None:
        """Publish an event to all subscribers interested in it."""
        for _sid, (events, queue) in self._subscribers.items():
            if event in events or "*" in events:
                try:
                    queue.put_nowait((event, data))
                except asyncio.QueueFull:
                    # Drop oldest if queue is full
                    try:
                        queue.get_nowait()
                    except asyncio.QueueEmpty:
                        pass
                    try:
                        queue.put_nowait((event, data))
                    except asyncio.QueueFull:
                        pass

    @asynccontextmanager
    async def subscribe(self, *events: str) -> AsyncIterator[asyncio.Queue[tuple[str, Any]]]:
        """Subscribe to events. Yields a queue that receives (event, data) tuples."""
        sid = self._next_id
        self._next_id += 1
        queue: asyncio.Queue[tuple[str, Any]] = asyncio.Queue(maxsize=1000)
        self._subscribers[sid] = (set(events), queue)
        try:
            yield queue
        finally:
            self._subscribers.pop(sid, None)

    async def wait_for(
        self, event: str, timeout: float = 5.0, predicate: Any = None
    ) -> dict[str, Any] | None:
        """Wait for a single event matching an optional predicate, with timeout."""
        async with self.subscribe(event) as queue:
            try:
                deadline = asyncio.get_event_loop().time() + timeout
                while True:
                    remaining = deadline - asyncio.get_event_loop().time()
                    if remaining <= 0:
                        return None
                    evt, data = await asyncio.wait_for(queue.get(), timeout=remaining)
                    if predicate is None or predicate(data):
                        return data
            except (asyncio.TimeoutError, asyncio.CancelledError):
                return None
