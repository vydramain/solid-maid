#!/usr/bin/env python3
"""Solidmaid — reclaim decay tails from the shipped one-shots.

tools/prep_audio.py trims at -50 dBFS ABSOLUTE, which is the right threshold for
cutting a recording down to its content: at -50 dB a struck object is still
ringing, and cutting there turns the strike into a click.

This is a second, narrower pass, and it asks a different question — not "where
does the recording end" but "where does this sound stop carrying information
against everything else playing over it". The threshold is therefore RELATIVE to
each sample's own peak, and it is -35 dB: a pipe swing whose last third sits 40
dB under its own transient is inaudible under a melody, a conveyor and a fight,
and in this game it is paid for at 86 KiB a second.

It exists because the factory has to hold two continuous loops on top of
everything else, and 80 KiB of inaudible tail is exactly the shortfall.

Operates IN PLACE on assets/snd/, on the one-shots only:

  * mus_*   are bars of a melody. Their length IS the tempo.
  * sfx_loop_* are continuous beds. Their length IS the period, and they must
    meet themselves — a trim would put a seam in the loop.

Idempotent: a file already inside the threshold is left byte-for-byte alone.
"""

import os
import struct
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
DISC = os.path.dirname(HERE)
SND = os.path.join(DISC, "assets", "snd")

RATE = 44100
# Below its own peak. See the module docstring for why this is not the -50 dBFS
# absolute figure prep_audio.py cuts at.
KEEP_DB = -35.0
TAIL_PAD = 0.020  # seconds kept after the last audible instant
FADE_OUT = 0.004  # a hard cut across a non-zero sample is itself a click
WINDOW = 220      # 5 ms, the resolution the decision is made at


def samples_of(path):
    with open(path, "rb") as f:
        raw = f.read()
    count = len(raw) // 2
    return list(struct.unpack("<%dh" % count, raw[: count * 2]))


def audible_end(x):
    """Last instant carrying more than KEEP_DB below the sample's own peak."""
    peak = max((abs(v) for v in x), default=0)
    if peak == 0:
        return len(x)
    threshold = peak * (10.0 ** (KEEP_DB / 20.0))
    last = 0
    for start in range(0, len(x) - WINDOW + 1, WINDOW):
        block = x[start:start + WINDOW]
        rms = (sum(float(v) * v for v in block) / WINDOW) ** 0.5
        if rms > threshold:
            last = start + WINDOW
    return last


def write_pair(name, x):
    raw = struct.pack("<%dh" % len(x), *x)
    with open(os.path.join(SND, name + ".pcm"), "wb") as f:
        f.write(raw)
    with wave.open(os.path.join(SND, name + ".wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(raw)


def main():
    if not os.path.isdir(SND):
        sys.exit("no assets/snd")

    names = sorted(f[:-4] for f in os.listdir(SND) if f.endswith(".pcm"))
    print(f"{'NAME':<26} {'WAS':>7} {'NOW':>7} {'SAVED':>9}")
    saved_total = 0
    for name in names:
        if name.startswith("mus_") or name.startswith("sfx_loop_"):
            continue
        path = os.path.join(SND, name + ".pcm")
        x = samples_of(path)
        if not x:
            continue

        end = audible_end(x) + int(TAIL_PAD * RATE)
        if end >= len(x):
            continue  # already inside the threshold; leave the bytes untouched

        y = x[:end]
        fade = int(FADE_OUT * RATE)
        for i in range(max(0, len(y) - fade), len(y)):
            k = (len(y) - i) / float(fade)
            y[i] = int(y[i] * k)

        saved = (len(x) - len(y)) * 2
        saved_total += saved
        write_pair(name, y)
        print(f"{name:<26} {len(x)/RATE:>6.3f}s {len(y)/RATE:>6.3f}s "
              f"{saved:>9d}")

    print(f"\nreclaimed {saved_total} bytes ({saved_total/1024:.1f} KiB)")


if __name__ == "__main__":
    main()
