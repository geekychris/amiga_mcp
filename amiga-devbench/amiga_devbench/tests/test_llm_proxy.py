"""Deterministic tests for :mod:`amiga_devbench.llm_proxy`.

We stub the LLM backend so a fixed sequence of deltas drives the proxy —
no network, no emulator. The Amiga side is stubbed too: ``FakeHook``
records every CALLHOOK we would have issued and replays scripted results
for tool calls. That lets us assert the exact TOKEN/TOOL/DONE stream the
Amiga would receive.

Run with: ``pytest amiga-devbench/amiga_devbench/tests/test_llm_proxy.py``
or ``python -m unittest amiga_devbench.tests.test_llm_proxy``.
"""

from __future__ import annotations

import asyncio
import unittest
from typing import Any, AsyncIterator, Optional

from amiga_devbench.llm_proxy import (
    LLMBackend,
    LLMDelta,
    LLMProxy,
    LLMProxyConfig,
    config_from_toml,
    parse_ask,
)


class ScriptedBackend(LLMBackend):
    """Emit a scripted sequence of deltas across successive ``chat_stream``
    invocations. Each call consumes one script entry."""

    def __init__(self, scripts: list[list[LLMDelta]]) -> None:
        self._scripts = list(scripts)
        self.received: list[list[dict[str, Any]]] = []

    async def chat_stream(
        self,
        messages: list[dict[str, Any]],
        tools: Optional[list[dict[str, Any]]] = None,
    ) -> AsyncIterator[LLMDelta]:
        self.received.append([dict(m) for m in messages])
        if not self._scripts:
            raise AssertionError("backend called more times than scripted")
        script = self._scripts.pop(0)
        for d in script:
            yield d


class FakeHook:
    """Captures CALLHOOK invocations. For TOOL|… calls, returns whatever
    the test queued up so the proxy can feed the "tool result" back."""

    def __init__(self, tool_results: Optional[list[str]] = None) -> None:
        self.calls: list[tuple[str, str, str]] = []
        self._tool_results = list(tool_results or [])

    async def __call__(self, client: str, hook: str, args: str) -> str:
        self.calls.append((client, hook, args))
        if args.startswith("TOOL|"):
            if not self._tool_results:
                return "exit=127\ntest didn't queue a tool result"
            return self._tool_results.pop(0)
        return ""


# ─── tests ───────────────────────────────────────────────────────────────


class ParseAskTests(unittest.TestCase):

    def test_ok(self):
        self.assertEqual(parse_ask("LLM_ASK|3|hello world"),
                         (3, "hello world"))

    def test_wrong_prefix(self):
        self.assertIsNone(parse_ask("LLM|3|x"))

    def test_bad_session(self):
        self.assertIsNone(parse_ask("LLM_ASK|abc|x"))

    def test_missing_prompt(self):
        self.assertIsNone(parse_ask("LLM_ASK|1"))

    def test_empty_prompt(self):
        # Empty prompt is legal — the model gets an empty user message.
        self.assertEqual(parse_ask("LLM_ASK|1|"), (1, ""))


class ProxyTokenStreamTests(unittest.TestCase):

    def test_plain_answer_streams_tokens_then_done(self):
        script = [
            LLMDelta(content="Hi"),
            LLMDelta(content=" there"),
            LLMDelta(content="!", done=True),
        ]
        backend = ScriptedBackend([script])
        hook = FakeHook()
        proxy = LLMProxy(backend, LLMProxyConfig(), hook)

        asyncio.run(proxy.run_turn(session=1, user_msg="hello"))

        payloads = [args for _, _, args in hook.calls]
        self.assertEqual(payloads, [
            "TOKEN|Hi",
            "TOKEN| there",
            "TOKEN|!",
            "DONE|ok",
        ])

    def test_pipe_in_token_is_escaped(self):
        script = [LLMDelta(content="use |grep", done=True)]
        backend = ScriptedBackend([script])
        hook = FakeHook()
        proxy = LLMProxy(backend, LLMProxyConfig(), hook)

        asyncio.run(proxy.run_turn(session=1, user_msg="hi"))

        self.assertEqual(hook.calls[0][2], "TOKEN|use /grep")


class ProxyToolLoopTests(unittest.TestCase):

    def test_tool_call_then_final_answer(self):
        # Turn 1: model says "let me check" then requests a run_command.
        turn1 = [
            LLMDelta(content="checking"),
            LLMDelta(
                content="",
                tool_call={"name": "run_command",
                           "arguments": {"command": "List DH2:Dev QUICK"}},
                done=True,
            ),
        ]
        # Turn 2: model reads the tool result and answers.
        turn2 = [
            LLMDelta(content="18 files."),
            LLMDelta(content="", done=True),
        ]
        backend = ScriptedBackend([turn1, turn2])
        hook = FakeHook(tool_results=["exit=0\nlauncher\nboing_ball\n"])
        proxy = LLMProxy(backend, LLMProxyConfig(), hook)

        asyncio.run(proxy.run_turn(session=1, user_msg="how many games?"))

        payloads = [args for _, _, args in hook.calls]
        self.assertEqual(payloads, [
            "TOKEN|checking",
            "TOOL|1|run_command|List DH2:Dev QUICK",
            "TOKEN|18 files.",
            "DONE|ok",
        ])

        # The proxy must have fed the tool result to the model on turn 2.
        self.assertEqual(len(backend.received), 2)
        second_call_msgs = backend.received[1]
        # Last message before turn 2 should be the tool result.
        self.assertEqual(second_call_msgs[-1]["role"], "tool")
        self.assertIn("launcher", second_call_msgs[-1]["content"])

    def test_step_budget_terminates_runaway_tool_loop(self):
        # Model always requests a tool call — proxy should give up after
        # MAX_STEPS iterations (6 in the current impl) with a status.
        def loop_delta():
            return [LLMDelta(
                content="",
                tool_call={"name": "run_command",
                           "arguments": {"command": "echo again"}},
                done=True,
            )]
        backend = ScriptedBackend([loop_delta() for _ in range(6)])
        hook = FakeHook(tool_results=["exit=0\n"] * 6)
        proxy = LLMProxy(backend, LLMProxyConfig(), hook)

        asyncio.run(proxy.run_turn(session=1, user_msg="loop"))

        done_calls = [c[2] for c in hook.calls if c[2].startswith("DONE|")]
        self.assertEqual(done_calls, ["DONE|step_budget_exceeded"])

    def test_arguments_may_arrive_as_json_string(self):
        # Some backends flatten arguments to a JSON string rather than an
        # object — parse it before extracting the command.
        turn = [LLMDelta(
            content="",
            tool_call={"name": "run_command",
                       "arguments": '{"command":"Version"}'},
            done=True,
        )]
        after = [LLMDelta(content="ok", done=True)]
        backend = ScriptedBackend([turn, after])
        hook = FakeHook(tool_results=["exit=0\nKickstart 40.68\n"])
        proxy = LLMProxy(backend, LLMProxyConfig(), hook)

        asyncio.run(proxy.run_turn(session=1, user_msg="version"))

        tool_calls = [c[2] for c in hook.calls if c[2].startswith("TOOL|")]
        self.assertEqual(tool_calls, ["TOOL|1|run_command|Version"])


class ConfigTests(unittest.TestCase):

    def test_defaults(self):
        cfg = config_from_toml({})
        self.assertFalse(cfg.enabled)
        self.assertEqual(cfg.host, "spark.hitorro.com")
        self.assertEqual(cfg.port, 11434)
        self.assertEqual(cfg.model, "nemotron")

    def test_overrides(self):
        cfg = config_from_toml({"llm": {
            "enabled": True, "model": "llama3", "port": 8080,
            "system_prompt": "test",
        }})
        self.assertTrue(cfg.enabled)
        self.assertEqual(cfg.model, "llama3")
        self.assertEqual(cfg.port, 8080)
        self.assertEqual(cfg.system_prompt, "test")


if __name__ == "__main__":
    unittest.main()
