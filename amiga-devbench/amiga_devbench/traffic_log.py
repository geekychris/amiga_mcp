"""Traffic logger for MCP and REST API calls."""

from __future__ import annotations

import asyncio
import json
import time
import uuid
from collections import deque
from dataclasses import dataclass, field, asdict
from typing import Any


@dataclass
class TrafficEntry:
    id: str
    timestamp: float
    kind: str  # "mcp" or "rest"
    method: str  # HTTP method or MCP tool name
    path: str  # URL path or "mcp://<tool>"
    request_body: Any = None
    response_body: Any = None
    status: int = 200
    duration_ms: float = 0.0
    error: str | None = None

    def to_dict(self) -> dict:
        d = asdict(self)
        # Truncate large bodies for list view
        return d

    def summary(self) -> dict:
        """Compact version for list views."""
        return {
            "id": self.id,
            "timestamp": self.timestamp,
            "kind": self.kind,
            "method": self.method,
            "path": self.path,
            "status": self.status,
            "duration_ms": self.duration_ms,
            "error": self.error,
        }


class TrafficLog:
    """In-memory circular buffer of API/MCP traffic."""

    def __init__(self, maxlen: int = 2000):
        self._entries: deque[TrafficEntry] = deque(maxlen=maxlen)
        self._by_id: dict[str, TrafficEntry] = {}
        self._lock = asyncio.Lock()

    def record(
        self,
        kind: str,
        method: str,
        path: str,
        request_body: Any = None,
        response_body: Any = None,
        status: int = 200,
        duration_ms: float = 0.0,
        error: str | None = None,
    ) -> TrafficEntry:
        entry = TrafficEntry(
            id=uuid.uuid4().hex[:12],
            timestamp=time.time(),
            kind=kind,
            method=method,
            path=path,
            request_body=request_body,
            response_body=response_body,
            status=status,
            duration_ms=duration_ms,
            error=error,
        )
        # If buffer is full, evict oldest from index
        if len(self._entries) == self._entries.maxlen:
            evicted = self._entries[0]
            self._by_id.pop(evicted.id, None)
        self._entries.append(entry)
        self._by_id[entry.id] = entry
        return entry

    def get(self, entry_id: str) -> TrafficEntry | None:
        return self._by_id.get(entry_id)

    def list_entries(
        self,
        kind: str | None = None,
        search: str | None = None,
        limit: int = 100,
        offset: int = 0,
    ) -> list[dict]:
        """Return entries matching filters, newest first."""
        results = []
        for entry in reversed(self._entries):
            if kind and entry.kind != kind:
                continue
            if search:
                q = search.lower()
                haystack = f"{entry.method} {entry.path} {entry.error or ''}".lower()
                # Also search request/response bodies
                if entry.request_body:
                    haystack += " " + json.dumps(entry.request_body, default=str).lower()
                if entry.response_body:
                    body_str = json.dumps(entry.response_body, default=str)
                    # Only search first 2000 chars of response to keep it fast
                    haystack += " " + body_str[:2000].lower()
                if q not in haystack:
                    continue
            results.append(entry)

        total = len(results)
        page = results[offset : offset + limit]
        return {
            "total": total,
            "entries": [e.summary() for e in page],
        }

    def get_detail(self, entry_id: str) -> dict | None:
        entry = self._by_id.get(entry_id)
        if not entry:
            return None
        return entry.to_dict()

    def clear(self):
        self._entries.clear()
        self._by_id.clear()

    @property
    def count(self) -> int:
        return len(self._entries)
