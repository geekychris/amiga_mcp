"""File transfer between host and Amiga via serial/bridge protocol.

Supports multi-chunk transfers with CRC32 verification. Designed for
remote setups where the shared folder is not available.
"""

from __future__ import annotations

import asyncio
import binascii
import logging
import os
import time
from dataclasses import dataclass, field
from pathlib import Path

from .serial_conn import SerialConnection
from .state import EventBus

logger = logging.getLogger(__name__)

# 1024-byte chunks (2048 hex chars/line). Smaller than the 8192-char protocol
# limit on purpose: over a lossy serial link a dropped byte corrupts the whole
# line, so shorter lines transfer clean far more often and the per-chunk retry
# below has to fire less. TCP doesn't care about the extra round-trips.
CHUNK_SIZE = 1024
MAX_RETRIES = 6
CHUNK_TIMEOUT = 5.0


async def _push_chunk(conn, bus, path: str, offset: int, chunk: bytes) -> bool:
    """Write one chunk at an explicit offset and confirm the daemon wrote
    exactly len(chunk) bytes, retrying on timeout/err/short-write.

    Uses WRITEFILE with the real offset (never APPEND) so a retry overwrites the
    same span — idempotent, so re-sending after a lost ack can't duplicate data.
    The daemon echoes the byte count it wrote in the OK; a dropped byte yields an
    odd-length hex line -> fewer bytes written -> count mismatch -> we retry,
    instead of silently accepting corruption (caught only by the final CRC).
    """
    expect = len(chunk)
    hex_data = chunk.hex()
    for _ in range(MAX_RETRIES):
        conn.send({"type": "WRITEFILE", "path": path, "offset": offset, "hexData": hex_data})
        ok = await bus.wait_for(
            "ok", timeout=CHUNK_TIMEOUT,
            predicate=lambda d: d.get("context") == "WRITEFILE",
        )
        if ok:
            try:
                wrote = int(str(ok.get("message", "")).split("|")[0])
            except (ValueError, TypeError):
                wrote = -1
            if wrote == expect:
                return True
            logger.warning("chunk@%d short write %d/%d, retrying", offset, wrote, expect)
        else:
            await bus.wait_for(
                "err", timeout=0.3,
                predicate=lambda d: d.get("context") == "WRITEFILE",
            )
        # Let any corrupt partial line flush through the daemon's line parser
        # (its terminating newline resyncs the stream) before re-sending.
        await asyncio.sleep(0.05)
    return False


@dataclass
class TransferResult:
    success: bool
    message: str
    bytes_transferred: int = 0
    elapsed: float = 0.0
    crc_match: bool | None = None
    files: list[str] = field(default_factory=list)


async def push_file(
    conn: SerialConnection,
    bus: EventBus,
    local_path: str,
    amiga_path: str,
    chunk_size: int = CHUNK_SIZE,
) -> TransferResult:
    """Transfer a file from host to Amiga via serial protocol.

    Uses WRITEFILE for the first chunk (creates/truncates) and APPEND for
    subsequent chunks. Verifies with CHECKSUM after transfer.
    """
    t0 = time.monotonic()
    src = Path(local_path)
    if not src.is_file():
        return TransferResult(False, f"Local file not found: {local_path}")

    data = src.read_bytes()
    total = len(data)
    if total == 0:
        return TransferResult(False, f"File is empty: {local_path}")

    local_crc = binascii.crc32(data) & 0xFFFFFFFF

    offset = 0
    chunk_num = 0

    while offset < total:
        chunk = data[offset:offset + chunk_size]
        if not await _push_chunk(conn, bus, amiga_path, offset, chunk):
            elapsed = time.monotonic() - t0
            return TransferResult(
                False,
                f"Failed at chunk {chunk_num} (offset {offset}/{total}) "
                f"after {MAX_RETRIES} retries",
                bytes_transferred=offset,
                elapsed=elapsed,
            )
        offset += len(chunk)
        chunk_num += 1

    # Verify with checksum
    conn.send({"type": "CHECKSUM", "path": amiga_path})
    crc_msg = await bus.wait_for(
        "checksum", timeout=10.0,
        predicate=lambda d: d.get("path") == amiga_path,
    )

    elapsed = time.monotonic() - t0
    crc_match = None
    if crc_msg:
        remote_crc_str = crc_msg.get("crc32", "")
        try:
            remote_crc = int(remote_crc_str, 16)
            crc_match = remote_crc == local_crc
        except (ValueError, TypeError):
            crc_match = None

    if crc_match is False:
        return TransferResult(
            False,
            f"CRC mismatch: local={local_crc:08x} remote={remote_crc_str}",
            bytes_transferred=total,
            elapsed=elapsed,
            crc_match=False,
        )

    rate = total / elapsed if elapsed > 0 else 0
    return TransferResult(
        True,
        f"Transferred {total} bytes in {elapsed:.1f}s ({rate:.0f} B/s), "
        f"{chunk_num} chunks"
        + (f", CRC32 verified" if crc_match else ""),
        bytes_transferred=total,
        elapsed=elapsed,
        crc_match=crc_match,
    )


async def pull_file(
    conn: SerialConnection,
    bus: EventBus,
    amiga_path: str,
    local_path: str,
    chunk_size: int = CHUNK_SIZE,
) -> TransferResult:
    """Transfer a file from Amiga to host via serial protocol.

    Uses READFILE with sequential offsets to download file chunks.
    Verifies with CHECKSUM after transfer.
    """
    t0 = time.monotonic()

    # First get remote checksum and size
    conn.send({"type": "CHECKSUM", "path": amiga_path})
    crc_msg = await bus.wait_for(
        "checksum", timeout=10.0,
        predicate=lambda d: d.get("path") == amiga_path,
    )
    if not crc_msg:
        # Check for error (file not found etc.)
        err = await bus.wait_for(
            "err", timeout=0.5,
            predicate=lambda d: d.get("context") == "CHECKSUM",
        )
        msg = err.get("message", "timeout") if err else "No response"
        return TransferResult(False, f"Cannot read remote file: {msg}")

    remote_size = int(crc_msg.get("size", 0))
    remote_crc_str = crc_msg.get("crc32", "")

    if remote_size == 0:
        return TransferResult(False, f"Remote file is empty or not found: {amiga_path}")

    # Download chunks. Outer loop re-downloads the whole file if the assembled
    # CRC doesn't match — a value-corrupted (not dropped) byte keeps the hex
    # length right and slips past the per-chunk check, but fails the final CRC.
    collected = bytearray()
    local_crc = None
    crc_match = None
    for file_attempt in range(3):
        collected = bytearray()
        offset = 0
        chunk_num = 0
        failed_chunk = None

        while offset < remote_size:
            remaining = remote_size - offset
            req_size = min(chunk_size, remaining)
            chunk_bytes = None

            for _ in range(MAX_RETRIES):
                conn.send({
                    "type": "READFILE",
                    "path": amiga_path,
                    "offset": offset,
                    "size": req_size,
                })
                msg = await bus.wait_for(
                    "file", timeout=CHUNK_TIMEOUT,
                    predicate=lambda d: d.get("path") == amiga_path,
                )
                if not msg:
                    await asyncio.sleep(0.05)
                    continue
                hex_data = msg.get("hexData", "")
                try:
                    cb = bytes.fromhex(hex_data) if hex_data else b""
                except ValueError:
                    # dropped byte -> odd-length hex; retry the same offset
                    await asyncio.sleep(0.05)
                    continue
                # With sub-line chunks the daemon never truncates, so a short
                # read means transit loss -> retry rather than misalign.
                if len(cb) != req_size:
                    logger.warning("read@%d short %d/%d, retrying", offset, len(cb), req_size)
                    await asyncio.sleep(0.05)
                    continue
                chunk_bytes = cb
                break

            if chunk_bytes is None:
                failed_chunk = chunk_num
                break
            collected.extend(chunk_bytes)
            offset += len(chunk_bytes)
            chunk_num += 1

        if failed_chunk is not None:
            elapsed = time.monotonic() - t0
            return TransferResult(
                False,
                f"Failed reading chunk {failed_chunk} (offset {offset}/{remote_size}) "
                f"after {MAX_RETRIES} retries",
                bytes_transferred=offset,
                elapsed=elapsed,
            )

        local_crc = binascii.crc32(bytes(collected)) & 0xFFFFFFFF
        if remote_crc_str:
            try:
                crc_match = int(remote_crc_str, 16) == local_crc
            except (ValueError, TypeError):
                crc_match = None
        if crc_match is not False:
            break   # matched, or no remote CRC to compare against
        logger.warning("pull CRC mismatch, re-downloading (attempt %d)", file_attempt + 1)

    if crc_match is False:
        elapsed = time.monotonic() - t0
        return TransferResult(
            False,
            f"CRC mismatch after retries: local={local_crc:08x} remote={remote_crc_str}",
            bytes_transferred=len(collected),
            elapsed=elapsed,
            crc_match=False,
        )

    # Write local file
    dest = Path(local_path)
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(bytes(collected))

    elapsed = time.monotonic() - t0
    rate = len(collected) / elapsed if elapsed > 0 else 0
    return TransferResult(
        True,
        f"Downloaded {len(collected)} bytes in {elapsed:.1f}s ({rate:.0f} B/s), "
        f"{chunk_num} chunks"
        + (f", CRC32 verified" if crc_match else ""),
        bytes_transferred=len(collected),
        elapsed=elapsed,
        crc_match=crc_match,
        files=[local_path],
    )


async def push_files(
    conn: SerialConnection,
    bus: EventBus,
    file_pairs: list[tuple[str, str]],
) -> TransferResult:
    """Transfer multiple files from host to Amiga.

    Args:
        file_pairs: List of (local_path, amiga_path) tuples.
    """
    t0 = time.monotonic()
    total_bytes = 0
    transferred = []
    errors = []

    for local_path, amiga_path in file_pairs:
        result = await push_file(conn, bus, local_path, amiga_path)
        if result.success:
            total_bytes += result.bytes_transferred
            transferred.append(f"{Path(local_path).name} -> {amiga_path}")
        else:
            errors.append(f"{Path(local_path).name}: {result.message}")

    elapsed = time.monotonic() - t0
    if errors:
        return TransferResult(
            False,
            f"Transferred {len(transferred)}/{len(file_pairs)} files, "
            f"errors: {'; '.join(errors)}",
            bytes_transferred=total_bytes,
            elapsed=elapsed,
            files=[p for p, _ in file_pairs[:len(transferred)]],
        )

    return TransferResult(
        True,
        f"Transferred {len(transferred)} file(s), {total_bytes} bytes in {elapsed:.1f}s",
        bytes_transferred=total_bytes,
        elapsed=elapsed,
        files=[p for _, p in file_pairs],
    )


async def pull_files(
    conn: SerialConnection,
    bus: EventBus,
    file_pairs: list[tuple[str, str]],
) -> TransferResult:
    """Transfer multiple files from Amiga to host.

    Args:
        file_pairs: List of (amiga_path, local_path) tuples.
    """
    t0 = time.monotonic()
    total_bytes = 0
    transferred = []
    errors = []

    for amiga_path, local_path in file_pairs:
        result = await pull_file(conn, bus, amiga_path, local_path)
        if result.success:
            total_bytes += result.bytes_transferred
            transferred.append(f"{amiga_path} -> {Path(local_path).name}")
        else:
            errors.append(f"{amiga_path}: {result.message}")

    elapsed = time.monotonic() - t0
    if errors:
        return TransferResult(
            False,
            f"Downloaded {len(transferred)}/{len(file_pairs)} files, "
            f"errors: {'; '.join(errors)}",
            bytes_transferred=total_bytes,
            elapsed=elapsed,
            files=[p for _, p in file_pairs[:len(transferred)]],
        )

    return TransferResult(
        True,
        f"Downloaded {len(transferred)} file(s), {total_bytes} bytes in {elapsed:.1f}s",
        bytes_transferred=total_bytes,
        elapsed=elapsed,
        files=[p for _, p in file_pairs],
    )
