# Generate a ProTracker MOD File

Procedurally generate a 4-channel ProTracker .mod file using a Python script. This creates chip-tune style music suitable for Amiga games.

## Arguments
- $ARGUMENTS: Description of the music (e.g., "uptempo space theme in C minor", "simple 120bpm bass loop"). If empty, ask what they want.

## How it works

Create and run a Python script that generates a binary .mod file. The script should follow the ProTracker format:

### ProTracker MOD Format Reference
```
Offset  Size    Description
0       20      Song title (padded with zeros)
20      30×31   31 sample headers (30 bytes each):
                  - 22 bytes: sample name
                  - 2 bytes: sample length in words (big-endian)
                  - 1 byte: finetune (0-15)
                  - 1 byte: volume (0-64)
                  - 2 bytes: repeat offset in words
                  - 2 bytes: repeat length in words (1 = no loop)
950     1       Song length (number of positions, 1-128)
951     1       Restart position (usually 127)
952     128     Pattern order table (which pattern to play at each position)
1080    4       Format tag: "M.K." (4-channel ProTracker)
1084    N×1024  Pattern data (64 rows × 4 channels × 4 bytes per note)
After patterns: Sample data (8-bit signed PCM, concatenated)
```

### Note encoding (4 bytes per note)
```
Byte 0: upper 4 bits of sample number | upper 4 bits of period
Byte 1: lower 8 bits of period
Byte 2: lower 4 bits of sample number | effect command upper nibble
Byte 3: effect parameter
```

### ProTracker Period Table (octave 1-3, notes C to B)
```python
PERIODS = [
    # C    C#   D    D#   E    F    F#   G    G#   A    A#   B
    856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,  # Octave 1
    428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,  # Octave 2
    214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113,  # Octave 3
]
```

### Sample generation helpers
```python
import math, struct

def gen_sine(length=64, volume=64):
    return bytes(int(math.sin(2 * math.pi * i / length) * volume) & 0xFF for i in range(length))

def gen_square(length=64, volume=48):
    return bytes((volume if i < length // 2 else -volume) & 0xFF for i in range(length))

def gen_noise(length=256, volume=40):
    import random; random.seed(42)
    return bytes(random.randint(-volume, volume) & 0xFF for i in range(length))

def gen_kick(length=128, volume=64):
    data = []
    for i in range(length):
        freq = 20 - (i * 18) / length
        env = max(0, 1 - i / length)
        data.append(int(math.sin(2 * math.pi * freq * i / length) * volume * env) & 0xFF)
    return bytes(data)
```

### Example script structure
See `examples/ace_pilot/gen_mod.py` or `examples/tetris/generate_mod.py` for complete working examples in this repo.

## Steps
1. Create a Python script at `examples/PROJECT/gen_mod.py`
2. Run it: `python3 examples/PROJECT/gen_mod.py`
3. Verify: `file examples/PROJECT/output.mod` (should say "4-channel Protracker module")
4. Deploy to AmiKit shared folder if requested

## Important
- All multi-byte values are BIG-ENDIAN (use `struct.pack('>H', value)`)
- Sample lengths in header are in WORDS (divide byte count by 2)
- Pattern data: 64 rows × 4 channels × 4 bytes = 1024 bytes per pattern
- Period 0 = no note (silence/continue)
- Sample number 0 = no sample change
- Effect F (speed/tempo): Fxx where xx < 0x20 = speed, >= 0x20 = BPM
