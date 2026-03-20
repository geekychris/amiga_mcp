#!/usr/bin/env python3
"""Generate a ProTracker MOD file with Korobeiniki (Tetris theme)."""

import os
import struct
import math
import random

# Output path
OUTPUT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_FILE = os.path.join(OUTPUT_DIR, "stakattack.mod")
os.makedirs(OUTPUT_DIR, exist_ok=True)

# ProTracker period table
PERIODS = {
    'C-1': 856, 'C#1': 808, 'D-1': 762, 'D#1': 720, 'E-1': 678, 'F-1': 640,
    'F#1': 604, 'G-1': 570, 'G#1': 538, 'A-1': 508, 'A#1': 480, 'B-1': 453,
    'C-2': 428, 'C#2': 404, 'D-2': 381, 'D#2': 360, 'E-2': 339, 'F-2': 320,
    'F#2': 302, 'G-2': 285, 'G#2': 269, 'A-2': 254, 'A#2': 240, 'B-2': 226,
    'C-3': 214, 'C#3': 202, 'D-3': 190, 'D#3': 180, 'E-3': 170, 'F-3': 160,
    'F#3': 151, 'G-3': 143, 'G#3': 135, 'A-3': 127, 'A#3': 120, 'B-3': 113,
}


def clamp(val, lo, hi):
    return max(lo, min(hi, int(val)))


# --- Generate sample data ---

def gen_square(length=64):
    """Square wave: +80 for first half, -80 for second half."""
    data = bytes([80 & 0xFF] * 32 + [(-80) & 0xFF] * 32)
    return data


def gen_triangle(length=64):
    """Triangle wave: ramp up then down, signed -64 to +63."""
    samples = []
    for i in range(32):
        val = -64 + int(127 * i / 31)
        samples.append(clamp(val, -128, 127) & 0xFF)
    for i in range(32):
        val = 63 - int(127 * i / 31)
        samples.append(clamp(val, -128, 127) & 0xFF)
    return bytes(samples)


def gen_kick(length=128):
    """Kick drum: decaying low-freq sine."""
    samples = []
    for i in range(length):
        val = int(127 * (1 - i / 128) * math.sin(i * 0.15))
        samples.append(clamp(val, -128, 127) & 0xFF)
    return bytes(samples)


def gen_hihat(length=64):
    """Hi-hat: white noise."""
    random.seed(42)
    samples = []
    for _ in range(length):
        val = random.randint(-60, 60)
        samples.append(clamp(val, -128, 127) & 0xFF)
    return bytes(samples)


def gen_snare(length=128):
    """Snare: noise + pitched sine, decaying."""
    random.seed(123)
    samples = []
    for i in range(length):
        noise = random.randint(-80, 80)
        pitched = 60 * math.sin(i * 0.3)
        val = int((noise + pitched) * (1 - i / 128))
        samples.append(clamp(val, -128, 127) & 0xFF)
    return bytes(samples)


# Instrument definitions: (name, sample_data, volume, loop_start_bytes, loop_len_bytes)
instruments = []

lead_data = gen_square(64)
instruments.append(("lead", lead_data, 50, 0, 64))

bass_data = gen_triangle(64)
instruments.append(("bass", bass_data, 55, 0, 64))

kick_data = gen_kick(128)
instruments.append(("kick", kick_data, 64, 0, 0))

hihat_data = gen_hihat(64)
instruments.append(("hihat", hihat_data, 40, 0, 0))

snare_data = gen_snare(128)
instruments.append(("snare", snare_data, 55, 0, 0))


def make_instrument_header(name, sample_data, volume, loop_start, loop_len):
    """Build 30-byte instrument header."""
    name_bytes = name.encode('ascii')[:22].ljust(22, b'\x00')
    length_words = len(sample_data) // 2
    finetune = 0
    loop_start_words = loop_start // 2
    loop_len_words = loop_len // 2 if loop_len > 0 else 1  # 1 = no loop in MOD convention
    # Actually for no-loop, repeat length should be 1 word
    if loop_len == 0:
        loop_len_words = 1
        loop_start_words = 0
    return name_bytes + struct.pack('>HBBhh', length_words, finetune, volume,
                                     loop_start_words, loop_len_words)


def encode_note(sample_num, period, effect_cmd=0, effect_param=0):
    """Encode a 4-byte MOD note.
    sample_num: 1-31 (0 = no sample)
    period: from period table (0 = no note)
    effect_cmd: 0-15
    effect_param: 0-255
    """
    sample_upper = (sample_num >> 4) & 0x0F
    sample_lower = sample_num & 0x0F
    period_upper = (period >> 8) & 0x0F
    period_lower = period & 0xFF

    byte0 = (sample_upper << 4) | period_upper
    byte1 = period_lower
    byte2 = (sample_lower << 4) | (effect_cmd & 0x0F)
    byte3 = effect_param & 0xFF
    return bytes([byte0, byte1, byte2, byte3])


def empty_note():
    return encode_note(0, 0)


def note(sample, pitch, effect_cmd=0, effect_param=0):
    """Helper: sample is 1-indexed, pitch is string like 'E-2'."""
    if pitch is None or pitch == '---':
        return encode_note(sample, 0, effect_cmd, effect_param)
    return encode_note(sample, PERIODS[pitch], effect_cmd, effect_param)


def effect_only(effect_cmd, effect_param):
    return encode_note(0, 0, effect_cmd, effect_param)


# --- Pattern building ---
# Each pattern: 64 rows x 4 channels x 4 bytes = 1024 bytes

def make_empty_pattern():
    return [empty_note()] * (64 * 4)


def set_note(pattern, row, channel, note_data):
    pattern[row * 4 + channel] = note_data


# Melody for Korobeiniki
# Each entry: (note_name, duration_in_rows)
# At speed 6, each row ~ 1 tick. We'll use 4 rows = 1 beat.
# Time signature: 4/4

melody_part1 = [
    # Bar 1: E B C D | C B A
    ('E-2', 4), ('B-1', 2), ('C-2', 2), ('D-2', 4), ('C-2', 2), ('B-1', 2),
    # Bar 2: A A C E | D C
    ('A-1', 2), ('A-1', 2), ('C-2', 2), ('E-2', 4), ('D-2', 2), ('C-2', 2),
    # Bar 3: B B C D | E
    ('B-1', 2), ('B-1', 2), ('C-2', 2), ('D-2', 4), ('E-2', 4),
    # Bar 4: C A A rest
    ('C-2', 4), ('A-1', 4), ('A-1', 4), (None, 4),
]

melody_part2 = [
    # Bar 5: D _ F A | G F
    ('D-2', 4), (None, 2), ('F-2', 2), ('A-2', 4), ('G-2', 2), ('F-2', 2),
    # Bar 6: E _ C E | D C
    ('E-2', 4), (None, 2), ('C-2', 2), ('E-2', 4), ('D-2', 2), ('C-2', 2),
    # Bar 7: B B C D | E
    ('B-1', 2), ('B-1', 2), ('C-2', 2), ('D-2', 4), ('E-2', 4),
    # Bar 8: C A A rest
    ('C-2', 4), ('A-1', 4), ('A-1', 4), (None, 4),
]

# Bass patterns (root notes at octave 1, each note held for 8 rows)
bass_part1 = [
    ('A-1', 8), ('A-1', 8), ('C-2', 8), ('C-2', 8),
    ('G#1', 8), ('G#1', 8), ('A-1', 8), ('A-1', 8),
]

bass_part2 = [
    ('D-1', 8), ('D-1', 8), ('A-1', 8), ('A-1', 8),
    ('E-1', 8), ('E-1', 8), ('A-1', 8), ('A-1', 8),
]

# Variation bass (bridge section)
bass_bridge = [
    ('F-1', 8), ('F-1', 8), ('C-2', 8), ('C-2', 8),
    ('D-1', 8), ('D-1', 8), ('E-1', 8), ('E-1', 8),
]

# Bridge melody - variation
melody_bridge = [
    # Ascending pattern
    ('A-1', 2), ('C-2', 2), ('E-2', 2), ('A-2', 6), ('G-2', 2), ('E-2', 2),
    ('F-2', 4), ('E-2', 2), ('D-2', 2), ('C-2', 4), ('D-2', 2), ('E-2', 2),
    # Descending
    ('F-2', 4), ('E-2', 2), ('D-2', 2), ('C-2', 2), ('B-1', 2), ('A-1', 2), ('B-1', 2),
    ('C-2', 4), ('E-2', 4), ('A-1', 4), (None, 4),
]


def expand_melody(melody_data):
    """Expand melody to list of (row, note_name) tuples."""
    result = []
    row = 0
    for pitch, dur in melody_data:
        result.append((row, pitch))
        row += dur
    return result


def expand_bass(bass_data):
    """Expand bass line similarly."""
    result = []
    row = 0
    for pitch, dur in bass_data:
        result.append((row, pitch))
        row += dur
    return result


def add_drums(pattern):
    """Add drum pattern to channels 2 (kick) and 3 (hihat/snare)."""
    # Kick on beats 1 and 3: rows 0, 8, 16, 24, 32, 40, 48, 56
    kick_rows = [0, 8, 16, 24, 32, 40, 48, 56]
    # Snare on beats 2 and 4: rows 4, 12, 20, 28, 36, 44, 52, 60
    snare_rows = [4, 12, 20, 28, 36, 44, 52, 60]
    # Hihat on every even row not covered by kick
    hihat_rows = [r for r in range(0, 64, 2) if r not in kick_rows and r not in snare_rows]

    for r in kick_rows:
        set_note(pattern, r, 2, note(3, 'C-2'))
    for r in snare_rows:
        set_note(pattern, r, 2, note(5, 'C-2'))
    for r in hihat_rows:
        set_note(pattern, r, 3, note(4, 'C-3'))
    # Also hihat on some kick/snare rows in channel 3
    for r in kick_rows + snare_rows:
        if r % 4 == 0:
            set_note(pattern, r, 3, note(4, 'C-3'))


def build_pattern(melody_data, bass_data, set_speed=False):
    """Build a complete pattern with melody, bass, and drums."""
    pat = make_empty_pattern()

    # Set speed on first row
    if set_speed:
        set_note(pat, 0, 0, effect_only(0x0F, 6))  # Will be overwritten if melody starts at row 0

    # Melody on channel 0
    mel = expand_melody(melody_data)
    for row, pitch in mel:
        if pitch is not None:
            n = note(1, pitch)
            if set_speed and row == 0:
                n = note(1, pitch, 0x0F, 6)
            set_note(pat, row, 0, n)
        elif set_speed and row == 0:
            set_note(pat, row, 0, effect_only(0x0F, 6))

    # Bass on channel 1
    bass = expand_bass(bass_data)
    for row, pitch in bass:
        if pitch is not None:
            set_note(pat, row, 1, note(2, pitch))

    # Drums on channels 2-3
    add_drums(pat)

    return pat


# Build the 4 patterns
pattern0 = build_pattern(melody_part1, bass_part1, set_speed=True)
pattern1 = build_pattern(melody_part2, bass_part2)
pattern2 = build_pattern(melody_bridge, bass_bridge)
pattern3 = build_pattern(melody_part1, bass_part2)  # Main melody with second bass

patterns = [pattern0, pattern1, pattern2, pattern3]

# Song order
song_order = [0, 1, 0, 1, 2, 3, 0, 1]
song_length = len(song_order)
restart_position = 0

# --- Assemble the MOD file ---

mod_data = bytearray()

# Song title (20 bytes)
title = b'Korobeiniki'
mod_data += title.ljust(20, b'\x00')

# 31 instrument headers (30 bytes each)
for i in range(31):
    if i < len(instruments):
        name, sdata, vol, ls, ll = instruments[i]
        mod_data += make_instrument_header(name, sdata, vol, ls, ll)
    else:
        # Empty instrument
        mod_data += make_instrument_header("", b"", 0, 0, 0)

# Song length and restart position
mod_data += struct.pack('BB', song_length, restart_position)

# Pattern order table (128 bytes)
order_table = bytearray(128)
for i, p in enumerate(song_order):
    order_table[i] = p
mod_data += bytes(order_table)

# Magic identifier
mod_data += b'M.K.'

# Pattern data
num_patterns = max(song_order) + 1
for pi in range(num_patterns):
    pat = patterns[pi]
    for note_data in pat:
        mod_data += note_data

# Sample data
for i in range(len(instruments)):
    name, sdata, vol, ls, ll = instruments[i]
    mod_data += sdata

# Write file
with open(OUTPUT_FILE, 'wb') as f:
    f.write(mod_data)

# Print summary
file_size = len(mod_data)
print(f"Generated: {OUTPUT_FILE}")
print(f"File size: {file_size} bytes")
print(f"Song title: Korobeiniki")
print(f"Patterns: {num_patterns}")
print(f"Song length: {song_length} positions")
print(f"Song order: {song_order}")
print(f"Instruments:")
for i, (name, sdata, vol, ls, ll) in enumerate(instruments):
    loop_str = f"loop {ls}-{ls+ll}" if ll > 0 else "no loop"
    print(f"  {i+1}. {name}: {len(sdata)} bytes, vol={vol}, {loop_str}")
print("Done!")
