# SPDX-License-Identifier: MIT
"""WinUAE checkpoints: state files, numbered slots, an automatic snapshot
ring and a chunk-level comparison of two state files.

WinUAE writes state files at the next frame start, so a save moves the CPU
on by up to one frame, with this session's breakpoints lifted for that run.
Restoring clears WinUAE's remote debugger state; this session's
breakpoints and watches are reinstalled afterwards.
"""
from __future__ import annotations

import asyncio
import json
import time
import zlib
from pathlib import Path
from typing import Any

from .winuae_debug import DebugError, WinuaeDebugger, action, integer

SNAPSHOT_DIR = Path.home() / '.amiga-devbench' / 'snapshots' / 'winuae'
SLOTS = range(1, 10)


def slot_path(name: str) -> Path:
    """slot-1 .. slot-9 or auto-N, as files in the WinUAE snapshot folder."""
    kind, _, number = name.partition('-')
    if kind not in ('slot', 'auto') or not number.isdigit() or (kind == 'slot' and int(number) not in SLOTS):
        raise DebugError('Snapshot slots are slot-1 .. slot-9 or auto-N')
    return SNAPSHOT_DIR / f'{kind}-{int(number)}.uss'


def state_file(value: Any) -> Path:
    if isinstance(value, str) and (value.startswith('slot-') or value.startswith('auto-')):
        return slot_path(value)
    if not isinstance(value, str) or not value or any(c in value for c in '\0\r\n'):
        raise DebugError('Supply a state file path or a slot name')
    path = Path(value).expanduser().resolve()
    if len(str(path).encode()) > 900:
        raise DebugError('State file path is too long')
    return path


async def _checkpoint(d: WinuaeDebugger, operation: str, path: Path) -> dict:
    if operation == 'restore':
        if not path.is_file():
            raise DebugError(f'No state file at {path}')
        await d.monitor_text(f'checkpoint restore {path}', 'checkpoint')
        d.last_stop = None
        await d.rearm()
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        # The CPU runs to the next frame start, and any stop cancels the
        # save; lift this session's breakpoints for that run.
        points = []
        try:
            for kind, address, size in list(d._points):
                await d._ok(f'z{kind},{address:x},{size:x}')
                points.append((kind, address, size))
            await d.monitor_text(f'checkpoint save {path}', 'checkpoint')
        finally:
            for kind, address, size in points:  # Only those actually lifted.
                await d._ok(f'Z{kind},{address:x},{size:x}')
    d.execution = 'stopped'
    d._stop.set()
    current = await d.registers()
    result = dict(operation=operation, path=str(path), registers=current)
    if operation == 'save':
        # Faster CPU modes can stop one instruction after the frame-start
        # save; report the registers the file holds.
        saved = saved_registers(path)
        result.update(registers=saved, suspended_breakpoints=len(points))
        if saved['PC'] != current['PC']:
            result['stopped_at'] = current
    return result


def saved_registers(path: Path) -> dict[str, int]:
    """D0-A7, SR, PC and the stack pointers from a state file's CPU chunk:
    model, flags, D0-D7, A0-A6, PC, IRC, IR, USP, ISP, SR, ..., VBR (68010+)."""
    cpu = next((body for name, body in parse_state(path) if name == 'CPU '), None)
    if cpu is None or len(cpu) < 84:
        raise DebugError(f'{path} has no CPU state')
    longs = lambda offset, count: [int.from_bytes(cpu[offset + 4 * i:offset + 4 * i + 4], 'big') for i in range(count)]
    model = longs(0, 1)[0]
    regs = dict(zip([f'D{i}' for i in range(8)] + [f'A{i}' for i in range(7)], longs(8, 15)))
    regs['PC'] = longs(68, 1)[0]
    usp, isp = longs(76, 2)
    sr = int.from_bytes(cpu[84:86], 'big')
    regs.update(A7=isp if sr & 0x2000 else usp, SR=sr, USP=usp, ISP=isp)
    if model >= 68010 and len(cpu) >= 102:
        regs['VBR'] = longs(98, 1)[0]
    return regs


@action('checkpoint', capability='checkpoint')
async def _checkpoint_action(d: WinuaeDebugger, operation: str = 'save', path: Any = None) -> dict:
    """Save or restore a WinUAE state file, or a slot such as slot-1."""
    if operation not in ('save', 'restore'):
        raise DebugError('Checkpoint operation must be save or restore')
    return await _checkpoint(d, operation, state_file(path))


def _labels_file() -> Path:
    return SNAPSHOT_DIR / '.labels.json'


def load_labels() -> dict[str, str]:
    try:
        labels = json.loads(_labels_file().read_text())
        return labels if isinstance(labels, dict) else {}
    except (OSError, ValueError):
        return {}


def _describe(name: str) -> dict:
    path = slot_path(name)
    stat = path.stat() if path.exists() else None
    return dict(slot=name, path=str(path), size=stat.st_size if stat else 0, mtime=stat.st_mtime if stat else None)


@action('snapshots', locked=False, stopped=False)
async def _snapshots(d: WinuaeDebugger, operation: str = 'list', slot: str = '', label: str = '') -> dict:
    """Numbered quick-save slots and their labels, plus the automatic ring."""
    if operation == 'label':
        slot_path(slot)
        labels = load_labels()
        text = str(label).strip()[:80]
        if text:
            labels[slot] = text
        else:
            labels.pop(slot, None)
        SNAPSHOT_DIR.mkdir(parents=True, exist_ok=True)
        _labels_file().write_text(json.dumps(labels, indent=2, sort_keys=True))
    elif operation in ('save', 'load'):
        return await d.call('checkpoint', operation='save' if operation == 'save' else 'restore', path=slot)
    elif operation != 'list':
        raise DebugError('Snapshot operation must be list, save, load or label')
    ring = autosnap(d)
    return dict(dir=str(SNAPSHOT_DIR), slots=[_describe(f'slot-{n}') for n in SLOTS],
                auto=[_describe(f'auto-{n}') for n in range(ring.ring_size) if slot_path(f'auto-{n}').exists()],
                labels=load_labels(), autosnap=ring.status())


class AutoSnapshots:
    """Save to a rotating ring of state files every interval seconds. Each
    save pauses the guest briefly; a running guest resumes afterwards."""

    def __init__(self, debugger: WinuaeDebugger) -> None:
        self.debugger = debugger
        self.interval = 0
        self.ring_size = 5
        self.next_slot = 0
        self.saves = 0
        self.last_save: float | None = None
        self.last_error: str | None = None
        self.task: asyncio.Task | None = None

    def status(self) -> dict:
        return dict(enabled=self.interval > 0, interval=self.interval, ring_size=self.ring_size,
                    next_slot=self.next_slot % self.ring_size, saves=self.saves,
                    last_save=self.last_save, last_error=self.last_error)

    def configure(self, interval: int, ring_size: int) -> None:
        self.interval, self.ring_size = interval, ring_size
        if interval and (self.task is None or self.task.done()):
            self.task = asyncio.get_running_loop().create_task(self._loop())
        elif not interval and self.task:
            self.task.cancel()
            self.task = None

    async def _loop(self) -> None:
        d = self.debugger
        while self.interval:
            await asyncio.sleep(self.interval)
            if not d.connected or d.execution == 'host-paused':
                continue
            try:
                running = d.execution != 'stopped'
                if running:
                    await d.call('pause')
                try:
                    await d.call('checkpoint', operation='save', path=f'auto-{self.next_slot % self.ring_size}')
                finally:
                    if running and d.connected:
                        await d.call('resume')
                self.next_slot += 1
                self.saves += 1
                self.last_save, self.last_error = time.time(), None
            except DebugError as exc:
                self.last_error = str(exc)


def autosnap(d: WinuaeDebugger) -> AutoSnapshots:
    # Kept on the debugger, not d.state: the ring outlives one connection.
    if not hasattr(d, '_autosnap'):
        d._autosnap = AutoSnapshots(d)
    return d._autosnap


@action('autosnap', locked=False, stopped=False)
async def _autosnap(d: WinuaeDebugger, interval: Any = None, ring_size: Any = None) -> dict:
    """Show, or set with interval (seconds, 0 = off) and ring_size, the
    automatic snapshot ring."""
    ring = autosnap(d)
    if interval is not None or ring_size is not None:
        ring.configure(integer(interval if interval is not None else ring.interval, 'interval', 3600),
                       integer(ring_size if ring_size is not None else ring.ring_size, 'ring_size', 64, 1))
    return ring.status()


# ─── State file comparison ───

def parse_state(path: Path) -> list[tuple[str, bytes]]:
    """Chunks of a WinUAE state file: name, size, flags (bit 0: zlib data
    preceded by its length) and data padded with 4 - (length & 3) bytes."""
    data = path.read_bytes()
    chunks, pos = [], 0
    while pos + 8 <= len(data):
        name = data[pos:pos + 4].decode('latin-1')
        size = int.from_bytes(data[pos + 4:pos + 8], 'big')
        if size <= 12:
            if name == 'END ':
                break
            raise DebugError(f'Invalid empty chunk {name!r} in {path}')
        flags = int.from_bytes(data[pos + 8:pos + 12], 'big')
        payload = size - 12
        body = data[pos + 12:pos + 12 + payload]
        if len(body) != payload:
            raise DebugError(f'Truncated chunk {name!r} in {path}')
        written = payload - 4 if flags & 1 else payload
        if flags & 1:
            try:
                body = zlib.decompress(body[4:])
            except zlib.error:
                raise DebugError(f'Corrupt compressed chunk {name!r} in {path}') from None
        chunks.append((name, body))
        pos += 12 + payload + 4 - (written & 3)
    if not chunks or chunks[0][0] != 'ASF ':
        raise DebugError(f'{path} is not a WinUAE state file')
    return chunks


def _ranges(a: bytes, b: bytes, block: int = 256, limit: int = 16) -> tuple[int, list[dict]]:
    """Count differing bytes and list the first differing regions."""
    differing, ranges = 0, []
    length = max(len(a), len(b))
    for start in range(0, length, block):
        x, y = a[start:start + block], b[start:start + block]
        if x == y:
            continue
        count = sum(1 for i in range(max(len(x), len(y))) if (x[i:i + 1] or None) != (y[i:i + 1] or None))
        differing += count
        if ranges and ranges[-1]['end'] == start:
            ranges[-1]['end'] = start + block
            ranges[-1]['bytes'] += count
        elif len(ranges) < limit:
            ranges.append(dict(start=start, end=start + block, bytes=count))
    return differing, ranges


def diff_states(a: Path, b: Path) -> dict:
    def keyed(chunks):
        seen: dict[str, int] = {}
        result = {}
        for name, body in chunks:
            index = seen.get(name, 0)
            seen[name] = index + 1
            result[f'{name.strip()}#{index}' if index else name.strip()] = body
        return result
    first, second = keyed(parse_state(a)), keyed(parse_state(b))
    changed = []
    for key in [k for k in first if k in second]:
        if first[key] != second[key]:
            count, ranges = _ranges(first[key], second[key])
            changed.append(dict(chunk=key, size_a=len(first[key]), size_b=len(second[key]),
                                differing_bytes=count, regions=ranges))
    return dict(a=str(a), b=str(b), changed=changed,
                only_in_a=[k for k in first if k not in second], only_in_b=[k for k in second if k not in first],
                unchanged=sum(1 for k in first if k in second and first[k] == second[k]))


@action('snapshot_diff', locked=False, stopped=False)
async def _snapshot_diff(d: WinuaeDebugger, a: Any = None, b: Any = None) -> dict:
    """Compare two state files or slots chunk by chunk; RAM chunks report
    differing byte regions by offset."""
    first, second = state_file(a), state_file(b)
    for path in (first, second):
        if not path.is_file():
            raise DebugError(f'No state file at {path}')
    return await asyncio.get_running_loop().run_in_executor(None, diff_states, first, second)
