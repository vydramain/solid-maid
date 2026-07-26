#!/usr/bin/env python3
"""Solidmaid — write a save into a memory card image, by hand.

Reaching a late shift by playing takes ten minutes; reaching it to check one
frame of one screen should not. This writes the four fields the game actually
persists (src/sm_state.cpp, sm_save_encode) straight into slot 0 of an
.mppccard image.

    tools/set_save.py CARD --shifts 1              # start of the last shift
    tools/set_save.py CARD --shifts 0              # the final lap: БАНКРОТ
    tools/set_save.py CARD --shifts 3 --phase street
    tools/set_save.py CARD                         # just print what is there

The card layout comes from src/rv_pconsole/cm/rv_pccard.cpp: a 32-byte header,
then one 8-byte length per slot, then the slots themselves. The default geometry
is 16 slots of 8192 bytes, so slot 0's payload begins at 32 + 16*8 = 160.

The existing image is copied to CARD.bak before anything is written.
"""

import argparse
import os
import shutil
import struct
import sys

MAGIC = 0x534D4131  # "SMA1", src/sm_common.hpp
SAVE_BYTES = 16
HEADER = 32
LENGTH_ENTRY = 8

PHASES = {"home": 0, "street": 1, "factory": 2, "returning": 3, "final": 4}


def payload_offset(data):
    if data[:8] != b"MPPCCARD":
        sys.exit("not an .mppccard image")
    slot_count = struct.unpack_from("<q", data, 16)[0]
    return HEADER + slot_count * LENGTH_ENTRY, slot_count


def show(data, offset):
    magic, shifts, phase = struct.unpack_from("<Iii", data, offset)
    if magic != MAGIC:
        print("slot 0 holds no Solidmaid save")
        return
    name = next((k for k, v in PHASES.items() if v == phase), str(phase))
    print(f"shifts_remaining={shifts}  phase={name}  "
          f"assembly_step={data[offset + 12]}  finished={data[offset + 13]}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("card")
    ap.add_argument("--shifts", type=int,
                    help="5 is a fresh run, 1 is the last shift, 0 is the final lap")
    ap.add_argument("--phase", choices=sorted(PHASES), default="home")
    ap.add_argument("--step", type=int, default=0, help="assembly step 0..3")
    args = ap.parse_args()

    if not os.path.isfile(args.card):
        sys.exit(f"no such card: {args.card}")
    data = bytearray(open(args.card, "rb").read())
    offset, slots = payload_offset(data)

    if args.shifts is None:
        show(data, offset)
        return
    if slots < 1:
        sys.exit("card has no slots")

    shutil.copyfile(args.card, args.card + ".bak")
    struct.pack_into("<Iii", data, offset, MAGIC, args.shifts,
                     PHASES[args.phase])
    data[offset + 12] = args.step & 0xFF
    # `finished` is the flag for a run that has already walked the final lap.
    # Writing a save to play FROM, it is always false: setting it would put the
    # player past the ending rather than in front of it.
    data[offset + 13] = 0
    # The slot's recorded length. The card refuses to read a slot it believes is
    # empty, so a hand-written payload has to declare itself too.
    struct.pack_into("<q", data, HEADER, SAVE_BYTES)

    open(args.card, "wb").write(bytes(data))
    print(f"{args.card}  (backup at {args.card}.bak)")
    show(data, offset)


if __name__ == "__main__":
    main()
