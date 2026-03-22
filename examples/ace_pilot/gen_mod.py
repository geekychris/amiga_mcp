#!/usr/bin/env python3
"""
Generate a Blue Danube waltz MOD file for Red Baron.
Produces a valid ProTracker .mod file with a simple sine wave sample
and the opening theme of Strauss's Blue Danube waltz.

Usage: python3 gen_mod.py [output.mod]
"""
import struct
import math
import sys

# ProTracker note periods (PAL)
NOTES = {
    'C-1': 856, 'C#1': 808, 'D-1': 762, 'D#1': 720, 'E-1': 678,
    'F-1': 640, 'F#1': 604, 'G-1': 570, 'G#1': 538, 'A-1': 508,
    'A#1': 480, 'B-1': 453,
    'C-2': 428, 'C#2': 404, 'D-2': 381, 'D#2': 360, 'E-2': 339,
    'F-2': 320, 'F#2': 302, 'G-2': 285, 'G#2': 269, 'A-2': 254,
    'A#2': 240, 'B-2': 226,
    'C-3': 214, 'C#3': 202, 'D-3': 190, 'D#3': 180, 'E-3': 170,
    'F-3': 160, 'F#3': 151, 'G-3': 143, 'G#3': 135, 'A-3': 127,
    'A#3': 120, 'B-3': 113,
    '---': 0,
}

def note_bytes(note, sample=1, effect=0, param=0):
    """Encode a note into 4 ProTracker bytes."""
    period = NOTES.get(note, 0)
    samp_hi = (sample >> 4) & 0x0F
    samp_lo = sample & 0x0F
    b0 = (samp_hi << 4) | ((period >> 8) & 0x0F)
    b1 = period & 0xFF
    b2 = (samp_lo << 4) | (effect & 0x0F)
    b3 = param & 0xFF
    return bytes([b0, b1, b2, b3])

def empty_note():
    return bytes([0, 0, 0, 0])

def effect_note(effect, param, sample=0, note='---'):
    """Note with effect only."""
    return note_bytes(note, sample, effect, param)

def make_pattern(rows_data):
    """Create a 64-row pattern (4 channels per row).
    rows_data: list of up to 64 items, each is [ch0, ch1, ch2, ch3]
    where each ch is 4 bytes. Missing rows are filled with empty notes.
    """
    data = bytearray()
    for r in range(64):
        if r < len(rows_data):
            row = rows_data[r]
            for ch in range(4):
                if ch < len(row):
                    data += row[ch]
                else:
                    data += empty_note()
        else:
            data += empty_note() * 4
    return bytes(data)

def make_sample(length=64):
    """Generate a sine wave sample (8-bit signed)."""
    data = bytearray()
    for i in range(length):
        val = int(80 * math.sin(2 * math.pi * i / length))
        data.append(val & 0xFF)
    return bytes(data)

def make_bass_sample(length=32):
    """Generate a softer sine for bass."""
    data = bytearray()
    for i in range(length):
        val = int(60 * math.sin(2 * math.pi * i / length))
        data.append(val & 0xFF)
    return bytes(data)

def build_mod():
    # Settings: speed=4, BPM=120 -> effective 180 quarter-note BPM
    # 4 rows per beat, 12 rows per measure (3/4 time)
    # Pattern = 64 rows = 5 measures + 4 spare rows

    # Samples
    melody_sample = make_sample(64)   # Sample 1: melody (64 bytes = 32 words)
    bass_sample = make_bass_sample(32)  # Sample 2: bass (32 bytes = 16 words)

    # Blue Danube melody (simplified, D major)
    # Channel 0: Bass (beat 1 of each measure)
    # Channel 1: Chord stabs (beats 2-3)
    # Channel 2: Melody
    # Channel 3: Empty (reserved for SFX)

    # Notation: row numbers within pattern, 12 rows per measure
    # Beat 1 = row 0, Beat 2 = row 4, Beat 3 = row 8

    def bass(note):
        return note_bytes(note, sample=2)

    def chord(note):
        return note_bytes(note, sample=2)

    def mel(note):
        return note_bytes(note, sample=1)

    E = empty_note()

    # Pattern 0: Intro + measures 1-5
    p0 = []
    # Row 0: Set speed=4 (F04) on ch0, tempo=120 (F78) on ch1
    p0.append([effect_note(0xF, 0x04, 2, 'D-1'), effect_note(0xF, 0x78), E, E])
    for r in range(1, 4): p0.append([E, E, E, E])
    # Row 4: chord beat 2
    p0.append([E, chord('A-2'), E, E])
    for r in range(5, 8): p0.append([E, E, E, E])
    # Row 8: chord beat 3, pickup melody D-2
    p0.append([E, chord('D-2'), mel('D-2'), E])
    for r in range(9, 10): p0.append([E, E, E, E])
    p0.append([E, E, mel('D-2'), E])  # row 10
    p0.append([E, E, E, E])           # row 11

    # Measure 2: F#-2 (held)
    p0.append([bass('D-1'), E, mel('F#2'), E])  # row 12
    for r in range(13, 16): p0.append([E, E, E, E])
    p0.append([E, chord('A-2'), E, E])  # row 16
    for r in range(17, 20): p0.append([E, E, E, E])
    p0.append([E, chord('D-2'), E, E])  # row 20
    for r in range(21, 24): p0.append([E, E, E, E])

    # Measure 3: F#-2 (held)
    p0.append([bass('D-1'), E, mel('F#2'), E])  # row 24
    for r in range(25, 28): p0.append([E, E, E, E])
    p0.append([E, chord('A-2'), E, E])  # row 28
    for r in range(29, 32): p0.append([E, E, E, E])
    p0.append([E, chord('D-2'), mel('D-2'), E])  # row 32
    for r in range(33, 34): p0.append([E, E, E, E])
    p0.append([E, E, mel('D-2'), E])  # row 34
    p0.append([E, E, E, E])           # row 35

    # Measure 4: A-2 (held)
    p0.append([bass('A-1'), E, mel('A-2'), E])  # row 36
    for r in range(37, 40): p0.append([E, E, E, E])
    p0.append([E, chord('E-2'), E, E])  # row 40
    for r in range(41, 44): p0.append([E, E, E, E])
    p0.append([E, chord('A-2'), E, E])  # row 44
    for r in range(45, 48): p0.append([E, E, E, E])

    # Measure 5: A-2 (held)
    p0.append([bass('A-1'), E, mel('A-2'), E])  # row 48
    for r in range(49, 52): p0.append([E, E, E, E])
    p0.append([E, chord('E-2'), mel('D-2'), E])  # row 52
    for r in range(53, 54): p0.append([E, E, E, E])
    p0.append([E, E, mel('D-2'), E])  # row 54
    for r in range(55, 56): p0.append([E, E, E, E])
    p0.append([E, chord('A-2'), E, E])  # row 56
    for r in range(57, 60): p0.append([E, E, E, E])
    # Rows 60-63: empty
    for r in range(60, 64): p0.append([E, E, E, E])

    # Pattern 1: measures 6-10 (second phrase, going higher)
    p1 = []
    # Measure 6: F#-2
    p1.append([bass('D-1'), E, mel('F#2'), E])
    for r in range(1, 4): p1.append([E, E, E, E])
    p1.append([E, chord('A-2'), E, E])
    for r in range(5, 8): p1.append([E, E, E, E])
    p1.append([E, chord('D-2'), E, E])
    for r in range(9, 12): p1.append([E, E, E, E])

    # Measure 7: F#-2
    p1.append([bass('D-1'), E, mel('F#2'), E])
    for r in range(13, 16): p1.append([E, E, E, E])
    p1.append([E, chord('A-2'), mel('A-2'), E])
    for r in range(17, 18): p1.append([E, E, E, E])
    p1.append([E, E, mel('G#2'), E])  # passing tone
    p1.append([E, E, mel('A-2'), E])
    p1.append([E, chord('D-2'), E, E])
    for r in range(22, 24): p1.append([E, E, E, E])

    # Measure 8: B-2
    p1.append([bass('G-1'), E, mel('B-2'), E])
    for r in range(25, 28): p1.append([E, E, E, E])
    p1.append([E, chord('D-2'), E, E])
    for r in range(29, 32): p1.append([E, E, E, E])
    p1.append([E, chord('G-2'), E, E])
    for r in range(33, 36): p1.append([E, E, E, E])

    # Measure 9: B-2 -> D-3
    p1.append([bass('G-1'), E, mel('B-2'), E])
    for r in range(37, 40): p1.append([E, E, E, E])
    p1.append([E, chord('D-2'), mel('A#2'), E])
    p1.append([E, E, E, E])
    p1.append([E, E, mel('B-2'), E])
    for r in range(43, 44): p1.append([E, E, E, E])
    p1.append([E, chord('G-2'), E, E])
    for r in range(45, 48): p1.append([E, E, E, E])

    # Measure 10: D-3 resolution
    p1.append([bass('D-1'), E, mel('D-3'), E])
    for r in range(49, 52): p1.append([E, E, E, E])
    p1.append([E, chord('A-2'), E, E])
    for r in range(53, 56): p1.append([E, E, E, E])
    p1.append([E, chord('D-2'), E, E])
    for r in range(57, 60): p1.append([E, E, E, E])
    for r in range(60, 64): p1.append([E, E, E, E])

    # Pattern 2: measures 11-15 (resolution, descend back)
    p2 = []
    # Measure 11: D-3 held
    p2.append([bass('D-1'), E, mel('D-3'), E])
    for r in range(1, 4): p2.append([E, E, E, E])
    p2.append([E, chord('A-2'), E, E])
    for r in range(5, 8): p2.append([E, E, E, E])
    p2.append([E, chord('D-2'), mel('C#3'), E])
    for r in range(9, 12): p2.append([E, E, E, E])

    # Measure 12: B-2
    p2.append([bass('G-1'), E, mel('B-2'), E])
    for r in range(13, 16): p2.append([E, E, E, E])
    p2.append([E, chord('D-2'), E, E])
    for r in range(17, 20): p2.append([E, E, E, E])
    p2.append([E, chord('G-2'), mel('A-2'), E])
    for r in range(21, 24): p2.append([E, E, E, E])

    # Measure 13: F#-2
    p2.append([bass('D-1'), E, mel('F#2'), E])
    for r in range(25, 28): p2.append([E, E, E, E])
    p2.append([E, chord('A-2'), E, E])
    for r in range(29, 32): p2.append([E, E, E, E])
    p2.append([E, chord('D-2'), mel('E-2'), E])
    for r in range(33, 36): p2.append([E, E, E, E])

    # Measure 14: D-2 (home)
    p2.append([bass('D-1'), E, mel('D-2'), E])
    for r in range(37, 40): p2.append([E, E, E, E])
    p2.append([E, chord('A-2'), E, E])
    for r in range(41, 44): p2.append([E, E, E, E])
    p2.append([E, chord('D-2'), E, E])
    for r in range(45, 48): p2.append([E, E, E, E])

    # Measure 15: D-2 held (resolve)
    p2.append([bass('D-1'), E, mel('D-2'), E])
    for r in range(49, 52): p2.append([E, E, E, E])
    p2.append([E, chord('A-2'), E, E])
    for r in range(53, 56): p2.append([E, E, E, E])
    p2.append([E, chord('D-2'), E, E])
    for r in range(57, 64): p2.append([E, E, E, E])

    patterns_data = [make_pattern(p0), make_pattern(p1), make_pattern(p2)]
    num_patterns = len(patterns_data)

    # Song: loop all 3 patterns then repeat
    song_length = 3
    song_positions = [0, 1, 2] + [0] * 125  # pad to 128

    # --- Build MOD file ---
    mod = bytearray()

    # Song name (20 bytes)
    mod += b'Blue Danube\x00' + b'\x00' * 8

    # 31 instrument headers (30 bytes each)
    # Sample 1: melody sine (64 bytes = 32 words)
    mod += b'melody\x00' + b'\x00' * 15          # name (22 bytes)
    mod += struct.pack('>H', len(melody_sample) // 2)  # length in words
    mod += bytes([0])                              # finetune
    mod += bytes([50])                             # volume (0-64)
    mod += struct.pack('>H', 0)                    # loop start
    mod += struct.pack('>H', len(melody_sample) // 2)  # loop length (loop whole sample)

    # Sample 2: bass sine (32 bytes = 16 words)
    mod += b'bass\x00' + b'\x00' * 17             # name (22 bytes)
    mod += struct.pack('>H', len(bass_sample) // 2)
    mod += bytes([0])
    mod += bytes([40])                             # softer volume
    mod += struct.pack('>H', 0)
    mod += struct.pack('>H', len(bass_sample) // 2)

    # Samples 3-31: empty
    for i in range(29):
        mod += b'\x00' * 22                        # name
        mod += struct.pack('>H', 0)                # length
        mod += bytes([0, 0])                       # finetune, volume
        mod += struct.pack('>H', 0)                # loop start
        mod += struct.pack('>H', 1)                # loop length (1 = no loop)

    # Song length and restart
    mod += bytes([song_length])
    mod += bytes([127])  # restart position (unused, traditionally 127)

    # Song positions (128 bytes)
    mod += bytes(song_positions[:128])

    # Module identifier
    mod += b'M.K.'

    # Pattern data
    for pat in patterns_data:
        mod += pat

    # Sample data
    mod += melody_sample
    mod += bass_sample

    return bytes(mod)

if __name__ == '__main__':
    output = sys.argv[1] if len(sys.argv) > 1 else 'blue_danube.mod'
    mod_data = build_mod()
    with open(output, 'wb') as f:
        f.write(mod_data)
    print(f"Generated {output} ({len(mod_data)} bytes)")
