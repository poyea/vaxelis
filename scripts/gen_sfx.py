#!/usr/bin/env python3
"""Generate small placeholder SFX as 16-bit PCM mono WAV files.

Synthesized, royalty-free cues for the platformer demo. Run from the repo root:
    python scripts/gen_sfx.py
Outputs runtime/assets/audio/{jump,squash,level-complete,death,win-game}.wav
matching the cue names passed to Platformer::play_cue.
"""
import math
import os
import struct
import wave

RATE = 44100
OUT = os.path.join("runtime", "assets", "audio")


def write_wav(name, samples):
    path = os.path.join(OUT, name)
    with wave.open(path, "w") as w:
        w.setnchannels(1)
        w.setsampwidth(2)  # 16-bit
        w.setframerate(RATE)
        frames = bytearray()
        for s in samples:
            v = max(-1.0, min(1.0, s))
            frames += struct.pack("<h", int(v * 32767))
        w.writeframes(bytes(frames))
    print(f"wrote {path} ({len(samples)} samples, {len(samples) / RATE:.3f}s)")


def env(i, n, attack=0.01, release=0.25):
    t = i / n
    a = min(1.0, t / attack) if attack > 0 else 1.0
    r = min(1.0, (1.0 - t) / release) if release > 0 else 1.0
    return a * r


def tone(freq, dur, vol=0.5, attack=0.01, release=0.3, square=False):
    n = int(RATE * dur)
    out = []
    for i in range(n):
        ph = 2 * math.pi * freq * (i / RATE)
        s = (1.0 if math.sin(ph) >= 0 else -1.0) if square else math.sin(ph)
        out.append(s * vol * env(i, n, attack, release))
    return out


def sweep(f0, f1, dur, vol=0.5, square=True):
    n = int(RATE * dur)
    out = []
    for i in range(n):
        t = i / n
        f = f0 + (f1 - f0) * t
        ph = 2 * math.pi * f * (i / RATE)
        s = (1.0 if math.sin(ph) >= 0 else -1.0) if square else math.sin(ph)
        out.append(s * vol * env(i, n, 0.01, 0.2))
    return out


def main():
    os.makedirs(OUT, exist_ok=True)

    # jump: short rising square sweep
    write_wav("jump.wav", sweep(300, 900, 0.18, vol=0.45, square=True))

    # squash (enemy stomp / pickup): bright two-tone blip
    squash = tone(988, 0.06, vol=0.4, release=0.4) + tone(1319, 0.12, vol=0.4, release=0.5)
    write_wav("squash.wav", squash)

    # level-complete: ascending 3-note flourish (E5 G5 C6)
    lvl = []
    for f in (659, 784, 1047):
        lvl += tone(f, 0.12, vol=0.4, release=0.4)
    write_wav("level-complete.wav", lvl)

    # death: short descending tone
    write_wav("death.wav", sweep(500, 120, 0.22, vol=0.45, square=True))

    # win-game: happy ascending jingle (C5 E5 G5 C6 + hold)
    win = []
    for f in (523, 659, 784, 1047):
        win += tone(f, 0.12, vol=0.4, release=0.4)
    win += tone(1047, 0.24, vol=0.4, release=0.6)
    write_wav("win-game.wav", win)


if __name__ == "__main__":
    main()
