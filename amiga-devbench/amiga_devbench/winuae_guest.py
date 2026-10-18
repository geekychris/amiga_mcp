# SPDX-License-Identifier: MIT
"""WinUAE guest input, guest debug output and native screenshots.

Input goes through WinUAE's shared input handlers without the guest bridge,
so it also reaches games that have taken over the machine. Sequences are
timed in emulated frames and pause while the debugger is stopped.
"""
from __future__ import annotations

import asyncio
import base64
import tempfile
import time
from pathlib import Path
from typing import Any

from .winuae_debug import REARM, DebugError, WinuaeDebugger, action, boolean, integer

DIRECTIONS = ['left', 'right', 'up', 'down']
SEQUENCE_STEPS, SEQUENCE_FRAMES = 256, 3600


def input_event(args: dict) -> list[int]:
    """kind, port, code, value for WinUAE's input event command."""
    kind = args.get('action')
    port = integer(args.get('port', 0), 'port', 1)
    if kind == 'mouse':
        delta = args.get('delta')
        if args.get('axis') not in ('x', 'y') or type(delta) is not int or abs(delta) > 127:
            raise DebugError('Mouse motion needs axis x or y and an integer delta of -127..127')
        return [1, port, 0 if args['axis'] == 'x' else 1, delta & 0xffffffff]
    state = 1 if boolean(args.get('pressed'), 'pressed') else 0
    if kind == 'key':
        if port:
            raise DebugError('The keyboard has no port')
        return [0, 0, integer(args.get('code'), 'code', 0x67), state]
    if kind == 'button':
        return [2, port, 4 + integer(args.get('button'), 'button', 2), state]
    if kind == 'joystick' and args.get('direction') in DIRECTIONS:
        return [2, port, DIRECTIONS.index(args['direction']), state]
    raise DebugError('Input action must be key, mouse, button or joystick')


# US Amiga keymap positions of printable characters: (raw key, shifted).
TEXT_KEYS: dict[str, tuple[int, bool]] = {}
for _row, _base in (('`1234567890-=', 0x00), ('qwertyuiop[]', 0x10), ("asdfghjkl;'", 0x20), ('zxcvbnm,./', 0x31)):
    for _i, _char in enumerate(_row):
        TEXT_KEYS[_char] = (_base + _i, False)
for _char in 'abcdefghijklmnopqrstuvwxyz':
    TEXT_KEYS[_char.upper()] = (TEXT_KEYS[_char][0], True)
for _plain, _shifted in (('`1234567890-=', '~!@#$%^&*()_+'), ('[]', '{}'), (";'", ':"'), (',./', '<>?')):
    for _a, _b in zip(_plain, _shifted):
        TEXT_KEYS[_b] = (TEXT_KEYS[_a][0], True)
TEXT_KEYS.update({'\\': (0x0d, False), '|': (0x0d, True), ' ': (0x40, False), '\t': (0x42, False), '\n': (0x44, False)})
KEY_SHIFT = 0x60


def build_sequence(kind: str, args: dict) -> list[list[int]]:
    """Steps of [delay frames, kind, port, code, value]; kind 3 waits."""
    steps: list[list[int]] = []
    if kind == 'input_sequence':
        raw = args.get('steps')
        if not isinstance(raw, list) or not 1 <= len(raw) <= SEQUENCE_STEPS:
            raise DebugError(f'Supply 1..{SEQUENCE_STEPS} steps')
        for step in raw:
            if not isinstance(step, dict):
                raise DebugError('Each step must be an object')
            delay = integer(step.get('after_frames', 0), 'after_frames', SEQUENCE_FRAMES)
            steps.append([delay] + ([3, 0, 0, 0] if step.get('action') == 'wait' else input_event(step)))
    else:
        hold = integer(args.get('hold_frames', 3), 'hold_frames', 120, 1)
        gap = integer(args.get('gap_frames', 3), 'gap_frames', 120, 1)

        def tap(codes: list[int]) -> None:
            steps.extend([0, 0, 0, code, 1] for code in codes)
            steps.extend([hold if i == 0 else 0, 0, 0, code, 0] for i, code in enumerate(reversed(codes)))
            steps.append([gap, 3, 0, 0, 0])
        if kind == 'key_tap':
            codes = args.get('codes')
            if not isinstance(codes, list) or not 1 <= len(codes) <= 8:
                raise DebugError('Supply 1..8 key codes')
            codes = [integer(code, 'code', 0x67) for code in codes]
            if len(set(codes)) != len(codes):
                raise DebugError('A chord cannot contain a key twice')
            tap(codes)
        else:
            text = args.get('text')
            if args.get('layout') != 'us' or not isinstance(text, str) or not 1 <= len(text) <= 60:
                raise DebugError('Supply 1..60 characters and layout "us" (the guest keymap)')
            for char in text:
                if char not in TEXT_KEYS:
                    raise DebugError(f'Unsupported character for the US keymap: {char!r}')
                code, shifted = TEXT_KEYS[char]
                tap([KEY_SHIFT, code] if shifted else [code])
    if len(steps) > SEQUENCE_STEPS or sum(s[0] for s in steps) > SEQUENCE_FRAMES:
        raise DebugError(f'The sequence exceeds {SEQUENCE_STEPS} events or {SEQUENCE_FRAMES} frames; split it')
    return steps


@action('input', stopped=False, capability='input')
async def _input(d: WinuaeDebugger, event: str = 'status', **args: Any) -> Any:
    """Send one input event (key, mouse, button, joystick), or report or
    release remotely held input (status, release). Execution is not
    changed; a stopped guest sees input when it resumes."""
    if not d.connected:
        raise DebugError('Connect to the WinUAE debugger first')
    if event in ('status', 'release'):
        return await d._monitor(f'input {event}')
    event = input_event(dict(args, action=event))
    return await d._monitor('input event ' + ' '.join(f'{n:x}' for n in event))


async def _sequence(d: WinuaeDebugger, kind: str, args: dict) -> Any:
    if not d.connected:
        raise DebugError('Connect to the WinUAE debugger first')
    resume = boolean(args.pop('resume', False), 'resume')
    steps = build_sequence(kind, args)
    result = await d._monitor('input sequence ' + ';'.join(' '.join(f'{n:x}' for n in s) for s in steps))
    if resume and d.execution == 'stopped':
        d._run('c', 'continue')
    return result


@action('input_sequence', stopped=False, capability='input-sequence')
async def _input_sequence(d: WinuaeDebugger, **args: Any) -> Any:
    """Queue up to 256 events with delays in emulated frames (3600 total)."""
    return await _sequence(d, 'input_sequence', args)


@action('key_tap', stopped=False, capability='input-sequence')
async def _key_tap(d: WinuaeDebugger, **args: Any) -> Any:
    """Tap a raw key or chord: press in order, hold, release in reverse."""
    return await _sequence(d, 'key_tap', args)


@action('type_text', stopped=False, capability='input-sequence')
async def _type_text(d: WinuaeDebugger, **args: Any) -> Any:
    """Type up to 60 characters, assuming the guest uses the US keymap."""
    return await _sequence(d, 'type_text', args)


@action('guest_output', capability='guest-output')
async def _guest_output(d: WinuaeDebugger, operation: str = 'read') -> Any:
    """Capture messages a guest sends through uaelib function 86."""
    if operation not in ('on', 'off', 'read', 'clear'):
        raise DebugError('Guest output operation must be on, off, read or clear')
    if operation in ('on', 'off'):
        d.state['guest_output'] = operation == 'on'
    return await d._monitor(f'guest-output {operation}')


async def _rearm(d: WinuaeDebugger) -> None:
    if d.state.get('guest_output'):
        await d._monitor('guest-output on')


REARM.append(_rearm)


@action('screenshot', capability='screenshot')
async def _screenshot(d: WinuaeDebugger, path: Any = None, include_image: Any = True) -> dict:
    """Save the emulated display as a PNG on the host and return it."""
    if path is None:
        file = Path(tempfile.gettempdir()) / f'winuae-screen-{time.strftime("%Y%m%d-%H%M%S")}.png'
    elif isinstance(path, str) and path and not any(c in path for c in '\0\r\n'):
        file = Path(path).expanduser().resolve()
    else:
        raise DebugError('Supply a host path for the PNG file')
    file.parent.mkdir(parents=True, exist_ok=True)
    await d.monitor_text(f'screenshot {file}')
    if not file.is_file():
        raise DebugError('WinUAE did not write the screenshot; Unix builds need libpng')
    result: dict[str, Any] = dict(path=str(file), size=file.stat().st_size)
    if boolean(include_image, 'include_image') and file.stat().st_size <= 4 * 1024 * 1024:
        result['png_base64'] = base64.b64encode(file.read_bytes()).decode()
    return result


@action('disk', locked=False, stopped=False)
async def _disk(d: WinuaeDebugger, operation: str = 'status', drive: Any = 0, file: Any = None) -> dict:
    """Insert or eject a floppy image. WinUAE applies drive changes at
    launch, so a WinUAE that DevBench started is restarted; the debugger
    reconnects if it was connected."""
    manager = d.emulator
    if manager is None:
        raise DebugError('Disk changes need a WinUAE launched by DevBench ([emulator] binary pointing to WinUAE)')
    if operation == 'status':
        return dict(floppies=manager.floppies, running=manager.is_running)
    number = integer(drive, 'drive', 3)
    if operation == 'insert':
        image = Path(str(file or '')).expanduser()
        if not image.is_file():
            raise DebugError('Supply the path of an existing disk image')
        manager.set_floppy(number, str(image))
    elif operation == 'eject':
        manager.set_floppy(number, None)
    else:
        raise DebugError('Disk operation must be status, insert or eject')
    owned = manager._process is not None and manager._process.returncode is None
    if not owned:
        return dict(floppies=manager.floppies, restarted=False,
                    note='Applies when DevBench next starts WinUAE')
    reconnect = d.connected
    if reconnect:
        await d.aclose()
    if not await manager.restart():
        raise DebugError('WinUAE did not restart; see the DevBench log')
    if reconnect:
        loop = asyncio.get_running_loop()
        deadline = loop.time() + 30
        while True:
            try:
                await d.call('connect')
                break
            except DebugError:
                if loop.time() > deadline:
                    raise DebugError('WinUAE restarted, but its GDB server did not answer in time') from None
                await asyncio.sleep(0.5)
    return dict(floppies=manager.floppies, restarted=True, connected=d.connected)
