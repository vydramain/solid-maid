#!/usr/bin/env python3
"""Solidmaid — font atlas generator.

Produces assets/tex/font.png: a 128x64 IDX4 atlas holding a 16x8 grid of 8x8
cells.  Every glyph is 5x7 ink placed at (1,0) inside its cell, which leaves two
empty columns on the right and one empty row at the bottom — that is the letter
spacing and the leading, baked into the cell so the text drawer never has to
know about kerning.

  cells   0..94   ASCII 32..126,  index = codepoint - 32   (cell 0 = space)
  cell    95      notdef, a hollow rectangle
  cells  96..127  Cyrillic uppercase A..YA, U+0410..U+042F,
                  index = 96 + (cp - 0x0410)

The glyph bitmaps below are hand-authored here, in this file, on purpose: no
system font is consulted, so the output is byte-identical on any machine.

`GLYPHS` is imported by gen_textures.py, which paints the same letterforms onto
the in-world signage.  That is why a handful of glyphs live in the table without
having a cell in the atlas (YO, the lowercase Cyrillic the atlas layout has no
room for): the signs need them, the 128-cell layout is fixed by the contract.

Run:  python3 tools/gen_font.py
"""

import os
import sys

import numpy as np
from PIL import Image

GLYPH_W = 5
GLYPH_H = 7
CELL = 8
COLS = 16
ROWS = 8

# The one ink colour, snapped to the console's 5-bit lattice so the baker's
# 8->5 bit reduction is a no-op.  Bone white rather than pure white: the board
# and the prompts are painted lettering, not emitted light.
INK = (247, 247, 239)

# --- glyph bitmaps ------------------------------------------------------------
#
# Seven rows of five cells each.  '#' is ink.  Industrial stencil: uniform
# 1-texel strokes, flat terminals, no optical correction — the shapes are cut,
# not drawn.

A_ = {
    " ": ("     ", "     ", "     ", "     ", "     ", "     ", "     "),
    "!": ("  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "     ", "  #  "),
    '"': (" # # ", " # # ", "     ", "     ", "     ", "     ", "     "),
    "#": (" # # ", " # # ", "#####", " # # ", "#####", " # # ", " # # "),
    "$": ("  #  ", " ####", "# #  ", " ### ", "  # #", "#### ", "  #  "),
    "%": ("##  #", "##  #", "   # ", "  #  ", " #   ", "#  ##", "#  ##"),
    "&": (" ##  ", "#  # ", "# #  ", " #   ", "# # #", "#  # ", " ## #"),
    "'": ("  #  ", "  #  ", "     ", "     ", "     ", "     ", "     "),
    "(": ("   # ", "  #  ", " #   ", " #   ", " #   ", "  #  ", "   # "),
    ")": (" #   ", "  #  ", "   # ", "   # ", "   # ", "  #  ", " #   "),
    "*": ("     ", "# # #", " ### ", "#####", " ### ", "# # #", "     "),
    "+": ("     ", "  #  ", "  #  ", "#####", "  #  ", "  #  ", "     "),
    ",": ("     ", "     ", "     ", "     ", " ##  ", "  #  ", " #   "),
    "-": ("     ", "     ", "     ", "#####", "     ", "     ", "     "),
    ".": ("     ", "     ", "     ", "     ", "     ", " ##  ", " ##  "),
    "/": ("    #", "   # ", "   # ", "  #  ", " #   ", " #   ", "#    "),
    "0": (" ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### "),
    "1": ("  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "),
    "2": (" ### ", "#   #", "    #", "   # ", "  #  ", " #   ", "#####"),
    "3": ("#####", "   # ", "  #  ", "   # ", "    #", "#   #", " ### "),
    "4": ("   # ", "  ## ", " # # ", "#  # ", "#####", "   # ", "   # "),
    "5": ("#####", "#    ", "#### ", "    #", "    #", "#   #", " ### "),
    "6": ("  ## ", " #   ", "#    ", "#### ", "#   #", "#   #", " ### "),
    "7": ("#####", "#   #", "    #", "   # ", "  #  ", "  #  ", "  #  "),
    "8": (" ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### "),
    "9": (" ### ", "#   #", "#   #", " ####", "    #", "   # ", " ##  "),
    ":": ("     ", " ##  ", " ##  ", "     ", " ##  ", " ##  ", "     "),
    ";": ("     ", " ##  ", " ##  ", "     ", " ##  ", "  #  ", " #   "),
    "<": ("   # ", "  #  ", " #   ", "#    ", " #   ", "  #  ", "   # "),
    "=": ("     ", "     ", "#####", "     ", "#####", "     ", "     "),
    ">": (" #   ", "  #  ", "   # ", "    #", "   # ", "  #  ", " #   "),
    "?": (" ### ", "#   #", "    #", "   # ", "  #  ", "     ", "  #  "),
    "@": (" ### ", "#   #", "    #", " ## #", "# # #", "# # #", " ####"),
    "A": (" ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"),
    "B": ("#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### "),
    "C": (" ### ", "#   #", "#    ", "#    ", "#    ", "#   #", " ### "),
    "D": ("###  ", "#  # ", "#   #", "#   #", "#   #", "#  # ", "###  "),
    "E": ("#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####"),
    "F": ("#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#    "),
    "G": (" ### ", "#   #", "#    ", "# ###", "#   #", "#   #", " ####"),
    "H": ("#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"),
    "I": (" ### ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "),
    "J": ("  ###", "   # ", "   # ", "   # ", "   # ", "#  # ", " ##  "),
    "K": ("#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #"),
    "L": ("#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####"),
    "M": ("#   #", "## ##", "# # #", "# # #", "#   #", "#   #", "#   #"),
    "N": ("#   #", "#   #", "##  #", "# # #", "#  ##", "#   #", "#   #"),
    "O": (" ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "),
    "P": ("#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    "),
    "Q": (" ### ", "#   #", "#   #", "#   #", "# # #", "#  # ", " ## #"),
    "R": ("#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #"),
    "S": (" ### ", "#   #", "#    ", " ### ", "    #", "#   #", " ### "),
    "T": ("#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "),
    "U": ("#   #", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "),
    "V": ("#   #", "#   #", "#   #", "#   #", "#   #", " # # ", "  #  "),
    "W": ("#   #", "#   #", "#   #", "# # #", "# # #", "## ##", "#   #"),
    "X": ("#   #", "#   #", " # # ", "  #  ", " # # ", "#   #", "#   #"),
    "Y": ("#   #", "#   #", " # # ", "  #  ", "  #  ", "  #  ", "  #  "),
    "Z": ("#####", "    #", "   # ", "  #  ", " #   ", "#    ", "#####"),
    "[": (" ### ", " #   ", " #   ", " #   ", " #   ", " #   ", " ### "),
    "\\": ("#    ", " #   ", " #   ", "  #  ", "   # ", "   # ", "    #"),
    "]": (" ### ", "   # ", "   # ", "   # ", "   # ", "   # ", " ### "),
    "^": ("  #  ", " # # ", "#   #", "     ", "     ", "     ", "     "),
    "_": ("     ", "     ", "     ", "     ", "     ", "     ", "#####"),
    "`": (" #   ", "  #  ", "     ", "     ", "     ", "     ", "     "),
    "a": ("     ", "     ", " ### ", "    #", " ####", "#   #", " ####"),
    "b": ("#    ", "#    ", "#### ", "#   #", "#   #", "#   #", "#### "),
    "c": ("     ", "     ", " ### ", "#   #", "#    ", "#   #", " ### "),
    "d": ("    #", "    #", " ####", "#   #", "#   #", "#   #", " ####"),
    "e": ("     ", "     ", " ### ", "#   #", "#####", "#    ", " ### "),
    "f": ("  ## ", " #  #", " #   ", "###  ", " #   ", " #   ", " #   "),
    "g": ("     ", " ####", "#   #", "#   #", " ####", "    #", " ### "),
    "h": ("#    ", "#    ", "#### ", "#   #", "#   #", "#   #", "#   #"),
    "i": ("  #  ", "     ", " ##  ", "  #  ", "  #  ", "  #  ", " ### "),
    "j": ("   # ", "     ", "  ## ", "   # ", "   # ", "#  # ", " ##  "),
    "k": ("#    ", "#    ", "#  # ", "# #  ", "##   ", "# #  ", "#  # "),
    "l": (" ##  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "),
    "m": ("     ", "     ", "## # ", "# # #", "# # #", "# # #", "# # #"),
    "n": ("     ", "     ", "#### ", "#   #", "#   #", "#   #", "#   #"),
    "o": ("     ", "     ", " ### ", "#   #", "#   #", "#   #", " ### "),
    "p": ("     ", "#### ", "#   #", "#   #", "#### ", "#    ", "#    "),
    "q": ("     ", " ####", "#   #", "#   #", " ####", "    #", "    #"),
    "r": ("     ", "     ", "# ## ", "##  #", "#    ", "#    ", "#    "),
    "s": ("     ", "     ", " ####", "#    ", " ### ", "    #", "#### "),
    "t": (" #   ", " #   ", "###  ", " #   ", " #   ", " #  #", "  ## "),
    "u": ("     ", "     ", "#   #", "#   #", "#   #", "#   #", " ####"),
    "v": ("     ", "     ", "#   #", "#   #", "#   #", " # # ", "  #  "),
    "w": ("     ", "     ", "#   #", "#   #", "# # #", "# # #", " # # "),
    "x": ("     ", "     ", "#   #", " # # ", "  #  ", " # # ", "#   #"),
    "y": ("     ", "     ", "#   #", "#   #", " ####", "    #", " ### "),
    "z": ("     ", "     ", "#####", "   # ", "  #  ", " #   ", "#####"),
    "{": ("   ##", "  #  ", "  #  ", " #   ", "  #  ", "  #  ", "   ##"),
    "|": ("  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "),
    "}": ("##   ", "  #  ", "  #  ", "   # ", "  #  ", "  #  ", "##   "),
    "~": ("     ", "     ", " #  #", "# # #", "#  # ", "     ", "     "),
    # notdef — deliberately a shape no letter has, so a missing codepoint is
    # visible as a missing codepoint rather than as a plausible letter.
    "�": ("#####", "#   #", "#   #", "#   #", "#   #", "#   #", "#####"),
}

# Cyrillic uppercase.  These are the reason this font exists: the board must
# spell OSTALOS' and PLAN VYPOLNEN, at a few texels tall, on a factory wall.
C_ = {
    "А": (" ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"),  # A
    "Б": ("#####", "#    ", "#    ", "#### ", "#   #", "#   #", "#### "),  # BE
    "В": ("#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### "),  # VE
    "Г": ("#####", "#    ", "#    ", "#    ", "#    ", "#    ", "#    "),  # GHE
    "Д": ("  ###", "  # #", "  # #", " #  #", " #  #", "#####", "#   #"),  # DE
    "Е": ("#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####"),  # IE
    "Ж": ("# # #", "# # #", "#####", "  #  ", "#####", "# # #", "# # #"),  # ZHE
    "З": (" ### ", "#   #", "    #", "  ## ", "    #", "#   #", " ### "),  # ZE
    "И": ("#   #", "#   #", "#  ##", "# # #", "##  #", "#   #", "#   #"),  # I
    "Й": (" ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", "#   #"),  # SHORT I
    "К": ("#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #"),  # KA
    "Л": ("  ###", " #  #", " #  #", " #  #", " #  #", " #  #", "#   #"),  # EL
    "М": ("#   #", "## ##", "# # #", "# # #", "#   #", "#   #", "#   #"),  # EM
    "Н": ("#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"),  # EN
    "О": (" ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "),  # O
    "П": ("#####", "#   #", "#   #", "#   #", "#   #", "#   #", "#   #"),  # PE
    "Р": ("#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    "),  # ER
    "С": (" ### ", "#   #", "#    ", "#    ", "#    ", "#   #", " ### "),  # ES
    "Т": ("#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "),  # TE
    "У": ("#   #", "#   #", "#   #", " ####", "    #", "#   #", " ### "),  # U
    "Ф": ("  #  ", " ### ", "# # #", "# # #", "# # #", " ### ", "  #  "),  # EF
    "Х": ("#   #", "#   #", " # # ", "  #  ", " # # ", "#   #", "#   #"),  # HA
    "Ц": ("#  # ", "#  # ", "#  # ", "#  # ", "#  # ", "#####", "    #"),  # TSE
    "Ч": ("#   #", "#   #", "#   #", " ####", "    #", "    #", "    #"),  # CHE
    "Ш": ("# # #", "# # #", "# # #", "# # #", "# # #", "# # #", "#####"),  # SHA
    "Щ": ("# # #", "# # #", "# # #", "# # #", "# # #", "#####", "    #"),  # SHCHA
    "Ъ": ("##   ", " #   ", " #   ", " ### ", " #  #", " #  #", " ### "),  # HARD SIGN
    "Ы": ("#   #", "#   #", "#   #", "##  #", "# # #", "# # #", "##  #"),  # YERU
    "Ь": ("#    ", "#    ", "#    ", "#### ", "#   #", "#   #", "#### "),  # SOFT SIGN
    "Э": (" ### ", "#   #", "    #", "  ###", "    #", "#   #", " ### "),  # E
    "Ю": ("#  # ", "# # #", "# # #", "### #", "# # #", "# # #", "#  # "),  # YU
    "Я": (" ####", "#   #", "#   #", " ####", "  # #", " #  #", "#   #"),  # YA
    # Not in the atlas (the 128-cell layout has no room) but needed by the
    # in-world signs, which gen_textures.py paints from this same table.
    "Ё": ("# #  ", "#####", "#    ", "#### ", "#    ", "#    ", "#####"),  # YO
}

GLYPHS = dict(A_)
GLYPHS.update(C_)

NOTDEF = GLYPHS["�"]


def glyph(ch):
    """Rows for `ch`, falling back to the notdef box."""
    return GLYPHS.get(ch, NOTDEF)


def text_width(s, advance=6):
    return max(0, len(s) * advance - (advance - GLYPH_W))


def blit_text(rgba, x, y, s, colour, advance=6):
    """Paint `s` into an HxWx4 uint8 array at (x, y).  Used by gen_textures."""
    for i, ch in enumerate(s):
        rows = glyph(ch)
        gx = x + i * advance
        for ry, row in enumerate(rows):
            py = y + ry
            if py < 0 or py >= rgba.shape[0]:
                continue
            for rx, cell in enumerate(row):
                if cell != "#":
                    continue
                px = gx + rx
                if 0 <= px < rgba.shape[1]:
                    rgba[py, px] = (colour[0], colour[1], colour[2], 255)


# --- the atlas ----------------------------------------------------------------


def cell_origin(index):
    return (index % COLS) * CELL, (index // COLS) * CELL


def build():
    img = np.zeros((ROWS * CELL, COLS * CELL, 4), dtype=np.uint8)

    def put(index, rows):
        ox, oy = cell_origin(index)
        for ry, row in enumerate(rows):
            for rx, c in enumerate(row):
                if c == "#":
                    img[oy + ry, ox + 1 + rx] = (INK[0], INK[1], INK[2], 255)

    # cells 0..94 — ASCII 32..126
    for cp in range(32, 127):
        put(cp - 32, glyph(chr(cp)))
    # cell 95 — notdef
    put(95, NOTDEF)
    # cells 96..127 — U+0410..U+042F
    for i in range(32):
        put(96 + i, glyph(chr(0x0410 + i)))

    return img


# --- checks -------------------------------------------------------------------


def check(img):
    problems = []
    h, w = img.shape[:2]
    if (w, h) != (COLS * CELL, ROWS * CELL):
        problems.append(f"font.png is {w}x{h}, expected 128x64")

    flat = img.reshape(-1, 4)
    opaque = flat[flat[:, 3] >= 128]
    holes = int((flat[:, 3] < 128).sum())
    uniq = {tuple(int(v) for v in c) for c in np.unique(opaque[:, :3], axis=0)}
    budget = 15 if holes else 16
    if len(uniq) > budget:
        problems.append(f"font.png has {len(uniq)} opaque colours, budget {budget}")

    for c in uniq:
        if c[0] < 10 or c[1] < 10 or c[2] < 12:
            problems.append(f"font.png has an opaque colour darker than (10,10,12): {c}")

    # Every cell that must carry a glyph carries one, and space stays blank.
    def cell_ink(index):
        ox, oy = cell_origin(index)
        return int((img[oy:oy + CELL, ox:ox + CELL, 3] >= 128).sum())

    if cell_ink(0) != 0:
        problems.append("cell 0 (space) is not blank")
    for cp in range(33, 127):
        if cell_ink(cp - 32) == 0:
            problems.append(f"ASCII {cp} ({chr(cp)!r}) cell {cp - 32} is blank")
    if cell_ink(95) == 0:
        problems.append("notdef cell 95 is blank")

    # The Cyrillic letters are the point.  Present, and no two alike.
    seen = {}
    for i in range(32):
        cp = 0x0410 + i
        idx = 96 + i
        if cell_ink(idx) == 0:
            problems.append(f"U+{cp:04X} ({chr(cp)}) cell {idx} is blank")
        ox, oy = cell_origin(idx)
        key = (img[oy:oy + CELL, ox:ox + CELL, 3] >= 128).tobytes()
        if key in seen:
            problems.append(
                f"U+{cp:04X} ({chr(cp)}) is identical to U+{seen[key]:04X} ({chr(seen[key])})"
            )
        seen[key] = cp

    # And the two strings the game must actually spell.
    for word in ("ОСТАЛОСЬ:",
                 "ПЛАН ВЫПОЛНЕН"):
        for ch in word:
            if ch == " ":
                continue
            cp = ord(ch)
            idx = (96 + cp - 0x0410) if cp >= 0x0410 else (cp - 32)
            if cell_ink(idx) == 0:
                problems.append(f"{word!r} needs {ch!r}, cell {idx} is blank")

    return problems, len(uniq), holes > 0


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_dir = os.path.join(root, "assets", "tex")
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, "font.png")

    img = build()
    problems, uniq, has_alpha = check(img)
    if problems:
        for p in problems:
            print("FAIL:", p, file=sys.stderr)
        return 1

    Image.fromarray(img, "RGBA").save(out, optimize=True)
    print(f"font.png  128x64  colours={uniq}  alpha={has_alpha}  -> {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
