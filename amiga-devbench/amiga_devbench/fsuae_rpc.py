# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins

"""Client for the FS-UAE remote-debug HTTP API.

The API is provided by the patched fs-uae build at
https://github.com/geekychris/fsuae_remote_patch — when launched with
FSUAE_RPC_PORT=N, fs-uae binds an HTTP/JSON-RPC server on 127.0.0.1:N
exposing /v1/pause, /v1/cpu, /v1/breakpoints, /v1/disasm, etc.

Stock fs-uae ignores FSUAE_RPC_PORT, so probes simply fail to connect.
We treat that as "feature unavailable" and degrade gracefully.
"""

from __future__ import annotations

import asyncio
import json
import logging
import time
from typing import Any

import httpx

try:
    import websockets
    _HAS_WEBSOCKETS = True
except ImportError:
    websockets = None  # type: ignore[assignment]
    _HAS_WEBSOCKETS = False

from .state import EventBus

logger = logging.getLogger(__name__)


class FsuaeRpcClient:
    """Async wrapper around the fs-uae-rpc HTTP API with availability tracking.

    `status` cycles unavailable → available → error → unavailable as the
    poller (started via `start_poller`) probes /v1/ping. Subscribers see
    `fsuae_rpc_status` events when the status changes; the snapshot is
    also exposed via `snapshot()` for /api/status.
    """

    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = 8765,
        enabled: str = "auto",
        event_bus: EventBus | None = None,
        poll_interval: float = 5.0,
    ) -> None:
        self._host = host
        self._port = port
        self._enabled = enabled.lower()
        self._event_bus = event_bus
        self._poll_interval = poll_interval
        self._base = f"http://{host}:{port}"

        self._client = httpx.AsyncClient(base_url=self._base, timeout=5.0)
        self._status = "unavailable"  # unavailable | available | error | disabled
        self._service: str | None = None
        self._last_probe: float | None = None
        self._last_error: str | None = None
        self._poll_task: asyncio.Task | None = None

        # WebSocket /v1/events bridge — connects when status flips to available,
        # republishes frames as `fsuae_event` on the EventBus so the UI gets
        # push notifications for paused / running / wp_hit without polling.
        self._ws_task: asyncio.Task | None = None
        self._ws_connected = False

        if self._enabled == "off":
            self._status = "disabled"

    @property
    def base_url(self) -> str:
        return self._base

    @property
    def status(self) -> str:
        return self._status

    @property
    def available(self) -> bool:
        return self._status == "available"

    def snapshot(self) -> dict[str, Any]:
        return {
            "status": self._status,
            "enabled": self._enabled,
            "host": self._host,
            "port": self._port,
            "base_url": self._base,
            "service": self._service,
            "last_probe": self._last_probe,
            "last_error": self._last_error,
        }

    async def aclose(self) -> None:
        for t in (self._poll_task, self._ws_task):
            if t:
                t.cancel()
                try:
                    await t
                except (asyncio.CancelledError, Exception):
                    pass
        self._poll_task = None
        self._ws_task = None
        await self._client.aclose()

    # ─── Availability poller ────────────────────────────────────────────

    def start_poller(self) -> None:
        if self._enabled == "off":
            logger.info("fsuae-rpc: disabled in config; poller not started")
            return
        if self._poll_task is not None:
            return
        self._poll_task = asyncio.ensure_future(self._poll_loop())

    async def _poll_loop(self) -> None:
        try:
            while True:
                await self.probe()
                await asyncio.sleep(self._poll_interval)
        except asyncio.CancelledError:
            pass

    async def probe(self) -> bool:
        """One-shot /v1/ping probe. Updates status and publishes events on change."""
        prev = self._status
        prev_service = self._service
        self._last_probe = time.time()
        try:
            r = await self._client.get("/v1/ping", timeout=1.5)
            if r.status_code == 200:
                body = r.json()
                if body.get("ok"):
                    self._status = "available"
                    self._service = body.get("service")
                    self._last_error = None
                else:
                    self._status = "error"
                    self._last_error = body.get("err") or "ping returned ok=false"
            else:
                self._status = "error"
                self._last_error = f"HTTP {r.status_code}"
        except (httpx.ConnectError, httpx.ReadTimeout, httpx.ConnectTimeout):
            self._status = "unavailable"
            self._last_error = None
            self._service = None
        except Exception as e:
            self._status = "error"
            self._last_error = f"{type(e).__name__}: {e}"

        if (self._status, self._service) != (prev, prev_service):
            logger.info("fsuae-rpc: status %s → %s (%s)",
                        prev, self._status, self._service or self._last_error or "")
            if self._event_bus:
                self._event_bus.publish("fsuae_rpc_status", self.snapshot())

        # Start the WS event bridge the first time we see `available`. The loop
        # itself handles reconnects, so we don't tear it down on transient
        # status drops — we just leave it to retry.
        if self._status == "available" and self._ws_task is None and _HAS_WEBSOCKETS:
            self._ws_task = asyncio.ensure_future(self._ws_loop())

        return self._status == "available"

    # ─── WebSocket /v1/events bridge ────────────────────────────────────

    async def _ws_loop(self) -> None:
        """Connect to /v1/events and republish frames as 'fsuae_event' on the bus.

        Reconnects with backoff on any failure. The patched fs-uae build
        speaks plain JSON frames, one event per WS message — typically
        `{"event": "hello"|"paused"|"running"|"wp_hit", ...}`. We pass them
        through as-is so the UI can dispatch on `event`.

        Note on fs-uae quirks: the patched server doesn't run a ws-read
        loop, so client pings get no pong → set ping_interval=None to
        avoid spurious client-side timeouts. The server-side
        single-threaded HTTP loop also drops idle WS connections fairly
        aggressively on some macOS builds; we silently reconnect.
        """
        ws_url = f"ws://{self._host}:{self._port}/v1/events"
        backoff = 1.0
        ever_connected = False
        while True:
            try:
                async with websockets.connect(
                    ws_url,
                    ping_interval=None,  # fs-uae doesn't pong; disable client pings
                    open_timeout=3.0,
                    close_timeout=1.0,
                ) as ws:
                    self._ws_connected = True
                    backoff = 1.0
                    if not ever_connected:
                        logger.info("fsuae-rpc: WS event stream connected (%s)", ws_url)
                        ever_connected = True
                        if self._event_bus:
                            self._event_bus.publish("fsuae_event",
                                                    {"event": "ws_connected", "url": ws_url})
                    async for msg in ws:
                        try:
                            data = json.loads(msg) if isinstance(msg, (str, bytes)) else None
                        except (ValueError, TypeError):
                            continue
                        if not isinstance(data, dict):
                            continue
                        if self._event_bus:
                            self._event_bus.publish("fsuae_event", data)
            except asyncio.CancelledError:
                self._ws_connected = False
                return
            except Exception as e:
                self._ws_connected = False
                logger.debug("fsuae-rpc: WS disconnected (%s: %s); retry in %.0fs",
                             type(e).__name__, e, backoff)
                # Backoff up to 15s. Don't spam the event bus on each blip —
                # the UI doesn't need every reconnect, just the final state.
                await asyncio.sleep(backoff)
                backoff = min(backoff * 1.5, 15.0)

    # ─── Endpoint wrappers ──────────────────────────────────────────────
    # Each returns the parsed JSON body; on transport failure returns
    # {"ok": False, "err": "<reason>"} so callers can render a uniform error.

    async def _call(self, method: str, path: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
        if self._status == "disabled":
            return {"ok": False, "err": "fsuae-rpc disabled in config"}
        # fs-uae's tiny HTTP parser does NOT URL-decode query strings, so
        # we have to send forward-slashes and `:` literal — otherwise the
        # `path=` param for /v1/state/save arrives as `%2FUsers%2F...`.
        # Build the query string by hand with a permissive safe-set.
        from urllib.parse import quote
        url = path
        if params:
            parts = []
            for k, v in params.items():
                if v is None:
                    continue
                # Keep `/ : . - _ ~` literal — they're never ambiguous in
                # query values and fs-uae's parser doesn't decode them.
                ev = quote(str(v), safe="/:.-_~")
                parts.append(f"{quote(k, safe='')}={ev}")
            if parts:
                url = path + "?" + "&".join(parts)
        try:
            r = await self._client.request(method, url)
            try:
                return r.json()
            except ValueError:
                return {"ok": False, "err": f"non-JSON response (HTTP {r.status_code})"}
        except httpx.ConnectError:
            self._status = "unavailable"
            return {"ok": False, "err": f"fsuae-rpc not reachable at {self._base} — is fs-uae running with FSUAE_RPC_PORT set?"}
        except Exception as e:
            return {"ok": False, "err": f"{type(e).__name__}: {e}"}

    # State / control
    async def ping(self) -> dict[str, Any]:
        return await self._call("GET", "/v1/ping")

    async def state(self) -> dict[str, Any]:
        return await self._call("GET", "/v1/state")

    async def pause(self) -> dict[str, Any]:
        return await self._call("POST", "/v1/pause")

    async def resume(self) -> dict[str, Any]:
        return await self._call("POST", "/v1/resume")

    async def step(self, n: int = 1, mode: str | None = None) -> dict[str, Any]:
        params: dict[str, Any] = {"n": n}
        if mode:
            params["mode"] = mode
        return await self._call("POST", "/v1/step", params)

    async def reset(self, hard: bool = True) -> dict[str, Any]:
        return await self._call("POST", "/v1/reset", {"hard": 1 if hard else 0})

    # Inspection
    async def cpu(self) -> dict[str, Any]:
        return await self._call("GET", "/v1/cpu")

    async def mem_read(self, addr: str | int, length: int) -> dict[str, Any]:
        return await self._call("GET", "/v1/mem", {"addr": addr, "len": length})

    async def disasm(self, addr: str | int = "pc", count: int = 16,
                     annotate: bool = True, library: str | None = None) -> dict[str, Any]:
        params: dict[str, Any] = {"addr": addr, "count": count, "annotate": 1 if annotate else 0}
        if library:
            params["library"] = library
        return await self._call("GET", "/v1/disasm", params)

    async def custom(self) -> dict[str, Any]:
        return await self._call("GET", "/v1/custom")

    async def memmap(self) -> dict[str, Any]:
        return await self._call("GET", "/v1/memmap")

    async def stack(self, depth: int = 32) -> dict[str, Any]:
        return await self._call("GET", "/v1/stack", {"depth": depth})

    # Mutation
    async def mem_write(self, addr: str | int, hex_bytes: str) -> dict[str, Any]:
        return await self._call("POST", "/v1/mem", {"addr": addr, "hex": hex_bytes})

    async def cpu_write(self, reg: str, value: str | int) -> dict[str, Any]:
        return await self._call("POST", "/v1/cpu", {"reg": reg, "value": value})

    # Breakpoints
    async def bp_add(self, addr: str | int, skip: int = 0, oneshot: bool = False) -> dict[str, Any]:
        return await self._call("POST", "/v1/breakpoints",
                                {"addr": addr, "skip": skip, "oneshot": 1 if oneshot else 0})

    async def bp_list(self) -> dict[str, Any]:
        return await self._call("GET", "/v1/breakpoints")

    async def bp_clear(self) -> dict[str, Any]:
        return await self._call("POST", "/v1/breakpoints/clear")

    # Watchpoints
    async def wp_add(self, addr: str | int, size: int = 1, rwi: str = "RW",
                     mustchange: bool = False, val: str | int | None = None,
                     valmask: str | int | None = None) -> dict[str, Any]:
        params: dict[str, Any] = {"addr": addr, "size": size, "rwi": rwi,
                                  "mustchange": 1 if mustchange else 0}
        if val is not None:
            params["val"] = val
        if valmask is not None:
            params["valmask"] = valmask
        return await self._call("POST", "/v1/watchpoints", params)

    async def wp_list(self) -> dict[str, Any]:
        return await self._call("GET", "/v1/watchpoints")

    async def wp_last(self) -> dict[str, Any]:
        return await self._call("GET", "/v1/watchpoints/last")

    async def wp_clear(self) -> dict[str, Any]:
        return await self._call("POST", "/v1/watchpoints/clear")

    async def wp_rearm(self) -> dict[str, Any]:
        return await self._call("POST", "/v1/watchpoints/rearm")

    # State snapshots
    async def state_save(self, path: str) -> dict[str, Any]:
        return await self._call("POST", "/v1/state/save", {"path": path})

    async def state_load(self, path: str) -> dict[str, Any]:
        return await self._call("POST", "/v1/state/load", {"path": path})

    # Symbols / FD
    async def symbols(self) -> dict[str, Any]:
        return await self._call("GET", "/v1/symbols")

    async def symbol_lookup(self, addr: str | int) -> dict[str, Any]:
        return await self._call("GET", "/v1/symbols/lookup", {"addr": addr})

    async def fd_libraries(self) -> dict[str, Any]:
        return await self._call("GET", "/v1/fd/libraries")

    async def fd_list(self, library: str = "exec") -> dict[str, Any]:
        return await self._call("GET", "/v1/fd/list", {"library": library})

    async def fd_load(self, path: str, library: str) -> dict[str, Any]:
        return await self._call("POST", "/v1/fd/load", {"path": path, "library": library})

    async def fd_lookup(self, offset: int, library: str = "exec") -> dict[str, Any]:
        return await self._call("GET", "/v1/fd/lookup", {"offset": offset, "library": library})
