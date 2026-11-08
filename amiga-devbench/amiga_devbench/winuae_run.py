# SPDX-License-Identifier: MIT
"""Debug a DevBench project under WinUAE from its first instruction: build,
deploy, arm WinUAE's process break for the program's name, launch it in the
guest and stop at its entry or at a function such as main."""
from __future__ import annotations

import asyncio
import time
from typing import Any, Awaitable, Callable

from .winuae_debug import DebugError

Call = Callable[..., Awaitable[Any]]


def program_name(project: str) -> str:
    return project.rstrip('/').split('/')[-1]


async def _stop_client(conn: Any, bus: Any, name: str) -> str:
    """Ask the bridge to stop a running instance; it may not be running."""
    async with bus.subscribe('ok', 'err') as queue:
        conn.send({'type': 'STOP', 'name': name})
        loop = asyncio.get_running_loop()
        deadline = loop.time() + 2.0
        while (remaining := deadline - loop.time()) > 0:
            try:
                event, data = await asyncio.wait_for(queue.get(), timeout=remaining)
            except asyncio.TimeoutError:
                break
            if 'STOP' in data.get('context', '') or 'Client' in data.get('context', ''):
                return f'[{event}] {data.get("message", name)}'
    return 'no running instance answered'


async def _stopped(call: Call, timeout: float) -> dict:
    state = await call('wait', timeout=timeout)
    if state.get('execution') != 'stopped':
        raise TimeoutError
    return state


async def debug_run(call: Call, project: str, *, builder: Any = None, deployer: Any = None,
                    conn: Any = None, bus: Any = None, command: str | None = None,
                    stop_at: str = 'main', launch: str = 'bridge', build: bool = True,
                    timeout: float = 30) -> dict:
    if launch not in ('bridge', 'none'):
        raise DebugError("launch must be 'bridge' or 'none'")
    if not isinstance(stop_at, str) or not stop_at:
        raise DebugError("stop_at must be 'entry' or a function name")
    name = program_name(project)
    command = command or f'DH2:Dev/{name}'
    bridge = conn is not None and bus is not None and getattr(conn, 'connected', False)
    if launch == 'bridge' and not bridge:
        raise DebugError("The guest bridge is not connected; connect it, or pass launch='none' "
                         'and start the program in the guest yourself')
    steps: list[str] = []
    result: dict[str, Any] = dict(project=project, program=name, steps=steps)

    if build:
        built = await builder.build(project)
        steps.append(f"build: {'OK' if built.success else 'FAILED'} ({built.duration}ms)")
        if not built.success:
            return dict(result, ok=False, errors=built.errors or built.output)
        deployed = await deployer.deploy_smart(project, conn=conn, bus=bus)
        steps.append(f"deploy: {'OK' if deployed.success else 'FAILED'} {deployed.message}")
        if not deployed.success:
            return dict(result, ok=False)
    if bridge:
        steps.append('stop old instance: ' + await _stop_client(conn, bus, name))

    state = await call('status')
    if not state.get('connected'):
        state = await call('connect')       # attaching normally pauses the CPU
    if state.get('execution') != 'stopped':
        await call('pause')
    await call('process_break', operation='name', name=name)
    steps.append(f'process break armed for {name}')
    await call('resume')

    if launch == 'bridge':
        # The program stops at its entry, so the bridge cannot answer until
        # it runs again; do not wait for the launch reply.
        conn.send({'type': 'LAUNCH', 'id': int(time.time() * 1000) % 100000, 'command': command})
        steps.append(f'launch: {command}')
    else:
        steps.append(f'waiting up to {timeout:g}s for {name} to start in the guest')

    try:
        await _stopped(call, timeout)
    except TimeoutError:
        return dict(result, ok=False, error=f'{name} did not start within {timeout:g}s; the process break '
                    'stays armed until it starts, or until amiga_winuae_process_break(operation="clear")')
    steps.append('stopped at the program entry')

    if stop_at != 'entry':
        try:
            await call('breakpoint', operation='add', symbol=stop_at, project=project, oneshot=True)
        except DebugError as exc:
            return dict(result, ok=False, stopped_at='entry',
                        error=f'Stopped at the program entry, but cannot break at {stop_at}: {exc}')
        await call('resume')
        try:
            await _stopped(call, timeout)
        except TimeoutError:
            return dict(result, ok=False, error=f'The program did not reach {stop_at} within {timeout:g}s; '
                        'it is still running with a one-shot breakpoint there')
        steps.append(f'stopped at {stop_at}')

    regs = await call('registers')
    result.update(ok=True, pc=f'0x{regs["PC"]:08x}', stopped_at=stop_at)
    try:
        result['source'] = await call('source', project=project)
    except DebugError as exc:
        result['source_error'] = str(exc)
    return result
