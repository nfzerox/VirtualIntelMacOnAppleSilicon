#!/usr/bin/env python3
"""Inspect a QEMU mixer capture of qemu-audio-smoke's generated 440 Hz tone.

This is a digital continuity check, not a speaker/latency/perceptual audio test.
Only use an explicitly generated test signal, never microphone/user recordings.
"""
import argparse
import array
import json
import math
import sys
import wave

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('capture')
a = p.parse_args()
with wave.open(a.capture, 'rb') as w:
    if w.getsampwidth() != 2 or w.getcomptype() != 'NONE':
        p.error('requires uncompressed signed 16-bit PCM WAV')
    rate, channels = w.getframerate(), w.getnchannels()
    samples = array.array('h', w.readframes(w.getnframes()))
if sys.byteorder != 'little':
    samples.byteswap()
samples = samples[::channels]
peak = max(map(abs, samples), default=0)
active = [i for i, v in enumerate(samples) if abs(v) > max(10, peak * .02)]
if not active:
    p.error('no generated tone found')
interior = samples[active[0] + rate // 10:active[-1] - rate // 10]
if len(interior) < rate:
    p.error('requires at least a second of steady-state tone')
expected_delta = 2 * peak * math.sin(math.pi * 440 / rate)
threshold = max(peak * .2, expected_delta * 3)
jumps = [i for i in range(1, len(interior))
         if abs(interior[i] - interior[i - 1]) > threshold]
run = longest = 0
for value in interior:
    run = run + 1 if value == 0 else 0
    longest = max(longest, run)
print(json.dumps({
    'SampleRate': rate, 'Channels': channels,
    'DurationSeconds': len(samples) / rate, 'Peak': peak,
    'InteriorFrames': len(interior), 'LargeJumps': len(jumps),
    'MaximumDelta': max(abs(interior[i] - interior[i - 1])
                        for i in range(1, len(interior))),
    'LongestZeroRun': longest,
    'ContinuousToneCheck': not jumps and longest < rate // 1000,
    'SpeakerQualityQualified': False,
}, indent=2))
