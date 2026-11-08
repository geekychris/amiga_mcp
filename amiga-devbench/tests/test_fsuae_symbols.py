# SPDX-License-Identifier: MIT
"""Symbolic FS-UAE breakpoints go where AmigaOS loaded the program: the
guest's process list is walked through memory reads."""
import asyncio

import pytest

from amiga_devbench import amiga_processes as ap
from amiga_devbench import winuae_program
from amiga_devbench.fsuae_symbols import symbol_breakpoint
from amiga_devbench.winuae_files import parse_hunk_file
from tests.test_winuae_program import FIXTURE, program_bytes, static_offset, symbol

BASES = [0x2000, 0x3000, 0x3100, 0x3200]   # as in test_winuae_program
EXEC = 0x1000
HUNKS = parse_hunk_file(program_bytes()).hunks


class Memory:
    """A guest with exec, a shell and a CLI process running the fixture."""

    def __init__(self, command='DH2:Dev/winuae_program', copies=1):
        self.mem = bytearray(0x10000)
        self.put(4, EXEC)
        self.put(EXEC + ap.CHK_BASE, ~EXEC & 0xFFFFFFFF)
        self.put(EXEC + ap.THIS_TASK, 0)
        tasks = [self.shell(0x5000)] + [self.cli(0x6000 + 0x400 * n, command) for n in range(copies)]
        self.list(EXEC + ap.TASK_READY, [])
        self.list(EXEC + ap.TASK_WAIT, tasks)
        # Each segment: allocation size, next BPTR, then the hunk's data.
        for n, (base, hunk) in enumerate(zip(BASES, HUNKS)):
            following = BASES[n + 1] - 4 if n + 1 < len(BASES) else 0
            self.put(base - 8, hunk.size + 8)
            self.put(base - 4, following >> 2)

    def put(self, address, value):
        self.mem[address:address + 4] = value.to_bytes(4, 'big')

    def list(self, head, nodes):
        # lh_Head, lh_Tail (0), lh_TailPred; the last node points at &lh_Tail
        self.put(head, nodes[0] if nodes else head + 4)
        for node, successor in zip(nodes, nodes[1:] + [head + 4]):
            self.put(node, successor)

    def task(self, address, name):
        self.mem[address + ap.LN_TYPE] = ap.NT_PROCESS
        self.put(address + ap.LN_NAME, address + 0x200)
        self.mem[address + 0x200:address + 0x200 + len(name) + 1] = name.encode() + b'\0'

    def shell(self, address):
        self.task(address, 'Shell Process')
        cli = address + 0x100
        self.put(address + ap.PR_CLI, cli >> 2)
        self.put(cli + ap.CLI_MODULE, 0)
        return address

    def cli(self, address, command):
        self.task(address, 'Background CLI')
        cli, name = address + 0x100, address + 0x300
        self.put(address + ap.PR_CLI, cli >> 2)
        self.put(cli + ap.CLI_MODULE, (BASES[0] - 4) >> 2)
        self.put(cli + ap.CLI_COMMAND_NAME, name >> 2)
        self.mem[name] = len(command)
        self.mem[name + 1:name + 1 + len(command)] = command.encode()
        return address


class Rpc:
    def __init__(self, memory):
        self.memory, self.breakpoints = memory, []

    async def mem_read(self, addr, length):
        a = int(addr, 16)
        return {'ok': True, 'hex': self.memory.mem[a:a + length].hex()}

    async def bp_add(self, addr, skip=0, oneshot=False):
        self.breakpoints.append(int(addr, 16))
        return {'ok': True, 'id': len(self.breakpoints)}


@pytest.fixture
def fixture_project(monkeypatch):
    monkeypatch.setattr(winuae_program, 'project_binary', lambda project: str(FIXTURE))


def _read(memory):
    async def read(address, length):
        return bytes(memory.mem[address:address + length])
    return read


def test_finds_the_cli_process_and_its_segments():
    found = asyncio.run(ap.running(_read(Memory()), 'WINUAE_PROGRAM'))   # names compare without case
    assert [p.address for p in found] == [0x6000]
    assert [(s['address'], s['size']) for s in found[0].segments] == [
        (b, h.size + 8) for b, h in zip(BASES, HUNKS)]


def test_breakpoint_lands_at_the_loaded_address(fixture_project):
    rpc = Rpc(Memory())
    body = asyncio.run(symbol_breakpoint(rpc, 'walk', 'tests/winuae_program'))
    assert body['ok'], body
    assert rpc.breakpoints == [symbol('_walk')]          # BASES[0] + offset, not the offset
    assert body['address'] == f"0x{symbol('_walk'):08x}" and body['process'] == '0x00006000'


def test_static_function_through_dwarf(fixture_project):
    pytest.importorskip('elftools')
    rpc = Rpc(Memory())
    body = asyncio.run(symbol_breakpoint(rpc, 'scale', 'tests/winuae_program'))
    assert body['ok'], body
    assert rpc.breakpoints == [BASES[0] + static_offset('scale')]


def test_program_not_running(fixture_project):
    rpc = Rpc(Memory(command='C:Dir'))
    body = asyncio.run(symbol_breakpoint(rpc, 'walk', 'tests/winuae_program'))
    assert not body['ok'] and 'not running' in body['err'] and not rpc.breakpoints


def test_two_copies_are_ambiguous(fixture_project):
    rpc = Rpc(Memory(copies=2))
    body = asyncio.run(symbol_breakpoint(rpc, 'walk', 'tests/winuae_program'))
    assert not body['ok'] and '2 copies' in body['err'] and not rpc.breakpoints


def test_no_amigaos(fixture_project):
    memory = Memory()
    memory.put(4, 0)
    body = asyncio.run(symbol_breakpoint(Rpc(memory), 'walk', 'tests/winuae_program'))
    assert not body['ok'] and 'exec.library' in body['err']
