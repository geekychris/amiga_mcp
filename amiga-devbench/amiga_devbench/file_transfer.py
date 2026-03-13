"""File transfer between host and Amiga via serial/bridge protocol.

Supports multi-chunk transfers with CRC32 verification. Designed for
remote setups where the shared folder is not available.
"""

from __future__ import annotations

import binascii
import logging
import os
import time
from dataclasses import dataclass, field
from pathlib import Path

from .serial_conn import SerialConnection
from .state import EventBus

logger = logging.getLogger(__name__)

CHUNK_SIZE = 400        # bytes per chunk (800 hex chars, fits in 1024-char line)
MAX_RETRIES = 3
CHUNK_TIMEOUT = 5.0


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
        hex_data = chunk.hex()
        retries = 0
        success = False

        while retries < MAX_RETRIES and not success:
            if chunk_num == 0:
                # First chunk: WRITEFILE creates/truncates file
                conn.send({
                    "type": "WRITEFILE",
                    "path": amiga_path,
                    "offset": 0,
                    "hexData": hex_data,
                })
                ok = await bus.wait_for(
                    "ok", timeout=CHUNK_TIMEOUT,
                    predicate=lambda d: d.get("context") == "WRITEFILE",
                )
            else:
                # Subsequent chunks: APPEND
                conn.send({
                    "type": "APPEND",
                    "path": amiga_path,
                    "hexData": hex_data,
                })
                ok = await bus.wait_for(
                    "ok", timeout=CHUNK_TIMEOUT,
                    predicate=lambda d: d.get("context") == "APPEND",
                )

            if ok:
                success = True
            else:
                # Check for error
                err = await bus.wait_for(
                    "err", timeout=0.5,
                    predicate=lambda d: d.get("context") in ("WRITEFILE", "APPEND"),
                )
                retries += 1
                if retries < MAX_RETRIES:
                    logger.warning(
                        "Chunk %d retry %d: %s",
                        chunk_num, retries,
                        err.get("message", "timeout") if err else "timeout",
                    )

        if not success:
            elapsed = time.monotonic() - t0
            return TransferResult(
                False,
                f"Failed at chunk {chunk_num} (offset {offset}/{total})",
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

    # Download chunks
    collected = bytearray()
    offset = 0
    chunk_num = 0

    while offset < remote_size:
        remaining = remote_size - offset
        req_size = min(chunk_size, remaining)
        retries = 0
        success = False

        while retries < MAX_RETRIES and not success:
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
            if msg:
                hex_data = msg.get("hexData", "")
                if hex_data:
                    chunk_bytes = bytes.fromhex(hex_data)
                    collected.extend(chunk_bytes)
                    success = True
                else:
                    retries += 1
            else:
                retries += 1
                if retries < MAX_RETRIES:
                    logger.warning("Read chunk %d retry %d", chunk_num, retries)

        if not success:
            elapsed = time.monotonic() - t0
            return TransferResult(
                False,
                f"Failed reading chunk {chunk_num} (offset {offset}/{remote_size})",
                bytes_transferred=offset,
                elapsed=elapsed,
            )

        offset += req_size
        chunk_num += 1

    # Write local file
    dest = Path(local_path)
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(bytes(collected))

    # Verify CRC
    local_crc = binascii.crc32(bytes(collected)) & 0xFFFFFFFF
    crc_match = None
    if remote_crc_str:
        try:
            remote_crc = int(remote_crc_str, 16)
            crc_match = remote_crc == local_crc
        except (ValueError, TypeError):
            pass

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
