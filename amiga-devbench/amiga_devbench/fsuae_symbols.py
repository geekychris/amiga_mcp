# SPDX-License-Identifier: MIT
"""Symbolic breakpoints for FS-UAE's remote debugger.

A symbol's address in the executable is an offset into its hunk; AmigaOS
puts each hunk wherever it finds memory when the program starts. The
breakpoint therefore goes to the hunk's load address, found by walking the
guest's process list, plus that offset.
"""
from __future__ import annotations

from pathlib import Path
from typing import Any

from .amiga_processes import GuestError, running
from .winuae_debug import DebugError
from .winuae_program import check_image, find_program, locate_symbol, match_segments


def reader(rpc: Any):
    async def read(address: int, length: int) -> bytes:
        reply = await rpc.mem_read(f'0x{address:08x}', length)
        if not reply.get('ok') or not isinstance(reply.get('hex'), str):
            raise GuestError(f"Cannot read guest memory at 0x{address:08x}: {reply.get('err', 'no data')}")
        return bytes.fromhex(reply['hex'])
    return read


async def symbol_address(rpc: Any, name: str, project: str | None = None) -> dict:
    """Where a function or symbol of a running program is, or why not."""
    if project is None:
        from .winuae_program import project_defining
        project, program = await project_defining(name)
    else:
        program = await find_program(name, project, None)
    hunk, offset = await locate_symbol(program, name)
    binary = Path(program.path).name
    read = reader(rpc)
    processes = await running(read, binary)
    if not processes:
        raise DebugError(f'{binary} is not running in the guest. Start it first: AmigaOS decides where '
                         'a program is loaded when it starts, and the breakpoint goes there')
    if len(processes) > 1:
        raise DebugError(f'{len(processes)} copies of {binary} are running (processes '
                         + ', '.join(f'0x{p.address:08x}' for p in processes) + '); stop all but one')
    process = processes[0]
    lay = match_segments(program, process.segments, process.address)
    await check_image(read, lay, hunk, offset)
    address = lay.address(hunk, offset)
    return dict(address=address, project=project, process=process.address, hunk=hunk, offset=offset)


async def symbol_breakpoint(rpc: Any, name: str, project: str | None = None,
                            skip: int = 0, oneshot: bool = False) -> dict:
    """Install an FS-UAE CPU breakpoint at a symbol of the running program."""
    try:
        where = await symbol_address(rpc, name, project)
    except (DebugError, GuestError) as exc:
        return {'ok': False, 'err': str(exc), 'symbol': name}
    body = await rpc.bp_add(f"0x{where['address']:08x}", skip=skip, oneshot=oneshot)
    body.update(symbol=name, resolved_project=where['project'], address=f"0x{where['address']:08x}",
                process=f"0x{where['process']:08x}", hunk=where['hunk'], offset=f"0x{where['offset']:x}")
    return body
