#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""Record the AmigaOS 4 guest's screen and sound to an MP4, through QEMU's HMP monitor.

    scripts/record_os4.py out.mp4 [--secs 30] [--fps 30] [--monitor 127.0.0.1:2348]

Stops after --secs, or on Ctrl-C. Only the guest's display is recorded (no host
desktop): each frame is an HMP `screendump` (about 6 ms each), taken on a fixed
clock, so a slow dump repeats the last frame instead of drifting. Frames whose
size differs from 1280x720 (a game's own 800x600 or 640x480 screen) are scaled
into 1280x720 with black bars.

Sound is HMP `wavcapture` on the `snd0` audiodev. QEMU only writes samples
while the guest's audio DMA runs, so a game that opens its audio a few seconds
in would come out early; the recorder samples `info capture` every few frames
and puts the samples back on the wall clock.

For classic 68k in FS-UAE use scripts/wincap.swift (ScreenCaptureKit, one
window and its app's sound; works when FS-UAE is in its own full-screen Space).

Needs ffmpeg, numpy and Pillow. Take the emulator first (scripts/claude_busy.py).
"""
import argparse
import os
import re
import socket
import subprocess
import tempfile
import threading
import time
import wave

W, H = 1280, 720
FFMPEG = os.environ.get("FFMPEG", "ffmpeg")


class HMP:
    def __init__(self, host, port):
        self.k = socket.create_connection((host, port), timeout=5)
        self.buf = b""
        self._read()

    def _read(self):
        while not self.buf.endswith(b"(qemu) "):
            d = self.k.recv(65536)
            if not d:
                raise EOFError("monitor closed")
            self.buf += d
        out, self.buf = self.buf, b""
        return out.decode(errors="replace")

    def cmd(self, c):
        self.k.sendall((c + "\n").encode())
        return self._read()

    def close(self):
        self.k.close()


def read_ppm(path):
    with open(path, "rb") as f:
        parts = f.read().split(b"\n", 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]


def fit(w, h, px):
    if (w, h) == (W, H):
        return px
    from PIL import Image
    im = Image.frombytes("RGB", (w, h), px)
    s = min(W / w, H / h)
    nw, nh = int(w * s) // 2 * 2, int(h * s) // 2 * 2
    out = Image.new("RGB", (W, H))
    out.paste(im.resize((nw, nh), Image.BILINEAR), ((W - nw) // 2, (H - nh) // 2))
    return out.tobytes()


def retime(wav, timeline, t_video, dur, rate=44100):
    """Place the captured samples where they were produced on the wall clock."""
    import numpy as np
    with wave.open(wav) as w:
        data = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, 2)
    out = np.zeros((int(dur * rate) + rate, 2), dtype=np.int16)
    prev = 0
    for t, nbytes in timeline:
        s0, s1 = prev, min(nbytes // 4, len(data))
        if s1 > s0:
            end = int((t - t_video) * rate)
            start = end - (s1 - s0)
            chunk = data[s0:s1]
            if start < 0:
                chunk, start = chunk[-start:], 0
            chunk = chunk[:max(0, len(out) - start)]
            out[start:start + len(chunk)] = chunk
        prev = max(prev, s1)
    fixed = wav[:-4] + "_timed.wav"
    with wave.open(fixed, "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
        w.writeframes(out[:int(dur * rate)].tobytes())
    return fixed


def record(out, secs, fps, host, port, stop=None):
    stop = stop or threading.Event()
    tmp = tempfile.mkdtemp(prefix="record_os4_")
    vid, wav, dump = f"{tmp}/v.mp4", f"{tmp}/a.wav", f"{tmp}/sd.ppm"
    hmp = HMP(host, port)
    ff = subprocess.Popen([FFMPEG, "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24",
                           "-s", f"{W}x{H}", "-r", str(fps), "-i", "-", "-c:v", "libx264",
                           "-preset", "veryfast", "-crf", "16", "-pix_fmt", "yuv420p", vid],
                          stdin=subprocess.PIPE)
    hmp.cmd(f"wavcapture {wav} snd0 44100 16 2")
    timeline, last, n, late = [], bytes(W * H * 3), 0, 0
    t0 = time.time()
    try:
        while not stop.is_set() and time.time() - t0 < secs:
            target = t0 + n / fps
            now = time.time()
            if now < target:
                time.sleep(target - now)
            elif now - target > 1 / fps:
                late += 1
            hmp.cmd(f"screendump {dump}")
            last = fit(*read_ppm(dump))
            ff.stdin.write(last)
            if n % 6 == 0:
                m = re.search(r"\.wav: (\d+) bytes", hmp.cmd("info capture"))
                if m:
                    timeline.append((time.time(), int(m.group(1))))
            n += 1
    except KeyboardInterrupt:
        pass
    for i, path in re.findall(r"\[(\d+)\]: Capturing audio\([^)]*\) to (\S+?): ", hmp.cmd("info capture")):
        if path == wav:
            hmp.cmd(f"stopcapture {i}")
    hmp.close()
    ff.stdin.close()
    ff.wait()
    dur = n / fps
    audio = retime(wav, timeline, t0, dur) if os.path.exists(wav) else None
    if audio:
        cmd = [FFMPEG, "-v", "error", "-y", "-i", vid, "-i", audio, "-map", "0:v", "-map", "1:a",
               "-c:v", "copy", "-c:a", "aac", "-b:a", "192k", "-ar", "48000", "-shortest", out]
    else:
        cmd = [FFMPEG, "-v", "error", "-y", "-i", vid, "-c", "copy", out]
    subprocess.run(cmd, check=True)
    print(f"{out}: {n} frames ({dur:.1f} s), {late} late ticks")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--secs", type=float, default=30)
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--monitor", default="127.0.0.1:2348")
    a = ap.parse_args()
    host, port = a.monitor.rsplit(":", 1)
    record(a.out, a.secs, a.fps, host, int(port))


if __name__ == "__main__":
    main()
