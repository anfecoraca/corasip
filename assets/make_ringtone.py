#!/usr/bin/env python3
"""Generate assets/ringtone.wav: a two-second ring pattern (two tones, then silence), 8 kHz mono.

The tone is generated here, so the file has no third-party audio. Run: python3 assets/make_ringtone.py
"""
import math
import os
import struct
import wave

RATE = 8000
AMP = 0.3
PATTERN = [(440.0, 0.4), (0.0, 0.2), (440.0, 0.4), (0.0, 1.0)]  # (Hz, seconds); total 2.0 s

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ringtone.wav")
samples = []
t = 0
for hz, seconds in PATTERN:
    for _ in range(int(RATE * seconds)):
        v = AMP * math.sin(2 * math.pi * hz * t / RATE) if hz > 0 else 0.0
        samples.append(int(v * 32767))
        t += 1
with wave.open(out, "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(RATE)
    w.writeframes(b"".join(struct.pack("<h", s) for s in samples))
print(f"wrote {out} ({len(samples) / RATE:.1f} s)")
