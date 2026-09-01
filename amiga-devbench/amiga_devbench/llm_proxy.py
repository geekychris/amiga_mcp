# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins

"""LLM proxy — bridges Amiga-side ``ask`` calls to a remote LLM.

Listens for CLOG lines of the form ``LLM_ASK|<session>|<prompt>`` and:

1. Streams a completion from a remote inference server (Ollama-compatible
   by default; the shape is easy to swap for OpenAI-style if we need).
2. Pushes each token delta into the running ``amiterm`` client on the Amiga
   via ``CALLHOOK amiterm llm_event TOKEN|<delta>``.
3. Signals end-of-turn with ``CALLHOOK amiterm llm_event DONE|<status>``.

Tool calling extends the same pattern: when the model asks to run a shell
command we emit ``TOOL|<id>|<name>|<cmdline>``. The hook return value from
the Amiga side becomes the tool-result string we feed back to the model.

The proxy is testable without a live server: ``LLMBackend`` is an abstract
base, and unit tests supply a scripted stub instead of ``OllamaBackend``.
"""

from __future__ import annotations

import asyncio
import json
import logging
import time
from dataclasses import dataclass, field
from typing import Any, AsyncIterator, Awaitable, Callable, Optional

logger = logging.getLogger(__name__)


# ─── Backend interface + Ollama implementation ────────────────────────────

@dataclass
class LLMDelta:
    """One decoded token/chunk from the model.

    ``content`` is the text delta (may be ``""`` on control frames).
    ``tool_call`` is set when the model wants to invoke a tool — the proxy
    turns it into a ``TOOL|`` event.
    """
    content: str = ""
    tool_call: Optional[dict[str, Any]] = None
    done: bool = False


class LLMBackend:
    """Streaming inference client. Subclass + implement ``chat_stream``."""

    async def chat_stream(
        self,
        messages: list[dict[str, Any]],
        tools: Optional[list[dict[str, Any]]] = None,
    ) -> AsyncIterator[LLMDelta]:
        raise NotImplementedError
        # yield  # so the type checker recognises this as async gen


class OllamaBackend(LLMBackend):
    """Talks to an Ollama server's ``/api/chat`` endpoint.

    Ollama streams NDJSON (one JSON object per newline) rather than SSE,
    which is what "the protocol things like ollama use" refers to. Each
    frame looks like::

        {"model": "...", "created_at": "...",
         "message": {"role": "assistant", "content": "hi"},
         "done": false}

    When ``done`` is true the stream ends. Tool calls arrive as
    ``message.tool_calls`` on a done frame.
    """

    def __init__(self, host: str, model: str, port: int = 11434,
                 scheme: str = "http", timeout: float = 120.0) -> None:
        self.host = host
        self.model = model
        self.port = port
        self.scheme = scheme
        self.timeout = timeout

    @property
    def url(self) -> str:
        return f"{self.scheme}://{self.host}:{self.port}/api/chat"

    async def chat_stream(
        self,
        messages: list[dict[str, Any]],
        tools: Optional[list[dict[str, Any]]] = None,
    ) -> AsyncIterator[LLMDelta]:
        try:
            import httpx  # lazy — avoids the import if the proxy is disabled
        except ImportError as e:
            raise RuntimeError(
                "httpx is required for OllamaBackend "
                "(pip install httpx)"
            ) from e

        payload: dict[str, Any] = {
            "model": self.model,
            "messages": messages,
            "stream": True,
        }
        if tools:
            payload["tools"] = tools

        # Overall budget is self.timeout, connect 15s, but no per-read cap —
        # the first token on a large model can be tens of seconds and any
        # per-read limit fires spuriously.
        timeout = httpx.Timeout(self.timeout, connect=15.0, read=None)
        async with httpx.AsyncClient(timeout=timeout) as client:
            async with client.stream("POST", self.url, json=payload) as resp:
                if resp.status_code >= 400:
                    body = await resp.aread()
                    raise RuntimeError(
                        f"Ollama returned {resp.status_code}: {body[:400]!r}"
                    )
                async for line in resp.aiter_lines():
                    if not line.strip():
                        continue
                    try:
                        frame = json.loads(line)
                    except json.JSONDecodeError:
                        logger.warning("bad NDJSON from ollama: %r", line[:120])
                        continue
                    msg = frame.get("message") or {}
                    delta = LLMDelta()
                    delta.content = msg.get("content", "") or ""
                    tcs = msg.get("tool_calls") or []
                    if tcs:
                        # Ollama emits one call per frame typically; use first.
                        tc = tcs[0]
                        fn = tc.get("function") or {}
                        delta.tool_call = {
                            "name": fn.get("name", ""),
                            "arguments": fn.get("arguments", {}),
                        }
                    if frame.get("done"):
                        delta.done = True
                    yield delta
                    if delta.done:
                        return


# ─── Proxy: CLOG-listener + hook-caller loop ─────────────────────────────

# Callable the proxy uses to send CALLHOOK to the Amiga side. Injected so
# tests can supply a stub. Returns the tool-result string (or "" for
# TOKEN/DONE events which don't need a meaningful reply).
CallHookFn = Callable[[str, str, str], Awaitable[str]]


@dataclass
class LLMProxyConfig:
    """Wiring for the proxy — read from devbench.toml under [llm]."""
    enabled: bool = False
    host: str = "spark.hitorro.com"
    port: int = 11434
    scheme: str = "http"
    model: str = "nemotron"
    system_prompt: str = (
        "You are an assistant embedded in an Amiga terminal. Prefer short, "
        "concrete answers. When you need to inspect the machine, use the "
        "run_command tool with an AmigaDOS command; do not fabricate output."
    )
    # Amiga-side hook to invoke: CALLHOOK amiterm llm_event <args>
    client_name: str = "amiterm"
    hook_name: str = "llm_event"


# Tool schema shared with the model — currently one tool: run_command.
# Ollama accepts the OpenAI tool-schema shape.
def default_tools() -> list[dict[str, Any]]:
    return [
        {
            "type": "function",
            "function": {
                "name": "run_command",
                "description": (
                    "Run an AmigaDOS command line in the amiterm shell "
                    "and return its exit code and stdout."
                ),
                "parameters": {
                    "type": "object",
                    "properties": {
                        "command": {
                            "type": "string",
                            "description": "The command line to run.",
                        }
                    },
                    "required": ["command"],
                },
            },
        }
    ]


class LLMProxy:
    """Drives one turn of the LLM loop, driven by an ``LLM_ASK`` CLOG."""

    def __init__(
        self,
        backend: LLMBackend,
        cfg: LLMProxyConfig,
        call_hook: CallHookFn,
    ) -> None:
        self._backend = backend
        self._cfg = cfg
        self._call_hook = call_hook
        # {session_id: [messages...]} — the model needs the running history.
        self._conversations: dict[int, list[dict[str, Any]]] = {}

    def _get_messages(self, session: int, user_msg: str) -> list[dict[str, Any]]:
        conv = self._conversations.get(session)
        if conv is None:
            conv = [{"role": "system", "content": self._cfg.system_prompt}]
            self._conversations[session] = conv
        conv.append({"role": "user", "content": user_msg})
        return conv

    async def _send_token(self, delta: str) -> None:
        # amiterm's hook takes TOKEN|<text> — swap literal | for \| so we
        # don't accidentally split the payload. amiterm treats "TOKEN|" as
        # the prefix; anything after is content and is written verbatim.
        safe = delta.replace("|", "/")
        await self._call_hook(
            self._cfg.client_name, self._cfg.hook_name, f"TOKEN|{safe}"
        )

    async def _send_done(self, status: str) -> None:
        await self._call_hook(
            self._cfg.client_name, self._cfg.hook_name, f"DONE|{status}"
        )

    async def _send_tool(self, tool_id: int, name: str, cmdline: str) -> str:
        safe_cmd = cmdline.replace("|", "/")
        return await self._call_hook(
            self._cfg.client_name,
            self._cfg.hook_name,
            f"TOOL|{tool_id}|{name}|{safe_cmd}",
        )

    async def run_turn(self, session: int, user_msg: str) -> None:
        """One user_msg → possibly multi-step LLM interaction → DONE."""
        messages = self._get_messages(session, user_msg)
        tools = default_tools()
        MAX_STEPS = 6
        step = 0
        try:
            while step < MAX_STEPS:
                step += 1
                accumulated = ""
                tool_call: Optional[dict[str, Any]] = None
                async for delta in self._backend.chat_stream(messages, tools):
                    if delta.content:
                        accumulated += delta.content
                        await self._send_token(delta.content)
                    if delta.tool_call:
                        tool_call = delta.tool_call
                    if delta.done:
                        break
                # Persist what the assistant said this step.
                assistant_msg: dict[str, Any] = {
                    "role": "assistant",
                    "content": accumulated,
                }
                messages.append(assistant_msg)
                if not tool_call:
                    await self._send_done("ok")
                    return
                # Execute the tool call on the Amiga.
                name = tool_call.get("name", "")
                args = tool_call.get("arguments", {})
                if isinstance(args, str):
                    try:
                        args = json.loads(args)
                    except json.JSONDecodeError:
                        args = {}
                cmdline = args.get("command", "") if name == "run_command" else ""
                if not cmdline:
                    result = f"error: unknown tool {name!r}"
                else:
                    result = await self._send_tool(step, name, cmdline)
                messages.append({
                    "role": "tool",
                    "name": name,
                    "content": result,
                })
                # Loop: give the tool result back to the model.
            await self._send_done("step_budget_exceeded")
        except asyncio.CancelledError:
            await self._send_done("cancelled")
            raise
        except Exception as e:  # noqa: BLE001 — surface any backend error
            logger.exception("LLM turn failed")
            try:
                await self._send_done(f"error:{e}")
            except Exception:
                pass


# ─── Config loader ────────────────────────────────────────────────────────

def config_from_toml(data: dict[str, Any]) -> LLMProxyConfig:
    """Build an ``LLMProxyConfig`` from a devbench.toml ``[llm]`` block."""
    section = data.get("llm", {}) if isinstance(data, dict) else {}
    cfg = LLMProxyConfig()
    if "enabled" in section:      cfg.enabled = bool(section["enabled"])
    if "host" in section:         cfg.host = str(section["host"])
    if "port" in section:         cfg.port = int(section["port"])
    if "scheme" in section:       cfg.scheme = str(section["scheme"])
    if "model" in section:        cfg.model = str(section["model"])
    if "system_prompt" in section: cfg.system_prompt = str(section["system_prompt"])
    if "client_name" in section:  cfg.client_name = str(section["client_name"])
    if "hook_name" in section:    cfg.hook_name = str(section["hook_name"])
    return cfg


# ─── Parsing helpers used by the CLOG listener ────────────────────────────

_ASK_PREFIX = "LLM_ASK|"


def parse_ask(message: str) -> Optional[tuple[int, str]]:
    """Return ``(session, prompt)`` if ``message`` is a well-formed LLM_ASK,
    else ``None``. The client-name check happens at the caller."""
    if not message.startswith(_ASK_PREFIX):
        return None
    rest = message[len(_ASK_PREFIX):]
    sep = rest.find("|")
    if sep < 0:
        return None
    try:
        session = int(rest[:sep])
    except ValueError:
        return None
    return session, rest[sep + 1:]
