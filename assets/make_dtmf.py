#!/usr/bin/env python3
"""Generate the local keypad tones: assets/dtmf/<key>.wav, 8 kHz mono, 150 ms each.

Each key is the standard DTMF pair (row frequency + column frequency), so it sounds like a phone keypad.
Generated here, so the files have no third-party audio. Run: python3 assets/make_dtmf.py
"""
import math
import os
import struct
import wave

RATE = 8000
DURATION = 0.15
AMP = 0.25
KEYS = {
    "1": (697, 1209), "2": (697, 1336), "3": (697, 1477),
    "4": (770, 1209), "5": (770, 1336), "6": (770, 1477),
    "7": (852, 1209), "8": (852, 1336), "9": (852, 1477),
    "*": (941, 1209), "0": (941, 1336), "#": (941, 1477),
}
NAMES = {"*": "star", "#": "hash"}

here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "dtmf")
os.makedirs(here, exist_ok=True)
n = int(RATE * DURATION)
for key, (low, high) in KEYS.items():
    frames = []
    for i in range(n):
        t = i / RATE
        # short fade in and out, so the tone does not click
        env = min(1.0, t / 0.01, (DURATION - t) / 0.01)
        v = AMP * env * (math.sin(2 * math.pi * low * t) + math.sin(2 * math.pi * high * t))
        frames.append(struct.pack("<h", int(max(-1.0, min(1.0, v)) * 32767)))
    name = NAMES.get(key, key)
    with wave.open(os.path.join(here, f"{name}.wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(frames))
print(f"wrote {len(KEYS)} tones to {here}")
