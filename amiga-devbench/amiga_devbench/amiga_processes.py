# SPDX-License-Identifier: MIT
"""The DOS processes of a running AmigaOS 3.x guest and where their
programs are loaded, found by walking exec's task lists through any
emulator's memory reads (an emulator without a segment report of its own,
such as FS-UAE's remote debugger).

The guest keeps running while the lists are read, so a list may change
under the walk; every pointer is checked and every walk is bounded, and a
process caught half-way is simply missed.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Awaitable, Callable

Read = Callable[[int, int], Awaitable[bytes]]

# exec.library
EXEC_BASE = 4
CHK_BASE = 0x26                 # ~ExecBase
THIS_TASK = 0x114
TASK_READY = 0x196              # struct List
TASK_WAIT = 0x1A4
# struct Node / Task / Process / CommandLineInterface
LN_TYPE, LN_NAME = 8, 10
NT_PROCESS = 13
PR_SEGLIST = 0x80               # BPTR to the seglist array of a process
PR_CLI = 0xAC                   # BPTR to its CommandLineInterface
CLI_COMMAND_NAME = 0x10         # BSTR
CLI_MODULE = 0x3C               # BPTR to the running command's seglist
SEG_ARRAY_PROGRAM = 3           # seglist array entry of a Workbench program

MAX_TASKS = 512
MAX_SEGMENTS = 256


class GuestError(Exception):
    """The guest's exec structures cannot be read."""


@dataclass
class Process:
    address: int
    task_name: str
    command: str                # the CLI command name, '' for a Workbench program
    segments: list[dict] = field(default_factory=list)

    @property
    def program(self) -> str:
        """The name of the program it runs, without its path."""
        name = self.command or self.task_name
        return name.replace(':', '/').rsplit('/', 1)[-1]


def _pointer(value: int) -> bool:
    return 0x400 <= value < 0xFF000000 and value % 2 == 0


class Guest:
    def __init__(self, read: Read):
        self._read = read

    async def long(self, address: int) -> int:
        return int.from_bytes(await self._read(address, 4), 'big')

    async def string(self, address: int, limit: int = 64) -> str:
        data = await self._read(address, limit)
        return data.split(b'\0', 1)[0].decode('latin-1')

    async def bstring(self, bptr: int) -> str:
        if not bptr:
            return ''
        data = await self._read(bptr << 2, 256)
        return data[1:1 + data[0]].decode('latin-1')

    async def exec_base(self) -> int:
        base = await self.long(EXEC_BASE)
        if not _pointer(base) or (base + await self.long(base + CHK_BASE)) & 0xFFFFFFFF != 0xFFFFFFFF:
            raise GuestError('The guest has no valid exec.library at address 4; is AmigaOS running?')
        return base

    async def tasks(self) -> list[int]:
        base = await self.exec_base()
        found = [await self.long(base + THIS_TASK)]
        for head in (base + TASK_READY, base + TASK_WAIT):
            node = await self.long(head)
            while _pointer(node) and len(found) < MAX_TASKS:
                successor = await self.long(node)
                if not successor:          # the list's tail sentinel
                    break
                found.append(node)
                node = successor
        return [t for t in dict.fromkeys(found) if _pointer(t)]

    async def seglist(self, bptr: int) -> list[dict]:
        """The segments of a seglist: the address and size of each one's
        data. As in WinUAE's report, the size leaves out the 8 bytes in front
        of the data (the allocation's size and the next segment's BPTR)."""
        segments = []
        while bptr and len(segments) < MAX_SEGMENTS:
            segment = bptr << 2
            if not _pointer(segment):
                break
            size = await self.long(segment - 4)
            if size < 8:
                raise GuestError(f'The segment at 0x{segment:08x} has a short header')
            segments.append(dict(index=len(segments), address=segment + 4, size=size - 8))
            bptr = await self.long(segment)
        return segments

    async def process(self, task: int) -> Process | None:
        header = await self._read(task, 14)
        if header[LN_TYPE] != NT_PROCESS:
            return None
        name_at = int.from_bytes(header[LN_NAME:LN_NAME + 4], 'big')
        name = await self.string(name_at) if _pointer(name_at) else ''
        cli = await self.long(task + PR_CLI)
        if cli and _pointer(cli << 2):
            module = await self.long((cli << 2) + CLI_MODULE)
            if not module:                 # a shell between commands
                return Process(task, name, '')
            command = await self.bstring(await self.long((cli << 2) + CLI_COMMAND_NAME))
            return Process(task, name, command, await self.seglist(module))
        array = await self.long(task + PR_SEGLIST) << 2
        if not _pointer(array) or await self.long(array) < SEG_ARRAY_PROGRAM:
            return Process(task, name, '')
        return Process(task, name, '', await self.seglist(await self.long(array + 4 * SEG_ARRAY_PROGRAM)))

    async def processes(self) -> list[Process]:
        found = []
        for task in await self.tasks():
            try:
                process = await self.process(task)
            except (ValueError, IndexError):
                continue
            if process is not None:
                found.append(process)
        return found


async def running(read: Read, program: str) -> list[Process]:
    """The processes running a program of that name (AmigaDOS compares names
    without regard to case)."""
    wanted = program.lower()
    return [p for p in await Guest(read).processes() if p.segments and p.program.lower() == wanted]
