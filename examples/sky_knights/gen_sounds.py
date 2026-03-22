#!/usr/bin/env python3
"""Generate raw 8-bit signed PCM sound effects for Joust (11025 Hz mono)"""
import struct
import math
import random
import os

RATE = 11025

def generate_flap():
    """Wing flap - breathy whoosh with flutter modulation"""
    duration = 0.15
    n = int(RATE * duration)
    if n & 1: n += 1  # Paula needs even length

    samples = []
    random.seed(42)

    # Pre-generate noise and smooth it
    raw_noise = [random.uniform(-1, 1) for _ in range(n)]
    # Simple low-pass: average with neighbors for breathy quality
    smooth = [0.0] * n
    for i in range(n):
        total = raw_noise[i]
        count = 1
        if i > 0:
            total += raw_noise[i-1]
            count += 1
        if i < n - 1:
            total += raw_noise[i+1]
            count += 1
        smooth[i] = total / count

    for i in range(n):
        t = i / RATE
        # Amplitude envelope: fast attack, moderate decay
        env = math.exp(-t * 18) * min(1.0, t * 300)
        # Flutter modulation (~30 Hz wing beat)
        flutter = 0.4 + 0.6 * abs(math.sin(2 * math.pi * 30 * t))
        # Mix smoothed noise with slight tonal whoosh
        whoosh = math.sin(2 * math.pi * 180 * t * (1 - t * 2)) * 0.3
        val = (smooth[i] * 0.7 + whoosh) * env * flutter * 120
        val = max(-127, min(127, int(val)))
        samples.append(val)

    return samples

def generate_smash():
    """Bird squash - sharp impact thud with crunchy splat decay"""
    duration = 0.2
    n = int(RATE * duration)
    if n & 1: n += 1

    samples = []
    random.seed(99)

    for i in range(n):
        t = i / RATE
        # Sharp attack, moderate decay
        env = math.exp(-t * 12) * min(1.0, t * 500)
        # Low thud component (impact)
        thud = math.sin(2 * math.pi * 70 * t) * math.exp(-t * 30)
        # Mid crunch (distorted noise)
        noise = random.uniform(-1, 1)
        crunch = max(-0.7, min(0.7, noise * 1.8))
        # High crackle that decays faster
        crackle = random.uniform(-1, 1) * math.exp(-t * 25)
        # Mix: heavy thud + crunch + crackle
        val = (thud * 0.45 + crunch * 0.35 + crackle * 0.2) * env * 125
        val = max(-127, min(127, int(val)))
        samples.append(val)

    return samples

def save_raw(filename, samples):
    with open(filename, 'wb') as f:
        for s in samples:
            f.write(struct.pack('b', s))
    print(f"  {filename}: {len(samples)} bytes")

if __name__ == '__main__':
    outdir = os.path.dirname(os.path.abspath(__file__))
    print("Generating Joust sound effects (8-bit signed, 11025 Hz)...")
    save_raw(os.path.join(outdir, 'flap.raw'), generate_flap())
    save_raw(os.path.join(outdir, 'smash.raw'), generate_smash())
    print("Done.")
