# SPDX-License-Identifier: MIT
"""MCP entry points for the shared WinUAE native debugger session."""
import base64
import json
from typing import Any, Literal

from mcp.server.fastmcp import Image

from .winuae_debug import WinuaeDebugger, DebugError

_debugger: WinuaeDebugger | None = None
DISK_IMAGES = ('.adf', '.adz', '.dms', '.ipf', '.scp')


def init_winuae_tools(debugger: WinuaeDebugger) -> None:
    global _debugger
    _debugger = debugger


async def call(action: str, **args):
    if _debugger is None:
        raise DebugError('WinUAE debugger is not initialized')
    return await _debugger.call(action, **{k: v for k, v in args.items() if v is not None})


def _with_image(result: dict, key: str = 'png_base64') -> list:
    """Return JSON metadata and, for MCP clients, the PNG itself."""
    data = result.pop(key, None)
    content: list[Any] = [json.dumps(result)]
    if data:
        content.append(Image(data=base64.b64decode(data), format='png'))
    return content


def register_winuae_tools(mcp):
    # ─── Session and execution ───

    @mcp.tool()
    async def amiga_winuae_control(
        action: Literal['status', 'connect', 'disconnect', 'pause', 'resume', 'step', 'step_over', 'step_out',
                        'wait', 'reset'] = 'status',
        timeout: float = 10, count: int = 1, hard: bool = False, resume: bool = True,
    ) -> dict:
        """Control WinUAE's native debugger, independently of the guest bridge.

        Connect pauses the CPU (WinUAE GDB attach behavior); status never attaches.
        Pause before inspection or mutation if execution has resumed.
        step runs count instructions (1..1000; one returns immediately, more
        wait and stop early at a watch, breakpoint or exception). step_over
        runs to the next instruction; step_out runs until the current
        subroutine returns, stepping over nested calls. wait observes the next
        stop without interrupting on timeout. reset is a keyboard reset
        (hard=true: hard reset); WinUAE detaches on reset, so DevBench
        reconnects, keeps this session's breakpoints and resumes unless
        resume=false. Disconnect releases points and resumes the guest.
        The endpoint is configured in [winuae_debug]; do not share its GDB port
        with another debugger. The FS-UAE and guest debugger tools are separate.
        """
        if action == 'step':
            return await call('step', count=count)
        if action == 'reset':
            return await call('reset', hard=hard, resume=resume)
        if action == 'wait':
            return await call('wait', timeout=timeout)
        return await call(action)

    @mcp.tool()
    async def amiga_winuae_range_step(start: str, end: str) -> dict:
        """Step while the PC stays in [start, end), then stop; returns at once.
        Use control(action="wait") or pause. Requires pause."""
        return await call('range_step', start=start, end=end)

    @mcp.tool()
    async def amiga_winuae_registers(register: str | None = None, value: str | None = None,
                                     stack_pointers: bool = True) -> dict:
        """Read m68k registers, or write one (D0-D7, A0-A7, SR, PC, USP, ISP, MSP). Requires pause.
        USP, ISP and MSP (68020-68040) are read and written by switching SR's stack bits
        briefly; SR itself is restored. Values accept decimal, 0x-prefixed hex or $-prefixed hex.
        """
        if register is None and value is not None:
            raise DebugError('Supply a register with the value')
        return await call('registers', register=register, value=value, stack_pointers=stack_pointers)

    @mcp.tool()
    async def amiga_winuae_read_memory(address: str, size: int = 256) -> dict:
        """Read 1..65536 bytes of physical memory while paused. Addresses accept 0x/$ hex or decimal.
        Custom chip registers ($DFF000-$DFF1FF) read WinUAE's saved custom state."""
        return await call('read_memory', address=address, size=size)

    @mcp.tool()
    async def amiga_winuae_write_memory(address: str, hex_data: str) -> dict:
        """Write 1..4096 bytes to physical RAM while paused. Rejects ROM, I/O and mixed ranges."""
        return await call('write_memory', address=address, hex_data=hex_data)

    @mcp.tool()
    async def amiga_winuae_disassemble(address: str = 'pc', count: int = 20, annotate: bool = True,
                                       library: str | None = None, source_xref: bool = False,
                                       project: str | None = None) -> dict:
        """Use WinUAE's native disassembler for 1..256 instructions while paused.
        annotate names library calls such as JSR -$228(A6) as exec.OpenLibrary(); at the PC the
        library comes from A6, otherwise pass library. source_xref adds file and line from symbols
        loaded with amiga_load_symbols (project selects one)."""
        return await call('disassemble', address=address, count=count, annotate=annotate, library=library,
                          source_xref=source_xref, project=project)

    # ─── Breakpoints and watches ───

    @mcp.tool()
    async def amiga_winuae_breakpoint(
        operation: Literal['list', 'add', 'remove', 'clear'] = 'list', address: str = '0',
        kind: Literal['execute', 'read', 'write', 'access'] = 'execute', size: int = 1,
        skip: int = 0, oneshot: bool = False, symbol: str | None = None, project: str | None = None,
    ) -> list:
        """Manage this session's native breakpoints/watchpoints while paused.
        Execute breakpoints require an even address; size applies to watchpoints.
        skip ignores the first N hits while resuming; oneshot removes the breakpoint after it
        stops. symbol sets an execute breakpoint at a function from amiga_load_symbols.
        Hit counts are listed. Points belong to this connection and are released when it
        disconnects; they survive checkpoint restores and resets.
        """
        return await call('breakpoint', operation=operation, address=address if symbol is None else None,
                          kind=kind, size=size, skip=skip, oneshot=oneshot, symbol=symbol, project=project)

    @mcp.tool()
    async def amiga_winuae_watch(
        operation: Literal['add', 'remove', 'clear', 'list', 'last', 'events', 'clear-events'] = 'list',
        id: int | None = None, address: str | None = None, size: int = 1, access: str = 'write',
        sources: list[str] | None = None, value: str | None = None, mask: str | None = None,
        change_only: bool = False, log_only: bool = False, exact: bool = False,
    ) -> dict:
        """Watch memory and keep a 64-entry log of the matching accesses. Requires pause.

        access: read, write, access, fetch, or R/W/I letters as in FS-UAE (RW, RWI, I).
        A range (size up to 65536) matches any overlapping access. exact=true matches one
        complete aligned 1, 2 or 4 byte access; chip RAM is 16 bits wide, so longword writes
        there are two word accesses. value/mask need size 1, 2 or 4. change_only fires only
        when a write changes RAM. log_only records without stopping. sources: cpu (default),
        blitter, copper, disk, audio, bitplane, sprite. last/events read the log, which also
        records breakpoint-tool and DMA watch hits. Fetches are seen when the CPU emulation
        fetches through memory banks, such as the compatible 68000.
        """
        return await call('watch', operation=operation, id=id, address=address, size=size, access=access,
                          sources=sources, value=value, mask=mask, change_only=change_only,
                          log_only=log_only, exact=exact)

    @mcp.tool()
    async def amiga_winuae_dma_watch(
        operation: Literal['add', 'remove', 'list'] = 'list', id: int | None = None,
        address: str | None = None, length: int = 1, mode: Literal['read', 'write', 'access'] = 'write',
        sources: list[str] | None = None,
    ) -> Any:
        """Watch a range for DMA accesses only: blitter, copper, disk, audio, bitplane, sprite
        (default: all). Stops report the source mask and custom register. Requires pause."""
        return await call('dma_watch', operation=operation, id=id, address=address, length=length,
                          mode=mode, sources=sources)

    @mcp.tool()
    async def amiga_winuae_condition(
        operation: Literal['add', 'remove', 'list'] = 'list', id: int | None = None,
        register: str | None = None, operator: Literal['eq', 'ne', 'le', 'ge', 'lt', 'gt'] = 'eq',
        value: str = '0', mask: str = '0xffffffff', signed: bool = False,
    ) -> Any:
        """Stop when a register comparison holds (D0-D7, A0-A7, USP, MSP, ISP, VBR, SR), checked
        at every instruction. Use breakpoints for PC. Requires pause."""
        return await call('condition', operation=operation, id=id, register=register, operator=operator,
                          value=value, mask=mask, signed=signed)

    @mcp.tool()
    async def amiga_winuae_history(operation: Literal['read', 'on', 'off'] = 'read', count: int = 32) -> Any:
        """Read the debugger's PC history (up to 128 entries), or turn recording on or off.
        Recording slows execution. Requires pause."""
        return await call('history', operation=operation, count=count)

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

    # ─── Inspection ───

    @mcp.tool()
    async def amiga_winuae_inspect(
        subject: Literal['tasks', 'segments', 'exception', 'memory-map', 'capabilities'] = 'exception',
        process: str = '0',
    ) -> dict | list:
        """Inspect native Amiga tasks, loaded segments, last exception, memory map or the
        target's capabilities while paused. process is the Exec Process address for segments;
        0 selects the current process. No running guest bridge is required, making this useful
        after a guest crash.
        """
        return await call('inspect', subject=subject, process=process)

    @mcp.tool()
    async def amiga_winuae_custom() -> dict:
        """Decode the custom chip registers: DMACON, INTENA/INTREQ, BPLCON0 and planes,
        bitplane, sprite, Copper and blitter pointers, display window, colors and the beam
        position. Includes write-only registers. Requires pause."""
        return await call('custom')

    @mcp.tool()
    async def amiga_winuae_copper(address: str = 'cop1lc', count: int = 64) -> dict:
        """Decode a Copper list (MOVE, WAIT, SKIP, END) at cop1lc, cop2lc or an address.
        Requires pause."""
        return await call('copper', address=address, count=count)

    @mcp.tool()
    async def amiga_winuae_stack(depth: int = 32) -> dict:
        """Read 1..1024 longwords from A7, tagging values that point into ROM or the current
        process's segments as code. Requires pause."""
        return await call('stack', depth=depth)

    @mcp.tool()
    async def amiga_winuae_lookup(address: str) -> dict:
        """Name a well-known Amiga address: custom chip and CIA registers, exception vectors
        and memory regions. Needs no connection."""
        return await call('lookup', address=address)

    @mcp.tool()
    async def amiga_winuae_fd(
        operation: Literal['libraries', 'lookup', 'list', 'load'] = 'libraries', offset: int | None = None,
        library: str = 'exec', path: str | None = None,
    ) -> Any:
        """Library vector tables used to annotate disassembly. lookup names an offset such as
        -552 (exec.OpenLibrary); load registers an .fd file under library. Built-in: exec,
        dos, intuition, graphics (from the AmigaOS 3.2 NDK). Needs no connection."""
        return await call('fd', operation=operation, offset=offset, library=library, path=path)

    @mcp.tool()
    async def amiga_winuae_memory_search(address: str, length: int, hex: str, max_matches: int = 256,
                                         alignment: int = 1) -> dict:
        """Find exact bytes in a range of up to 16 MiB, including overlapping matches.
        next_address continues after max_matches. Requires pause."""
        return await call('memory_search', address=address, length=length, hex=hex,
                          max_matches=max_matches, alignment=alignment)

    @mcp.tool()
    async def amiga_winuae_bitmap(width: int, height: int, planes: list[str], palette: list[str],
                                  row_stride: int | None = None) -> list:
        """Decode 1..8 bitplanes (addresses, least significant first) with 2^planes RGB colors
        to a PNG image. row_stride defaults to a word-aligned row. HAM, EHB and display-mode
        inference are not supported. Requires pause."""
        return _with_image(await call('bitmap', width=width, height=height, planes=planes,
                                      palette=palette, row_stride=row_stride))

    @mcp.tool()
    async def amiga_winuae_screenshot(path: str | None = None) -> list:
        """Save the emulated display as a PNG on the host (default: a temporary file) and
        return it. Works without the guest bridge. Requires pause."""
        return _with_image(await call('screenshot', path=path))

    # ─── Diagnosis ───

    @mcp.tool()
    async def amiga_winuae_capture(ranges: list[dict] | None = None) -> dict:
        """Capture registers, the stop reason and up to 16 memory ranges ({address, length},
        256 KiB in total) in one call. Requires pause."""
        return await call('capture', ranges=ranges)

    @mcp.tool()
    async def amiga_winuae_postmortem(file: str | None = None, mappings: list[dict] | None = None,
                                      max_frames: int = 32) -> dict:
        """A bounded crash report: fault-time and current registers, disassembly, stack bytes,
        loaded segments and guest output. The fault context is used only when it matches the
        current exception stop. With a linked ELF file and mappings, includes a DWARF
        backtrace. Requires pause."""
        return await call('postmortem', file=file, mappings=mappings, max_frames=max_frames)

    @mcp.tool()
    async def amiga_winuae_guest_output(operation: Literal['on', 'off', 'read', 'clear'] = 'read') -> dict:
        """Capture debug messages the guest sends through uaelib function 86 (64 records,
        4096 bytes). Not console or serial output. Requires pause."""
        return await call('guest_output', operation=operation)

    # ─── State ───

    @mcp.tool()
    async def amiga_winuae_checkpoint(operation: Literal['save', 'restore'], path: str) -> dict:
        """Save or restore a WinUAE state file at a host path, or a slot such as slot-1.
        WinUAE saves at the next frame start, so the CPU moves on by up to one frame, with this
        session's breakpoints lifted for that run; the result holds the saved registers.
        Restore keeps the connection and the CPU stopped and reinstalls this session's
        breakpoints, watches and exception stops. Requires pause."""
        return await call('checkpoint', operation=operation, path=path)

    @mcp.tool()
    async def amiga_winuae_snapshots(operation: Literal['list', 'save', 'load', 'label'] = 'list',
                                     slot: str = '', label: str = '') -> dict:
        """Quick-save slots slot-1..slot-9 under ~/.amiga-devbench/snapshots/winuae, with labels,
        plus the automatic ring (auto-N). save and load require pause."""
        return await call('snapshots', operation=operation, slot=slot, label=label)

    @mcp.tool()
    async def amiga_winuae_autosnap(interval: int | None = None, ring_size: int | None = None) -> dict:
        """Show or set the automatic snapshot ring: a save every interval seconds (0 = off) to
        ring_size rotating slots. Each save pauses the guest briefly."""
        return await call('autosnap', interval=interval, ring_size=ring_size)

    @mcp.tool()
    async def amiga_winuae_snapshot_diff(a: str, b: str) -> dict:
        """Compare two state files or slots chunk by chunk; RAM chunks report the differing
        byte regions by offset. Needs no connection."""
        return await call('snapshot_diff', a=a, b=b)

    # ─── Input ───

    @mcp.tool()
    async def amiga_winuae_input(
        event: Literal['key', 'mouse', 'button', 'joystick', 'status', 'release'] = 'status',
        code: int | None = None, port: int = 0, axis: Literal['x', 'y'] | None = None,
        delta: int | None = None, button: int | None = None,
        direction: Literal['left', 'right', 'up', 'down'] | None = None, pressed: bool | None = None,
    ) -> dict:
        """Send raw Amiga keys, mouse motion (-127..127 counts), buttons 0-2 or joystick
        directions on ports 0/1 through WinUAE's input handlers, without the guest bridge and
        without changing execution. status lists held input; release releases it. A stopped
        guest sees input when it resumes; use the sequence tools for taps."""
        return await call('input', event=event, code=code, port=port, axis=axis, delta=delta,
                          button=button, direction=direction, pressed=pressed)

    @mcp.tool()
    async def amiga_winuae_input_sequence(steps: list[dict], resume: bool = False) -> dict:
        """Queue up to 256 input events, each with after_frames (emulated frames, 3600 in total)
        and the amiga_winuae_input fields, using action key/mouse/button/joystick/wait. Runs
        asynchronously, pauses while stopped and releases held input at the end."""
        return await call('input_sequence', steps=steps, resume=resume)

    @mcp.tool()
    async def amiga_winuae_key_tap(codes: list[int], hold_frames: int = 3, gap_frames: int = 3,
                                   resume: bool = False) -> dict:
        """Tap a raw key or chord: press in order, hold, release in reverse."""
        return await call('key_tap', codes=codes, hold_frames=hold_frames, gap_frames=gap_frames, resume=resume)

    @mcp.tool()
    async def amiga_winuae_type_text(text: str, layout: Literal['us'] = 'us', hold_frames: int = 3,
                                     gap_frames: int = 3, resume: bool = False) -> dict:
        """Type up to 60 printable ASCII characters, tab or newline, assuming the guest uses
        the US keymap."""
        return await call('type_text', text=text, layout=layout, hold_frames=hold_frames,
                          gap_frames=gap_frames, resume=resume)

    # ─── Programs, symbols and DWARF ───

    @mcp.tool()
    async def amiga_winuae_load(file: str, address: str | None = None, drive: int = 0) -> dict:
        """Copy a raw binary into RAM at address and verify it, or insert a disk image (.adf,
        .adz, .dms, .ipf) into a drive. Hunk executables need amiga_winuae_hunk."""
        if file.lower().endswith(DISK_IMAGES):
            return await call('disk', operation='insert', drive=drive, file=file)
        return await call('load', file=file, address=address)

    @mcp.tool()
    async def amiga_winuae_disk(operation: Literal['status', 'insert', 'eject'] = 'status', drive: int = 0,
                                file: str | None = None) -> dict:
        """Insert or eject a floppy image. A WinUAE launched by DevBench restarts to apply it,
        and the debugger reconnects; otherwise it applies at the next start."""
        return await call('disk', operation=operation, drive=drive, file=file)

    @mcp.tool()
    async def amiga_winuae_hunk(operation: Literal['inspect', 'load'], file: str,
                                placements: list[dict] | None = None) -> Any:
        """inspect validates a Hunk executable and lists hunks and symbols. load relocates it
        into RAM you reserved: one {address, capacity, memory: chip|fast} per hunk. It zeroes
        BSS, verifies writes and rolls back on failure; it does not create a process. load
        requires pause."""
        if operation == 'inspect':
            return await call('hunk_inspect', file=file)
        return await call('hunk_load', file=file, placements=placements)

    @mcp.tool()
    async def amiga_winuae_symbols(file: str, prefix: str = '', limit: int = 256) -> dict:
        """List the symbols and sections of a Hunk or ELF32 m68k file. Needs no connection."""
        return await call('symbols', file=file, prefix=prefix, limit=limit)

    @mcp.tool()
    async def amiga_winuae_symbol_read(file: str, symbol: str, format: str = 'bytes', length: int | None = None,
                                       count: int = 1, offset: int = 0, section: int | None = None,
                                       process: str | None = None, mappings: list[dict] | None = None) -> dict:
        """Read an exact symbol of the running program. Hunk sections map to loaded segments;
        ELF needs mappings such as {section: ".data", segment: 1} or {section: 1, address,
        size}. format is bytes (with length) or u8/s8/u16/s16/u32/s32 (with count); no C types
        are inferred. Requires pause."""
        return await call('symbol_read', file=file, symbol=symbol, format=format, length=length, count=count,
                          offset=offset, section=section, process=process, mappings=mappings)

    @mcp.tool()
    async def amiga_winuae_source(file: str, mappings: list[dict], address: str | None = None,
                                  context: Literal['current', 'fault'] = 'current', process: str | None = None) -> dict:
        """Source file, line and function for the PC or an address, from a linked m68k ELF file
        with DWARF and its runtime section mappings. Needs the dwarf extra (pyelftools).
        Requires pause."""
        return await call('source', file=file, mappings=mappings, address=address, context=context, process=process)

    @mcp.tool()
    async def amiga_winuae_variable(file: str, mappings: list[dict], expression: str,
                                    context: Literal['current', 'fault'] = 'current', process: str | None = None) -> dict:
        """Read a C variable with .member, ->member and [index] selectors from DWARF type and
        location information. Unsupported or optimized-out locations are errors. Requires pause."""
        return await call('variable', file=file, mappings=mappings, expression=expression, context=context,
                          process=process)

    @mcp.tool()
    async def amiga_winuae_backtrace(file: str, mappings: list[dict], max_frames: int = 32,
                                     context: Literal['current', 'fault'] = 'current', process: str | None = None) -> dict:
        """Unwind up to 64 frames from DWARF call-frame information; stops with a reason
        instead of guessing. Requires pause."""
        return await call('backtrace', file=file, mappings=mappings, max_frames=max_frames, context=context,
                          process=process)
