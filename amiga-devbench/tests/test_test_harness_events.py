# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

"""The bridge test harness streams TEST_* lines inside CLOG messages.

Regression: serial_conn.py called protocol.parse_message without importing
`protocol`, so the first TEST_BEGIN raised NameError inside the TCP read loop,
devbench dropped the connection, and amiga_run_tests never saw a result.
"""
import asyncio

from amiga_devbench.serial_conn import SerialConnection
from amiga_devbench.state import AmigaState, EventBus


def test_clog_test_lines_publish_test_events():
    async def run():
        bus = EventBus()
        conn = SerialConnection(AmigaState(), bus)
        got = []
        async with bus.subscribe("test") as q:
            conn._handle_data(
                "CLOG|game_test|I|0|TEST_BEGIN|suite\n"
                "CLOG|game_test|I|0|TEST_PASS|maths works|t.c|12\n"
                "CLOG|game_test|I|0|TEST_FAIL|physics works|t.c|40\n"
                "CLOG|game_test|I|0|TEST_END|1|1|2\n")
            while not q.empty():
                got.append(q.get_nowait()[1])
        return got

    got = asyncio.run(run())
    assert [g["type"] for g in got] == ["TEST_BEGIN", "TEST_PASS", "TEST_FAIL", "TEST_END"]
    assert got[0]["suite"] == "suite"
    assert all(g["client"] == "game_test" for g in got)
