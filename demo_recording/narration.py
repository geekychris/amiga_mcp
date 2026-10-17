#!/usr/bin/env python3
"""narration.py - synthesise the short's voice-over with the local Kokoro runtime.

One line per beat of make_short.py, spoken by bf_emma (British female, en-gb).
Writes video/short/narr_NN.wav and video/short/narration.json (durations),
which make_short.py reads to stretch any beat whose line runs long.
Kokoro server: /Users/chris/code/claude_world/kokoro_runtime (127.0.0.1:8770).
"""
import json, os, subprocess, urllib.request

D = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(D, "video", "short")
VOICE, LANG, SPEED = "bf_emma", "en-gb", 1.08

LINES = [
    "This is Terra Strike: an Amiga game, written by Claude Code, and tested live through Amiga DevBench.",
    "It started with one request: a Zarch-style landscape shooter for the sixty-eight K Amiga, "
    "with tanks, buildings and ships to destroy, unit tests, and testing through devbench.",
    "Claude began by talking to the Amiga over M C P: a ping, a task list, and a look at the existing games, "
    "to reuse their 3D maths and music player.",
    "The game logic is portable C, with sixty-seven unit tests that run on the Mac, on the Amiga, "
    "and inside the game. The first run caught a real bug: enemies firing every frame.",
    "Devbench cross-compiles in Docker, deploys to the emulator, and launches the game. "
    "First frame on the Amiga. But only five frames a second.",
    "So Claude exposed timers as bridge variables, and read them live. A hundred and fifty-nine milliseconds "
    "were going on area fills. A hand-written span filler took it to twenty-seven frames a second.",
    "Then it played the game. Key presses are injected straight into the Amiga's input device. "
    "Return starts the game, holding cursor up lifts off, and the take-off event arrives in the remote log.",
    "Screenshots exposed bugs the tests couldn't: cracks at the screen edge, and flickering colours. "
    "A palette read showed the blitter was clearing the buffer still on screen.",
    "A scene hook drops the craft straight into a tank battle. Hold fire, and the score comes back over the bridge. "
    "One hundred. First kill.",
    "Running the tests on the Amiga uncovered two bugs in devbench itself. Once those were fixed, "
    "all sixty-seven passed on the emulated sixty-eight oh sixty.",
    "Underneath, devbench speaks a simple line protocol to the bridge daemon. Here is the first kill, verbatim: "
    "the hook, the fire key, and the Amiga reporting the kill.",
    "The end-to-end test turned up a mystery: the wrong score. A second game, started by the Amiga's launcher, "
    "had grabbed our key presses and variable names. Claude tracked it down, and every check passed.",
    "For the main screen, Claude wrote an image prompt, ChatGPT painted it, and Claude converted it to "
    "thirty-two Amiga colours, choosing a palette that keeps the logo and the craft intact.",
    "Each screen loads its own palette. Devbench checks the colour registers and the on-screen text, "
    "and a key press takes us into the game.",
    "And here it is: played entirely by Claude, through devbench key injection and scene hooks.",
    "One session. Under forty minutes from a prompt to a tested, playable Amiga game.",
]

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    meta = []
    for i, text in enumerate(LINES, 1):
        path = os.path.join(OUT, f"narr_{i:02d}.wav")
        req = urllib.request.Request("http://127.0.0.1:8770/tts", json.dumps(
            {"text": text, "voice": VOICE, "lang": LANG, "speed": SPEED, "format": "wav"}).encode(),
            {"Content-Type": "application/json"})
        open(path, "wb").write(urllib.request.urlopen(req, timeout=300).read())
        dur = float(subprocess.check_output(["ffprobe", "-v", "error", "-show_entries", "format=duration",
                                             "-of", "csv=p=0", path]))
        meta.append({"beat": i, "file": path, "dur": round(dur, 2), "text": text})
        print(f"{i:02d} {dur:5.1f}s  {text[:70]}")
    json.dump(meta, open(os.path.join(OUT, "narration.json"), "w"), indent=1)
    print(f"total speech {sum(m['dur'] for m in meta):.0f}s")
