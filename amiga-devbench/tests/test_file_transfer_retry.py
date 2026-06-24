"""Unit tests for the serial-robust file-transfer retry path.

Mocks the bridge so we can inject a dropped-byte (short write/read) and assert:
- a corrupt chunk is retried, not silently accepted;
- retries are idempotent — every write is WRITEFILE at an explicit offset, never
  APPEND (so re-sending after a lost ack can't duplicate data);
- the pull side retries a short read instead of misaligning.
"""
import asyncio
import binascii

from amiga_devbench import file_transfer as ft


class FakeConn:
    connected = True

    def __init__(self):
        self.sent = []

    def send(self, msg):
        self.sent.append(msg)


class FakeBus:
    """wait_for() pops the next scripted response for an event (predicate ignored)."""
    def __init__(self, responses):
        self.responses = {k: list(v) for k, v in responses.items()}

    async def wait_for(self, event, timeout=5.0, predicate=None):
        q = self.responses.get(event)
        return q.pop(0) if q else None


def _ok(n):
    return {"type": "OK", "context": "WRITEFILE", "message": str(n)}


def _push(tmp, data, responses, chunk_size=1024):
    p = tmp / "src.bin"
    p.write_bytes(data)
    conn = FakeConn()
    bus = FakeBus(responses)
    res = asyncio.run(ft.push_file(conn, bus, str(p), "RAM:dst.bin", chunk_size=chunk_size))
    return res, conn


def test_short_write_is_retried(tmp_path):
    data = b"ABCDEFGH"                      # 8 bytes, one chunk
    crc = binascii.crc32(data) & 0xFFFFFFFF
    res, conn = _push(tmp_path, data, {
        "ok": [_ok(7), _ok(8)],             # first ack short (dropped byte) -> retry
        "checksum": [{"path": "RAM:dst.bin", "crc32": f"{crc:08x}", "size": 8}],
    })
    assert res.success, res.message
    writes = [m for m in conn.sent if m["type"] == "WRITEFILE"]
    assert len(writes) == 2, conn.sent          # retried once
    assert all(w["offset"] == 0 for w in writes)  # idempotent same-offset overwrite
    assert not any(m["type"] == "APPEND" for m in conn.sent)


def test_multichunk_uses_offsets_never_append(tmp_path):
    data = bytes(range(256)) * 12              # 3072 bytes -> 3 chunks @1024
    crc = binascii.crc32(data) & 0xFFFFFFFF
    res, conn = _push(tmp_path, data, {
        "ok": [_ok(1024), _ok(1024), _ok(1024)],
        "checksum": [{"path": "RAM:dst.bin", "crc32": f"{crc:08x}", "size": len(data)}],
    }, chunk_size=1024)
    assert res.success, res.message
    writes = [m for m in conn.sent if m["type"] == "WRITEFILE"]
    assert [w["offset"] for w in writes] == [0, 1024, 2048], writes
    assert not any(m["type"] == "APPEND" for m in conn.sent)


def test_push_gives_up_after_max_retries(tmp_path):
    data = b"ABCDEFGH"
    res, _ = _push(tmp_path, data, {
        "ok": [_ok(7)] * ft.MAX_RETRIES,        # always short
        "checksum": [],
    })
    assert not res.success
    assert "Failed at chunk 0" in res.message


def test_pull_retries_short_read(tmp_path):
    data = b"ABCDEFGH"                           # 8 bytes
    crc = binascii.crc32(data) & 0xFFFFFFFF
    conn = FakeConn()
    bus = FakeBus({
        "checksum": [{"path": "RAM:src.bin", "crc32": f"{crc:08x}", "size": 8}],
        # first FILE response dropped a byte (7 bytes) -> retry; second is whole
        "file": [{"path": "RAM:src.bin", "hexData": data[:7].hex()},
                 {"path": "RAM:src.bin", "hexData": data.hex()}],
    })
    out = tmp_path / "out.bin"
    res = asyncio.run(ft.pull_file(conn, bus, "RAM:src.bin", str(out), chunk_size=1024))
    assert res.success, res.message
    assert out.read_bytes() == data
    assert res.crc_match is True


def test_all():
    import tempfile, pathlib
    for fn in (test_short_write_is_retried, test_multichunk_uses_offsets_never_append,
               test_push_gives_up_after_max_retries, test_pull_retries_short_read):
        with tempfile.TemporaryDirectory() as d:
            fn(pathlib.Path(d))
    print("OK: file-transfer retry tests passed")


if __name__ == "__main__":
    test_all()
