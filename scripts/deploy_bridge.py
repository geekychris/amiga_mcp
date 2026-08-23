#!/usr/bin/env python3
"""Push a new amiga-bridge binary to the running daemon over TCP, verify it,
then swap it in and restart the daemon.

Usage: python3 deploy_bridge.py HOST PORT LOCAL_BINARY

Steps:
  1. WRITEFILE the binary to MEDIA1:_mcp/amiga-bridge.new (chunked, acked).
  2. Verify exact size (FILEINFO) + head/tail bytes (READFILE) vs local file.
  3. Only if intact: SCRIPT to Copy .new over amiga-bridge + launch restart-bridge
     detached, then SHUTDOWN the running daemon.
"""
import sys, time, socket, binascii

DEST = "MEDIA1:_mcp/amiga-bridge.new"
LIVE = "MEDIA1:_mcp/amiga-bridge"
CHUNK = 3072


class Conn:
    def __init__(self, host, port):
        self.s = socket.create_connection((host, port), timeout=10)
        self.s.settimeout(0.3)
        self.buf = b""

    def lines(self, upto=0.3):
        """Yield available complete lines for up to `upto` seconds idle."""
        out = []
        t = time.time()
        while time.time() - t < upto:
            try:
                c = self.s.recv(65536)
                if not c:
                    break
                self.buf += c
                t = time.time()
            except socket.timeout:
                pass
            while b"\n" in self.buf:
                ln, self.buf = self.buf.split(b"\n", 1)
                out.append(ln.decode("latin-1").strip())
        return out

    def send(self, line):
        self.s.sendall((line + "\n").encode("latin-1"))

    def send_wait(self, line, match_prefixes, timeout=10):
        """Send a line, wait until a reply line starts with one of match_prefixes."""
        self.send(line)
        deadline = time.time() + timeout
        while time.time() < deadline:
            for ln in self.lines(0.3):
                for p in match_prefixes:
                    if ln.startswith(p):
                        return ln
        return None

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass


def main():
    host, port, path = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    with open(path, "rb") as f:
        data = f.read()
    total = len(data)
    print(f"local binary: {path} = {total} bytes")

    c = Conn(host, port)
    c.lines(0.5)  # drain READY/HB

    # --- 1. transfer ---
    t0 = time.time()
    off = 0
    n = 0
    while off < total:
        chunk = data[off:off + CHUNK]
        hexs = binascii.hexlify(chunk).decode("ascii")
        reply = c.send_wait(f"WRITEFILE|{DEST}|{off}|{hexs}",
                            ("OK|WRITEFILE", "ERR"), timeout=15)
        if reply is None or reply.startswith("ERR"):
            print(f"  FAILED at offset {off}: {reply}")
            c.close()
            sys.exit(1)
        off += len(chunk)
        n += 1
        if n % 10 == 0 or off >= total:
            print(f"  {off}/{total} bytes ({n} chunks, {time.time()-t0:.1f}s)")
    print(f"transfer done in {time.time()-t0:.1f}s")

    # --- 2. verify ---
    info = c.send_wait(f"FILEINFO|{DEST}", ("FILE|" + DEST, "FILEINFO|", "ERR"), timeout=10)
    print(f"FILEINFO: {info}")
    # FILE|path|size|type|...
    try:
        remote_size = int(info.split("|")[2])
    except Exception:
        print("could not parse remote size; aborting before swap")
        c.close(); sys.exit(1)
    if remote_size != total:
        print(f"SIZE MISMATCH remote={remote_size} local={total}; aborting before swap")
        c.close(); sys.exit(1)

    def readback(offset, size):
        r = c.send_wait(f"READFILE|{DEST}|{offset}|{size}", ("FILE|" + DEST, "ERR"), timeout=10)
        if not r or r.startswith("ERR"):
            return None
        return binascii.unhexlify(r.split("|", 4)[4])

    head = readback(0, 256)
    tail = readback(total - 256, 256)
    if head != data[:256] or tail != data[-256:]:
        print(f"HEAD/TAIL MISMATCH (head_ok={head==data[:256]} tail_ok={tail==data[-256:]}); aborting before swap")
        c.close(); sys.exit(1)
    print("verified: size + head(256) + tail(256) match local binary")

    # --- 3. swap + restart ---
    # Copy .new over the live binary (safe: running daemon is already in RAM),
    # launch restart-bridge detached (it Waits 4s then starts the new binary),
    # then ask the current daemon to shut down so it releases the TCP port.
    script = (f"Copy >NIL: {DEST} TO {LIVE};"
              f"Run >NIL: Execute MEDIA1:_mcp/restart-bridge")
    print(f"swap+restart script: {script}")
    rep = c.send_wait(f"SCRIPT|9001|{script}", ("OK", "CMDRESP|9001", "ERR", "RESP"), timeout=15)
    print(f"script reply: {rep}")
    time.sleep(0.5)
    print(c.lines(1.0))
    print("sending SHUTDOWN to current daemon...")
    c.send("SHUTDOWN")
    print(c.lines(1.5))
    c.close()
    print("done: old daemon asked to exit; restart-bridge will bring up v1.11 in ~4s")


if __name__ == "__main__":
    main()
