# SPDX-License-Identifier: MIT
"""MCP entry points for the shared WinUAE native debugger session."""
from typing import Literal
from .winuae_debug import WinuaeDebugger, DebugError

_debugger: WinuaeDebugger | None = None


def init_winuae_tools(debugger: WinuaeDebugger) -> None:
    global _debugger
    _debugger = debugger


async def call(action: str, **args):
    if _debugger is None:
        raise DebugError('WinUAE debugger is not initialized')
    return await _debugger.call(action, **args)


def register_winuae_tools(mcp):
    @mcp.tool()
    async def amiga_winuae_control(
        action: Literal['status', 'connect', 'disconnect', 'pause', 'resume', 'step', 'step_over', 'wait'] = 'status',
        timeout: float = 10,
    ) -> dict:
        """Control WinUAE's native debugger, independently of the guest bridge.

        Connect pauses the CPU (WinUAE GDB attach behavior); status never attaches.
        Pause before inspection or mutation if execution has resumed.
        Step and step_over start execution; wait observes its next stop without
        interrupting on timeout. Disconnect releases points and resumes the guest.
        The endpoint is configured in [winuae_debug]; do not share its GDB port
        with another debugger. The FS-UAE and guest debugger tools are separate.
        """
        return await call(action, timeout=timeout)

    @mcp.tool()
    async def amiga_winuae_registers(register: str | None = None, value: str | None = None) -> dict:
        """Read m68k registers, or write one (D0-D7, A0-A7, SR, PC). Requires pause.
        Values accept decimal, 0x-prefixed hex or $-prefixed hex.
        """
        if register is None and value is not None:
            raise DebugError('Supply a register with the value')
        return await call('registers', **({} if register is None else dict(register=register, value=value)))

    @mcp.tool()
    async def amiga_winuae_read_memory(address: str, size: int = 256) -> dict:
        """Read 1..65536 bytes of physical memory while paused. Addresses accept 0x/$ hex or decimal."""
        return await call('read_memory', address=address, size=size)

    @mcp.tool()
    async def amiga_winuae_write_memory(address: str, hex_data: str) -> dict:
        """Write 1..4096 bytes to physical RAM while paused. Rejects ROM, I/O and mixed ranges."""
        return await call('write_memory', address=address, hex_data=hex_data)

    @mcp.tool()
    async def amiga_winuae_disassemble(address: str = 'pc', count: int = 20) -> dict:
        """Use WinUAE's native disassembler for 1..100 instructions while paused."""
        return await call('disassemble', address=address, count=count)

    @mcp.tool()
    async def amiga_winuae_breakpoint(
        operation: Literal['list', 'add', 'remove'] = 'list', address: str = '0',
        kind: Literal['execute', 'read', 'write', 'access'] = 'execute', size: int = 1,
    ) -> list:
        """Manage this session's native breakpoints/watchpoints while paused.
        Execute breakpoints require an even address; size applies to watchpoints.
        Points belong to this connection and are released when it disconnects.
        """
        return await call('breakpoint', operation=operation, address=address, kind=kind, size=size)

    @mcp.tool()
    async def amiga_winuae_inspect(
        subject: Literal['tasks', 'segments', 'exception', 'memory-map'] = 'exception', process: str = '0',
    ) -> dict | list:
        """Inspect native Amiga tasks, loaded segments, last exception or memory map while paused.
        process is the Exec Process address for segments; 0 selects the current process.
        No running guest bridge is required, making this useful after a guest crash.
        """
        return await call('inspect', subject=subject, process=process)

    @mcp.tool()
    async def amiga_winuae_process_break(
        operation: Literal['status', 'name', 'address', 'clear'] = 'status', name: str = '', address: str = '0',
    ) -> dict:
        """Arm a one-shot program-entry stop by DOS command name or Exec Process address.
        Requires pause. Resume separately, then launch the program in the guest.
        """
        result = await call('process_break', operation=operation, name=name, address=address)
        return {'result': result}

    @mcp.tool()
    async def amiga_winuae_exception_stops(vectors: list[int]) -> dict:
        """Replace native exception-stop mask while paused. Vectors are 2..63; [] disables.
        For example [2,3,4,5,8,10,11] covers common faults. Does not patch guest vectors.
        """
        return await call('exception_mask', vectors=vectors)
