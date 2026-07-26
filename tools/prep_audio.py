#!/usr/bin/env python3
"""Solidmaid — audio preparation.

Takes the raw recordings in assets/sound/ and produces console-shaped,
level-matched, trimmed working files in assets/snd/.

Four things happen here, and each one is forced by the hardware rather than by
taste:

  * RESAMPLE to 44 100 Hz mono. The console has no resampler and no stereo
    voice: a sample plays at whatever rate the mixer runs at, so a file at any
    other rate plays back at the wrong speed and pitch, and a stereo file is
    simply the wrong shape. A voice is mono and is placed with volume_l /
    volume_r. (docs/art-and-audio.md; pdk/ca/rv_sample.hpp.)

  * TRIM to the sound. Sound RAM is 512 KiB and one second costs 86.1 KiB, so
    silence is charged at exactly the same rate as music. The design document is
    blunt about it: "Duration is a hard spec, not a target... Trim at the source
    rather than fading, since a fade still costs full bytes."

  * LEVEL MATCH by peak. Almost everything here is a short transient, and
    loudness metering (EBU R128) is meaningless below about three seconds — it
    would happily push a 0.1 s click to the same integrated loudness as a
    two-second drone and blow its peak off. So one-shots are peak-normalised to
    a common ceiling and the beds are set a fixed amount below them.

  * NEVER REACH 0 dBFS. Voices sum in int32 and CLIP at the int16 rails without
    being divided by voice count (specs.md), so several sounds landing on one
    frame add up. The ceiling below is the headroom that keeps that from turning
    into grit.

Originals in assets/sound/ are never modified.
"""

import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DISC = os.path.dirname(HERE)
SRC = os.path.join(DISC, "assets", "sound")
OUT = os.path.join(DISC, "assets", "snd")

# One-shots land here; the beds land BED_BELOW decibels under them. -2 dBFS is
# the ceiling asked for, and 6 dB is exactly half the amplitude.
PEAK_TARGET = -2.0
BED_BELOW = 6.0

RATE = 44100
BYTES_PER_SECOND = RATE * 2  # 16-bit mono
# rv_ca::sound_memory_size, from src/rv_pconsole/rv_pconsole_conf.hpp. Raised
# from the PSX-faithful 512 KiB so the recorded set fits without shortening any
# audible sound; docs/platform/specs.md carries the same figure.
POOL_BYTES = 1024 * 1024

# The melody is played as consecutive one-second bars on a clock, so only TWO
# are ever in sound RAM — the one sounding and the one being loaded behind it.
# The rest sit on the medium, which has no size ceiling at all (specs.md).
MUSIC_BARS_RESIDENT = 2

# Trimming. The threshold is deliberately lower than one would use for
# MEASURING content: at -50 dB a decay tail is still audible, and cutting it
# turns a struck object into a click. The pads keep the attack intact and let
# the tail breathe; the fade exists because a hard cut across a non-zero sample
# is itself a click.
SILENCE_DB = -50
HEAD_PAD = 0.008   # seconds kept before the first sound
TAIL_PAD = 0.050   # seconds kept after the last sound
FADE_OUT = 0.004   # seconds of fade at the very end


def stderr_of(args):
    return subprocess.run(args, capture_output=True, text=True).stderr


def probe_duration(path):
    out = subprocess.run(
        ["ffprobe", "-v", "error", "-show_entries", "format=duration",
         "-of", "csv=p=0", path], capture_output=True, text=True).stdout.strip()
    return float(out) if out else 0.0


def probe_peak(path):
    vd = stderr_of(["ffmpeg", "-i", path, "-af", "volumedetect", "-f", "null", "-"])
    m = re.search(r"max_volume: ([-\d.]+)", vd)
    return float(m.group(1)) if m else None


def sound_span(path, duration):
    """First and last instant carrying signal, in seconds.

    Built from the COMPLEMENT of what silencedetect reports rather than from its
    first and last event, so a sound with a gap in the middle — two knocks, a
    step and its scuff — is measured end to end instead of being cut in half.
    """
    sd = stderr_of(["ffmpeg", "-i", path,
                    "-af", f"silencedetect=noise={SILENCE_DB}dB:d=0.02",
                    "-f", "null", "-"])
    starts = [float(v) for v in re.findall(r"silence_start: ([-\d.]+)", sd)]
    ends = [float(v) for v in re.findall(r"silence_end: ([-\d.]+)", sd)]

    intervals = []
    si, ei = 0, 0
    if ends and (not starts or ends[0] < starts[0]):
        intervals.append((0.0, ends[0]))
        ei = 1
    while si < len(starts):
        a = starts[si]
        b = ends[ei] if ei < len(ends) else duration
        intervals.append((a, b))
        si += 1
        ei += 1

    first = 0.0
    for a, b in intervals:
        if a <= first + 1e-4:
            first = max(first, b)
        else:
            break
    last = duration
    for a, b in reversed(intervals):
        if b >= last - 1e-4:
            last = min(last, a)
        else:
            break
    if last <= first:
        return 0.0, 0.0
    return first, last


def convert(src, dst, gain_db, start, length, fade):
    # The fade exists because a hard cut across a non-zero sample is a click.
    # A musical bar is NOT faded: it butts against the next one, and a fade
    # would put a dip in the middle of the melody every second.
    filters = f"volume={gain_db:.2f}dB"
    if fade:
        filters += f",afade=t=out:st={max(0.0, length - FADE_OUT):.4f}:d={FADE_OUT}"
    subprocess.run(
        ["ffmpeg", "-y", "-v", "error", "-ss", f"{start:.4f}", "-i", src,
         "-t", f"{length:.4f}", "-af", filters,
         "-ac", "1", "-ar", str(RATE), "-c:a", "pcm_s16le", dst],
        check=True)


# ── the names ────────────────────────────────────────────────────────────────
#
# ASCII, lowercase, no spaces. Not cosmetic: rv_cd resolves a resource BY NAME
# with no path separators and the burner flattens every asset into one namespace
# (docs/platform/disc-loading.md), so a name has to survive a zip, a filesystem
# and a C string. The prefixes match docs/art-and-audio.md so a sample can be
# found from the design document.
RENAME = {
    "бросок кирпича.ogg": "sfx_brick_throw",
    "кирпич о стене.ogg": "sfx_brick_hit_hard",
    "кирпич по телу (хитмаркер).ogg": "sfx_brick_hit_soft",
    "взмах трубы.ogg": "sfx_pipe_swing",
    "хитмаркер трубы (попадание).ogg": "sfx_pipe_hit",
    "подбор предмета.ogg": "sfx_pickup",
    "урон.ogg": "sfx_player_hurt",
    "смерть игрока.ogg": "sfx_player_death",
    "нокдаун врага.ogg": "sfx_enemy_down",
    "Кипучка взмах.ogg": "sfx_kipuchka_windup",
    "Кипучка шаг.ogg": "sfx_kipuchka_step",
    "Дымарь предупреждение.ogg": "sfx_smoker_prewarm",
    "Дмыарь атака.ogg": "sfx_smoker_attack",   # the source name is a typo
    "прерывание сборки.ogg": "sfx_assembly_break",
    "board clack.ogg": "sfx_board_clack",
    # REAPER read the slash in this item's name as a path separator and
    # buried it in a directory of its own; copied out flat.
    "тик взаимодействия.ogg": "sfx_ui_prompt",

    # Two variants per surface. With no pitch control on this console, identical
    # samples fired on one frame sum in phase and read as ONE loud step rather
    # than as several — variety has to be authored (docs/art-and-audio.md).
    "шаги дома_1.ogg": "sfx_step_lino_a",
    "шаги дома_2.ogg": "sfx_step_lino_b",
    "шаг на улице_1.ogg": "sfx_step_asphalt_a",
    "шаг на улице_2.ogg": "sfx_step_asphalt_b",
    "шаг на заводе_1.ogg": "sfx_step_concrete_a",
    "шаг на заводе_2.ogg": "sfx_step_concrete_b",

    "улица_1-001.ogg": "mus_street_01",
    "улица_1-002.ogg": "mus_street_02",
    "улица_2.ogg": "mus_street_03",
    "улица_3.ogg": "mus_street_04",
    "улица_4.ogg": "mus_street_05",
}
for i in range(1, 11):
    RENAME[f"дом_{i}.ogg"] = f"mus_home_{i:02d}"
for i in range(1, 4):
    RENAME[f"завод_{i}.ogg"] = f"mus_factory_{i:02d}"


def is_bed(name):
    return name.startswith("mus_")


def area_of(name):
    if name.startswith("mus_home"):
        return "home"
    if name.startswith("mus_street"):
        return "street"
    if name.startswith("mus_factory"):
        return "factory"
    return "sfx"


def main():
    if not os.path.isdir(SRC):
        sys.exit(f"no source directory: {SRC}")
    os.makedirs(OUT, exist_ok=True)

    rows, unmapped = [], []
    for src_name in sorted(os.listdir(SRC)):
        if not src_name.lower().endswith((".ogg", ".wav", ".flac", ".mp3")):
            continue
        name = RENAME.get(src_name)
        if name is None:
            unmapped.append(src_name)
            continue

        src = os.path.join(SRC, src_name)
        duration = probe_duration(src)
        peak = probe_peak(src)

        if is_bed(name):
            # MUSIC IS NEVER TRIMMED. These are consecutive bars of one melody,
            # played back to back on a clock, so a bar's LENGTH is the tempo —
            # trimming a quiet tail out of bar 3 does not save memory worth
            # having, it shortens that bar and the melody limps from there on.
            # Every one of them is exactly 1.000 s, which is exactly 60 frames
            # at 60 fps, and that is what makes the sequencing land.
            start, length, dead = 0.0, duration, False
        elif True:
            first, last = sound_span(src, duration)
            if last <= first:
                # Nothing above the threshold anywhere. Convert it whole so the
                # file exists and the failure is visible rather than absent.
                start, length, dead = 0.0, duration, True
            else:
                start = max(0.0, first - HEAD_PAD)
                end = min(duration, last + TAIL_PAD)
                length, dead = end - start, False

        target = PEAK_TARGET - (BED_BELOW if is_bed(name) else 0.0)
        gain = (target - peak) if peak is not None else 0.0

        dst = os.path.join(OUT, name + ".wav")
        convert(src, dst, gain, start, length, fade=not is_bed(name))

        # The console takes NAKED SAMPLE BYTES: rv_ca::sound_asset_write is
        # handed an rv_sample that is a pointer and a length, and there is no
        # audio baker in the toolchain to strip a container at pack time (the
        # burner only bakes PNGs). So the shipped form is headerless raw S16LE.
        # The .wav beside it is the editable one — same audio, 44 bytes of RIFF
        # header more.
        raw = os.path.join(OUT, name + ".pcm")
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", dst,
                        "-f", "s16le", "-acodec", "pcm_s16le", raw], check=True)

        rows.append({
            "name": name, "was": duration, "now": length,
            "bytes": os.path.getsize(raw), "peak": peak,
            "gain": gain, "dead": dead, "area": area_of(name),
        })

    rows.sort(key=lambda r: (r["area"] != "sfx", r["name"]))

    print(f"{'NAME':<24} {'WAS':>6} {'NOW':>6} {'SAVED':>6} {'KiB':>7} "
          f"{'WAS dB':>7} {'GAIN':>7}")
    for r in rows:
        flag = "  <-- NO SIGNAL" if r["dead"] else ""
        print(f"{r['name']:<24} {r['was']:>5.2f}s {r['now']:>5.2f}s "
              f"{r['was'] - r['now']:>5.2f}s {r['bytes']/1024:>7.1f} "
              f"{r['peak'] if r['peak'] is not None else 0:>7.1f} "
              f"{r['gain']:>+7.1f}{flag}")

    by_area = {}
    for r in rows:
        by_area[r["area"]] = by_area.get(r["area"], 0) + r["bytes"]

    sfx = by_area.get("sfx", 0)
    bar = max((r["bytes"] for r in rows if r["area"] != "sfx"), default=0)
    music_resident = bar * MUSIC_BARS_RESIDENT
    on_medium = sum(v for k, v in by_area.items() if k != "sfx")

    print(f"\n{'RESIDENT BUDGET':<28}{'KiB':>9}   of {POOL_BYTES/1024:.0f} KiB")
    print(f"{'  every sfx, loaded at boot':<28}{sfx/1024:>9.1f}")
    print(f"{'  music: 2 bars streaming':<28}{music_resident/1024:>9.1f}")
    simple = sfx + music_resident
    print(f"{'  -> all at once':<28}{simple/1024:>9.1f}   "
          f"{'FITS, %.1f spare' % ((POOL_BYTES - simple)/1024) if simple <= POOL_BYTES else 'OVER by %.1f' % ((simple - POOL_BYTES)/1024)}")

    print(f"\n{'  on the medium (all bars)':<28}{on_medium/1024:>9.1f}   "
          f"never resident; the disc has no size ceiling")

    if unmapped:
        print("\nnot renamed (no mapping):")
        for u in unmapped:
            print("  " + u)


if __name__ == "__main__":
    main()
