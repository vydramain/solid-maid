#!/usr/bin/env python3
"""Solidmaid — texture atlas generator.

Paints all 24 non-font PNGs into assets/tex/ (font.png has its own script,
tools/gen_font.py, because its layout is a different kind of contract).

Everything here is authored to the console, not to a monitor:

  * Every atlas declares a palette of at most 16 colours and every paint call is
    checked against it, so the IDX4 baker never has to throw a colour away.
  * Colours are snapped to the 5-bit lattice the console stores, so the baker's
    8->5 bit reduction is a no-op and nothing shifts under us.
  * The darkest opaque colour stays above RGB(10,10,12): 0000h is the hole, and
    an opaque black would punch a window through a wall.
  * There is no alpha blending on this console.  Every soft edge — the light
    pool, the smoke, the vignette — is an ordered-dither stipple, on purpose.
  * Tiling surfaces are fully opaque.  Only cut-out sprites carry alpha.

The region layout is not repeated here: it is read out of src/sm_atlas.hpp at
run time and checked against what was painted, so a region cannot go blank or
drift without this script failing.

Run:  python3 tools/gen_textures.py
"""

import os
import random
import re
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_font import blit_text, text_width  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "assets", "tex")
ATLAS_HPP = os.path.join(ROOT, "src", "sm_atlas.hpp")

MIN_OPAQUE = (10, 10, 12)


def c5(r, g, b):
    """A colour on the console's 5-bit lattice, expressed in 8-bit."""
    return (int(r * 255 / 31 + 0.5), int(g * 255 / 31 + 0.5), int(b * 255 / 31 + 0.5))


# Ordered dither matrices.  The house style: gradients are visible patterns.
# The two sentinels a colour argument can carry instead of a colour: None means
# "leave this texel alone" (so a dither can lay one tone over whatever is
# underneath), HOLE means "cut this texel out".
HOLE = "hole"

B4 = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]])
B8 = np.array([
    [0, 32, 8, 40, 2, 34, 10, 42], [48, 16, 56, 24, 50, 18, 58, 26],
    [12, 44, 4, 36, 14, 46, 6, 38], [60, 28, 52, 20, 62, 30, 54, 22],
    [3, 35, 11, 43, 1, 33, 9, 41], [51, 19, 59, 27, 49, 17, 57, 25],
    [15, 47, 7, 39, 13, 45, 5, 37], [63, 31, 55, 23, 61, 29, 53, 21],
])


class Pad:
    """A rectangular window onto an atlas, painted in local coordinates.

    Every colour argument is either a member of the atlas palette or None,
    which means "the hole".  The membership check is the whole point: a typo'd
    colour is caught here rather than by the baker silently median-cutting the
    image down to sixteen colours of its own choosing.
    """

    def __init__(self, view, palette, name):
        self.a = view
        self.h, self.w = view.shape[:2]
        self.pal = palette
        self.name = name

    def _c(self, c):
        if c is HOLE:
            return (0, 0, 0, 0)
        if c not in self.pal:
            raise ValueError(f"{self.name}: colour {c} is not in the atlas palette")
        return (c[0], c[1], c[2], 255)

    # --- primitives -----------------------------------------------------------

    def px(self, x, y, c):
        if c is None:  # "leave whatever is already there"
            return
        if 0 <= x < self.w and 0 <= y < self.h:
            self.a[y, x] = self._c(c)

    def fill(self, c):
        self.a[:, :] = self._c(c)

    def rect(self, x, y, w, h, c):
        if c is None:
            return
        x0, y0 = max(0, x), max(0, y)
        x1, y1 = min(self.w, x + w), min(self.h, y + h)
        if x1 > x0 and y1 > y0:
            self.a[y0:y1, x0:x1] = self._c(c)

    def frame(self, x, y, w, h, c):
        self.rect(x, y, w, 1, c)
        self.rect(x, y + h - 1, w, 1, c)
        self.rect(x, y, 1, h, c)
        self.rect(x + w - 1, y, 1, h, c)

    def hline(self, x, y, w, c):
        self.rect(x, y, w, 1, c)

    def vline(self, x, y, h, c):
        self.rect(x, y, 1, h, c)

    def line(self, x0, y0, x1, y1, c):
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        while True:
            self.px(x0, y0, c)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def ellipse(self, cx, cy, rx, ry, c, filled=True, inner=0.0):
        for y in range(max(0, cy - ry), min(self.h, cy + ry + 1)):
            for x in range(max(0, cx - rx), min(self.w, cx + rx + 1)):
                d = ((x - cx) / max(rx, 0.5)) ** 2 + ((y - cy) / max(ry, 0.5)) ** 2
                if filled:
                    if inner <= d <= 1.0:
                        self.px(x, y, c)
                elif 0.72 <= d <= 1.0:
                    self.px(x, y, c)

    def disc(self, cx, cy, r, c, filled=True):
        self.ellipse(cx, cy, r, r, c, filled)

    # --- dither ---------------------------------------------------------------

    def dith(self, x, y, w, h, c0, c1, t, mat=B4):
        """Ordered stipple: `t` (0..1) of c1 mixed into c0 over the rect."""
        self.dithf(x, y, w, h, c0, c1, lambda u, v: t, mat)

    def dithf(self, x, y, w, h, c0, c1, fn, mat=B4):
        n = mat.shape[0]
        scale = float(n * n)
        for py in range(max(0, y), min(self.h, y + h)):
            for px_ in range(max(0, x), min(self.w, x + w)):
                t = fn(px_ - x, py - y)
                if t <= 0.0:
                    c = c0
                elif t >= 1.0:
                    c = c1
                else:
                    c = c1 if t * scale > mat[py % n, px_ % n] else c0
                self.px(px_, py, c)

    def ramp(self, x, y, w, h, tones, vertical=True, mat=B8):
        """A dithered gradient through a list of tones, ends included."""
        n = len(tones) - 1
        for py in range(y, y + h):
            for px_ in range(x, x + w):
                s = ((py - y) / max(h - 1, 1)) if vertical else ((px_ - x) / max(w - 1, 1))
                f = s * n
                i = min(int(f), n - 1) if n else 0
                t = f - i
                lo, hi = tones[i], tones[min(i + 1, n)]
                m = mat.shape[0]
                c = hi if t * (m * m) > mat[py % m, px_ % m] else lo
                self.px(px_, py, c)

    # --- noise ----------------------------------------------------------------

    def spray(self, x, y, w, h, c, prob, seed):
        rnd = random.Random(seed)
        for py in range(y, y + h):
            for px_ in range(x, x + w):
                if rnd.random() < prob:
                    self.px(px_, py, c)

    def speckle(self, x, y, w, h, pairs, seed):
        """pairs: [(probability, colour), ...] evaluated in order, per texel."""
        rnd = random.Random(seed)
        for py in range(y, y + h):
            for px_ in range(x, x + w):
                r = rnd.random()
                acc = 0.0
                for p, c in pairs:
                    acc += p
                    if r < acc:
                        self.px(px_, py, c)
                        break

    def scatter(self, x, y, w, h, c, count, seed, size=1):
        rnd = random.Random(seed)
        for _ in range(count):
            sx = rnd.randrange(x, x + max(1, w))
            sy = rnd.randrange(y, y + max(1, h))
            self.rect(sx, sy, size, size, c)

    # --- lettering ------------------------------------------------------------

    def text(self, x, y, s, c, adv=6):
        self._c(c)
        blit_text(self.a, x, y, s, c, adv)

    def ctext(self, y, s, c, adv=6):
        """Centred lettering — most signage is centred, and eyeballing the
        left margin of five different strings is how a sign ends up crooked."""
        self.text((self.w - text_width(s, adv)) // 2, y, s, c, adv)


class Atlas:
    def __init__(self, name, w, h, palette, alpha):
        assert len(palette) <= (15 if alpha else 16), f"{name}: palette too large"
        self.name = name
        self.w, self.h = w, h
        self.palette = set(palette)
        self.alpha = alpha
        self.img = np.zeros((h, w, 4), dtype=np.uint8)
        if not alpha:
            self.img[:, :, 3] = 255
            self.img[:, :, :3] = palette[0]

    def pad(self, x, y, w, h):
        return Pad(self.img[y:y + h, x:x + w], self.palette, self.name)

    def whole(self):
        return self.pad(0, 0, self.w, self.h)

    def cell(self, cols, size, index):
        return self.pad((index % cols) * size, (index // cols) * size, size, size)

    def cell64(self, i):
        return self.cell(4, 64, i)

    def cell32(self, i):
        return self.cell(4, 32, i)

    def cell32x2(self, i):
        return self.cell(2, 32, i)


# ══════════════════════════════════════════════════════════════════════════════
# Palettes
#
# Two entries in this whole file are canon and must never be reconciled:
# streetlights are cold mercury GREEN-CYAN, apartment windows are warm YELLOW.
# They appear in several atlases and they keep the same values in all of them.
# ══════════════════════════════════════════════════════════════════════════════

MERCURY = c5(23, 31, 28)      # the RKU lantern's hard pale green-cyan
WINDOW_YELLOW = c5(31, 26, 13)  # a lit flat, and the Smokers' eyes

# ── home_walls ────────────────────────────────────────────────────────────────
HW = [
    c5(27, 25, 21),  # 0  wallpaper cream, as hung
    c5(29, 28, 25),  # 1  cream that never saw the sun (behind the television)
    c5(24, 22, 19),  # 2  cream gone grey with light
    c5(20, 18, 14),  # 3  cream in shadow
    c5(18, 14, 8),   # 4  ochre lozenge
    c5(12, 9, 5),    # 5  the lozenge's dark centre
    c5(25, 21, 11),  # 6  yellowed by the radiator
    c5(21, 19, 17),  # 7  bare plaster
    c5(14, 13, 12),  # 8  plaster crack
    c5(10, 13, 9),   # 9  hallway oil paint, dull green
    c5(13, 17, 12),  # 10 the same paint where hands polished it
    c5(28, 28, 27),  # 11 whitewash
    c5(23, 23, 22),  # 12 whitewash, unevenly laid
    c5(17, 10, 6),   # 13 the neighbour's leak
    c5(11, 7, 4),    # 14 skirting brown
    c5(4, 4, 5),     # 15 a nail
]

# ── home_floor ────────────────────────────────────────────────────────────────
HF = [
    c5(16, 11, 6),   # 0  linoleum, wood-imitation ground
    c5(20, 15, 9),   # 1  the light band of the print
    c5(12, 8, 4),    # 2  the dark band
    c5(9, 6, 3),     # 3  grain
    c5(15, 14, 13),  # 4  felt, where the print walked off
    c5(11, 10, 10),  # 5  felt, dirtier
    c5(28, 28, 26),  # 6  the chalk-white glass ring
    c5(6, 5, 4),     # 7  a compressed dent
    c5(18, 18, 18),  # 8  concrete
    c5(13, 13, 14),  # 9  concrete in shadow
    c5(23, 23, 23),  # 10 concrete nosing
    c5(11, 7, 4),    # 11 skirting brown
    c5(22, 19, 14),  # 12 crumbs
    c5(4, 4, 5),     # 13 the gap behind the skirting
    c5(22, 17, 11),  # 14 lino highlight
    c5(14, 10, 6),   # 15 lino seam
]

# ── home_furniture ────────────────────────────────────────────────────────────
HU = [
    c5(12, 8, 5),    # 0  dark cased wood
    c5(18, 12, 7),   # 1  veneer
    c5(24, 17, 10),  # 2  honey veneer, polished
    c5(28, 26, 22),  # 3  doily / lace / crockery cream
    c5(6, 7, 8),     # 4  the inside of things
    c5(11, 13, 12),  # 5  a dark convex screen
    c5(22, 26, 28),  # 6  the screen alight, and cut crystal
    c5(13, 4, 5),    # 7  the wall carpet's red
    c5(22, 15, 6),   # 8  its ochre figure
    c5(17, 17, 18),  # 9  grey blanket
    c5(27, 27, 26),  # 10 pillow
    c5(12, 12, 13),  # 11 painted metal
    c5(23, 22, 19),  # 12 the radiator, silver-white over many coats
    c5(8, 11, 8),    # 13 a dark green
    c5(19, 9, 7),    # 14 rust, and the calendar's mountain shadow
]

# ── home_window ───────────────────────────────────────────────────────────────
HN = [
    c5(28, 28, 27),  # 0  window paint, white
    c5(21, 21, 21),  # 1  the same paint, dirty
    c5(10, 13, 18),  # 2  the older blue coat, showing through the chips
    c5(6, 7, 9),     # 3  frame shadow
    c5(20, 21, 23),  # 4  daylight: flat, cold, grey
    c5(12, 13, 17),  # 5  dusk
    c5(6, 7, 11),    # 6  night
    MERCURY,         # 7  the courtyard lamppost, seen from indoors
    WINDOW_YELLOW,   # 8  a neighbour's window
    c5(21, 16, 6),   # 9  a further one
    c5(15, 20, 17),  # 10 three-litre jars
    c5(17, 17, 16),  # 11 a tin can
    c5(9, 15, 8),    # 12 the aloe
    c5(24, 24, 22),  # 13 a folded newspaper, and cotton wool
    c5(19, 14, 9),   # 14 the painted sill
]

# ── home_door ─────────────────────────────────────────────────────────────────
HD = [
    c5(26, 25, 22),  # 0  door paint
    c5(20, 19, 16),  # 1  its shadow
    c5(14, 13, 11),  # 2  a recessed panel
    c5(6, 6, 7),     # 3  outline
    c5(13, 8, 5),    # 4  dermatin
    c5(18, 12, 8),   # 5  dermatin, where it catches the light
    c5(8, 5, 3),     # 6  dermatin, in the quilting
    c5(23, 18, 8),   # 7  an upholstery stud
    c5(19, 19, 18),  # 8  a handle
    c5(11, 11, 11),  # 9  its shadow
    c5(10, 13, 9),   # 10 the kitchen door's green
    c5(14, 17, 12),  # 11 the same, polished by hands
    c5(29, 29, 27),  # 12 the switch plate
    c5(24, 23, 20),  # 13 the wiring channel
    c5(5, 5, 6),     # 14 a keyhole
    c5(31, 30, 28),  # 15 gloss
]

# ── street_facade ─────────────────────────────────────────────────────────────
SF = [
    c5(21, 21, 19),  # 0  pebble dash
    c5(25, 25, 23),  # 1  a pebble catching light
    c5(16, 16, 15),  # 2  a pebble in shadow
    c5(12, 12, 12),  # 3  damp, darkening unevenly
    c5(8, 8, 9),     # 4  a panel seam
    c5(17, 10, 5),   # 5  rust from a fixing
    c5(22, 14, 7),   # 6  fresher rust
    c5(28, 28, 27),  # 7  window frames, painted white
    c5(6, 7, 10),    # 8  a dark flat
    c5(9, 10, 14),   # 9  a dark flat with something in it
    WINDOW_YELLOW,   # 10 a lit flat — the last colour on the street
    c5(22, 17, 7),   # 11 a lit flat further off
    c5(19, 15, 10),  # 12 cardboard where a pane should be
    c5(13, 15, 18),  # 13 balcony glazing
    c5(6, 10, 20),   # 14 blue enamel, for the number plate and the street sign
    c5(18, 18, 18),  # 15 concrete
]

# ── street_ground ─────────────────────────────────────────────────────────────
SG = [
    c5(12, 12, 13),  # 0  asphalt
    c5(16, 16, 17),  # 1  asphalt, worn smooth
    c5(9, 9, 10),    # 2  asphalt, in a crack
    c5(7, 7, 8),     # 3  a darker patch repair
    c5(20, 20, 20),  # 4  grit
    c5(19, 19, 18),  # 5  kerb concrete
    c5(24, 24, 23),  # 6  the kerb's top face
    c5(13, 13, 13),  # 7  the kerb's shadow
    c5(6, 8, 11),    # 8  standing water
    c5(17, 19, 22),  # 9  the overcast, in the water
    MERCURY,         # 10 a lamp, in the water
    c5(11, 9, 6),    # 11 mud
    c5(15, 12, 8),   # 12 mud, drying
    c5(14, 14, 13),  # 13 cast iron
    c5(25, 25, 24),  # 14 crossing paint
    c5(20, 20, 19),  # 15 crossing paint, mostly gone
]

# ── street_props ──────────────────────────────────────────────────────────────
SP = [
    c5(18, 18, 17),  # 0  precast concrete
    c5(23, 23, 22),  # 1  its lit face
    c5(12, 12, 12),  # 2  its shaded face
    c5(16, 17, 17),  # 3  steel
    c5(8, 9, 9),     # 4  steel in shadow
    c5(24, 25, 25),  # 5  steel, wet
    c5(17, 10, 5),   # 6  rust
    c5(9, 13, 9),    # 7  garage green
    c5(15, 7, 5),    # 8  garage oxide red
    c5(8, 11, 16),   # 9  garage blue
    c5(15, 10, 6),   # 10 bench timber
    c5(19, 20, 21),  # 11 galvanised casing
    c5(13, 12, 10),  # 12 poplar bark
    c5(24, 23, 19),  # 13 the kiosk's cream
    c5(23, 7, 6),    # 14 a sticker
]

# ── street_lamp ───────────────────────────────────────────────────────────────
SL = [
    c5(19, 19, 18),  # 0  the pole
    c5(24, 24, 23),  # 1  the pole's lit side
    c5(13, 13, 13),  # 2  the pole's shaded side
    c5(10, 10, 10),  # 3  weather stain
    c5(15, 16, 16),  # 4  the bracket
    c5(7, 8, 8),     # 5  the bracket's underside
    c5(22, 23, 23),  # 6  the bracket's top edge
    c5(28, 31, 30),  # 7  the arc itself
    MERCURY,         # 8  the glass
    c5(13, 21, 19),  # 9  the glass, further from the arc
    c5(7, 10, 9),    # 10 dead glass
    c5(11, 14, 13),  # 11 dead glass, catching the sky
    c5(16, 10, 5),   # 12 rust on the fixings
    c5(25, 26, 26),  # 13 the bowl
    c5(5, 5, 6),     # 14 shadow
]

# ── light_pool ────────────────────────────────────────────────────────────────
LP = [
    c5(22, 28, 26),  # 0  asphalt directly under the lamp
    c5(17, 22, 21),  # 1
    c5(13, 17, 16),  # 2
    c5(10, 13, 13),  # 3  the last ring before the stipple gives out
]

# ── sky ───────────────────────────────────────────────────────────────────────
SK = [
    c5(7, 8, 12),    # 0  overhead
    c5(9, 10, 14),
    c5(11, 12, 16),
    c5(13, 14, 18),
    c5(15, 16, 19),
    c5(17, 18, 21),
    c5(19, 20, 22),
    c5(21, 22, 24),  # 7  the horizon
    c5(16, 15, 15),  # 8  a dirty band of cloud
    c5(19, 18, 18),  # 9
    c5(22, 22, 21),  # 10
    c5(24, 24, 23),  # 11 the brightest the sky ever gets, and it is not bright
]

# ── gate ──────────────────────────────────────────────────────────────────────
GT = [
    c5(22, 22, 21),  # 0  silicate brick
    c5(19, 19, 18),  # 1  a darker brick
    c5(15, 15, 15),  # 2  mortar
    c5(12, 12, 12),  # 3  brick, weathered
    c5(9, 14, 10),   # 4  the green door
    c5(13, 18, 13),  # 5  its lit edge
    c5(5, 7, 5),     # 6  its recesses
    WINDOW_YELLOW,   # 7  the checkpoint window, lit
    c5(21, 17, 8),   # 8  the same, further in
    c5(27, 27, 26),  # 9  window frame
    c5(15, 16, 16),  # 10 gate steel
    c5(8, 8, 9),     # 11 gate steel, shaded
    c5(22, 23, 23),  # 12 gate steel, lit
    c5(17, 10, 5),   # 13 rust
    c5(26, 26, 24),  # 14 a sign
]

# ── factory_walls ─────────────────────────────────────────────────────────────
FW = [
    c5(21, 21, 20),  # 0  silicate brick
    c5(18, 18, 17),  # 1  a darker brick
    c5(14, 14, 14),  # 2  mortar
    c5(27, 27, 26),  # 3  whitewash
    c5(22, 22, 21),  # 4  whitewash, flaking
    c5(9, 13, 10),   # 5  oil paint to 1.8 m
    c5(13, 17, 13),  # 6  the same, rubbed
    c5(6, 9, 7),     # 7  the painted line
    c5(17, 17, 17),  # 8  column concrete
    c5(22, 22, 22),  # 9  its lit face
    c5(11, 11, 11),  # 10 its chipped corner
    c5(28, 23, 6),   # 11 hazard yellow
    c5(4, 4, 5),     # 12 hazard black
    c5(19, 20, 20),  # 13 dull silver steel
    c5(10, 11, 11),  # 14 steel in shadow
    c5(24, 25, 25),  # 15 wired glass
]

# ── factory_floor ─────────────────────────────────────────────────────────────
FF = [
    c5(16, 16, 16),  # 0  poured concrete
    c5(20, 20, 20),  # 1  where it has been swept
    c5(12, 12, 12),  # 2  where it has not
    c5(9, 9, 10),    # 3
    c5(6, 6, 7),     # 4  oil
    c5(10, 10, 12),  # 5  oil, catching a fixture
    c5(28, 23, 6),   # 6  the yellow line — a navigation anchor, at every tier
    c5(21, 17, 6),   # 7  the yellow line, walked on
    c5(22, 22, 21),  # 8  swarf
    c5(15, 20, 17),  # 9  coolant
    c5(20, 25, 22),  # 10 coolant, thinner
    c5(13, 13, 13),  # 11 grit
    c5(24, 24, 24),  # 12 grit, pale
    c5(15, 9, 5),    # 13 rust
    c5(8, 9, 10),    # 14 water
    c5(7, 7, 8),     # 15 a crack
]

# ── factory_machines ──────────────────────────────────────────────────────────
FM = [
    c5(15, 16, 16),  # 0  machine steel
    c5(9, 10, 10),   # 1  its shadow
    c5(22, 23, 23),  # 2  its worn edges
    c5(5, 6, 6),     # 3  the dark inside a machine
    c5(9, 14, 12),   # 4  machine green
    c5(13, 18, 16),  # 5  machine green, lit
    c5(17, 13, 8),   # 6  pallet timber
    c5(11, 8, 5),    # 7  pallet timber, oiled
    c5(17, 10, 5),   # 8  rust
    c5(21, 6, 5),    # 9  a red handle, a cylinder cap
    c5(25, 25, 24),  # 10 a white cylinder
    c5(5, 5, 6),     # 11 a black one
    c5(12, 15, 9),   # 12 the welding screen's canvas
    c5(27, 27, 25),  # 13 enamel
    c5(8, 12, 19),   # 14 blue enamel
    c5(19, 15, 10),  # 15 cardboard
]

# ── factory_board ─────────────────────────────────────────────────────────────
FB = [
    c5(14, 16, 15),  # 0  the panel's painted steel
    c5(19, 21, 20),  # 1  where the backlight reaches it
    c5(9, 10, 10),   # 2  where it does not
    c5(17, 18, 18),  # 3  the frame
    c5(6, 7, 7),     # 4  the frame's shadow
    c5(24, 25, 20),  # 5  the dim bulbs behind the plastic
    c5(19, 20, 16),  # 6  the same, through more grime
    c5(15, 15, 12),  # 7  grime on the plastic strip
    c5(21, 6, 5),    # 8  a red header band
    c5(13, 4, 4),    # 9  the same, faded
    c5(27, 27, 25),  # 10 painted lettering
    c5(19, 17, 14),  # 11 a faded photograph
    c5(11, 10, 9),   # 12 the dark of one
    c5(12, 13, 12),  # 13 a ruled line
    c5(16, 10, 5),   # 14 rust
    c5(23, 18, 8),   # 15 ochre
]

# ── factory_signs ─────────────────────────────────────────────────────────────
FS = [
    c5(28, 28, 26),  # 0  enamelled sign white
    c5(23, 23, 21),  # 1  the same, dirty
    c5(23, 6, 5),    # 2  sign red
    c5(4, 4, 5),     # 3  sign black
    c5(15, 15, 14),  # 4  the wall behind
    c5(19, 19, 18),  # 5  the wall, lit
    c5(28, 23, 6),   # 6  sign yellow
    c5(24, 24, 21),  # 7  newsprint
    c5(17, 17, 15),  # 8  a column of type
    c5(19, 17, 13),  # 9  a photograph
    c5(10, 10, 9),   # 10 the dark of one
    c5(8, 12, 19),   # 11 a blue heading
    c5(9, 14, 10),   # 12 a green one
    c5(21, 21, 20),  # 13 a drawing pin
    c5(8, 8, 8),     # 14 shadow
    c5(20, 19, 16),  # 15 tape
]

# ── lamppost_parts ────────────────────────────────────────────────────────────
LC = [
    c5(13, 14, 14),  # 0  the bench top these lie on
    c5(8, 9, 9),     # 1  its shadow
    c5(20, 20, 19),  # 2  the concrete pole section
    c5(14, 14, 13),  # 3  its shaded side
    c5(25, 25, 24),  # 4  its lit side
    c5(17, 18, 18),  # 5  the steel bracket
    c5(9, 10, 10),   # 6  its underside
    c5(23, 24, 24),  # 7  its top edge
    c5(25, 26, 26),  # 8  the bowl
    c5(20, 28, 25),  # 9  the glass, unlit but still green
    c5(13, 19, 17),  # 10 the glass, deeper in
    c5(16, 10, 5),   # 11 rust on the fixings
    c5(22, 18, 8),   # 12 a brass bolt
    c5(5, 6, 6),     # 13 shadow
    c5(7, 7, 8),     # 14 cable
    c5(28, 28, 26),  # 15 a chalk mark on the bench
]

# ── kipuchka ──────────────────────────────────────────────────────────────────
KP = [
    c5(11, 15, 7),   # 0  the signature colour: swamp moss
    c5(16, 20, 10),  # 1  its lit side
    c5(6, 9, 4),     # 2  its shaded side
    c5(13, 11, 9),   # 3  rag
    c5(8, 7, 6),     # 4  rag, wet
    c5(20, 19, 17),  # 5  skin
    c5(14, 13, 12),  # 6  skin, shaded
    c5(4, 4, 5),     # 7  hair
    c5(28, 28, 26),  # 8  the whites of the eyes
    c5(5, 6, 4),     # 9  the pupils
    c5(18, 17, 14),  # 10 fingers
    c5(17, 5, 5),    # 11 the mouth
    c5(22, 25, 15),  # 12 a highlight
    c5(6, 7, 6),     # 13 shadow
    c5(15, 18, 14),  # 14 damp sheen
]

# ── smoker ────────────────────────────────────────────────────────────────────
SM = [
    c5(13, 13, 14),  # 0  tracksuit grey
    c5(18, 18, 19),  # 1  its lit side
    c5(8, 8, 9),     # 2  its shaded side
    c5(26, 26, 25),  # 3  the three stripes
    c5(20, 20, 20),  # 4  the stripes, on the shaded side
    c5(16, 15, 14),  # 5  face
    c5(11, 10, 10),  # 6  face, shaded
    c5(5, 5, 6),     # 7  hair, and the gap under the hood
    WINDOW_YELLOW,   # 8  the eyes — window-yellow, and protected
    c5(22, 18, 6),   # 9  the eyes, one tier down
    c5(21, 21, 20),  # 10 the smoke he is made of
    c5(6, 6, 7),     # 11 shoes
    c5(24, 9, 4),    # 12 the ember
    c5(9, 9, 10),    # 13 shadow
    c5(10, 10, 11),  # 14 a cap
]

# ── smoke ─────────────────────────────────────────────────────────────────────
SO = [
    c5(22, 22, 21),  # 0  cloud
    c5(17, 17, 17),  # 1  cloud, deeper
    c5(12, 12, 13),  # 2  cloud, deepest
    c5(26, 26, 25),  # 3  the lit edge of one
    c5(31, 29, 16),  # 4  the pre-warm ring — self-lit, untiered, unmistakable
    c5(29, 22, 8),   # 5  the ring's body
    c5(21, 14, 4),   # 6  the ring's outer stipple
]

# ── items ─────────────────────────────────────────────────────────────────────
IT = [
    c5(12, 6, 4),    # 0  brick — reworked from assets/projecttiles/brick_1.png
    c5(14, 9, 7),    # 1  brick, lit  — the same file's highlight
    c5(8, 4, 3),     # 2  brick, shaded
    c5(19, 12, 10),  # 3  a fresh chip — from brick_2.png
    c5(16, 17, 17),  # 4  the pipe
    c5(23, 24, 24),  # 5  the pipe, along its top
    c5(9, 10, 10),   # 6  the pipe, underneath
    c5(16, 10, 5),   # 7  rust
    c5(5, 6, 6),     # 8  the bore
    c5(20, 19, 17),  # 9  grit
    c5(21, 20, 18),  # 10 mortar still stuck to it
    c5(6, 5, 5),     # 11 shadow
    c5(14, 14, 13),  # 12 tape round the grip
    c5(27, 27, 26),  # 13 a highlight
    c5(11, 9, 7),    # 14 dirt
]

# ── hands ─── the palette is protagonist_tex.png's, unchanged ─────────────────
HA = [
    c5(24, 22, 19),  # 0  pale skin        (196,178,160) in protagonist_tex.png
    c5(19, 17, 15),  # 1  skin, mid        (158,138,122)
    c5(15, 12, 11),  # 2  skin, shaded
    c5(11, 9, 8),    # 3  skin, in a crease
    c5(27, 24, 22),  # 4  a nail
    c5(10, 7, 5),    # 5  the coat         (82,59,43)
    c5(13, 9, 7),    # 6  the coat, lit    (104,76,54)
    c5(9, 6, 5),     # 7  the coat, shaded (72,52,39)
    c5(7, 7, 11),    # 8  the overalls     (54,60,88)
    c5(5, 5, 8),     # 9  the overalls, shaded (38,43,64)
    c5(4, 4, 5),     # 10 outline
    c5(28, 26, 24),  # 11 a highlight
    c5(13, 11, 10),  # 12 grime
    c5(16, 12, 9),   # 13 a seam
    c5(17, 14, 13),  # 14 a knuckle
]

# ── hud — every entry here is a protected colour ──────────────────────────────
HH = [
    c5(31, 31, 30),  # 0  white
    c5(26, 26, 24),  # 1  bone
    c5(18, 18, 18),  # 2  grey
    c5(9, 9, 10),    # 3  dark
    c5(4, 4, 6),     # 4  outline
    WINDOW_YELLOW,   # 5  hot: over an interactable
    c5(23, 19, 6),   # 6  hot, one step down
    c5(27, 7, 6),    # 7  health
    c5(16, 4, 4),    # 8  health, spent
    c5(22, 30, 28),  # 9  mercury, for the pipe icon's cold steel
    c5(15, 25, 14),  # 10 the assembly bar
    c5(9, 15, 9),    # 11 the assembly bar's track
    c5(15, 8, 5),    # 12 the brick icon
    c5(20, 21, 21),  # 13 the pipe icon
    c5(5, 5, 7),     # 14 the low-HP vignette
]


# ══════════════════════════════════════════════════════════════════════════════
# Shared motifs
# ══════════════════════════════════════════════════════════════════════════════


def wallpaper(p, base, patt, dot, clip=None, ph=(0, 0)):
    """Paper, not vinyl: a small geometric lozenge on a half-drop repeat.

    The half-drop is what makes it read as wallpaper rather than as graph paper
    at 320x240, and it is why the seam cell can be visibly mismatched.
    """
    x0, y0, w, h = clip or (0, 0, p.w, p.h)
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w):
            u, v = x + ph[0], y + ph[1]
            cy = (v % 16) - 8
            cx = ((u + (8 if (v // 16) % 2 else 0)) % 16) - 8
            m = abs(cx) + abs(cy)
            p.px(x, y, dot if m <= 1 else (patt if m == 5 else base))


def brickwork(p, x, y, w, h, a, b, mortar, bw=16, bh=6, seed=1):
    """Courses, offset by half a brick each row."""
    rnd = random.Random(seed)
    p.rect(x, y, w, h, mortar)
    row = 0
    py = y
    while py < y + h:
        off = (bw // 2) if row % 2 else 0
        px_ = x - off
        while px_ < x + w:
            p.rect(px_, py, bw - 1, bh - 1, a if rnd.random() < 0.72 else b)
            px_ += bw
        py += bh
        row += 1


def pebbledash(p, x, y, w, h, base, light, dark, damp, seed=2):
    p.rect(x, y, w, h, base)
    p.speckle(x, y, w, h, [(0.22, light), (0.22, dark)], seed)
    # Damp does not fall evenly; it climbs from the bottom and pools under sills.
    p.dithf(x, y, w, h, None, damp,
            lambda u, v: max(0.0, min(0.55, (v - h * 0.45) / (h * 0.9))))


def planks(p, x, y, w, h, a, b, grain, bh=8, seed=3):
    rnd = random.Random(seed)
    for i, py in enumerate(range(y, y + h, bh)):
        p.rect(x, py, w, bh, a if i % 2 else b)
        for _ in range(w // 5):
            gx = rnd.randrange(x, x + w)
            gl = rnd.randrange(3, 9)
            p.rect(gx, py + rnd.randrange(1, max(2, bh - 1)), gl, 1, grain)
        p.hline(x, py, w, grain)


def corrugate(p, x, y, w, h, base, light, dark, pitch=4, vertical=True):
    for i in range(w if vertical else h):
        phase = i % pitch
        c = light if phase == 0 else (dark if phase == pitch - 2 else base)
        if vertical:
            p.vline(x + i, y, h, c)
        else:
            p.hline(x, y + i, w, c)


def rivets(p, x, y, w, h, c, step=6):
    for py in range(y, y + h, step):
        for px_ in range(x, x + w, step):
            p.px(px_, py, c)


# ══════════════════════════════════════════════════════════════════════════════
# home_walls — 256x256, 4x4 of 64.  Opaque: a hole here is a hole in a wall.
# ══════════════════════════════════════════════════════════════════════════════


def build_home_walls():
    A = Atlas("home_walls", 256, 256, HW, alpha=False)
    base, unfaded, faded, shadow = HW[0], HW[1], HW[2], HW[3]
    ochre, dotc, yellowed = HW[4], HW[5], HW[6]
    plaster, crack = HW[7], HW[8]
    green, gloss, white, white2 = HW[9], HW[10], HW[11], HW[12]
    rust, skirt, nail = HW[13], HW[14], HW[15]

    # 0 — the room's default paper.
    p = A.cell64(0)
    wallpaper(p, base, ochre, dotc)
    p.vline(31, 0, 64, shadow)          # one drop join, slightly proud
    p.vline(32, 0, 64, unfaded)

    # 1 — sun-bleached, the window wall.  Lighter AND lower contrast: the ochre
    #     is what the light takes first.
    p = A.cell64(1)
    wallpaper(p, unfaded, faded, shadow)
    p.dith(0, 0, 64, 64, None, unfaded, 0.12)

    # 2 — the lifting joint near the corner.  The pattern does not line up
    #     across it, which is the whole reason a seam is visible.
    p = A.cell64(2)
    wallpaper(p, base, ochre, dotc, clip=(0, 0, 40, 64))
    wallpaper(p, base, ochre, dotc, clip=(40, 0, 24, 64), ph=(5, 3))
    p.vline(39, 0, 64, shadow)
    p.vline(40, 0, 64, dotc)            # the curling edge's shadow
    p.vline(41, 0, 64, unfaded)         # and the paper's pale reverse
    for y in range(0, 64, 7):           # the lift is worse further up
        p.rect(38, y, 2, 3, shadow)

    # 3 — yellowed by the radiator underneath it: strongest at the bottom of
    #     the run, thinning as the heat spreads out into the room.
    p = A.cell64(3)
    wallpaper(p, base, ochre, dotc)
    p.dithf(0, 0, 64, 64, None, yellowed,
            lambda u, v: max(0.0, min(1.0, (v - 6) / 46.0))
            * (0.72 + 0.28 * (((u + v // 3) // 9) % 2)))
    p.dith(0, 52, 64, 12, None, HW[5], 0.12)         # scorched, right at the fins

    # 4 — where the television stood: the paper under it never faded.
    p = A.cell64(4)
    wallpaper(p, unfaded, faded, shadow)
    wallpaper(p, base, ochre, dotc, clip=(10, 16, 44, 30))
    p.frame(10, 16, 44, 30, shadow)
    p.dith(8, 46, 48, 4, None, shadow, 0.45)   # the fuzz of dust along its foot
    p.dith(8, 50, 48, 2, None, shadow, 0.18)

    # 5 — the carpet's outline, and the four nails that held it.
    p = A.cell64(5)
    wallpaper(p, unfaded, faded, shadow)
    wallpaper(p, base, ochre, dotc, clip=(5, 7, 54, 44))
    p.frame(5, 7, 54, 44, shadow)
    for nx, ny in ((7, 9), (56, 9), (7, 48), (56, 48)):
        p.rect(nx, ny, 2, 2, nail)
        p.px(nx + 2, ny + 2, shadow)

    # 6 — the calendar: a smaller rectangle and the glue that held it.
    p = A.cell64(6)
    wallpaper(p, unfaded, faded, shadow)
    wallpaper(p, base, ochre, dotc, clip=(19, 13, 26, 34))
    p.frame(19, 13, 26, 34, shadow)
    p.dith(17, 8, 30, 6, None, yellowed, 0.5)  # the smear where it was torn off
    p.dith(17, 47, 30, 5, None, yellowed, 0.35)

    # 7 — the clock: one nail, a pale disc.
    p = A.cell64(7)
    wallpaper(p, unfaded, faded, shadow)
    for y in range(64):
        for x in range(64):
            if (x - 32) ** 2 + (y - 34) ** 2 <= 15 ** 2:
                u, v = x, y
                cy = (v % 16) - 8
                cx = ((u + (8 if (v // 16) % 2 else 0)) % 16) - 8
                m = abs(cx) + abs(cy)
                p.px(x, y, dotc if m <= 1 else (ochre if m == 5 else base))
    p.disc(32, 34, 15, shadow, filled=False)
    p.rect(31, 16, 2, 2, nail)

    # 8 — state 5: the paper is gone.
    p = A.cell64(8)
    p.fill(plaster)
    p.speckle(0, 0, 64, 64, [(0.16, crack), (0.13, shadow)], 11)
    for sx, sy, dx, dy in ((6, 4, 18, 26), (40, 2, 10, 30), (22, 40, 26, 18), (2, 34, 14, 24)):
        p.line(sx, sy, sx + dx, sy + dy, crack)
        p.line(sx + 1, sy, sx + dx, sy + dy - 3, crack)
    for gx, gy, gw, gh in ((2, 2, 16, 12), (46, 44, 16, 18), (24, 6, 10, 8)):
        p.dith(gx, gy, gw, gh, None, yellowed, 0.4)   # dried paste
    p.dith(0, 56, 64, 8, None, crack, 0.3)

    # 9 — the hallway: oil paint to 1.5 m, whitewash above, a painted line.
    p = A.cell64(9)
    p.rect(0, 0, 64, 26, white)
    p.speckle(0, 0, 64, 26, [(0.18, white2)], 12)
    p.rect(0, 26, 64, 3, HW[5])
    p.rect(0, 29, 64, 35, green)
    p.speckle(0, 29, 64, 35, [(0.10, gloss)], 13)
    p.dith(38, 30, 22, 30, None, gloss, 0.55)   # polished by hands, by the switch
    p.hline(0, 29, 64, gloss)

    # 10 — ceiling whitewash, laid unevenly, by brush.
    p = A.cell64(10)
    p.fill(white)
    p.dithf(0, 0, 64, 64, None, white2,
            lambda u, v: 0.18 + 0.3 * (((u + 3 * (v // 5)) // 9) % 2))
    p.speckle(0, 0, 64, 64, [(0.05, faded)], 14)

    # 11 — the neighbour's leak, in the corner nearest the window.
    p = A.cell64(11)
    p.fill(white)
    p.dithf(0, 0, 64, 64, None, white2, lambda u, v: 0.2)
    for r, c, t in ((30, rust, 0.30), (22, rust, 0.65), (14, yellowed, 0.85), (7, rust, 1.0)):
        p.dithf(0, 0, 64, 64, None, c,
                lambda u, v, r=r, t=t: t if ((u - 8) ** 2 + (v - 6) ** 2) <= r * r else 0.0)
    p.dithf(0, 0, 64, 64, None, white2,
            lambda u, v: 0.5 if ((u - 8) ** 2 + (v - 6) ** 2) <= 4 else 0.0)

    # 12 — brown-painted skirting, one length of it coming away.
    p = A.cell64(12)
    p.fill(skirt)
    planks(p, 0, 0, 64, 64, skirt, HW[5], dotc, bh=16, seed=15)
    p.rect(0, 0, 64, 3, ochre)
    p.rect(0, 3, 64, 2, skirt)
    p.rect(0, 58, 64, 6, HW[5])
    p.hline(0, 63, 64, nail)
    for x in range(34, 64):             # the length that has let go of the wall
        p.vline(x, 0, 1 + (x - 34) // 8, nail)
    p.dith(34, 2, 30, 4, None, nail, 0.4)

    # 13 — clean pale wall where the wardrobe stood, and the coat's nail.
    p = A.cell64(13)
    wallpaper(p, unfaded, faded, shadow)
    p.dith(0, 0, 64, 64, None, unfaded, 0.25)
    p.rect(30, 12, 2, 3, nail)
    p.px(32, 15, shadow)
    p.dith(26, 16, 11, 8, None, shadow, 0.28)   # the shadow the coat leaves
    p.hline(0, 60, 64, shadow)                  # a dark band, never washed

    # 14, 15 — spare paper.  Not blank: an unused cell that is flat is one bad
    # UV away from being a flat grey wall in the game.
    p = A.cell64(14)
    wallpaper(p, base, ochre, dotc, ph=(8, 8))
    p = A.cell64(15)
    p.fill(plaster)
    p.speckle(0, 0, 64, 64, [(0.2, crack), (0.1, shadow)], 16)
    return A


# ══════════════════════════════════════════════════════════════════════════════
# home_floor — 128x128, 4x4 of 32.  Opaque.
# ══════════════════════════════════════════════════════════════════════════════


def lino(p, seed=20):
    """Wood-imitation print, laid in one sheet: bands, not planks."""
    p.fill(HF[0])
    rnd = random.Random(seed)
    for py in range(0, 32, 8):
        p.rect(0, py, 32, 4, HF[1])
        p.rect(0, py + 4, 32, 4, HF[2])
        for _ in range(10):
            gx = rnd.randrange(0, 32)
            p.rect(gx, py + rnd.randrange(0, 8), rnd.randrange(2, 7), 1, HF[3])
        p.hline(0, py, 32, HF[15])
    p.speckle(0, 0, 32, 32, [(0.06, HF[14])], seed + 1)


def build_home_floor():
    A = Atlas("home_floor", 128, 128, HF, alpha=False)

    p = A.cell32(0)
    lino(p, 20)

    # 1 — the walking line, through to grey felt.
    p = A.cell32(1)
    lino(p, 22)
    p.dithf(0, 0, 32, 32, None, HF[4], lambda u, v: max(0.0, 1.0 - abs(u - 15) / 11.0))
    p.dithf(0, 0, 32, 32, None, HF[5], lambda u, v: max(0.0, 1.0 - abs(u - 15) / 5.0))
    p.rect(13, 0, 6, 32, HF[4])
    p.speckle(13, 0, 6, 32, [(0.25, HF[5])], 23)

    # 2 — four dark compressed dents, where the wardrobe's feet stood.
    p = A.cell32(2)
    lino(p, 24)
    for dx, dy in ((5, 6), (23, 6), (5, 22), (23, 22)):
        p.rect(dx, dy, 4, 3, HF[7])
        p.frame(dx - 1, dy - 1, 6, 5, HF[2])

    # 3 — the chalk-white ring the glass always left.
    p = A.cell32(3)
    lino(p, 26)
    p.ellipse(16, 15, 9, 8, HF[6], filled=False)
    p.ellipse(16, 15, 8, 7, HF[6], filled=False)
    p.dith(8, 8, 17, 15, None, HF[6], 0.10)
    p.scatter(4, 22, 24, 8, HF[12], 14, 27)

    # 4 — two long depressions, where the bed stood.
    p = A.cell32(4)
    lino(p, 28)
    for bx in (6, 21):
        p.rect(bx, 0, 5, 32, HF[2])
        p.vline(bx, 0, 32, HF[3])
        p.vline(bx + 4, 0, 32, HF[15])
        p.dith(bx + 1, 0, 3, 32, None, HF[3], 0.3)

    # 5 — the stairwell's concrete step.
    p = A.cell32(5)
    p.fill(HF[8])
    p.speckle(0, 0, 32, 32, [(0.18, HF[9]), (0.12, HF[10])], 29)
    p.rect(0, 0, 32, 4, HF[10])
    p.hline(0, 4, 32, HF[9])
    for cx in (7, 19, 27):                      # chipped nosing
        p.rect(cx, 0, 3, 2, HF[9])
    p.dith(0, 26, 32, 6, None, HF[9], 0.4)

    # 6, 7 — lino variants so a tessellated floor is not one repeating stamp.
    lino(A.cell32(6), 30)
    p = A.cell32(7)
    lino(p, 31)
    p.dith(0, 0, 32, 32, None, HF[4], 0.12)

    # 8 — the skirting seen from the floor's atlas, for the base of the wall.
    p = A.cell32(8)
    p.fill(HF[11])
    planks(p, 0, 0, 32, 32, HF[11], HF[2], HF[3], bh=10, seed=32)
    p.rect(0, 0, 32, 2, HF[1])
    p.rect(0, 29, 32, 3, HF[13])
    for i in range(9, 32):
        p.vline(i, 0, 1 + (i - 9) // 6, HF[13])

    # 9..15 — the remaining cells carry floor material rather than nothing.
    for i, s in zip(range(9, 16), range(40, 47)):
        p = A.cell32(i)
        lino(p, s)
        if i % 2:
            p.dith(0, 0, 32, 32, None, HF[2], 0.15)
    return A


# ══════════════════════════════════════════════════════════════════════════════
# home_furniture — 256x256, 4x4 of 64.
#
# Each cell is a flat front elevation of one object: the meshes are boxes and
# quads, so a picture of the thing is also the material for the thing.  Only the
# chandelier is a cut-out, because it hangs in air.
# ══════════════════════════════════════════════════════════════════════════════


def build_home_furniture():
    A = Atlas("home_furniture", 256, 256, HU, alpha=True)
    dark, veneer, honey, cream = HU[0], HU[1], HU[2], HU[3]
    inside, screen, lit = HU[4], HU[5], HU[6]
    red, ochre, blanket, pillow = HU[7], HU[8], HU[9], HU[10]
    metal, silver, green, rust = HU[11], HU[12], HU[13], HU[14]

    def television(p, on):
        p.fill(inside)
        p.rect(2, 8, 60, 50, veneer)                 # the wooden case
        p.rect(2, 8, 60, 2, honey)
        p.rect(2, 56, 60, 2, dark)
        p.rect(5, 12, 41, 40, dark)                  # the bezel
        for y in range(13, 52):                      # a convex screen: corners cut
            k = 3 if y in (13, 14, 50, 51) else (2 if y in (15, 49) else 0)
            p.rect(6 + k, y, 39 - 2 * k, 1, lit if on else screen)
        if on:
            for y in range(13, 52, 2):
                p.dith(6, y, 39, 1, None, cream, 0.35)
            p.dith(12, 18, 26, 20, None, cream, 0.5)
        else:
            p.dith(8, 15, 14, 12, None, blanket, 0.35)   # the room, reflected
        p.rect(48, 12, 12, 40, dark)                 # the control cheek
        for ky in (18, 30, 42):
            p.disc(53, ky, 3, metal)
            p.px(53, ky - 1, cream)
        p.rect(0, 0, 64, 8, cream)                   # the crocheted doily on top
        for x in range(0, 64, 4):
            p.rect(x, 5, 2, 3, veneer)
            p.px(x + 2, 3, veneer)
        p.rect(10, 58, 6, 6, dark)                   # feet
        p.rect(48, 58, 6, 6, dark)

    television(A.cell64(0), False)
    television(A.cell64(1), True)

    # 2 — the wardrobe-and-sideboard unit, honey-brown, with a glazed section.
    p = A.cell64(2)
    p.fill(veneer)
    p.speckle(0, 0, 64, 64, [(0.10, honey), (0.10, dark)], 50)
    p.rect(0, 0, 64, 3, honey)
    p.frame(2, 5, 28, 40, dark)                      # the glazed half
    p.rect(3, 6, 26, 38, inside)
    for gx in (6, 13, 20):                           # crystal, never used
        p.rect(gx, 12, 5, 9, lit)
        p.rect(gx, 21, 5, 1, cream)
        p.px(gx + 1, 13, cream)
    p.rect(4, 26, 24, 7, cream)                      # a stack of documents
    p.hline(4, 29, 24, veneer)
    p.rect(33, 5, 29, 40, honey)                     # the panelled half
    p.frame(36, 8, 23, 34, veneer)
    p.rect(33, 47, 29, 16, veneer)                   # drawers
    p.hline(33, 53, 29, dark)
    p.rect(2, 47, 28, 16, honey)
    p.hline(2, 55, 28, dark)
    for hx, hy in ((14, 51), (14, 59), (45, 51), (45, 59), (30, 24)):
        p.rect(hx, hy, 7, 2, metal)

    # 3 — the table, under an oilcloth with a faded floral print.
    p = A.cell64(3)
    p.fill(inside)
    p.rect(1, 10, 62, 26, cream)                     # the cloth
    p.speckle(3, 12, 58, 22, [(0.05, ochre)], 51)
    for fy in range(14, 34, 9):                      # the flowers, mostly gone
        for fx in range(6, 60, 11):
            p.px(fx, fy, red)
            p.px(fx + 1, fy - 1, ochre)
            p.px(fx - 1, fy + 1, ochre)
            p.px(fx + 1, fy + 1, ochre)
    p.rect(1, 33, 62, 3, blanket)                    # the hem, and the overhang
    p.dith(44, 12, 14, 20, None, ochre, 0.4)         # sticky, one corner
    p.rect(6, 36, 5, 28, veneer)                     # legs
    p.rect(53, 36, 5, 28, veneer)
    p.rect(6, 36, 52, 2, dark)
    p.rect(22, 2, 12, 9, lit)                        # a glass in its holder
    p.rect(21, 4, 14, 6, metal)
    p.rect(35, 5, 3, 4, metal)
    p.rect(40, 3, 11, 8, cream)                      # a jar of sugar
    p.rect(40, 2, 11, 2, metal)

    # 4 — one wooden chair.
    p = A.cell64(4)
    p.fill(inside)
    p.rect(14, 2, 36, 4, veneer)                     # the back rail
    p.rect(14, 12, 36, 4, veneer)
    p.rect(14, 2, 4, 30, honey)                      # the stiles
    p.rect(46, 2, 4, 30, honey)
    p.rect(10, 30, 44, 6, honey)                     # the seat
    p.rect(10, 36, 44, 2, dark)
    for lx in (12, 48):                              # the legs
        p.rect(lx, 38, 5, 26, veneer)
        p.vline(lx, 38, 26, dark)
    p.rect(12, 54, 41, 3, veneer)                    # the stretcher
    p.speckle(0, 0, 64, 64, [(0.05, dark)], 52)

    # 5 — the bed: a grey blanket and one flattened pillow.
    p = A.cell64(5)
    p.fill(inside)
    p.rect(0, 6, 64, 12, pillow)                     # the pillow
    p.dith(0, 6, 64, 12, None, blanket, 0.25)
    p.hline(0, 17, 64, blanket)
    p.rect(0, 18, 64, 38, blanket)                   # the blanket
    p.speckle(0, 18, 64, 38, [(0.07, metal)], 57)    # wool, not satin
    for fy in (24, 38, 50):                          # three folds, no more
        p.dith(0, fy, 64, 4, None, metal, 0.55)
        p.hline(0, fy - 1, 64, HU[3])
    p.rect(0, 56, 64, 8, veneer)                     # the frame
    p.hline(0, 56, 64, honey)
    p.rect(2, 60, 6, 4, dark)
    p.rect(56, 60, 6, 4, dark)

    # 6 — the carpet on the wall above the bed.  Dark red, geometric.
    p = A.cell64(6)
    p.fill(red)
    p.frame(1, 1, 62, 62, ochre)
    p.frame(3, 3, 58, 58, dark)
    p.frame(5, 5, 54, 54, ochre)
    for y in range(8, 56):                           # the central medallion
        for x in range(8, 56):
            m = abs(x - 32) + abs(y - 32)
            if m in (10, 11, 20, 21):
                p.px(x, y, ochre)
            elif m in (14, 15):
                p.px(x, y, cream)
            elif m <= 4:
                p.px(x, y, ochre)
    for cx, cy in ((12, 12), (52, 12), (12, 52), (52, 52)):
        p.rect(cx - 2, cy - 2, 5, 5, ochre)
        p.px(cx, cy, red)
    p.rect(0, 0, 64, 1, ochre)                       # fringe
    p.rect(0, 63, 64, 1, ochre)

    # 7 — a 199x calendar with a photograph of a mountain.
    p = A.cell64(7)
    p.fill(cream)
    p.frame(0, 0, 64, 64, veneer)
    p.rect(3, 3, 58, 30, HU[5])                      # the sky in the photograph
    p.dith(3, 3, 58, 30, None, lit, 0.3)
    for x in range(4, 60):                           # the mountain
        h = int(22 - abs(x - 30) * 0.62)
        if h > 0:
            p.rect(x, 33 - h, 1, h, blanket)
            if h > 15:
                p.rect(x, 33 - h, 1, min(4, h - 12), pillow)
    p.rect(3, 30, 58, 3, green)
    p.rect(3, 36, 58, 6, red)                        # the header band
    p.ctext(37, "1994", cream, adv=6)
    for gy in range(45, 62, 5):                      # the date grid
        for gx in range(5, 60, 5):
            p.rect(gx, gy, 3, 3, blanket if (gx // 5 + gy // 5) % 7 else red)
    p.rect(28, 0, 8, 3, metal)

    # 8 — a framed family photograph.
    p = A.cell64(8)
    p.fill(dark)
    p.frame(4, 8, 56, 48, honey)
    p.frame(5, 9, 54, 46, veneer)
    p.rect(7, 11, 50, 42, HU[3])
    p.speckle(7, 11, 50, 42, [(0.20, blanket)], 53)  # a bad print, gone flat
    p.dith(7, 11, 50, 42, None, silver, 0.25)
    for fx, fh in ((17, 20), (31, 24), (45, 18)):    # three of them, standing
        p.rect(fx - 5, 52 - fh, 11, fh, metal)
        p.disc(fx, 51 - fh, 4, cream)
        p.rect(fx - 5, 52 - fh, 11, 2, inside)
    p.rect(7, 46, 50, 7, silver)
    p.frame(4, 8, 56, 48, dark)

    # 9 — the wall clock.
    p = A.cell64(9)
    p.fill(inside)
    p.disc(32, 32, 30, dark)
    p.disc(32, 32, 27, cream)
    p.disc(32, 32, 24, pillow)
    for i in range(12):                              # the hours
        import math
        a = i * math.pi / 6.0
        x = int(32 + 21 * math.sin(a))
        y = int(32 - 21 * math.cos(a))
        p.rect(x - 1, y - 1, 2, 2, dark)
    p.line(32, 32, 32, 16, dark)                     # the hands, at ten past two
    p.line(32, 32, 45, 38, dark)
    p.disc(32, 32, 2, red)
    p.dith(14, 14, 20, 16, None, silver, 0.2)        # glass

    # 10 — the cast-iron radiator, silver-white over many coats.
    p = A.cell64(10)
    p.fill(inside)
    for i, x in enumerate(range(2, 62, 6)):          # the fins, soft-edged
        p.rect(x, 6, 5, 50, silver)
        p.vline(x, 6, 50, cream)
        p.vline(x + 4, 6, 50, metal)
        p.rect(x, 6, 5, 2, cream)
        p.rect(x, 54, 5, 2, metal)
    p.rect(0, 4, 64, 3, silver)                      # the top and bottom rails
    p.rect(0, 55, 64, 4, silver)
    p.hline(0, 4, 64, cream)
    p.hline(0, 58, 64, metal)
    p.rect(4, 56, 4, 8, metal)                       # the tail into the riser
    p.rect(52, 56, 4, 8, metal)
    p.dith(0, 40, 64, 16, None, rust, 0.12)          # rust at the joints
    p.rect(10, 0, 12, 6, blanket)                    # socks, drying
    p.rect(30, 0, 10, 5, HU[7])
    p.rect(44, 0, 11, 6, metal)

    # 11 — the three-arm chandelier.  Only one bulb works.  Cut-out.
    p = A.cell64(11)
    p.rect(30, 0, 4, 16, metal)                      # the drop
    p.disc(32, 18, 6, metal)                         # the body
    p.disc(32, 18, 4, dark)
    for ax, ay in ((10, 30), (32, 34), (54, 30)):    # three arms
        p.line(32, 20, ax, ay - 4, metal)
        p.line(31, 20, ax - 1, ay - 4, dark)
    for i, (ax, ay) in enumerate(((10, 30), (32, 34), (54, 30))):
        on = (i == 1)                                # only one bulb works
        body = lit if on else cream
        rim = cream if on else silver
        for dy in range(0, 12):                      # a pressed-glass shade
            w = 5 + dy
            p.rect(ax - w // 2, ay + dy, w, 1, body)
            if dy >= 2:                              # the pressing, inside it
                for gx in range(ax - w // 2 + 1, ax + w // 2, 3):
                    p.px(gx, ay + dy, rim)
            p.px(ax - w // 2, ay + dy, rim)
            p.px(ax + (w - 1) // 2, ay + dy, rim)
        p.rect(ax - 8, ay + 12, 17, 2, rim)          # the shade's rolled lip
        p.hline(ax - 8, ay + 13, 17, metal)
        if on:
            p.dith(ax - 10, ay + 14, 21, 6, None, lit, 0.4)
            p.dith(ax - 6, ay + 20, 13, 5, None, cream, 0.25)
        else:
            p.dith(ax - 6, ay + 4, 13, 8, None, silver, 0.35)

    # 12 — the mattress, on the floor, state 5.
    p = A.cell64(12)
    p.fill(cream)
    for x in range(0, 64, 6):                        # ticking stripes
        p.rect(x, 0, 3, 64, HU[3])
        p.vline(x, 0, 64, blanket)
    p.rect(0, 0, 64, 4, blanket)
    p.rect(0, 60, 64, 4, blanket)
    for by in range(10, 60, 14):                     # the buttons
        for bx in range(10, 60, 14):
            p.rect(bx, by, 2, 2, metal)
            p.frame(bx - 2, by - 2, 6, 6, HU[3])
    p.dith(4, 40, 30, 20, None, ochre, 0.25)         # a stain
    p.speckle(0, 0, 64, 64, [(0.04, blanket)], 54)

    # 13 — the low stand the television sat on, with a vase.
    p = A.cell64(13)
    p.fill(inside)
    p.rect(0, 14, 64, 6, honey)                      # the top
    p.hline(0, 14, 64, cream)
    p.rect(2, 20, 60, 30, veneer)                    # the carcass
    p.frame(6, 24, 22, 22, dark)
    p.frame(36, 24, 22, 22, dark)
    p.rect(7, 25, 20, 20, HU[0])
    p.rect(37, 25, 20, 20, HU[0])
    p.rect(14, 34, 7, 2, metal)
    p.rect(44, 34, 7, 2, metal)
    p.rect(4, 50, 6, 14, dark)                       # legs
    p.rect(54, 50, 6, 14, dark)
    p.rect(24, 2, 8, 12, green)                      # a vase
    p.rect(23, 1, 10, 3, HU[5])
    p.rect(25, 0, 6, 2, silver)
    p.rect(38, 8, 14, 6, cream)                      # a doily, corner of
    for x in range(38, 52, 3):
        p.px(x, 14, cream)

    # 14, 15 — spare material: veneer and painted metal, so a mis-aimed UV
    # still lands on furniture rather than on nothing.
    p = A.cell64(14)
    p.fill(veneer)
    planks(p, 0, 0, 64, 64, veneer, honey, dark, bh=11, seed=55)
    p = A.cell64(15)
    p.fill(metal)
    p.speckle(0, 0, 64, 64, [(0.15, silver), (0.12, inside)], 56)
    return A


# ══════════════════════════════════════════════════════════════════════════════
# home_window — 128x128, 4x4 of 32.  Alpha for the tulle only, which is a
# stipple, because there is no such thing as translucent here.
# ══════════════════════════════════════════════════════════════════════════════


def build_home_window():
    A = Atlas("home_window", 128, 128, HN, alpha=True)
    white, dirty, blue, shade = HN[0], HN[1], HN[2], HN[3]
    day, dusk, night = HN[4], HN[5], HN[6]
    merc, warm, warm2 = HN[7], HN[8], HN[9]
    jar, tin, aloe, paper, sill = HN[10], HN[11], HN[12], HN[13], HN[14]

    # 0 — wooden double frame, white over an older blue that shows in the chips.
    p = A.cell32(0)
    p.fill(white)
    p.speckle(0, 0, 32, 32, [(0.10, dirty)], 60)
    for cx, cy, cw, ch in ((3, 5, 6, 2), (20, 2, 3, 5), (11, 24, 7, 2),
                           (26, 14, 2, 8), (1, 17, 2, 4)):
        p.rect(cx, cy, cw, ch, blue)                 # chips down to the blue coat
        p.px(cx, cy, shade)
    p.vline(0, 0, 32, shade)                         # the rebates
    p.vline(31, 0, 32, shade)
    p.hline(0, 0, 32, dirty)
    p.hline(0, 31, 32, shade)
    p.rect(0, 14, 32, 2, dirty)                      # the putty line
    p.dith(0, 26, 32, 6, None, dirty, 0.35)          # grime along the bottom

    def view(p, sky, lamp_on, lit_windows):
        """What the courtyard looks like through the glass, including the L1
        lamppost — which is how the first dead lamp gets noticed from indoors."""
        p.ramp(0, 0, 32, 18, [sky, sky, day if sky is day else dusk])
        p.rect(0, 18, 32, 14, HN[3] if sky is not day else HN[1])
        p.rect(2, 4, 12, 16, shade)                  # the opposite block
        p.rect(18, 7, 12, 13, shade)
        for i, (wx, wy) in enumerate(((3, 6), (7, 6), (11, 6), (3, 11), (7, 11),
                                      (11, 11), (19, 9), (23, 9), (27, 9),
                                      (19, 14), (23, 14), (27, 14))):
            on = i in lit_windows
            p.rect(wx, wy, 3, 3, (warm if i % 2 else warm2) if on else HN[6])
        p.vline(24, 12, 8, shade)                    # the lamppost, from indoors
        p.line(24, 12, 27, 12, shade)
        if lamp_on:
            p.rect(26, 12, 4, 2, merc)
            p.dith(24, 13, 8, 8, None, merc, 0.35)
            p.dith(23, 19, 10, 4, None, merc, 0.5)   # its pool, seen from above
        else:
            p.rect(26, 12, 4, 2, HN[3])
        p.speckle(0, 0, 32, 32, [(0.03, dirty)], 61) # the glass is not clean

    view(A.cell32(1), day, False, ())                # 1 — day: flat, cold, grey
    view(A.cell32(2), dusk, True, (1, 4, 8))         # 2 — dusk
    view(A.cell32(3), night, True, (0, 3, 5, 7, 10)) # 3 — night

    # 4 — the wide painted sill, and what lives on it.
    p = A.cell32(4)
    p.fill(sill)
    planks(p, 0, 0, 32, 32, sill, HN[3], HN[1], bh=9, seed=62)
    p.rect(0, 0, 32, 3, white)                       # the sill's painted face
    p.rect(0, 3, 32, 1, dirty)
    p.rect(2, 6, 7, 14, jar)                         # two three-litre jars
    p.rect(2, 6, 7, 2, dirty)
    p.dith(3, 9, 5, 10, None, day, 0.3)
    p.rect(11, 8, 6, 12, jar)
    p.rect(11, 8, 6, 2, dirty)
    p.rect(20, 12, 6, 8, tin)                        # the aloe, in a tin can
    p.hline(20, 12, 6, HN[0])
    for i, (ax, ah) in enumerate(((21, 9), (23, 11), (25, 8), (22, 6))):
        p.rect(ax, 12 - ah, 2, ah, aloe)
        p.px(ax, 12 - ah, HN[3])
    p.rect(2, 22, 14, 5, paper)                      # a folded newspaper
    p.hline(2, 24, 14, HN[1])
    p.rect(19, 23, 7, 4, HN[3])                      # a box of matches
    p.rect(20, 24, 5, 2, warm2)
    p.dith(0, 28, 32, 4, None, HN[1], 0.4)           # dead flies and cotton wool
    p.scatter(0, 27, 32, 5, HN[3], 10, 63)

    # 5 — the tulle, greyed, and the one heavy drape that is never closed.
    p = A.cell32(5)
    p.rect(0, 0, 10, 32, HN[3])                      # the drape, pushed aside
    p.rect(0, 0, 8, 32, HN[9])
    for x in range(0, 9, 3):
        p.vline(x, 0, 32, HN[3])
        p.vline(x + 1, 0, 32, HN[14])
    p.vline(9, 0, 32, HN[3])
    # A stipple, not a blend: the console has no translucency to offer.
    p.dithf(10, 0, 22, 32, None, paper,
            lambda u, v: 0.30 + 0.16 * ((u // 3) % 2))
    p.dithf(10, 0, 22, 32, None, HN[1], lambda u, v: 0.10)
    p.rect(0, 0, 32, 2, HN[1])                       # the rail
    p.hline(0, 1, 32, HN[0])

    # 6 — the ventilation pane, closed, with its own catch.
    p = A.cell32(6)
    p.fill(white)
    p.frame(0, 0, 32, 32, dirty)
    p.rect(4, 4, 24, 24, day)
    p.dith(4, 4, 24, 24, None, dusk, 0.25)
    p.frame(4, 4, 24, 24, shade)
    p.rect(26, 14, 5, 3, tin)
    p.rect(2, 2, 3, 3, blue)

    # 7 — the winter stuffing between the frames, and the flies.
    p = A.cell32(7)
    p.fill(paper)
    p.speckle(0, 0, 32, 32, [(0.22, HN[1]), (0.06, HN[3])], 64)
    p.dith(0, 0, 32, 32, None, dirty, 0.2)
    p.scatter(1, 1, 30, 30, HN[3], 12, 65, size=2)
    p.rect(0, 0, 32, 2, HN[0])
    p.rect(0, 30, 32, 2, HN[3])

    # 8..15 — glass and paint stock, so the frame's minor faces have somewhere
    # to sample from.
    for i, (base, over, s) in enumerate(((white, dirty, 70), (dirty, shade, 71),
                                         (day, dusk, 72), (dusk, night, 73),
                                         (sill, HN[1], 74), (tin, HN[3], 75),
                                         (paper, HN[1], 76), (blue, shade, 77))):
        p = A.cell32(8 + i)
        p.fill(base)
        p.speckle(0, 0, 32, 32, [(0.18, over)], s)
        p.hline(0, 0, 32, over)
    return A


# ══════════════════════════════════════════════════════════════════════════════
# home_door — 64x64, 2x2 of 32.  Opaque.
# ══════════════════════════════════════════════════════════════════════════════


def build_home_door():
    A = Atlas("home_door", 64, 64, HD, alpha=False)
    paint, shade, panel, edge = HD[0], HD[1], HD[2], HD[3]
    derm, derm_l, derm_d, stud = HD[4], HD[5], HD[6], HD[7]
    handle, handle_d = HD[8], HD[9]
    green, green_l, plate, channel, keyhole, gloss = HD[10], HD[11], HD[12], HD[13], HD[14], HD[15]

    # 0 — the room door: two recessed panels, painted over more than once.
    p = A.cell32x2(0)
    p.fill(paint)
    p.speckle(0, 0, 32, 32, [(0.08, shade)], 80)
    p.frame(0, 0, 32, 32, edge)
    for py, ph in ((3, 12), (17, 12)):
        p.frame(4, py, 24, ph, shade)
        p.rect(5, py + 1, 22, ph - 2, panel)
        p.rect(6, py + 2, 20, ph - 4, shade)
        p.hline(6, py + 2, 20, gloss)
    p.rect(28, 15, 3, 2, handle)                     # the handle
    p.px(30, 16, gloss)
    p.rect(29, 18, 1, 2, handle_d)

    # 1 — the front door, padded with dermatin, studded in a diamond.
    p = A.cell32x2(1)
    p.fill(derm)
    p.speckle(0, 0, 32, 32, [(0.12, derm_l), (0.12, derm_d)], 81)
    p.frame(0, 0, 32, 32, edge)
    for y in range(2, 32, 8):                        # the quilting
        for x in range(2, 32, 8):
            off = 4 if (y // 8) % 2 else 0
            sx = x + off
            p.line(sx - 4, y, sx, y - 4, derm_d)
            p.line(sx, y - 4, sx + 4, y, derm_l)
            p.px(sx, y, stud)
    p.rect(25, 14, 4, 3, handle)                     # the handle and the lock
    p.rect(26, 19, 2, 3, handle_d)
    p.px(27, 20, keyhole)
    p.rect(2, 2, 5, 2, derm_l)

    # 2 — the doors that do not open: kitchen and bathroom, painted green.
    p = A.cell32x2(2)
    p.fill(green)
    p.speckle(0, 0, 32, 32, [(0.10, green_l)], 82)
    p.frame(0, 0, 32, 32, edge)
    p.frame(3, 3, 26, 26, HD[6])
    p.rect(4, 4, 24, 12, green_l)                    # a glazed upper panel,
    p.dith(4, 4, 24, 12, None, plate, 0.30)          # painted over from behind
    p.frame(4, 4, 24, 12, HD[6])
    p.rect(6, 20, 20, 6, HD[6])                      # a ventilation grille
    for gx in range(7, 26, 3):
        p.vline(gx, 21, 4, green_l)
    p.px(27, 17, keyhole)                            # no handle: it does not open
    p.dith(0, 26, 32, 6, None, HD[6], 0.3)

    # 3 — the switch, the socket, and the wiring run outside the wall.
    p = A.cell32x2(3)
    p.fill(paint)
    p.speckle(0, 0, 32, 32, [(0.09, shade)], 83)
    p.rect(2, 2, 13, 15, plate)                      # the switch plate
    p.frame(2, 2, 13, 15, shade)
    p.rect(4, 5, 9, 9, gloss)                        # the rocker
    p.hline(4, 9, 9, shade)
    p.rect(4, 10, 9, 4, HD[1])
    p.rect(18, 3, 12, 12, plate)                     # the round socket
    p.disc(24, 9, 5, gloss)
    p.disc(24, 9, 5, shade, filled=False)
    p.rect(22, 8, 2, 2, keyhole)
    p.rect(26, 8, 2, 2, keyhole)
    p.rect(0, 20, 32, 5, channel)                    # the plastic channel
    p.hline(0, 20, 32, gloss)
    p.hline(0, 24, 32, shade)
    p.rect(23, 15, 3, 5, channel)
    p.rect(0, 27, 32, 5, HD[4])                      # skirting under it
    p.hline(0, 27, 32, derm_l)
    p.dith(15, 5, 3, 10, None, gloss, 0.4)           # glossy, where hands go
    return A


# ══════════════════════════════════════════════════════════════════════════════
# street_facade — 256x256, 4x4 of 64.  Opaque: this is a building.
# ══════════════════════════════════════════════════════════════════════════════


def build_street_facade():
    A = Atlas("street_facade", 256, 256, SF, alpha=False)
    dash, light, dark, damp, seam = SF[0], SF[1], SF[2], SF[3], SF[4]
    rust, rust2, frame = SF[5], SF[6], SF[7]
    glass, glass2, warm, warm2, board = SF[8], SF[9], SF[10], SF[11], SF[12]
    glaze, enamel, conc = SF[13], SF[14], SF[15]

    def panel(p, seed):
        pebbledash(p, 0, 0, 64, 64, dash, light, dark, damp, seed)
        p.hline(0, 0, 64, seam)                      # the horizontal panel joint
        p.hline(0, 1, 64, light)
        p.hline(0, 63, 64, seam)
        p.vline(0, 0, 64, seam)                      # and the vertical one
        p.vline(1, 0, 64, light)

    # 0 — plain panel.
    panel(A.cell64(0), 90)

    # 1 — rust streaks from every embedded fixing.
    p = A.cell64(1)
    panel(p, 91)
    for fx, fy in ((11, 8), (33, 20), (52, 6), (24, 40), (46, 46)):
        p.rect(fx, fy, 3, 2, rust2)
        p.dithf(fx - 2, fy, 7, 64 - fy, None, rust,
                lambda u, v: max(0.0, 0.75 - v / 40.0) * (1.0 if 1 <= u <= 4 else 0.35))
    p.dith(0, 52, 64, 12, None, damp, 0.3)

    def windows(p, lit_set, seed):
        p.rect(0, 0, 64, 64, dash)
        p.speckle(0, 0, 64, 64, [(0.2, light), (0.2, dark)], seed)
        for r in range(2):
            for c in range(2):
                x, y = 3 + c * 31, 4 + r * 31
                p.rect(x, y, 27, 25, frame)          # the wooden frame
                idx = r * 2 + c
                on = idx in lit_set
                g = (warm if idx % 2 == 0 else warm2) if on else (glass if idx % 3 else glass2)
                for pr in range(2):                  # a grid of small panes
                    for pc in range(3):
                        px_, py = x + 2 + pc * 8, y + 2 + pr * 11
                        p.rect(px_, py, 7, 10, g)
                        if on:
                            p.dith(px_, py, 7, 10, None, SF[1], 0.15)
                if idx == 1:                         # a pane patched with card
                    p.rect(x + 10, y + 2, 7, 10, board)
                    p.line(x + 10, y + 2, x + 16, y + 11, SF[2])
                if idx == 2:                         # and one with tape
                    p.line(x + 2, y + 13, x + 8, y + 22, SF[1])
                p.hline(x, y + 24, 27, SF[2])        # the sill
                p.rect(x - 1, y + 25, 29, 2, conc)
                p.dith(x - 1, y + 27, 29, 5, None, damp, 0.35)
        p.hline(0, 0, 64, seam)
        p.hline(0, 63, 64, seam)

    windows(A.cell64(2), set(), 92)                  # 2 — a band of dark windows
    windows(A.cell64(3), {0, 3}, 93)                 # 3 — warm yellow, the last
                                                     #     colour left on a street

    # 4 — an open balcony: a rusted rail, and what gets stored on it.
    p = A.cell64(4)
    p.rect(0, 0, 64, 64, dash)
    p.speckle(0, 0, 64, 64, [(0.18, light), (0.18, dark)], 94)
    p.rect(0, 6, 64, 40, glass)                      # the recess
    p.dith(0, 6, 64, 40, None, glass2, 0.4)
    p.rect(4, 10, 12, 34, SF[15])                    # skis
    p.rect(6, 10, 3, 34, SF[12])
    p.rect(46, 22, 14, 22, SF[2])                    # a bicycle wheel, roughly
    p.disc(53, 33, 9, glass2)
    p.disc(53, 33, 9, SF[15], filled=False)
    for jx in range(22, 42, 6):                      # jars
        p.rect(jx, 30, 5, 12, glaze)
        p.rect(jx, 30, 5, 2, SF[1])
    p.rect(0, 44, 64, 6, conc)                       # the slab
    p.hline(0, 44, 64, light)
    p.rect(0, 50, 64, 4, rust)                       # the rail, gone to rust
    for bx in range(2, 64, 5):
        p.vline(bx, 50, 12, rust2)
    p.hline(0, 60, 64, rust)
    p.dith(0, 54, 64, 10, None, damp, 0.3)

    # 5 — glazed by the owner, in whatever frames were going.
    p = A.cell64(5)
    p.rect(0, 0, 64, 64, dash)
    p.speckle(0, 0, 64, 64, [(0.18, light), (0.18, dark)], 95)
    p.rect(1, 4, 30, 44, frame)                      # one owner's frames
    for c in range(3):
        for r in range(3):
            p.rect(3 + c * 9, 6 + r * 14, 8, 13, glaze)
            p.dith(3 + c * 9, 6 + r * 14, 8, 13, None, glass, 0.35)
    p.rect(33, 4, 30, 44, SF[2])                     # the next owner's, in grey
    for c in range(2):
        for r in range(2):
            p.rect(35 + c * 14, 7 + r * 20, 13, 19, glass2)
            p.dith(35 + c * 14, 7 + r * 20, 13, 19, None, glaze, 0.3)
    p.rect(0, 48, 64, 8, SF[15])                     # corrugated asbestos below
    corrugate(p, 0, 48, 64, 8, conc, light, SF[2], pitch=5)
    p.rect(0, 56, 64, 8, conc)
    p.dith(0, 56, 64, 8, None, damp, 0.35)

    # 6 — the entrance: a canopy, a heavy sprung door, a number plate.
    p = A.cell64(6)
    p.rect(0, 0, 64, 64, dash)
    p.speckle(0, 0, 64, 64, [(0.18, light), (0.18, dark)], 96)
    p.rect(0, 6, 64, 8, conc)                        # the concrete canopy
    p.hline(0, 6, 64, light)
    p.rect(0, 14, 64, 2, SF[2])
    p.dith(0, 16, 64, 6, None, damp, 0.5)
    p.rect(14, 18, 36, 46, SF[2])                    # the door frame
    p.rect(17, 21, 30, 43, glass)                    # the door itself
    p.rect(19, 24, 26, 18, glass2)                   # a small glazed panel
    p.dith(19, 24, 26, 18, None, SF[13], 0.3)
    p.frame(19, 24, 26, 18, SF[2])
    p.rect(19, 46, 26, 16, SF[13])                   # sheet steel below it
    corrugate(p, 19, 46, 26, 16, SF[13], glaze, glass, pitch=4)
    p.rect(43, 40, 3, 6, rust2)                      # the handle
    p.rect(4, 20, 9, 12, enamel)                     # the blue number plate
    p.text(6, 22, "12", SF[7], adv=4)
    p.rect(52, 20, 10, 8, enamel)
    p.rect(0, 60, 64, 4, conc)                       # the step
    p.scatter(0, 58, 64, 6, SF[1], 20, 97)           # cigarette ends

    # 7 — the street sign.
    p = A.cell64(7)
    p.fill(enamel)
    p.frame(0, 0, 64, 64, SF[8])
    p.frame(2, 2, 60, 60, frame)
    p.rect(3, 3, 58, 58, enamel)
    p.speckle(3, 3, 58, 58, [(0.04, SF[8])], 98)     # the enamel is chipped
    p.ctext(14, "УЛ.", frame, adv=6)
    p.ctext(30, "ЗАВОДСКАЯ", frame, adv=6)
    p.hline(8, 42, 48, frame)
    p.ctext(46, "1994", frame, adv=6)
    p.rect(6, 6, 3, 2, SF[2])
    p.rect(54, 55, 4, 3, SF[2])

    # 8 — the roof edge: a parapet, its coping, and the run of rust off it.
    p = A.cell64(8)
    p.rect(0, 0, 64, 22, conc)
    p.speckle(0, 0, 64, 22, [(0.2, light), (0.15, dark)], 99)
    p.rect(0, 20, 64, 4, SF[2])                      # the coping's shadow
    p.rect(0, 8, 64, 4, SF[15])                      # galvanised flashing
    p.hline(0, 8, 64, SF[1])
    p.hline(0, 11, 64, seam)
    pebbledash(p, 0, 24, 64, 40, dash, light, dark, damp, 100)
    p.hline(0, 24, 64, seam)
    for fx in (9, 27, 45, 58):                       # rust off the flashing clips
        p.rect(fx, 10, 2, 2, rust2)
        p.dithf(fx - 1, 12, 4, 30, None, rust,
                lambda u, v: max(0.0, 0.7 - v / 26.0))
    p.dith(0, 56, 64, 8, None, damp, 0.3)

    # 9..15 — more wall.  A facade is mostly wall, and a tessellated block that
    # repeats one cell reads as wallpaper rather than as a building.
    for i, s in zip(range(9, 16), range(110, 117)):
        p = A.cell64(i)
        panel(p, s)
        if i in (10, 13):
            p.dith(0, 0, 64, 64, None, damp, 0.22)
        if i == 11:
            p.rect(0, 28, 64, 3, seam)               # a joint that opened
            p.hline(0, 27, 64, light)
        if i == 14:
            for fx in (18, 40):
                p.rect(fx, 12, 2, 2, rust2)
                p.dithf(fx - 1, 14, 4, 40, None, rust,
                        lambda u, v: max(0.0, 0.6 - v / 34.0))
    return A


# ══════════════════════════════════════════════════════════════════════════════
# street_ground — 128x128, 4x4 of 32.  Opaque.
# ══════════════════════════════════════════════════════════════════════════════


def asphalt(p, seed, base=None):
    p.fill(base or SG[0])
    p.speckle(0, 0, 32, 32, [(0.16, SG[1]), (0.16, SG[2]), (0.05, SG[4])], seed)


def build_street_ground():
    A = Atlas("street_ground", 128, 128, SG, alpha=False)

    # 0 — cracked asphalt.
    p = A.cell32(0)
    asphalt(p, 120)
    for sx, sy, dx, dy in ((3, 0, 9, 18), (12, 18, 14, 13), (24, 2, 6, 12)):
        p.line(sx, sy, sx + dx, sy + dy, SG[2])
        p.px(sx + dx // 2, sy + dy // 2, SG[3])

    # 1 — patched with darker asphalt, badly.
    p = A.cell32(1)
    asphalt(p, 121)
    rnd = random.Random(122)
    for y in range(4, 26):
        w = 20 + rnd.randrange(-3, 4)
        p.rect(5 + rnd.randrange(-2, 3), y, w, 1, SG[3])
    p.speckle(4, 4, 24, 22, [(0.12, SG[2])], 123)
    p.dith(3, 3, 26, 24, None, SG[0], 0.12)

    # 2 — the kerb.
    p = A.cell32(2)
    p.fill(SG[5])
    p.speckle(0, 0, 32, 32, [(0.16, SG[6]), (0.14, SG[7])], 124)
    p.rect(0, 0, 32, 7, SG[6])                       # its top face
    p.hline(0, 7, 32, SG[7])
    p.rect(0, 24, 32, 8, SG[7])                      # and the gutter shadow
    for cx in (5, 17, 26):                           # chips
        p.rect(cx, 5, 3, 4, SG[7])
    for jx in (10, 22):                              # joints between stones
        p.vline(jx, 0, 32, SG[7])

    # 3 — standing water, reflecting whatever light is left.
    p = A.cell32(3)
    asphalt(p, 125)
    p.dithf(0, 0, 32, 32, None, SG[8],
            lambda u, v: 1.0 if ((u - 16) / 15.0) ** 2 + ((v - 16) / 12.0) ** 2 < 1 else 0.0)
    p.dithf(0, 0, 32, 32, None, SG[9],
            lambda u, v: (0.55 if ((u - 16) / 13.0) ** 2 + ((v - 16) / 10.0) ** 2 < 1 else 0.0)
            * (1.0 if (v // 2) % 2 == 0 else 0.35))  # the overcast, broken up
    p.dith(12, 10, 8, 3, None, SG[10], 0.6)          # and a lamp in it
    p.dith(13, 13, 6, 2, None, SG[10], 0.3)
    p.ellipse(16, 16, 15, 12, SG[2], filled=False)

    # 4 — mud at the verge, with tyre tracks.
    p = A.cell32(4)
    p.fill(SG[11])
    p.speckle(0, 0, 32, 32, [(0.18, SG[12]), (0.12, SG[2])], 126)
    for tx in (6, 21):                               # two tracks
        p.rect(tx, 0, 6, 32, SG[2])
        for y in range(0, 32, 3):                    # the tread
            p.rect(tx, y, 6, 1, SG[12])
            p.rect(tx + 1, y + 1, 4, 1, SG[11])
    p.dith(0, 0, 32, 32, None, SG[8], 0.10)          # it has been raining

    # 5 — a manhole cover.
    p = A.cell32(5)
    asphalt(p, 127)
    p.disc(16, 16, 14, SG[13])
    p.disc(16, 16, 14, SG[2], filled=False)
    p.disc(16, 16, 12, SG[7])
    for y in range(4, 29, 4):                        # the waffle pattern
        p.hline(4, y, 25, SG[13])
    for x in range(4, 29, 4):
        p.vline(x, 4, 25, SG[13])
    p.disc(16, 16, 3, SG[13])
    p.px(16, 16, SG[2])
    p.dith(3, 3, 27, 27, None, SG[4], 0.08)

    # 6 — a faded painted crossing.
    p = A.cell32(6)
    asphalt(p, 128)
    for sx in (2, 12, 22):
        p.rect(sx, 0, 7, 32, SG[14])
        p.speckle(sx, 0, 7, 32, [(0.34, SG[15]), (0.20, SG[0])], 129 + sx)
        p.dithf(sx, 0, 7, 32, None, SG[0],
                lambda u, v: 0.55 if (v % 11) < 4 else 0.15)  # worn by tyres

    # 7 — a storm drain grating.
    p = A.cell32(7)
    asphalt(p, 130)
    p.rect(4, 8, 24, 16, SG[7])
    p.frame(4, 8, 24, 16, SG[13])
    for gy in range(10, 23, 3):
        p.rect(6, gy, 20, 2, SG[2])
        p.hline(6, gy, 20, SG[13])
    p.dith(4, 22, 24, 4, None, SG[8], 0.5)

    # 8..15 — more road.  The lane is long and the eye finds a repeat fast.
    for i, s in zip(range(8, 16), range(140, 148)):
        p = A.cell32(i)
        asphalt(p, s)
        if i in (9, 13):
            p.dith(0, 0, 32, 32, None, SG[8], 0.14)  # still wet
        if i == 10:
            p.rect(0, 12, 32, 3, SG[3])
        if i == 11:
            p.scatter(0, 0, 32, 32, SG[4], 40, s)    # grit swept to the edge
        if i == 14:
            p.line(0, 6, 31, 24, SG[2])
            p.line(0, 7, 31, 25, SG[3])
        if i == 15:
            p.dith(0, 0, 32, 32, None, SG[11], 0.2)  # mud tracked out of the verge
    return A


# ══════════════════════════════════════════════════════════════════════════════
# street_props — 256x256, 4x4 of 64.
#
# Alpha only where the object is read as a silhouette against the sky: the
# poplar, the swings, the carpet-beating frame and the barbed wire.  The rest
# are surfaces on boxes and stay solid.
# ══════════════════════════════════════════════════════════════════════════════


def build_street_props():
    A = Atlas("street_props", 256, 256, SP, alpha=True)
    conc, conc_l, conc_d = SP[0], SP[1], SP[2]
    steel, steel_d, steel_l = SP[3], SP[4], SP[5]
    rust, green, oxide, blue = SP[6], SP[7], SP[8], SP[9]
    timber, galv, bark, cream, sticker = SP[10], SP[11], SP[12], SP[13], SP[14]

    # 0 — PO-2: the standard precast panel with the raised diamond.
    p = A.cell64(0)
    p.fill(conc)
    p.speckle(0, 0, 64, 64, [(0.14, conc_l), (0.14, conc_d)], 150)
    for y in range(64):                              # the diamond, in relief
        for x in range(64):
            m = abs(x - 32) + abs(y - 32)
            if 22 <= m <= 26:
                p.px(x, y, conc_l if (x - 32) * (y - 32) < 0 or y < 32 else conc_d)
            elif 8 <= m <= 12:
                p.px(x, y, conc_l if y < 32 else conc_d)
    p.frame(1, 1, 62, 62, conc_d)                    # the panel's own edge
    p.hline(0, 0, 64, conc_l)
    p.vline(0, 0, 64, conc_l)
    p.dith(0, 50, 64, 14, None, conc_d, 0.28)        # splash off the road
    p.dith(0, 0, 64, 6, None, conc_d, 0.15)

    # 1 — a metal garage, painted whatever was going.
    p = A.cell64(1)
    p.fill(green)
    corrugate(p, 0, 6, 64, 58, green, SP[13], conc_d, pitch=6)
    p.rect(0, 0, 64, 6, steel_d)                     # the lintel
    p.hline(0, 5, 64, steel_l)
    p.rect(0, 0, 3, 64, steel_d)                     # the jambs
    p.rect(61, 0, 3, 64, steel_d)
    p.rect(28, 30, 9, 8, steel)                      # the hasp and padlock
    p.rect(30, 34, 5, 6, steel_l)
    p.px(32, 36, steel_d)
    p.text(6, 10, "17", cream, adv=6)                # hand-painted, badly
    for rx, ry in ((5, 52), (48, 20), (20, 58), (58, 40)):
        p.dith(rx, ry, 7, 6, None, rust, 0.55)
    p.dith(0, 56, 64, 8, None, rust, 0.2)

    # 2 — the kiosk.  Closed on every shift.
    p = A.cell64(2)
    p.fill(cream)
    p.speckle(0, 0, 64, 64, [(0.10, conc_l), (0.08, conc_d)], 151)
    p.rect(0, 0, 64, 8, steel)                       # the fascia
    p.hline(0, 0, 64, steel_l)
    p.ctext(1, "24 ЧАСА", conc_d, adv=6)             # hand-lettered
    p.rect(4, 12, 56, 26, steel_d)                   # the shuttered window
    corrugate(p, 5, 13, 54, 24, steel, steel_l, steel_d, pitch=3, vertical=False)
    p.rect(24, 30, 16, 5, steel_d)                   # the serving slot, shut
    p.rect(26, 31, 12, 3, SP[4])
    for sx, sy, sw, sh, sc in ((6, 42, 12, 9, sticker), (22, 43, 14, 8, blue),
                               (40, 41, 13, 10, oxide), (8, 54, 16, 7, green)):
        p.rect(sx, sy, sw, sh, sc)                   # faded stickers
        p.frame(sx, sy, sw, sh, cream)
        p.dith(sx, sy, sw, sh, None, cream, 0.28)
    p.rect(30, 54, 20, 8, cream)
    p.hline(30, 58, 20, conc_d)
    p.dith(0, 60, 64, 4, None, rust, 0.3)

    # 3 — a bench by the door.
    p = A.cell64(3)
    p.fill(conc_d)
    for i, y in enumerate(range(10, 34, 8)):         # the slats
        p.rect(2, y, 60, 6, timber)
        p.hline(2, y, 60, SP[13])
        p.hline(2, y + 5, 60, SP[2])
        p.speckle(2, y, 60, 6, [(0.08, SP[2])], 152 + i)
    p.rect(4, 34, 10, 30, conc)                      # the concrete legs
    p.rect(50, 34, 10, 30, conc)
    p.speckle(4, 34, 10, 30, [(0.16, conc_l), (0.12, conc_d)], 155)
    p.speckle(50, 34, 10, 30, [(0.16, conc_l), (0.12, conc_d)], 156)
    p.rect(0, 0, 64, 8, conc_d)
    p.dith(0, 0, 64, 8, None, timber, 0.2)

    # 4 — a bin, dented.
    p = A.cell64(4)
    p.fill(conc_d)
    p.rect(7, 8, 50, 54, steel)                      # a plain steel drum
    p.speckle(7, 8, 50, 54, [(0.08, steel_l), (0.10, steel_d)], 153)
    p.vline(8, 8, 54, steel_l)                       # its lit and shaded flanks
    p.vline(9, 8, 54, steel_l)
    p.rect(52, 8, 5, 54, steel_d)
    p.rect(5, 3, 54, 6, steel_l)                     # the rolled rim
    p.hline(5, 3, 54, SP[5])
    p.hline(5, 8, 54, steel_d)
    p.rect(7, 22, 50, 3, steel_d)                    # two swaged rings
    p.hline(7, 21, 50, steel_l)
    p.rect(7, 44, 50, 3, steel_d)
    p.hline(7, 43, 50, steel_l)
    p.rect(7, 59, 50, 3, steel_d)
    for dx, dy in ((16, 28), (36, 12), (26, 50)):    # and the dents
        p.dith(dx, dy, 12, 9, None, steel_d, 0.6)
        p.hline(dx, dy, 12, steel_l)
    p.dith(7, 34, 50, 28, None, rust, 0.3)
    p.dith(7, 8, 50, 8, None, rust, 0.18)

    # 5 — the carpet-beating frame.  Cut-out: this is the courtyard's landmark
    #     and it has to read as a shape against the sky at every tier.
    p = A.cell64(5)
    p.rect(4, 8, 56, 5, steel)                       # the top rail
    p.hline(4, 8, 56, steel_l)
    p.hline(4, 12, 56, steel_d)
    for lx in (6, 53):                               # the two legs
        p.rect(lx, 12, 5, 50, steel)
        p.vline(lx, 12, 50, steel_l)
        p.vline(lx + 4, 12, 50, steel_d)
        p.rect(lx - 3, 58, 11, 6, conc)              # set into concrete
        p.hline(lx - 3, 58, 11, conc_l)
    p.rect(4, 26, 56, 4, steel)                      # the lower rail
    p.hline(4, 26, 56, steel_l)
    p.hline(4, 29, 56, steel_d)
    for bx in (20, 32, 44):                          # bracing
        p.rect(bx, 12, 3, 15, steel)
        p.vline(bx, 12, 15, steel_l)
    p.dith(4, 9, 56, 3, None, rust, 0.35)
    p.dith(6, 40, 5, 22, None, rust, 0.3)

    # 6 — a bare poplar.  Late October: there is nothing on it.  Cut-out.
    p = A.cell64(6)
    p.rect(26, 20, 11, 44, bark)                     # the trunk
    p.vline(26, 20, 44, conc_d)
    p.vline(36, 20, 44, SP[4])
    p.speckle(27, 20, 9, 44, [(0.22, conc_d), (0.14, conc)], 157)
    rnd = random.Random(158)
    for i in range(11):                              # the branches, all going up
        y = 18 + i * 4
        side = 1 if i % 2 else -1
        x = 31 + side * 5
        ln = 9 + rnd.randrange(0, 14)
        rise = ln + rnd.randrange(2, 9)
        p.line(x, y, x + side * ln, y - rise, bark)
        p.line(x, y + 1, x + side * ln, y - rise + 1, conc_d)
        for _ in range(2):                           # and the twigs off those
            t = rnd.randrange(2, max(3, ln))
            tx, ty = x + side * t, y - int(rise * t / ln)
            p.line(tx, ty, tx + side * rnd.randrange(2, 6), ty - rnd.randrange(4, 9), conc_d)
    for _ in range(14):                              # a thin crown of whips
        bx = 31 + rnd.randrange(-16, 17)
        p.line(bx, 20 + rnd.randrange(0, 8), bx + rnd.randrange(-4, 5), 2, conc_d)
    p.rect(22, 60, 19, 4, conc_d)                    # the ring of bare earth
    p.dith(20, 58, 24, 6, None, bark, 0.4)

    # 7 — the heating main: dented galvanised casing, the route's spine.
    p = A.cell64(7)
    p.fill(conc_d)
    p.rect(0, 8, 64, 48, galv)
    p.ramp(0, 8, 64, 24, [galv, steel_l])            # the cylinder's top half
    p.ramp(0, 32, 64, 24, [galv, steel_d])
    p.hline(0, 8, 64, steel_d)
    p.hline(0, 55, 64, steel_d)
    for jx in (0, 21, 42, 63):                       # the casing's seams
        p.vline(jx, 8, 48, steel_d)
        p.vline(min(jx + 1, 63), 8, 48, steel_l)
    for dx, dy in ((10, 20), (33, 38), (52, 16)):    # and its dents
        p.dith(dx, dy, 9, 7, None, steel_d, 0.55)
    p.dith(0, 44, 64, 12, None, rust, 0.22)
    p.dith(0, 0, 64, 8, None, conc, 0.3)
    p.dith(0, 56, 64, 8, None, conc_d, 0.4)

    # 8 — the low concrete supports it runs on.
    p = A.cell64(8)
    p.fill(conc_d)
    p.rect(14, 0, 36, 64, conc)
    p.speckle(14, 0, 36, 64, [(0.16, conc_l), (0.14, conc_d)], 159)
    p.rect(8, 0, 48, 8, conc_l)                      # the saddle
    p.hline(8, 7, 48, conc_d)
    p.vline(14, 0, 64, conc_l)
    p.vline(49, 0, 64, conc_d)
    for cy in (18, 40):                              # a lifting eye, and rust
        p.rect(20, cy, 5, 3, steel)
        p.dithf(19, cy + 3, 7, 24, None, rust, lambda u, v: max(0.0, 0.6 - v / 20.0))
    p.dith(14, 52, 36, 12, None, conc_d, 0.35)
    p.dith(0, 58, 64, 6, None, SP[12], 0.3)

    # 9 — two bent swings.  Cut-out.
    p = A.cell64(9)
    p.rect(2, 6, 60, 5, steel)                       # the top bar
    p.hline(2, 6, 60, steel_l)
    p.hline(2, 10, 60, steel_d)
    for lx, lean in ((6, 3), (54, -3)):              # the A-frames, leaning
        p.line(lx, 11, lx - lean * 3, 63, steel)
        p.line(lx + 1, 11, lx + 1 - lean * 3, 63, steel_d)
        p.line(lx, 11, lx + lean * 4, 63, steel)
        p.line(lx + 1, 11, lx + 1 + lean * 4, 63, steel_d)
    for sx in (22, 42):                              # the seats, on chains
        for cy in range(12, 40):
            p.px(sx - 4, cy, steel_l if cy % 2 else steel_d)
            p.px(sx + 4, cy, steel_l if cy % 2 else steel_d)
        p.rect(sx - 7, 40, 15, 4, timber)
        p.hline(sx - 7, 40, 15, SP[13])
        p.hline(sx - 7, 43, 15, SP[2])
    p.dith(2, 7, 60, 3, None, rust, 0.35)

    # 10 — barbed wire, on brackets along the top of the fence.  Cut-out.
    p = A.cell64(10)
    for bx in (8, 40):                               # the brackets
        p.line(bx, 62, bx + 10, 20, steel)
        p.line(bx + 1, 62, bx + 11, 20, steel_d)
        p.line(bx + 10, 20, bx + 22, 26, steel)
    for i, y in enumerate((22, 32, 42, 52)):         # four strands
        for x in range(0, 64):
            yy = y + ((x + i * 3) % 8 > 3)
            p.px(x, yy, steel_l if (x + i) % 3 else steel)
        for bx in range(2 + i * 3, 64, 11):          # the barbs
            p.px(bx, y - 2, steel_l)
            p.px(bx, y + 3, steel_l)
            p.px(bx - 1, y - 1, steel)
            p.px(bx + 1, y + 2, steel)
    p.dith(0, 20, 64, 44, None, rust, 0.14)

    # 11 — the sandpit, with no sand.
    p = A.cell64(11)
    p.fill(SP[2])
    p.speckle(0, 0, 64, 64, [(0.16, conc), (0.10, SP[12])], 160)
    p.frame(4, 10, 56, 46, timber)
    p.rect(5, 11, 54, 44, conc_d)
    p.speckle(6, 12, 52, 42, [(0.20, SP[12]), (0.08, conc)], 161)
    for i in range(4):                               # the seat boards, rotting
        p.rect(4, 10 + i * 15, 56, 3, timber)
        p.hline(4, 10 + i * 15, 56, SP[13])
    p.dith(6, 40, 52, 15, None, SP[9], 0.18)         # water in the bottom

    # 12 — the laundry line and its post.
    p = A.cell64(12)
    p.fill(SP[2])
    p.rect(28, 4, 7, 60, steel)
    p.vline(28, 4, 60, steel_l)
    p.vline(34, 4, 60, steel_d)
    p.rect(22, 4, 19, 4, steel)                      # the crosspiece
    p.hline(22, 4, 19, steel_l)
    for ly in (10, 16, 22):                          # the lines themselves
        for x in range(0, 64):
            p.px(x, ly + (0 if abs(x - 31) > 20 else 1), steel_l)
    p.rect(24, 56, 15, 8, conc)                      # set in concrete
    p.speckle(24, 56, 15, 8, [(0.2, conc_l)], 162)
    p.dith(28, 30, 7, 26, None, rust, 0.25)

    # 13, 14, 15 — concrete, steel and timber stock for the props' minor faces.
    p = A.cell64(13)
    p.fill(conc)
    p.speckle(0, 0, 64, 64, [(0.18, conc_l), (0.16, conc_d)], 163)
    p.hline(0, 32, 64, conc_d)
    p = A.cell64(14)
    p.fill(steel)
    p.speckle(0, 0, 64, 64, [(0.16, steel_l), (0.16, steel_d)], 164)
    p.dith(0, 40, 64, 24, None, rust, 0.28)
    p = A.cell64(15)
    p.fill(timber)
    planks(p, 0, 0, 64, 64, timber, SP[2], SP[13], bh=12, seed=165)
    return A


# ══════════════════════════════════════════════════════════════════════════════
# street_lamp — 64x128, 2x4 of 32.  The countdown's main visual.
# ══════════════════════════════════════════════════════════════════════════════


def build_street_lamp():
    A = Atlas("street_lamp", 64, 128, SL, alpha=True)
    conc, conc_l, conc_d, stain = SL[0], SL[1], SL[2], SL[3]
    steel, steel_d, steel_l = SL[4], SL[5], SL[6]
    arc, glass, glass2 = SL[7], SL[8], SL[9]
    dead, dead_l, rust, bowl, shadow = SL[10], SL[11], SL[12], SL[13], SL[14]

    # 0 — the pole: concrete, slightly tapered, weather-stained.  Opaque, it is
    #     wrapped round a mesh.
    p = A.cell(2, 32, 0)
    p.fill(conc)
    p.speckle(0, 0, 32, 32, [(0.12, conc_l), (0.14, conc_d)], 170)
    p.rect(0, 0, 7, 32, conc_l)                      # the lit side
    p.rect(25, 0, 7, 32, conc_d)                     # and the shaded one
    p.dithf(0, 0, 32, 32, None, stain,
            lambda u, v: 0.45 * (0.3 + 0.7 * (v / 31.0)) * (1.0 if 9 < u < 24 else 0.4))
    for cy in (6, 19):                               # the casting seams
        p.hline(0, cy, 32, conc_d)
        p.hline(0, cy + 1, 32, conc_l)
    p.rect(12, 24, 8, 5, steel_d)                    # the access hatch
    p.frame(12, 24, 8, 5, steel)

    # 1 — the steel bracket arm.  Cut-out.
    p = A.cell(2, 32, 1)
    for i in range(26):                              # the arm, rising then flat
        y = 20 - int(9 * (1 - (1 - i / 25.0) ** 2))
        p.rect(3 + i, y, 1, 4, steel)
        p.px(3 + i, y, steel_l)
        p.px(3 + i, y + 3, steel_d)
    p.rect(0, 14, 5, 12, steel)                      # the collar on the pole
    p.frame(0, 14, 5, 12, steel_d)
    p.hline(0, 15, 5, steel_l)
    p.line(4, 24, 16, 14, steel)                     # the stay
    p.line(4, 25, 16, 15, steel_d)
    p.dith(3, 10, 26, 3, None, rust, 0.3)
    p.rect(26, 8, 5, 5, steel_d)                     # where the fitting bolts on

    # 2 — lit: hard pale green-cyan.  Mercury, never sodium, never warm.
    p = A.cell(2, 32, 2)
    p.rect(4, 4, 24, 4, bowl)                        # the RKU's shallow bowl
    p.hline(4, 4, 24, SL[6])
    for i in range(9):                               # the reflector, flaring
        p.rect(4 - 0, 8 + i, 24, 1, bowl if i < 2 else steel)
    p.rect(2, 8, 28, 3, bowl)
    for i in range(7):                               # the glass, hottest inside
        w = 22 - i * 2
        p.rect(16 - w // 2, 11 + i, w, 1, glass if i > 1 else glass2)
    p.rect(11, 12, 10, 4, arc)                       # the arc tube itself
    p.dith(8, 11, 16, 8, None, arc, 0.45)
    # The halo is a stipple.  There is no blending here and there never will be.
    p.dithf(0, 8, 32, 24, None, glass2,
            lambda u, v: max(0.0, 0.85 - (((u - 16) / 15.0) ** 2 + ((v - 6) / 13.0) ** 2)))
    p.dithf(0, 18, 32, 14, None, glass2,
            lambda u, v: max(0.0, 0.4 - abs(u - 16) / 30.0))
    p.rect(13, 0, 6, 5, steel)                       # the neck onto the bracket
    p.frame(13, 0, 6, 5, steel_d)

    # 3 — dead: dark grey-green glass, and nothing else changes.  The pole
    #     stays.  The town keeps all five posts and loses all five lamps.
    p = A.cell(2, 32, 3)
    p.rect(4, 4, 24, 4, SL[13])
    p.hline(4, 4, 24, steel_l)
    for i in range(9):
        p.rect(4, 8 + i, 24, 1, SL[13] if i < 2 else steel)
    p.rect(2, 8, 28, 3, SL[13])
    for i in range(7):
        w = 22 - i * 2
        p.rect(16 - w // 2, 11 + i, w, 1, dead if i > 1 else dead_l)
    p.dith(9, 12, 14, 5, None, dead_l, 0.22)         # only the sky, in the glass
    p.rect(11, 12, 10, 3, dead)
    p.rect(13, 0, 6, 5, steel)
    p.frame(13, 0, 6, 5, steel_d)
    p.dith(4, 5, 24, 3, None, rust, 0.3)
    p.hline(4, 17, 24, shadow)

    # 4..7 — the pole again, so a tall post tessellates without an obvious
    # repeat, plus the base and the cable door.
    for i, s in zip(range(4, 7), (171, 172, 173)):
        p = A.cell(2, 32, i)
        p.fill(conc)
        p.speckle(0, 0, 32, 32, [(0.12, conc_l), (0.14, conc_d)], s)
        p.rect(0, 0, 7, 32, conc_l)
        p.rect(25, 0, 7, 32, conc_d)
        p.dithf(0, 0, 32, 32, None, stain,
                lambda u, v: 0.35 * (v / 31.0) * (1.0 if 9 < u < 24 else 0.35))
        if i == 6:
            p.dith(0, 20, 32, 12, None, stain, 0.5)  # the base is always filthy
    p = A.cell(2, 32, 7)
    p.fill(conc)
    p.speckle(0, 0, 32, 32, [(0.10, conc_l), (0.16, conc_d)], 174)
    p.rect(0, 0, 7, 32, conc_l)
    p.rect(25, 0, 7, 32, conc_d)
    p.rect(0, 22, 32, 10, conc_d)                    # the flare into the ground
    p.hline(0, 22, 32, conc_l)
    p.dith(0, 24, 32, 8, None, stain, 0.45)
    p.rect(11, 4, 10, 12, steel_d)                   # the cable door
    p.frame(11, 4, 10, 12, steel)
    p.px(19, 10, steel_l)
    p.dith(11, 5, 10, 10, None, rust, 0.35)
    return A


# ══════════════════════════════════════════════════════════════════════════════
# light_pool — 64x64, one region.  A decal on the asphalt under a live lamp.
# Every edge is an ordered stipple: binary cut-out is all this console has.
# ══════════════════════════════════════════════════════════════════════════════


def build_light_pool():
    A = Atlas("light_pool", 64, 64, LP, alpha=True)
    p = A.whole()

    def r2(u, v):
        return ((u - 32) / 31.0) ** 2 + ((v - 32) / 23.0) ** 2

    # Four rings, each stippled into the last, then stippled out to nothing.
    p.dithf(0, 0, 64, 64, None, LP[3], lambda u, v: max(0.0, 1.0 - r2(u, v)) * 1.6)
    p.dithf(0, 0, 64, 64, None, LP[2], lambda u, v: max(0.0, 0.78 - r2(u, v)) * 1.9)
    p.dithf(0, 0, 64, 64, None, LP[1], lambda u, v: max(0.0, 0.48 - r2(u, v)) * 2.6)
    p.dithf(0, 0, 64, 64, None, LP[0], lambda u, v: max(0.0, 0.18 - r2(u, v)) * 5.0)
    return A


# ══════════════════════════════════════════════════════════════════════════════
# sky — 128x64.  Flat overcast: no sun, no stars.  This is why the street is
# never pitch black, so it must never be flat black either.
# ══════════════════════════════════════════════════════════════════════════════


def build_sky():
    A = Atlas("sky", 128, 64, SK, alpha=False)
    p = A.whole()
    # Overhead at the top, the horizon at the bottom.  The 8x8 matrix is
    # deliberate: at 320x240 the pattern is visible, and it is meant to be.
    p.ramp(0, 0, 128, 64, SK[0:8], vertical=True, mat=B8)
    # Cloud, laid across the ramp rather than blended into it.
    for y0, h, c, t in ((10, 12, SK[8], 0.22), (26, 10, SK[9], 0.26),
                        (38, 12, SK[10], 0.30), (50, 14, SK[11], 0.34)):
        p.dithf(0, y0, 128, h, None, c,
                lambda u, v, h=h, t=t: t * (1.0 - abs(v - h / 2) / (h / 2 + 1))
                * (0.55 + 0.45 * (((u + v * 3) // 17) % 2)))
    p.dithf(0, 44, 128, 20, None, SK[11],
            lambda u, v: 0.35 * (v / 20.0))          # the murk at the horizon
    return A


# ══════════════════════════════════════════════════════════════════════════════
# gate — 128x128, 4x4 of 32.  The checkpoint.  L5 hangs over it: the last lamp
# burning in the game, and until then this is where the player is going.
# ══════════════════════════════════════════════════════════════════════════════


def build_gate():
    A = Atlas("gate", 128, 128, GT, alpha=True)
    br, br2, mortar, br_d = GT[0], GT[1], GT[2], GT[3]
    green, green_l, green_d = GT[4], GT[5], GT[6]
    warm, warm2, frame = GT[7], GT[8], GT[9]
    steel, steel_d, steel_l, rust, sign = GT[10], GT[11], GT[12], GT[13], GT[14]

    # 0 — silicate brick, pale and cold, nothing like the factory's red.
    p = A.cell32(0)
    brickwork(p, 0, 0, 32, 32, br, br2, mortar, bw=12, bh=5, seed=180)
    p.speckle(0, 0, 32, 32, [(0.07, br_d)], 181)
    p.dith(0, 24, 32, 8, None, br_d, 0.28)           # splash off the ground

    # 1 — the green door.
    p = A.cell32(1)
    p.fill(green)
    p.speckle(0, 0, 32, 32, [(0.08, green_l)], 182)
    p.frame(0, 0, 32, 32, green_d)
    p.frame(3, 3, 26, 12, green_d)
    p.rect(4, 4, 24, 10, green_l)
    p.dith(4, 4, 24, 10, None, green, 0.4)
    p.frame(3, 17, 26, 12, green_d)
    p.rect(4, 18, 24, 10, green_l)
    p.dith(4, 18, 24, 10, None, green, 0.4)
    p.rect(27, 15, 4, 2, steel)                      # the handle
    p.px(29, 16, steel_l)
    p.dith(0, 26, 32, 6, None, green_d, 0.3)

    # 2 — the lit window.  Warm yellow: someone is in there, and it is not
    #     the same light as the lamp above the gate.
    p = A.cell32(2)
    p.fill(br2)
    brickwork(p, 0, 0, 32, 32, br, br2, mortar, bw=12, bh=5, seed=183)
    p.rect(3, 5, 26, 22, frame)
    p.rect(5, 7, 22, 18, warm)
    p.dith(5, 7, 22, 18, None, warm2, 0.35)
    p.vline(16, 7, 18, frame)                        # the glazing bar
    p.hline(5, 15, 22, frame)
    p.rect(7, 16, 7, 8, warm2)                       # something in front of it
    p.rect(19, 9, 6, 5, GT[6])
    p.rect(2, 26, 28, 3, GT[2])                      # the sill
    p.hline(2, 26, 28, GT[9])
    p.dith(0, 29, 32, 3, None, br_d, 0.3)

    # 3 — the steel gate.
    p = A.cell32(3)
    p.fill(steel)
    p.rect(0, 0, 32, 3, steel_l)                     # the frame
    p.rect(0, 29, 32, 3, steel_d)
    p.vline(0, 0, 32, steel_l)
    p.vline(31, 0, 32, steel_d)
    for x in range(3, 31, 5):                        # sheeted, with ribs
        p.vline(x, 3, 26, steel_l)
        p.vline(x + 1, 3, 26, steel_d)
    p.line(1, 28, 30, 4, steel_l)                    # the cross-brace
    p.line(1, 29, 30, 5, steel_d)
    p.rect(12, 12, 8, 6, steel_d)                    # the lock plate
    p.frame(12, 12, 8, 6, steel_l)
    p.dith(0, 22, 32, 10, None, rust, 0.35)
    p.dith(0, 0, 32, 4, None, rust, 0.2)

    # 4 — the tripod turnstile.  Cut-out: it is a shape you walk through.
    p = A.cell32(4)
    p.rect(13, 6, 6, 26, steel)                      # the post
    p.vline(13, 6, 26, steel_l)
    p.vline(18, 6, 26, steel_d)
    p.rect(10, 28, 12, 4, GT[3])                     # its foot
    p.rect(11, 2, 10, 6, steel_d)                    # the head
    p.frame(11, 2, 10, 6, steel_l)
    for dx, dy in ((-13, 4), (13, 4), (0, 12)):      # three arms
        p.line(16, 8, 16 + dx, 8 + dy, steel)
        p.line(16, 9, 16 + dx, 9 + dy, steel_d)
        p.line(16, 7, 16 + dx, 7 + dy, steel_l)
        p.rect(16 + dx - 1, 8 + dy - 1, 3, 3, steel_l)
    p.dith(13, 18, 6, 14, None, rust, 0.3)

    # 5 — the sign on the gate.
    p = A.cell32(5)
    p.fill(sign)
    p.frame(0, 0, 32, 32, steel_d)
    p.frame(1, 1, 30, 30, GT[11])
    p.rect(2, 2, 28, 8, GT[13])                      # a red-oxide header band
    p.ctext(3, "ЗАВОД", sign, adv=6)
    p.ctext(13, "N 4", steel_d, adv=6)
    p.hline(4, 21, 24, steel_d)
    p.ctext(23, "ЦЕХ 2", steel_d, adv=5)
    p.speckle(2, 2, 28, 28, [(0.05, GT[2])], 184)
    p.rect(3, 3, 2, 2, GT[11])

    # 6 — brick, again, so the checkpoint's walls are not one stamp.
    p = A.cell32(6)
    brickwork(p, 0, 0, 32, 32, br2, br, mortar, bw=12, bh=5, seed=185)
    p.speckle(0, 0, 32, 32, [(0.09, br_d)], 186)
    p.dith(0, 0, 32, 32, None, br_d, 0.12)

    # 7 — the concrete plinth the brick sits on.
    p = A.cell32(7)
    p.fill(GT[2])
    p.speckle(0, 0, 32, 32, [(0.18, GT[1]), (0.14, br_d)], 187)
    p.hline(0, 0, 32, GT[0])
    p.rect(0, 26, 32, 6, br_d)
    p.dith(0, 20, 32, 12, None, GT[3], 0.35)

    # 8 — a dark window in the same building: nobody in that half.
    p = A.cell32(8)
    brickwork(p, 0, 0, 32, 32, br, br2, mortar, bw=12, bh=5, seed=188)
    p.rect(3, 5, 26, 22, frame)
    p.rect(5, 7, 22, 18, GT[11])
    p.dith(5, 7, 22, 18, None, GT[3], 0.3)
    p.vline(16, 7, 18, frame)
    p.hline(5, 15, 22, frame)
    p.rect(2, 26, 28, 3, GT[2])

    # 9 — the concrete gatepost.
    p = A.cell32(9)
    p.fill(GT[1])
    p.speckle(0, 0, 32, 32, [(0.15, GT[0]), (0.14, br_d)], 189)
    p.rect(0, 0, 5, 32, GT[0])
    p.rect(27, 0, 5, 32, br_d)
    p.rect(0, 0, 32, 3, GT[0])
    for hy in (10, 22):                              # the hinge straps
        p.rect(24, hy, 8, 4, steel)
        p.hline(24, hy, 8, steel_l)
        p.dith(24, hy + 4, 8, 6, None, rust, 0.4)

    # 10 — barbed wire over the gate, cut-out, matching street_props.
    p = A.cell32(10)
    for i, y in enumerate((6, 14, 22)):
        for x in range(32):
            p.px(x, y + ((x + i * 2) % 6 > 2), steel_l if (x + i) % 3 else steel)
        for bx in range(1 + i * 2, 32, 7):
            p.px(bx, y - 2, steel_l)
            p.px(bx, y + 3, steel_l)
    p.rect(2, 24, 3, 8, steel)                       # the bracket
    p.line(3, 24, 12, 4, steel)
    p.line(4, 24, 13, 4, steel_d)
    p.dith(0, 4, 32, 24, None, rust, 0.12)

    # 11 — asphalt at the gate, so the threshold is not the street's atlas.
    p = A.cell32(11)
    p.fill(GT[11])
    p.speckle(0, 0, 32, 32, [(0.16, GT[2]), (0.12, GT[3])], 190)
    p.rect(0, 14, 32, 4, GT[14])                     # a painted stop line
    p.dith(0, 14, 32, 4, None, GT[11], 0.4)

    # 12..15 — brick and steel stock.
    for i, (fn, s) in enumerate(((0, 191), (1, 192), (2, 193), (3, 194))):
        p = A.cell32(12 + i)
        if fn < 2:
            brickwork(p, 0, 0, 32, 32, br if fn else br2, br2 if fn else br,
                      mortar, bw=12, bh=5, seed=s)
            p.speckle(0, 0, 32, 32, [(0.08, br_d)], s + 1)
        else:
            p.fill(steel)
            p.speckle(0, 0, 32, 32, [(0.16, steel_l), (0.16, steel_d)], s)
            if fn == 3:
                p.dith(0, 0, 32, 32, None, rust, 0.3)
    return A


# ══════════════════════════════════════════════════════════════════════════════
# factory_walls — 256x256, 4x4 of 64.  Opaque.
# ══════════════════════════════════════════════════════════════════════════════


def build_factory_walls():
    A = Atlas("factory_walls", 256, 256, FW, alpha=False)
    br, br2, mortar = FW[0], FW[1], FW[2]
    wash, wash2 = FW[3], FW[4]
    green, green_l, line = FW[5], FW[6], FW[7]
    conc, conc_l, conc_d = FW[8], FW[9], FW[10]
    yellow, black, steel, steel_d, glass = FW[11], FW[12], FW[13], FW[14], FW[15]

    # 0 — dull green oil paint to 1.8 m, and the line that ends it.
    p = A.cell64(0)
    brickwork(p, 0, 0, 64, 64, br, br2, mortar, bw=18, bh=7, seed=200)
    p.rect(0, 12, 64, 52, green)                     # the paint, over the brick
    p.speckle(0, 12, 64, 52, [(0.12, green_l), (0.08, FW[7])], 201)
    for by in range(19, 64, 7):                      # the courses still show
        p.hline(0, by, 64, line)
    p.rect(0, 9, 64, 3, line)                        # the painted line
    p.hline(0, 8, 64, wash2)
    p.dith(0, 46, 64, 18, None, green_l, 0.22)       # rubbed where people pass
    for fx, fy in ((14, 30), (44, 22)):              # grime rings round brackets
        p.disc(fx, fy, 6, line, filled=False)
        p.dith(fx - 5, fy - 5, 11, 11, None, line, 0.2)
    p.dith(0, 0, 64, 9, None, wash2, 0.5)

    # 1 — whitewash above the line, flaking off the brick behind it.
    p = A.cell64(1)
    brickwork(p, 0, 0, 64, 64, br, br2, mortar, bw=18, bh=7, seed=202)
    p.dith(0, 0, 64, 64, None, wash, 0.88)
    p.speckle(0, 0, 64, 64, [(0.20, wash2)], 203)
    rnd = random.Random(204)
    for _ in range(16):                              # the flakes, gone
        fx, fy = rnd.randrange(0, 58), rnd.randrange(0, 58)
        fw, fh = rnd.randrange(3, 9), rnd.randrange(2, 7)
        p.rect(fx, fy, fw, fh, br if rnd.random() < 0.6 else br2)
        p.hline(fx, fy, fw, mortar)
    p.dith(0, 52, 64, 12, None, mortar, 0.22)
    for fx, fy in ((20, 14), (48, 40)):
        p.dith(fx - 6, fy - 6, 13, 13, None, mortar, 0.22)

    # 2 — a column: hazard band, stencilled number, chipped corners.
    p = A.cell64(2)
    p.fill(conc)
    p.speckle(0, 0, 64, 64, [(0.16, conc_l), (0.14, conc_d)], 205)
    p.rect(0, 0, 8, 64, conc_l)                      # its lit and shaded faces
    p.rect(56, 0, 8, 64, conc_d)
    for y in range(38, 64, 8):                       # the black/yellow band
        p.rect(0, y, 64, 8, yellow)
        for x in range(-8, 64, 8):
            for k in range(8):
                p.rect(x + k + (y - 38), y + k, 4, 1, black)
    p.rect(0, 36, 64, 2, black)
    p.rect(0, 62, 64, 2, black)
    p.text(20, 12, "12", black, adv=9)               # stencilled
    p.rect(19, 11, 26, 1, conc_d)
    for cx, cy in ((0, 20), (61, 8), (2, 50), (58, 30)):   # chipped corners
        p.rect(cx, cy, 3, 5, conc_d)
        p.px(cx + 1, cy + 2, mortar)
    p.dith(0, 26, 64, 10, None, conc_d, 0.18)

    # 3 — a riveted roof truss, dull silver, thick with dust.
    p = A.cell64(3)
    p.fill(FW[10])
    p.rect(0, 4, 64, 8, steel)                       # top and bottom chords
    p.hline(0, 4, 64, FW[9])
    p.hline(0, 11, 64, steel_d)
    p.rect(0, 52, 64, 8, steel)
    p.hline(0, 52, 64, FW[9])
    p.hline(0, 59, 64, steel_d)
    for i in range(4):                               # the web, zig-zagging
        x0 = i * 16
        for t in range(41):
            p.rect(x0 + int(t * 16 / 41), 12 + t, 4, 1, steel)
            p.rect(x0 + 16 - int(t * 16 / 41), 12 + t, 4, 1, steel_d)
        p.rect(x0 + 6, 12, 4, 40, steel)
    rivets(p, 2, 6, 62, 4, FW[9], step=6)
    rivets(p, 2, 54, 62, 4, FW[9], step=6)
    p.dith(0, 4, 64, 4, None, FW[9], 0.35)           # dust, on every top face
    p.dith(0, 52, 64, 3, None, FW[9], 0.35)

    # 4 — the roof lantern: dirty wired glass, weak colourless daylight.
    p = A.cell64(4)
    p.fill(glass)
    p.speckle(0, 0, 64, 64, [(0.16, FW[9]), (0.14, FW[4])], 206)
    for x in range(0, 64, 4):                        # the wire, in the glass
        p.vline(x, 0, 64, FW[9])
    for y in range(0, 64, 4):
        p.hline(0, y, 64, FW[9])
    p.rect(0, 0, 64, 3, steel_d)                     # the glazing bars
    p.rect(0, 30, 64, 3, steel_d)
    p.rect(0, 61, 64, 3, steel_d)
    p.vline(0, 0, 64, steel_d)
    p.vline(31, 0, 64, steel_d)
    p.dithf(0, 0, 64, 64, None, FW[4],               # decades of dirt, uneven
            lambda u, v: 0.3 + 0.35 * (((u // 7) + (v // 5)) % 2))
    p.dith(0, 0, 64, 8, None, steel, 0.3)

    # 5 — the crane rail, running the length of the bay.
    p = A.cell64(5)
    p.fill(conc)
    p.speckle(0, 0, 64, 64, [(0.14, conc_l), (0.14, conc_d)], 207)
    p.rect(0, 0, 64, 18, conc_d)                     # the corbel
    p.rect(0, 18, 64, 4, conc_l)
    p.rect(0, 26, 64, 10, steel)                     # the rail's web
    p.hline(0, 26, 64, FW[9])
    p.hline(0, 35, 64, steel_d)
    p.rect(0, 22, 64, 4, steel)                      # its head, polished
    p.hline(0, 23, 64, FW[15])
    p.rect(0, 36, 64, 5, steel_d)                    # its foot
    rivets(p, 3, 28, 60, 6, FW[9], step=8)
    p.dith(0, 41, 64, 23, None, conc_d, 0.3)
    p.dith(0, 36, 64, 6, None, FW[11], 0.10)         # a smear of yellow paint

    # 6..15 — the bay is mostly wall.  Two paints, two heights, several seeds.
    for i, (kind, s) in enumerate(((0, 210), (1, 211), (0, 212), (1, 213),
                                   (0, 214), (1, 215), (2, 216), (2, 217),
                                   (1, 218), (0, 219))):
        p = A.cell64(6 + i)
        if kind == 0:
            brickwork(p, 0, 0, 64, 64, br, br2, mortar, bw=18, bh=7, seed=s)
            p.rect(0, 0, 64, 64, green)
            p.speckle(0, 0, 64, 64, [(0.13, green_l), (0.09, line)], s + 1)
            for by in range(4, 64, 7):
                p.hline(0, by, 64, line)
            p.dith(0, 40, 64, 24, None, green_l, 0.18)
        elif kind == 1:
            brickwork(p, 0, 0, 64, 64, br, br2, mortar, bw=18, bh=7, seed=s)
            p.dith(0, 0, 64, 64, None, wash, 0.86)
            p.speckle(0, 0, 64, 64, [(0.18, wash2)], s + 1)
            rnd = random.Random(s + 2)
            for _ in range(12):
                fx, fy = rnd.randrange(0, 58), rnd.randrange(0, 58)
                p.rect(fx, fy, rnd.randrange(3, 8), rnd.randrange(2, 6), br)
        else:
            p.fill(conc)
            p.speckle(0, 0, 64, 64, [(0.16, conc_l), (0.14, conc_d)], s)
            p.rect(0, 0, 8, 64, conc_l)
            p.rect(56, 0, 8, 64, conc_d)
            p.dith(0, 44, 64, 20, None, conc_d, 0.2)
    return A


# ══════════════════════════════════════════════════════════════════════════════
# factory_floor — 128x128, 4x4 of 32.  The yellow lines are a navigation
# anchor and must stay readable at every tier, so they get the brightest
# entry in the palette and are never dithered down.
# ══════════════════════════════════════════════════════════════════════════════


def concrete(p, seed):
    p.fill(FF[0])
    p.speckle(0, 0, 32, 32, [(0.16, FF[1]), (0.16, FF[2]), (0.05, FF[11])], seed)


def build_factory_floor():
    A = Atlas("factory_floor", 128, 128, FF, alpha=False)

    # 0 — poured concrete, with swarf.
    p = A.cell32(0)
    concrete(p, 220)
    p.line(4, 2, 11, 9, FF[15])
    p.line(20, 24, 29, 17, FF[15])
    p.scatter(0, 0, 32, 32, FF[8], 14, 221)          # metal swarf
    p.scatter(0, 0, 32, 32, FF[12], 10, 222)

    # 1 — oil-stained.
    p = A.cell32(1)
    concrete(p, 223)
    p.dithf(0, 0, 32, 32, None, FF[4],
            lambda u, v: max(0.0, 1.15 - (((u - 15) / 13.0) ** 2 + ((v - 17) / 11.0) ** 2)))
    p.dithf(0, 0, 32, 32, None, FF[3],
            lambda u, v: max(0.0, 1.5 - (((u - 15) / 15.0) ** 2 + ((v - 17) / 14.0) ** 2)) * 0.5)
    p.dith(8, 10, 12, 8, None, FF[5], 0.3)           # the sheen a fixture puts on it
    p.scatter(0, 0, 32, 32, FF[8], 8, 224)

    # 2 — the yellow walkway line.
    p = A.cell32(2)
    concrete(p, 225)
    p.rect(0, 11, 32, 10, FF[6])
    p.hline(0, 11, 32, FF[7])
    p.hline(0, 20, 32, FF[7])
    p.speckle(0, 12, 32, 8, [(0.13, FF[7])], 226)    # walked on, not worn out
    for sx in (6, 19, 27):
        p.rect(sx, 13, 3, 6, FF[7])

    # 3 — the corner where a machine area is boxed out.
    p = A.cell32(3)
    concrete(p, 227)
    p.rect(4, 4, 24, 6, FF[6])
    p.rect(4, 4, 6, 24, FF[6])
    p.speckle(4, 4, 24, 24, [(0.10, FF[7])], 228)
    p.hline(4, 4, 24, FF[7])
    p.vline(4, 4, 24, FF[7])
    p.dith(12, 14, 16, 14, None, FF[4], 0.25)        # oil, inside the box

    # 4 — coolant, gone milky, standing where the floor dips.
    p = A.cell32(4)
    concrete(p, 229)
    p.dithf(0, 0, 32, 32, None, FF[9],
            lambda u, v: 1.0 if (((u - 16) / 14.0) ** 2 + ((v - 16) / 10.0) ** 2) < 1 else 0.0)
    p.dithf(0, 0, 32, 32, None, FF[10],
            lambda u, v: 0.55 if (((u - 16) / 11.0) ** 2 + ((v - 16) / 7.0) ** 2) < 1 else 0.0)
    p.ellipse(16, 16, 14, 10, FF[14], filled=False)
    p.scatter(4, 8, 24, 16, FF[8], 8, 230)           # swarf, floating in it

    # 5 — a floor crack, and the grit that collects in it.
    p = A.cell32(5)
    concrete(p, 231)
    p.line(0, 6, 31, 21, FF[15])
    p.line(0, 7, 31, 22, FF[3])
    p.line(14, 14, 22, 31, FF[15])
    p.dith(0, 6, 32, 4, None, FF[11], 0.3)

    # 6 — an inspection plate.
    p = A.cell32(6)
    concrete(p, 232)
    p.rect(4, 6, 24, 20, FF[11])
    p.frame(4, 6, 24, 20, FF[8])
    for gy in range(8, 25, 3):
        p.rect(6, gy, 20, 2, FF[2])
        p.hline(6, gy, 20, FF[12])
    p.dith(4, 20, 24, 6, None, FF[13], 0.35)

    # 7 — where a machine's feet have ground the floor away.
    p = A.cell32(7)
    concrete(p, 233)
    for dx, dy in ((5, 5), (21, 5), (5, 21), (21, 21)):
        p.rect(dx, dy, 6, 5, FF[3])
        p.frame(dx - 1, dy - 1, 8, 7, FF[2])
        p.dith(dx, dy, 6, 5, None, FF[4], 0.4)

    # 8..15 — more floor.  A shop is 24 x 14 m and one repeating stamp shows.
    for i, s in zip(range(8, 16), range(240, 248)):
        p = A.cell32(i)
        concrete(p, s)
        if i in (9, 12):
            p.dith(0, 0, 32, 32, None, FF[4], 0.16)
        if i == 10:
            p.rect(0, 0, 32, 32, FF[2])
            p.speckle(0, 0, 32, 32, [(0.2, FF[0]), (0.1, FF[3])], s)
        if i == 11:
            p.scatter(0, 0, 32, 32, FF[8], 30, s)
        if i == 13:
            p.dith(0, 0, 32, 32, None, FF[14], 0.18)
        if i == 14:
            p.rect(11, 0, 10, 32, FF[6])             # a line, running the other way
            p.vline(11, 0, 32, FF[7])
            p.vline(20, 0, 32, FF[7])
            p.speckle(12, 0, 8, 32, [(0.12, FF[7])], s + 1)
        if i == 15:
            p.dith(0, 0, 32, 32, None, FF[13], 0.14)
    return A


# ══════════════════════════════════════════════════════════════════════════════
# factory_machines — 256x256, 4x4 of 64.  The working set of one assembly shop.
# ══════════════════════════════════════════════════════════════════════════════


def build_factory_machines():
    A = Atlas("factory_machines", 256, 256, FM, alpha=False)
    st, st_d, st_l, st_k = FM[0], FM[1], FM[2], FM[3]
    grn, grn_l = FM[4], FM[5]
    wd, wd_d, rust, red = FM[6], FM[7], FM[8], FM[9]
    white, black, canvas, enamel, blue, card = FM[10], FM[11], FM[12], FM[13], FM[14], FM[15]

    # 0 — the assembly bench.  This is where the three beats happen.
    p = A.cell64(0)
    p.fill(st_k)
    p.rect(0, 10, 64, 8, st)                         # the top
    p.hline(0, 10, 64, st_l)
    p.hline(0, 17, 64, st_d)
    p.speckle(0, 11, 64, 6, [(0.14, st_d), (0.06, rust)], 250)
    p.rect(0, 18, 64, 6, st_d)                       # the apron
    for lx in (4, 52):                               # the legs
        p.rect(lx, 24, 8, 40, st)
        p.vline(lx, 24, 40, st_l)
        p.vline(lx + 7, 24, 40, st_d)
    p.rect(4, 44, 56, 5, st_d)                       # the lower shelf
    p.hline(4, 44, 56, st)
    p.rect(38, 0, 18, 11, st)                        # the vice
    p.rect(40, 2, 6, 8, st_l)
    p.rect(48, 2, 6, 8, st_l)
    p.rect(46, 4, 3, 4, st_d)
    p.rect(52, 4, 10, 2, st_d)
    p.rect(61, 3, 3, 5, red)
    p.rect(6, 2, 18, 8, wd)                          # the wooden mallet
    p.hline(6, 2, 18, FM[15])
    p.rect(24, 5, 14, 3, wd_d)
    p.scatter(26, 0, 12, 9, st_l, 16, 251)           # scattered fixings
    p.scatter(4, 45, 56, 4, card, 10, 252)
    p.dith(0, 24, 64, 20, None, st_k, 0.35)

    # 1 — the welding post: a curtain screen, and the helmet on its hook.
    p = A.cell64(1)
    p.fill(st_k)
    p.rect(2, 2, 60, 5, st)                          # the frame
    p.hline(2, 2, 60, st_l)
    p.rect(3, 7, 58, 44, canvas)                     # the curtain
    for x in range(3, 61, 5):                        # its folds — stipple, not shade
        p.dith(x, 7, 3, 44, None, grn, 0.55)
        p.vline(x, 7, 44, grn_l)
    p.speckle(3, 7, 58, 44, [(0.05, st_k)], 253)     # scorch marks
    p.dith(6, 34, 22, 16, None, st_k, 0.3)
    p.rect(3, 51, 58, 3, grn)
    for lx in (4, 56):                               # the frame's feet
        p.rect(lx, 54, 5, 10, st)
        p.rect(lx - 2, 61, 9, 3, st_d)
    p.rect(38, 10, 18, 16, st_d)                     # the helmet, on its hook
    p.rect(40, 12, 14, 9, black)
    p.rect(42, 14, 10, 4, blue)
    p.rect(44, 8, 6, 3, st)
    p.rect(14, 56, 34, 6, st_k)                      # a scorched sheet on the floor
    p.speckle(14, 56, 34, 6, [(0.3, st_d)], 254)

    # 2 — gas cylinders, chained to the wall.
    p = A.cell64(2)
    p.fill(st_k)
    for i, (cx, body, cap) in enumerate(((10, black, red), (30, white, black),
                                         (50, grn, white))):
        p.rect(cx - 8, 12, 17, 46, body)             # the bottle
        p.vline(cx - 8, 12, 46, st_l if body is not white else FM[0])
        p.vline(cx + 8, 12, 46, st_d)
        for k in range(5):                           # its shoulder
            p.rect(cx - 8 + k, 12 - k, 17 - 2 * k, 1, body)
        p.rect(cx - 3, 2, 7, 6, cap)                 # the valve guard
        p.rect(cx - 5, 7, 11, 3, st)
        p.rect(cx - 1, 0, 3, 3, st_l)
        p.rect(cx - 8, 56, 17, 4, st_d)
        p.dith(cx - 8, 44, 17, 14, None, rust, 0.22)
    for cy in (24, 44):                              # the chain across them
        for x in range(0, 64, 2):
            p.px(x, cy + (x // 2) % 2, st_l)
            p.px(x + 1, cy + (x // 2) % 2, st_d)

    # 3 — the finishing conveyor.  The lamppost leaves on this, in view.
    p = A.cell64(3)
    p.fill(st_k)
    p.rect(0, 20, 64, 6, st_d)                       # the frame rails
    p.rect(0, 40, 64, 6, st_d)
    for x in range(1, 64, 8):                        # the rollers
        p.rect(x, 24, 6, 18, st)
        p.vline(x, 24, 18, st_l)
        p.vline(x + 5, 24, 18, st_d)
        p.rect(x, 24, 6, 2, st_l)
    p.rect(0, 12, 64, 5, st)                         # the guard rail
    p.hline(0, 12, 64, st_l)
    p.hline(0, 16, 64, st_d)
    for lx in range(4, 64, 20):                      # its stanchions
        p.rect(lx, 17, 4, 5, st)
        p.rect(lx, 46, 4, 18, st)                    # and the legs
        p.rect(lx - 2, 61, 8, 3, st_d)
    p.rect(0, 46, 64, 3, st_k)
    p.dith(0, 40, 64, 6, None, rust, 0.2)
    p.dith(0, 49, 64, 15, None, st_k, 0.4)

    # 4 — steel shelving, with boxes.
    p = A.cell64(4)
    p.fill(st_k)
    for sy in (2, 22, 42):                           # the shelves
        p.rect(0, sy + 14, 64, 4, st)
        p.hline(0, sy + 14, 64, st_l)
        p.hline(0, sy + 17, 64, st_d)
        rnd = random.Random(255 + sy)
        bx = 2
        while bx < 60:                               # what is on them
            bw = rnd.randrange(9, 17)
            bh = rnd.randrange(7, 14)
            c = (card, grn, blue, st, white)[rnd.randrange(0, 5)]
            p.rect(bx, sy + 14 - bh, bw, bh, c)
            p.frame(bx, sy + 14 - bh, bw, bh, st_k)
            p.hline(bx + 1, sy + 14 - bh + 2, bw - 2, st_k)
            bx += bw + 2
    for ux in (1, 58):                               # the uprights
        p.rect(ux, 0, 5, 64, st)
        p.vline(ux, 0, 64, st_l)
        p.vline(ux + 4, 0, 64, st_d)
        for hy in range(3, 64, 6):
            p.rect(ux + 1, hy, 3, 2, st_d)

    # 5 — a wooden pallet.
    p = A.cell64(5)
    p.fill(st_k)
    for i, y in enumerate(range(6, 60, 9)):          # the deck boards
        p.rect(2, y, 60, 6, wd)
        p.hline(2, y, 60, FM[15])
        p.hline(2, y + 5, 60, wd_d)
        p.speckle(2, y, 60, 6, [(0.10, wd_d)], 260 + i)
    for bx in (4, 30, 55):                           # the bearers, end on
        p.rect(bx, 4, 6, 56, wd_d)
        p.vline(bx, 4, 56, wd)
    p.scatter(2, 6, 60, 54, FM[15], 20, 261)         # nail heads
    p.dith(0, 52, 64, 12, None, st_k, 0.25)

    # 6 — the drill press.
    p = A.cell64(6)
    p.fill(st_k)
    p.rect(24, 0, 16, 20, grn)                       # the head
    p.rect(24, 0, 16, 3, grn_l)
    p.vline(24, 0, 20, grn_l)
    p.vline(39, 0, 20, FM[3])
    p.rect(40, 4, 14, 6, grn)                        # the motor
    p.rect(40, 4, 14, 2, grn_l)
    p.rect(20, 6, 5, 4, red)                         # the feed handle
    p.rect(14, 7, 7, 2, st)
    p.rect(28, 20, 6, 12, st)                        # the quill
    p.vline(28, 20, 12, st_l)
    p.rect(29, 32, 4, 5, st_d)                       # the chuck
    p.rect(30, 37, 2, 4, st_l)
    p.rect(14, 42, 34, 6, st)                        # the table
    p.hline(14, 42, 34, st_l)
    p.hline(14, 47, 34, st_d)
    p.rect(28, 12, 6, 52, st)                        # the column
    p.vline(28, 12, 52, st_l)
    p.vline(33, 12, 52, st_d)
    p.rect(12, 58, 40, 6, st_d)                      # the base
    p.hline(12, 58, 40, st)
    p.dith(12, 58, 40, 6, None, rust, 0.2)
    p.scatter(14, 40, 34, 3, st_l, 10, 262)

    # 7 — an oil drum.
    p = A.cell64(7)
    p.fill(st_k)
    p.rect(10, 4, 44, 56, blue)
    p.rect(10, 4, 5, 56, FM[0])                      # the lit flank
    p.rect(49, 4, 5, 56, FM[3])
    p.rect(10, 4, 44, 4, st)                         # the top
    p.hline(10, 4, 44, st_l)
    p.rect(10, 56, 44, 4, st_d)
    for ry in (20, 36):                              # the rolling hoops
        p.rect(10, ry, 44, 4, st)
        p.hline(10, ry, 44, st_l)
        p.hline(10, ry + 3, 44, st_d)
    p.rect(20, 5, 8, 3, st_l)                        # the bung
    p.dith(10, 40, 44, 20, None, rust, 0.35)
    p.dith(10, 4, 44, 6, None, rust, 0.25)
    p.rect(30, 14, 16, 4, white)                     # a stencilled label, unreadable
    p.speckle(30, 14, 16, 4, [(0.4, blue)], 263)

    # 8 — the crane gantry, parked at the far end.  It never moves.
    p = A.cell64(8)
    p.fill(st_k)
    p.rect(0, 4, 64, 10, st)                         # the bridge girder
    p.hline(0, 4, 64, st_l)
    p.hline(0, 13, 64, st_d)
    rivets(p, 2, 6, 60, 6, st_l, step=7)
    p.rect(18, 14, 28, 16, grn)                      # the crab
    p.rect(18, 14, 28, 3, grn_l)
    p.rect(20, 18, 10, 9, FM[3])
    p.rect(34, 18, 9, 9, st_d)
    p.rect(44, 16, 6, 12, red)
    for wx in (4, 54):                               # the end carriages
        p.rect(wx, 14, 8, 8, st)
        p.disc(wx + 4, 20, 3, st_d)
    p.rect(30, 30, 4, 18, st_l)                      # the hoist rope
    p.rect(30, 30, 1, 18, st_d)
    p.rect(26, 48, 12, 8, st)                        # the hook block
    p.hline(26, 48, 12, st_l)
    p.line(32, 56, 32, 62, st_l)                     # and the hook
    p.line(32, 62, 27, 60, st_l)
    p.line(33, 56, 33, 62, st_d)
    p.dith(0, 4, 64, 3, None, st_l, 0.3)             # dust on the top flange

    # 9 — the kettle on its stool, and two enamel mugs.
    p = A.cell64(9)
    p.fill(st_k)
    p.rect(8, 34, 48, 5, wd)                         # the stool
    p.hline(8, 34, 48, FM[15])
    p.hline(8, 38, 48, wd_d)
    for lx in (11, 48):
        p.rect(lx, 39, 5, 25, wd_d)
        p.vline(lx, 39, 25, wd)
    p.rect(11, 52, 42, 3, wd_d)
    p.rect(16, 12, 24, 22, st)                       # the kettle
    p.rect(16, 12, 5, 22, st_l)
    p.rect(35, 12, 5, 22, FM[3])
    p.rect(18, 8, 20, 4, st)
    p.rect(24, 4, 8, 5, st_d)                        # its lid
    p.rect(26, 2, 4, 3, black)
    p.line(40, 16, 50, 12, st)                       # the spout
    p.line(40, 17, 50, 13, st_d)
    p.line(14, 14, 8, 22, st)                        # the handle
    p.line(8, 22, 14, 30, st)
    p.dith(16, 26, 24, 8, None, rust, 0.25)
    for mx, mc in ((42, enamel), (52, blue)):        # two enamel mugs
        p.rect(mx, 24, 9, 10, mc)
        p.rect(mx, 24, 9, 2, white)
        p.rect(mx + 9, 27, 3, 4, mc)
        p.px(mx + 1, 28, st_k)
    p.dith(8, 56, 48, 8, None, st_k, 0.4)

    # 10..15 — machine stock: painted steel, bare steel, timber, cardboard.
    for i, (kind, s) in enumerate(((0, 270), (1, 271), (2, 272), (0, 273),
                                   (1, 274), (2, 275))):
        p = A.cell64(10 + i)
        if kind == 0:
            p.fill(st)
            p.speckle(0, 0, 64, 64, [(0.14, st_l), (0.16, st_d)], s)
            p.rect(0, 0, 64, 4, st_l)
            p.dith(0, 44, 64, 20, None, rust, 0.24)
        elif kind == 1:
            p.fill(grn)
            p.speckle(0, 0, 64, 64, [(0.12, grn_l), (0.10, st_k)], s)
            p.rect(0, 0, 64, 3, grn_l)
            p.dith(0, 48, 64, 16, None, st_k, 0.25)
        else:
            p.fill(wd)
            planks(p, 0, 0, 64, 64, wd, wd_d, FM[15], bh=13, seed=s)
    return A


# ══════════════════════════════════════════════════════════════════════════════
# factory_board — 128x64, 4x2 of 32.
#
# The single most important object in the game.  The header and the digit are
# drawn as TEXT from the font atlas at run time, so this file is only the panel
# they are painted on — but the panel has to look like it is lit from within,
# because the board is the one thing that is always readable.
# ══════════════════════════════════════════════════════════════════════════════


def build_factory_board():
    A = Atlas("factory_board", 128, 64, FB, alpha=False)
    steel, steel_l, steel_d = FB[0], FB[1], FB[2]
    frame, frame_d = FB[3], FB[4]
    glow, glow2, grime = FB[5], FB[6], FB[7]
    red, red_d, white = FB[8], FB[9], FB[10]
    photo, photo_d, rule, rust, ochre = FB[11], FB[12], FB[13], FB[14], FB[15]

    # 0 — the panel itself.  Dim bulbs behind a grimy plastic strip, so the
    #     lettering sits on a field that is brighter in the middle.
    p = A.cell(4, 32, 0)
    p.fill(steel)
    p.speckle(0, 0, 32, 32, [(0.10, steel_l), (0.10, steel_d)], 280)
    p.dithf(0, 0, 32, 32, None, glow2,                # the backlight, uneven
            lambda u, v: max(0.0, 0.85 - (((u - 16) / 19.0) ** 2 + ((v - 15) / 15.0) ** 2)))
    p.dithf(0, 0, 32, 32, None, glow,
            lambda u, v: max(0.0, 0.5 - (((u - 16) / 15.0) ** 2 + ((v - 14) / 11.0) ** 2)) * 1.6)
    p.rect(0, 0, 32, 2, grime)                        # the plastic strip's edges
    p.rect(0, 30, 32, 2, grime)
    p.speckle(0, 0, 32, 32, [(0.05, grime)], 281)     # decades of shop dust
    p.hline(0, 15, 32, steel_d)                       # the seam between halves
    p.hline(0, 16, 32, steel_l)

    # 1 — the frame the panel is set into.
    p = A.cell(4, 32, 1)
    p.fill(frame)
    p.speckle(0, 0, 32, 32, [(0.14, FB[1]), (0.12, frame_d)], 282)
    p.rect(0, 0, 32, 4, FB[1])                        # a top face catching light
    p.rect(0, 4, 32, 2, frame_d)
    p.rect(0, 26, 32, 6, frame_d)
    p.hline(0, 26, 32, frame)
    p.vline(0, 0, 32, FB[1])
    p.vline(31, 0, 32, frame_d)
    for by in range(6, 27, 7):                        # its fixing bolts
        p.rect(4, by, 3, 3, FB[1])
        p.px(5, by + 1, frame_d)
        p.rect(25, by, 3, 3, FB[1])
        p.px(26, by + 1, frame_d)
    p.dith(0, 20, 32, 12, None, rust, 0.2)

    # 2 — the socialist-competition honour board, with its faded photographs.
    p = A.cell(4, 32, 2)
    p.fill(FB[10])
    p.frame(0, 0, 32, 32, frame_d)
    p.rect(1, 1, 30, 8, red)                          # the header band
    p.dith(1, 1, 30, 8, None, red_d, 0.3)
    p.ctext(2, "ДОСКА", white, adv=5)
    p.rect(1, 9, 30, 2, ochre)
    for i in range(4):                                # four faces, gone flat
        px_, py = 2 + (i % 2) * 15, 12 + (i // 2) * 10
        p.rect(px_, py, 13, 9, photo)
        p.frame(px_, py, 13, 9, photo_d)
        p.disc(px_ + 6, py + 5, 3, FB[10])
        p.rect(px_ + 3, py + 7, 7, 2, photo_d)
        p.dith(px_ + 1, py + 1, 11, 7, None, photo_d, 0.18)
    p.hline(1, 22, 30, rule)
    p.speckle(1, 11, 30, 20, [(0.04, rule)], 283)

    # 3 — the duty roster.
    p = A.cell(4, 32, 3)
    p.fill(FB[10])
    p.frame(0, 0, 32, 32, frame_d)
    p.rect(1, 1, 30, 8, FB[13])
    p.ctext(2, "ГРАФИК", white, adv=5)
    for gy in range(11, 31, 4):                       # the ruled rows
        p.hline(1, gy, 30, rule)
        p.rect(2, gy + 1, 9, 2, photo)                # a name
        p.rect(13, gy + 1, 5, 2, photo_d)             # a date
        p.rect(20, gy + 1, 8, 2, photo)
    p.vline(12, 10, 21, rule)
    p.vline(19, 10, 21, rule)
    p.speckle(1, 10, 30, 21, [(0.05, FB[7])], 284)
    p.rect(27, 26, 4, 4, ochre)                       # a corner someone marked

    # 4..7 — painted steel stock for the board's returns and the panel edges.
    for i, (kind, s) in enumerate(((0, 285), (1, 286), (0, 287), (1, 288))):
        p = A.cell(4, 32, 4 + i)
        if kind == 0:
            p.fill(steel)
            p.speckle(0, 0, 32, 32, [(0.14, steel_l), (0.14, steel_d)], s)
            p.hline(0, 0, 32, steel_l)
            p.dith(0, 22, 32, 10, None, rust, 0.22)
        else:
            p.fill(frame)
            p.speckle(0, 0, 32, 32, [(0.14, FB[1]), (0.12, frame_d)], s)
            p.rect(0, 0, 32, 3, FB[1])
            p.rect(0, 29, 32, 3, frame_d)
    return A


# ══════════════════════════════════════════════════════════════════════════════
# factory_signs — 128x128, 4x4 of 32.  Period dressing that supports the
# board's reading without explaining it.
# ══════════════════════════════════════════════════════════════════════════════


def build_factory_signs():
    A = Atlas("factory_signs", 128, 128, FS, alpha=False)
    white, off, red, black = FS[0], FS[1], FS[2], FS[3]
    wall, wall_l, yellow = FS[4], FS[5], FS[6]
    news, news2, photo, photo_d = FS[7], FS[8], FS[9], FS[10]
    blue, green, pin, shadow, tape = FS[11], FS[12], FS[13], FS[14], FS[15]

    def plate(p, seed, border):
        p.fill(wall)
        p.speckle(0, 0, 32, 32, [(0.16, wall_l)], seed)
        p.rect(2, 2, 28, 28, white)
        p.frame(2, 2, 28, 28, border)
        p.frame(3, 3, 26, 26, border)
        p.speckle(4, 4, 24, 24, [(0.05, off)], seed + 1)
        p.rect(30, 3, 2, 28, shadow)
        p.rect(3, 30, 28, 2, shadow)

    # 0 — СОБЛЮДАЙ ТБ.
    p = A.cell32(0)
    plate(p, 290, red)
    p.rect(4, 4, 24, 7, red)
    p.ctext(12, "СОБЛЮ", black, adv=5)
    p.ctext(20, "ДАЙ ТБ", black, adv=5)
    p.rect(9, 5, 14, 5, white)
    p.rect(11, 6, 3, 3, red)
    p.rect(18, 6, 3, 3, red)

    # 1 — НЕ КУРИТЬ.
    p = A.cell32(1)
    plate(p, 292, red)
    p.disc(16, 10, 7, red)
    p.disc(16, 10, 5, white)
    p.rect(11, 9, 10, 2, off)                        # the cigarette
    p.rect(20, 9, 2, 2, red)
    p.line(11, 15, 21, 5, red)                       # struck through
    p.line(12, 15, 22, 5, red)
    p.ctext(19, "НЕ", black, adv=5)
    p.ctext(25, "КУРИТЬ", black, adv=5)

    # 2 — ПОСТОРОННИМ ВХОД ВОСПРЕЩЁН, at the only length that fits and still
    #     reads: the sign is the shape plus the two words that matter.
    p = A.cell32(2)
    plate(p, 294, black)
    p.rect(4, 4, 24, 9, black)
    p.ctext(5, "ВХОД", yellow, adv=5)
    p.ctext(15, "ВОСПРЕ", black, adv=5)
    p.ctext(22, "ЩЁН", black, adv=5)
    p.rect(6, 28, 20, 1, red)

    # 3 — the wall newspaper, pinned up and curling.
    p = A.cell32(3)
    p.fill(wall)
    p.speckle(0, 0, 32, 32, [(0.16, wall_l)], 296)
    p.rect(1, 1, 30, 30, news)
    p.rect(1, 1, 30, 5, red)                         # the masthead
    p.ctext(0, "ВЕСТИ", news, adv=5)
    p.hline(1, 6, 30, news2)
    p.rect(2, 8, 12, 10, photo)                      # a photograph
    p.frame(2, 8, 12, 10, photo_d)
    p.disc(8, 12, 3, news)
    p.rect(5, 15, 7, 3, photo_d)
    for ty in range(8, 30, 2):                       # columns of type
        p.rect(16, ty, 14, 1, news2)
    for ty in range(20, 30, 2):
        p.rect(2, ty, 12, 1, news2)
    p.rect(16, 8, 14, 1, blue)
    p.rect(2, 19, 12, 1, green)
    for pxy in ((2, 1), (29, 1), (2, 29), (29, 29)): # the pins
        p.px(pxy[0], pxy[1], pin)
    p.rect(24, 1, 6, 2, tape)
    p.dith(24, 24, 7, 7, None, wall, 0.4)            # a corner that has curled

    # 4 — an arrow sign: the way out.
    p = A.cell32(4)
    plate(p, 298, green)
    p.rect(4, 4, 24, 24, green)
    for i in range(8):                               # the arrow, pointing up
        p.rect(16 - i, 8 + i, 2 * i + 1, 1, white)
    p.rect(13, 16, 7, 9, white)

    # 5 — a fire point.
    p = A.cell32(5)
    plate(p, 300, red)
    p.rect(4, 4, 24, 24, red)
    p.rect(12, 8, 8, 16, white)                      # the extinguisher
    p.rect(14, 5, 4, 4, black)
    p.rect(18, 6, 4, 2, black)
    p.rect(12, 12, 8, 2, red)

    # 6 — a voltage warning.
    p = A.cell32(6)
    plate(p, 302, black)
    p.rect(4, 4, 24, 24, yellow)
    for i in range(22):                              # the triangle
        w = i + 1
        p.rect(16 - w // 2, 6 + i, w, 1, yellow)
        p.px(16 - w // 2, 6 + i, black)
        p.px(16 + (w - 1) // 2, 6 + i, black)
    p.rect(5, 27, 22, 1, black)
    p.line(17, 11, 13, 18, black)                    # the bolt
    p.line(18, 11, 14, 18, black)
    p.line(13, 18, 19, 17, black)
    p.line(19, 17, 15, 24, black)
    p.line(20, 17, 16, 24, black)

    # 7 — a stencilled shop number, straight onto the whitewash.
    p = A.cell32(7)
    p.fill(wall_l)
    p.speckle(0, 0, 32, 32, [(0.18, wall), (0.08, FS[0])], 304)
    p.ctext(6, "ЦЕХ", black, adv=6)
    p.ctext(17, "2", black, adv=6)
    p.speckle(4, 4, 24, 24, [(0.10, wall_l)], 305)   # the stencil bled
    p.hline(6, 27, 20, black)

    # 8..15 — the wall these hang on, so a sign board is never floating on a
    # surface from a different atlas.
    for i, s in zip(range(8, 16), range(310, 318)):
        p = A.cell32(i)
        p.fill(wall)
        p.speckle(0, 0, 32, 32, [(0.18, wall_l), (0.06, FS[0])], s)
        if i in (9, 13):
            p.dith(0, 0, 32, 32, None, shadow, 0.18)
        if i == 10:
            p.rect(0, 14, 32, 2, black)
        if i == 11:
            p.dith(0, 0, 32, 32, None, FS[0], 0.35)
        if i == 14:
            p.rect(4, 6, 24, 20, off)                # a blank plate, unlettered
            p.frame(4, 6, 24, 20, FS[1])
            p.speckle(5, 7, 22, 18, [(0.06, wall)], s)
        if i == 15:
            p.dith(0, 0, 32, 32, None, news2, 0.2)
    return A


# ══════════════════════════════════════════════════════════════════════════════
# lamppost_parts — 128x64, 4x2 of 32.  The three assembly beats, and what
# rides the conveyor away at the end of every shift.
# ══════════════════════════════════════════════════════════════════════════════


def build_lamppost_parts():
    A = Atlas("lamppost_parts", 128, 64, LC, alpha=False)
    bench, bench_d = LC[0], LC[1]
    conc, conc_d, conc_l = LC[2], LC[3], LC[4]
    steel, steel_d, steel_l = LC[5], LC[6], LC[7]
    bowl, glass, glass2 = LC[8], LC[9], LC[10]
    rust, brass, shadow, cable, chalk = LC[11], LC[12], LC[13], LC[14], LC[15]

    def top(p, seed):
        p.fill(bench)
        p.speckle(0, 0, 32, 32, [(0.12, bench_d), (0.05, steel)], seed)
        p.hline(0, 0, 32, LC[5])
        p.rect(0, 29, 32, 3, bench_d)

    def pole(p, x, y, w, h):
        p.rect(x, y, w, h, conc)
        p.rect(x, y, 2, h, conc_l)
        p.rect(x + w - 2, y, 2, h, conc_d)
        for cy in range(y + 4, y + h, 9):
            p.hline(x, cy, w, conc_d)

    def bracket(p, x, y, flip=False):
        for i in range(13):
            yy = y - int(5 * (1 - (1 - i / 12.0) ** 2))
            xx = x + (12 - i if flip else i)
            p.rect(xx, yy, 1, 3, steel)
            p.px(xx, yy, steel_l)
            p.px(xx, yy + 2, steel_d)

    def head(p, cx, cy, lit_glass):
        p.rect(cx - 8, cy, 16, 3, bowl)
        p.hline(cx - 8, cy, 16, steel_l)
        for i in range(4):
            w = 14 - i * 3
            p.rect(cx - w // 2, cy + 3 + i, w, 1, lit_glass if i else glass2)
        p.px(cx, cy + 4, glass)

    # 0 — laid out on the bench: a pole section, a bracket, a bowl, fixings.
    p = A.cell(4, 32, 0)
    top(p, 320)
    pole(p, 2, 4, 8, 24)
    bracket(p, 12, 12)
    head(p, 25, 18, glass2)
    p.rect(12, 24, 3, 3, brass)                      # the fixings
    p.rect(17, 25, 3, 3, brass)
    p.rect(22, 26, 3, 2, steel_l)
    for cx in range(12, 28):                         # a coil of cable
        p.px(cx, 6 + (cx % 3), cable)
    p.text(13, 1, "1", chalk, adv=6)                 # the beat, chalked on

    # 1 — the bracket is on the pole.
    p = A.cell(4, 32, 1)
    top(p, 321)
    pole(p, 4, 3, 9, 26)
    bracket(p, 13, 10)
    p.rect(11, 8, 5, 7, steel)                       # the collar
    p.frame(11, 8, 5, 7, steel_d)
    p.px(13, 11, brass)
    head(p, 25, 21, glass2)
    p.rect(20, 27, 4, 2, brass)
    p.text(13, 1, "2", chalk, adv=6)

    # 2 — and the head is on the bracket.
    p = A.cell(4, 32, 2)
    top(p, 322)
    pole(p, 4, 3, 9, 26)
    bracket(p, 13, 10)
    p.rect(11, 8, 5, 7, steel)
    p.frame(11, 8, 5, 7, steel_d)
    head(p, 25, 6, glass2)
    p.rect(24, 4, 3, 3, steel_d)
    p.dith(18, 6, 14, 8, None, glass2, 0.2)
    p.rect(20, 26, 5, 3, brass)
    p.text(13, 1, "3", chalk, adv=6)

    # 3 — finished, on the conveyor, going.  The glass is green even unlit:
    #     mercury glass is green before anyone switches it on.
    p = A.cell(4, 32, 3)
    p.fill(bench_d)
    for rx in range(0, 32, 6):                       # the rollers under it
        p.rect(rx, 26, 5, 5, steel)
        p.hline(rx, 26, 5, steel_l)
    p.rect(0, 24, 32, 2, LC[6])
    pole(p, 2, 12, 26, 8)                            # lying on its side
    p.rect(2, 12, 26, 2, conc_l)
    p.rect(2, 18, 26, 2, conc_d)
    bracket(p, 4, 10, flip=True)
    p.rect(2, 8, 5, 5, steel)
    head(p, 24, 2, glass)
    p.dith(16, 2, 16, 9, None, glass2, 0.3)
    p.rect(28, 12, 3, 8, brass)                      # the base plate
    p.dith(0, 20, 32, 4, None, shadow, 0.4)
    p.text(1, 1, "OK", chalk, adv=6)

    # 4..7 — bench and steel stock, so the assembly rig's other faces have
    # somewhere to sample from.
    for i, (kind, s) in enumerate(((0, 323), (1, 324), (0, 325), (1, 326))):
        p = A.cell(4, 32, 4 + i)
        if kind == 0:
            top(p, s)
            p.dith(0, 0, 32, 32, None, rust, 0.14)
        else:
            p.fill(steel)
            p.speckle(0, 0, 32, 32, [(0.14, steel_l), (0.16, steel_d)], s)
            p.hline(0, 0, 32, steel_l)
            p.dith(0, 22, 32, 10, None, rust, 0.22)
    return A


# ══════════════════════════════════════════════════════════════════════════════
# kipuchka — 64x64, 2x2 of 32.  Small, fast, jittery.  One signature colour
# (swamp moss) and four poses that differ in SILHOUETTE, not in shading: at
# 320x240 and at the darkest tier the outline is all the player gets.
# ══════════════════════════════════════════════════════════════════════════════


def build_kipuchka():
    A = Atlas("kipuchka", 64, 64, KP, alpha=True)
    moss, moss_l, moss_d = KP[0], KP[1], KP[2]
    rag, rag_d = KP[3], KP[4]
    skin, skin_d, hair = KP[5], KP[6], KP[7]
    eye_w, eye_d, claw, mouth = KP[8], KP[9], KP[10], KP[11]
    hi, shadow, sheen = KP[12], KP[13], KP[14]

    def body(p, lean, crouch, arms, mouth_open, eyes):
        hy = 6 + crouch                                # the head
        p.disc(16, hy + 4, 6, moss)
        p.disc(16, hy + 4, 5, moss_l)
        p.rect(10, hy, 13, 4, hair)                    # matted weed for hair
        for hx in range(9, 24, 2):
            p.rect(hx, hy - 2, 1, 3, hair)
        for ex, ey in eyes:
            p.rect(ex, ey + crouch, 2, 2, eye_w)
            p.px(ex + (1 if lean > 0 else 0), ey + crouch, eye_d)
        if mouth_open:
            p.rect(13, hy + 8, 7, 3, mouth)
            p.hline(13, hy + 8, 7, eye_w)
        else:
            p.rect(14, hy + 8, 5, 1, moss_d)
        ty = hy + 11                                   # the torso, in rags
        p.rect(10 + lean, ty, 13, 11 - crouch // 2, rag)
        p.speckle(10 + lean, ty, 13, max(2, 11 - crouch // 2), [(0.3, rag_d)], 330)
        p.rect(10 + lean, ty, 13, 2, moss)
        for ax, ay, adx, ady in arms:                  # the arms
            p.line(ax, ay + crouch, ax + adx, ay + ady + crouch, moss)
            p.line(ax, ay + 1 + crouch, ax + adx, ay + ady + 1 + crouch, moss_d)
            p.rect(ax + adx - 1, ay + ady + crouch - 1, 3, 3, claw)
            p.px(ax + adx, ay + ady + crouch, skin_d)
        ly = ty + 11 - crouch // 2                     # the legs
        for lx in (11 + lean, 18 + lean):
            p.rect(lx, ly, 3, 31 - ly, moss)
            p.vline(lx, ly, 31 - ly, moss_d)
            p.rect(lx - 1, 29, 5, 2, skin_d)
        p.dith(9, ty, 15, 4, None, sheen, 0.3)         # it is always wet
        p.px(13, hy + 2, hi)
        p.px(19, hy + 3, hi)

    # 0 — idle: low, coiled, looking at you.
    body(A.cell32x2(0), 0, 2, [(9, 22, -5, 4), (23, 22, 5, 4)], False,
         [(13, 12), (18, 12)])
    # 1 — the telegraph: it rears, both arms up, mouth open.  ~300 ms of this.
    body(A.cell32x2(1), 0, 0, [(9, 18, -6, -8), (23, 18, 6, -8)], True,
         [(12, 10), (19, 10)])
    # 2 — the strike: thrown forward, arms down and across.
    body(A.cell32x2(2), 3, 4, [(9, 22, -4, 8), (23, 20, 6, 9)], True,
         [(14, 14), (19, 14)])
    # 3 — down.
    p = A.cell32x2(3)
    p.rect(4, 20, 24, 8, rag)
    p.speckle(4, 20, 24, 8, [(0.3, rag_d)], 331)
    p.rect(4, 20, 24, 2, moss)
    p.disc(8, 22, 5, moss)
    p.disc(8, 22, 4, moss_d)
    for hx in range(2, 14, 2):
        p.rect(hx, 17, 1, 3, hair)
    p.px(7, 22, eye_w)
    p.px(10, 22, eye_w)
    p.line(26, 22, 31, 18, moss)                       # one arm still out
    p.rect(30, 17, 3, 3, claw)
    for lx in (20, 25):
        p.rect(lx, 28, 3, 3, moss_d)
    p.dith(2, 27, 28, 4, None, shadow, 0.45)
    return A


# ══════════════════════════════════════════════════════════════════════════════
# smoker — 64x64, 2x2 of 32.  Grey tracksuit, window-yellow eyes.  The eyes are
# a protected palette entry: they are the one thing that must stay legible when
# the lampposts are gone, because that is the entire difficulty curve.
# ══════════════════════════════════════════════════════════════════════════════


def build_smoker():
    A = Atlas("smoker", 64, 64, SM, alpha=True)
    suit, suit_l, suit_d = SM[0], SM[1], SM[2]
    stripe, stripe_d = SM[3], SM[4]
    face, face_d, hair = SM[5], SM[6], SM[7]
    eye, eye_dim, smoke = SM[8], SM[9], SM[10]
    shoe, ember, shadow, cap = SM[11], SM[12], SM[13], SM[14]

    def figure(p, squat, arm, head_dy, eyes, ember_on, breath):
        hy = 3 + head_dy
        p.rect(11, hy, 11, 9, face)                    # the head
        p.rect(11, hy, 11, 3, hair)
        p.rect(10, hy + 1, 13, 2, cap)                 # the flat cap
        p.rect(9, hy + 2, 15, 1, hair)
        p.rect(20, hy + 3, 2, 6, face_d)
        for ex, ey in eyes:                            # the eyes
            p.rect(ex, hy + ey, 2, 2, eye)
            p.px(ex, hy + ey + 1, eye_dim)
        p.rect(13, hy + 7, 5, 1, face_d)
        ty = hy + 9                                    # the torso
        th = 13 - squat
        p.rect(8, ty, 17, th, suit)
        p.rect(8, ty, 3, th, suit_l)
        p.rect(22, ty, 3, th, suit_d)
        for sx in (9, 11, 13):                         # the three stripes
            p.vline(sx, ty + 1, th - 2, stripe if sx < 12 else stripe_d)
        p.rect(8, ty, 17, 2, suit_d)
        ax, ay, adx, ady = arm                         # the arm with the cigarette
        p.line(ax, ay + ty, ax + adx, ay + ady + ty, suit)
        p.line(ax + 1, ay + ty, ax + 1 + adx, ay + ady + ty, suit_d)
        hx, hyy = ax + adx, ay + ady + ty
        p.rect(hx - 1, hyy - 1, 3, 3, face)
        if ember_on:
            p.rect(hx, hyy - 3, 1, 3, smoke)
            p.px(hx, hyy - 4, ember)
            p.dith(hx - 2, hyy - 9, 5, 6, None, smoke, 0.35)
        p.line(24, ty + 2, 27, ty + 9, suit)           # the other arm
        p.line(25, ty + 2, 28, ty + 9, suit_d)
        ly = ty + th                                   # the legs
        for lx in (10, 18):
            p.rect(lx, ly, 5, 30 - ly, suit)
            p.vline(lx, ly, 30 - ly, stripe_d)
            p.rect(lx - 1, 29, 7, 3, shoe)
        if breath:                                     # the exhale
            p.dithf(0, hy + 2, 32, 12, None, smoke,
                    lambda u, v: max(0.0, 0.9 - abs(v - 5) / 6.0) * (u / 32.0) ** 0.6)
        p.dith(8, ly - 3, 17, 3, None, suit_d, 0.4)

    # 0 — idle: the squat.  Nothing in the game says more about where this is.
    figure(A.cell32x2(0), 4, (7, 3, -4, 6), 2, [(12, 4), (17, 4)], True, False)
    # 1 — the telegraph: he straightens, draws, and the eyes come up.
    figure(A.cell32x2(1), 0, (7, 2, -3, -6), 0, [(12, 3), (17, 3)], True, False)
    # 2 — the strike: the exhale.
    figure(A.cell32x2(2), 1, (7, 4, -5, 3), 1, [(12, 4), (17, 4)], False, True)
    # 3 — down.
    p = A.cell32x2(3)
    p.rect(3, 19, 25, 10, suit)
    p.rect(3, 19, 25, 3, suit_d)
    for sy in (23, 25):
        p.hline(4, sy, 22, stripe_d)
    p.rect(24, 16, 8, 8, face)
    p.rect(24, 16, 8, 3, hair)
    p.rect(23, 17, 9, 2, cap)
    p.rect(26, 20, 2, 2, eye_dim)
    p.rect(29, 20, 2, 2, eye_dim)
    p.rect(0, 26, 9, 5, shoe)
    p.rect(2, 21, 6, 5, suit_l)
    p.dith(1, 28, 30, 4, None, shadow, 0.45)
    p.dith(6, 12, 18, 8, None, smoke, 0.18)            # what is left of him
    return A


# ══════════════════════════════════════════════════════════════════════════════
# smoke — 64x64, 2x2 of 32.  Three cloud frames and the pre-warm ring.
#
# Every edge here is an ordered stipple.  A soft cloud is not available: the
# console's transparency is one bit, and pretending otherwise produces a hard
# ugly ellipse instead of a deliberate pattern.
# ══════════════════════════════════════════════════════════════════════════════


def build_smoke():
    A = Atlas("smoke", 64, 64, SO, alpha=True)
    pale, mid, deep, lit = SO[0], SO[1], SO[2], SO[3]
    hot, ring, ring_out = SO[4], SO[5], SO[6]

    def cloud(p, lobes):
        def field(u, v):
            t = 0.0
            for lx, ly, lr in lobes:
                d = ((u - lx) ** 2 + (v - ly) ** 2) / float(lr * lr)
                t = max(t, 1.0 - d)
            return t
        p.dithf(0, 0, 32, 32, None, deep, lambda u, v: field(u, v) * 2.2)
        p.dithf(0, 0, 32, 32, None, mid, lambda u, v: (field(u, v) - 0.18) * 2.4)
        p.dithf(0, 0, 32, 32, None, pale, lambda u, v: (field(u, v) - 0.42) * 2.6)
        p.dithf(0, 0, 32, 32, None, lit, lambda u, v: (field(u, v) - 0.72) * 3.0)

    cloud(A.cell32x2(0), [(15, 17, 13), (22, 12, 9), (9, 11, 8)])
    cloud(A.cell32x2(1), [(16, 15, 14), (10, 20, 10), (24, 18, 9)])
    cloud(A.cell32x2(2), [(17, 14, 15), (11, 22, 8), (25, 22, 7), (14, 8, 7)])

    # 3 — the pre-warm ring.  Self-lit, untiered, drawn with colours that stay
    #     bright in every palette: the player must always know it is coming.
    p = A.cell32x2(3)

    def r(u, v):
        return (((u - 16) / 15.0) ** 2 + ((v - 16) / 15.0) ** 2)

    p.dithf(0, 0, 32, 32, None, ring_out, lambda u, v: 1.0 if 0.36 < r(u, v) < 1.0 else 0.0)
    p.dithf(0, 0, 32, 32, None, ring, lambda u, v: 1.0 if 0.48 < r(u, v) < 0.90 else 0.0)
    p.dithf(0, 0, 32, 32, None, hot, lambda u, v: 1.0 if 0.58 < r(u, v) < 0.78 else 0.0)
    p.dithf(0, 0, 32, 32, None, ring_out,            # a stipple inside the ring,
            lambda u, v: 0.22 if r(u, v) <= 0.36 else 0.0)  # so it reads as volume
    p.rect(15, 15, 3, 3, hot)
    return A


# ══════════════════════════════════════════════════════════════════════════════
# items — 64x64, 2x2 of 32.  The brick and the pipe: world pickups and the
# first-person views of the same two objects.
# ══════════════════════════════════════════════════════════════════════════════


def build_items():
    A = Atlas("items", 64, 64, IT, alpha=True)
    br, br_l, br_d, chip = IT[0], IT[1], IT[2], IT[3]
    st, st_l, st_d, rust, bore = IT[4], IT[5], IT[6], IT[7], IT[8]
    grit, mortar, shadow, tape, hi, dirt = IT[9], IT[10], IT[11], IT[12], IT[13], IT[14]

    # 0 — the brick, on the ground, seen from standing height.
    p = A.cell32x2(0)
    p.rect(4, 14, 24, 11, br)                          # the stretcher face
    p.speckle(4, 14, 24, 11, [(0.14, br_l), (0.16, br_d)], 350)
    p.rect(4, 12, 24, 3, br_l)                         # the top bed, catching sky
    p.speckle(4, 12, 24, 3, [(0.2, br)], 351)
    p.rect(26, 13, 3, 12, br_d)                        # the header, in shadow
    p.rect(4, 23, 24, 2, br_d)
    p.rect(8, 15, 5, 4, chip)                          # a chip
    p.rect(19, 20, 4, 3, chip)
    p.dith(4, 12, 24, 4, None, mortar, 0.22)           # mortar still on it
    p.dith(2, 24, 28, 3, None, shadow, 0.5)
    p.scatter(2, 24, 28, 3, grit, 8, 352)

    # 1 — the brick in the hand, held up near the camera.
    p = A.cell32x2(1)
    p.rect(2, 4, 28, 24, br)
    p.speckle(2, 4, 28, 24, [(0.14, br_l), (0.16, br_d)], 353)
    p.rect(2, 4, 28, 3, br_l)
    p.rect(2, 25, 28, 3, br_d)
    p.vline(2, 4, 24, br_l)
    p.rect(28, 4, 3, 24, br_d)
    p.rect(6, 9, 8, 6, chip)                           # the chipped corner
    p.rect(18, 17, 7, 5, chip)
    p.speckle(6, 9, 8, 6, [(0.3, br_l)], 354)
    p.dith(2, 4, 28, 5, None, mortar, 0.28)
    p.dith(2, 20, 28, 8, None, dirt, 0.2)
    p.px(5, 6, hi)
    p.px(24, 7, hi)

    # 2 — the pipe, lying on the floor.
    p = A.cell32x2(2)
    p.rect(1, 14, 30, 7, st)
    p.hline(1, 14, 30, st_l)
    p.hline(1, 15, 30, st_l)
    p.hline(1, 20, 30, st_d)
    p.speckle(1, 16, 30, 4, [(0.10, st_l), (0.12, st_d)], 355)
    p.rect(0, 13, 4, 9, st_d)                          # the cut end, and its bore
    p.rect(1, 15, 2, 5, bore)
    p.rect(28, 13, 4, 9, st_d)
    p.rect(29, 15, 2, 5, bore)
    p.dith(6, 14, 12, 7, None, rust, 0.3)              # rust, in patches
    p.dith(22, 15, 7, 6, None, rust, 0.22)
    p.dith(1, 21, 30, 3, None, shadow, 0.45)
    p.rect(12, 14, 6, 7, tape)                         # tape round the grip
    p.hline(12, 14, 6, st_l)

    # 3 — the pipe in the hand, held across the view.
    p = A.cell32x2(3)
    for i in range(30):                                # a diagonal length
        x, y = 1 + i, 27 - i
        p.rect(x, y, 3, 5, st)
        p.px(x, y, st_l)
        p.px(x + 1, y, st_l)
        p.px(x + 2, y + 4, st_d)
    p.rect(0, 26, 5, 6, st_d)                          # the near end
    p.rect(1, 28, 3, 3, bore)
    p.rect(28, 0, 4, 5, st_d)
    p.dith(8, 12, 10, 9, None, rust, 0.3)
    p.dith(20, 3, 8, 7, None, rust, 0.2)
    for i in range(8, 16):                             # the taped grip
        p.rect(1 + i, 27 - i, 3, 5, tape)
        p.px(1 + i, 27 - i, st_l)
    p.px(6, 22, hi)
    p.px(24, 5, hi)
    p.dith(0, 24, 10, 8, None, shadow, 0.25)
    return A


# ══════════════════════════════════════════════════════════════════════════════
# hands — 64x64, 2x2 of 32.  Bare hands, always: it is canon.  The palette is
# protagonist_tex.png's, taken across unchanged.
# ══════════════════════════════════════════════════════════════════════════════


def build_hands():
    A = Atlas("hands", 64, 64, HA, alpha=True)
    sk, sk1, sk2, sk3, nail = HA[0], HA[1], HA[2], HA[3], HA[4]
    coat, coat_l, coat_d = HA[5], HA[6], HA[7]
    navy, navy_d, ink = HA[8], HA[9], HA[10]
    hi, grime, seam, knuck = HA[11], HA[12], HA[13], HA[14]

    def palm(p, x, y, w, h):
        p.rect(x, y, w, h, sk1)
        p.rect(x, y, w, 3, sk)
        p.rect(x, y + h - 2, w, 2, sk2)
        p.vline(x, y, h, sk2)
        p.vline(x + w - 1, y, h, sk2)
        p.dith(x + 1, y + 2, w - 2, h - 4, None, sk, 0.30)

    def finger(p, x, y, ln, bend, curl):
        """One finger, drawn as a stack of short segments so a curl reads as a
        silhouette change rather than as shading."""
        cx, cy = x, y
        for i in range(ln):
            cy -= 1
            cx += bend if i > ln // 2 else 0
            if curl and i > ln // 2:
                cy += 2
                cx += 1
            p.rect(cx, cy, 4, 2, sk1 if i % 2 else sk)
            p.px(cx, cy, sk2)
            p.px(cx + 3, cy, sk2)
        p.rect(cx, cy - 1, 4, 2, sk)
        p.rect(cx + 1, cy - 2, 2, 1, nail)

    # 0 — open: the reach, and the interact pose.
    p = A.cell32x2(0)
    palm(p, 7, 14, 17, 13)
    for i, fx in enumerate((7, 12, 17)):
        finger(p, fx, 14, 8 + (1 if i == 1 else 0), 0, False)
    finger(p, 21, 15, 6, 0, False)
    p.rect(3, 17, 6, 5, sk1)                           # the thumb
    p.rect(2, 16, 4, 3, sk)
    p.rect(3, 19, 3, 2, sk2)
    p.dith(8, 16, 15, 4, None, knuck, 0.35)
    p.rect(5, 27, 21, 5, coat)                         # the cuff, at the wrist
    p.hline(5, 27, 21, coat_l)
    p.scatter(8, 16, 14, 10, grime, 6, 360)

    # 1 — the fist.
    p = A.cell32x2(1)
    p.rect(6, 12, 20, 16, sk1)
    p.rect(6, 12, 20, 4, sk)
    p.rect(6, 26, 20, 2, sk2)
    p.vline(6, 12, 16, sk2)
    p.vline(25, 12, 16, sk2)
    for kx in (8, 13, 18, 22):                         # the knuckles
        p.rect(kx, 11, 4, 3, sk)
        p.px(kx + 1, 10, hi)
        p.vline(kx - 1, 12, 12, knuck)
    p.rect(3, 18, 6, 7, sk1)                           # the thumb, folded over
    p.rect(3, 17, 5, 3, sk)
    p.rect(4, 24, 4, 2, sk3)
    p.dith(7, 20, 18, 6, None, sk2, 0.30)
    p.rect(5, 28, 22, 4, coat)
    p.hline(5, 28, 22, coat_l)

    # 2 — closed around a shaft: the grip the pipe goes through.
    p = A.cell32x2(2)
    p.rect(6, 10, 20, 18, sk1)
    p.rect(6, 10, 20, 3, sk)
    p.vline(6, 10, 18, sk2)
    p.vline(25, 10, 18, sk2)
    for i, fy in enumerate((11, 15, 19, 23)):          # four fingers, wrapped
        p.rect(7, fy, 18, 3, sk if i % 2 else sk1)
        p.hline(7, fy, 18, sk)
        p.hline(7, fy + 2, 18, sk2)
        p.px(24, fy + 1, knuck)
    p.rect(3, 14, 6, 9, sk1)                           # the thumb, across them
    p.rect(3, 13, 5, 3, sk)
    p.rect(4, 22, 4, 2, sk3)
    p.rect(11, 6, 10, 5, ink)                          # the shaft's shadow
    p.rect(12, 7, 8, 3, HA[12])
    p.rect(5, 28, 22, 4, coat)
    p.hline(5, 28, 22, coat_l)

    # 3 — the coat's sleeve end, with the overalls showing inside it.
    p = A.cell32x2(3)
    p.fill(coat)
    p.speckle(0, 0, 32, 32, [(0.12, coat_l), (0.14, coat_d)], 361)
    p.rect(0, 0, 32, 4, coat_l)                        # the leather's top face
    p.rect(0, 26, 32, 6, coat_d)
    p.rect(2, 6, 28, 3, coat_d)                        # a seam
    p.hline(2, 6, 28, seam)
    for sx in range(3, 30, 3):
        p.px(sx, 7, seam)
    p.rect(0, 18, 32, 2, coat_d)
    p.rect(4, 22, 24, 8, navy)                         # the overalls, inside
    p.speckle(4, 22, 24, 8, [(0.2, navy_d)], 362)
    p.hline(4, 22, 24, HA[13])
    p.rect(24, 10, 5, 4, coat_d)                       # a scuff
    p.dith(24, 10, 5, 4, None, coat_l, 0.4)
    p.dith(0, 12, 32, 6, None, grime, 0.16)
    return A


# ══════════════════════════════════════════════════════════════════════════════
# hud — 128x128, 4x4 of 32.  Every colour in this atlas is protected: the HUD
# must not go down with the environment ramp.
# ══════════════════════════════════════════════════════════════════════════════


def build_hud():
    A = Atlas("hud", 128, 128, HH, alpha=True)
    white, bone, grey, dark, ink = HH[0], HH[1], HH[2], HH[3], HH[4]
    hot, hot2 = HH[5], HH[6]
    hp, hp2 = HH[7], HH[8]
    merc, bar, track = HH[9], HH[10], HH[11]
    brick, pipe, vign = HH[12], HH[13], HH[14]

    def cross(p, c, c2, gap, ln):
        for d in range(gap, gap + ln):
            for dx, dy in ((d, 0), (-d, 0), (0, d), (0, -d)):
                p.px(16 + dx, 16 + dy, c)
                p.px(16 + dx + (1 if dy else 0), 16 + dy + (1 if dx else 0), c2)

    # 0 — the crosshair.
    p = A.cell32(0)
    cross(p, bone, ink, 3, 6)
    p.px(16, 16, bone)
    p.px(17, 17, ink)

    # 1 — over an interactable.  Same shape, hot colour, plus corner ticks so
    #     it reads even where the background is already yellow.
    p = A.cell32(1)
    cross(p, hot, ink, 4, 5)
    for cx, cy in ((-8, -8), (8, -8), (-8, 8), (8, 8)):
        for k in range(4):
            p.px(16 + cx - (k if cx < 0 else -k), 16 + cy, hot)
            p.px(16 + cx, 16 + cy - (k if cy < 0 else -k), hot)
    p.rect(15, 15, 3, 3, hot2)
    p.px(16, 16, white)

    # 2 — the prompt frame: corners only, so it never boxes in the world.
    p = A.cell32(2)
    for cx, cy, sx, sy in ((1, 1, 1, 1), (30, 1, -1, 1), (1, 30, 1, -1), (30, 30, -1, -1)):
        for k in range(9):
            p.px(cx + sx * k, cy, bone)
            p.px(cx, cy + sy * k, bone)
            p.px(cx + sx * k, cy + sy, ink)
            p.px(cx + sx, cy + sy * k, ink)
    p.dith(3, 3, 26, 26, None, dark, 0.35)             # a stipple to sit text on

    # 3 — the health icon.
    p = A.cell32(3)
    p.rect(5, 7, 22, 18, hp)
    p.frame(5, 7, 22, 18, ink)
    p.frame(6, 8, 20, 16, hp2)
    p.rect(14, 11, 4, 10, white)                       # a plain cross
    p.rect(11, 14, 10, 4, white)
    p.dith(6, 18, 20, 6, None, hp2, 0.4)

    # 4 — the end cap of a bar.
    p = A.cell32(4)
    p.rect(6, 11, 20, 10, track)
    p.frame(5, 10, 22, 12, ink)
    p.rect(6, 11, 20, 2, grey)
    p.rect(22, 11, 4, 10, bone)                        # the cap itself
    p.vline(22, 11, 10, white)
    p.rect(6, 19, 20, 2, dark)

    # 5 — the repeating fill of a bar.
    p = A.cell32(5)
    p.rect(0, 11, 32, 10, bar)
    p.hline(0, 11, 32, white)
    p.hline(0, 20, 32, track)
    for x in range(0, 32, 4):                          # ticks, so the fill reads
        p.vline(x, 12, 8, track)                       # as progress not as a slab
    p.rect(0, 10, 32, 1, ink)
    p.rect(0, 21, 32, 1, ink)

    # 6 — the brick icon.
    p = A.cell32(6)
    p.rect(5, 10, 22, 13, brick)
    p.frame(5, 10, 22, 13, ink)
    p.hline(6, 11, 20, HH[6])
    p.rect(6, 16, 20, 1, ink)
    p.rect(12, 11, 1, 5, ink)
    p.rect(19, 17, 1, 5, ink)
    p.dith(6, 18, 20, 4, None, dark, 0.3)

    # 7 — the pipe icon.
    p = A.cell32(7)
    for i in range(22):
        x, y = 5 + i, 22 - i
        p.rect(x, y, 3, 4, pipe)
        p.px(x, y, merc)
        p.px(x + 2, y + 3, dark)
    p.rect(4, 21, 4, 5, ink)
    p.rect(5, 22, 2, 3, dark)
    p.rect(25, 0, 3, 4, ink)
    p.dith(12, 10, 8, 7, None, grey, 0.35)

    # 8 — the low-HP vignette corner, drawn four times.  A stipple, of course.
    p = A.cell32(8)
    p.dithf(0, 0, 32, 32, None, vign,
            lambda u, v: max(0.0, 1.0 - ((u / 30.0) ** 2 + (v / 30.0) ** 2) ** 0.5) * 1.25)
    p.dithf(0, 0, 32, 32, None, hp2,
            lambda u, v: max(0.0, 0.45 - ((u / 24.0) ** 2 + (v / 24.0) ** 2) ** 0.5) * 1.4)

    # 9 — the interact key cap.
    p = A.cell32(9)
    p.rect(7, 9, 18, 15, bone)
    p.frame(7, 9, 18, 15, ink)
    p.rect(8, 10, 16, 2, white)
    p.rect(8, 21, 16, 2, grey)
    p.text(12, 13, "E", ink, adv=6)

    # 10 — a small filled dot, for a marker or a bullet in a prompt.
    p = A.cell32(10)
    p.disc(16, 16, 6, ink)
    p.disc(16, 16, 5, hot)
    p.disc(16, 16, 2, white)

    # 11 — a damage flash corner: the same corner, in health red.
    p = A.cell32(11)
    p.dithf(0, 0, 32, 32, None, hp2,
            lambda u, v: max(0.0, 1.0 - ((u / 30.0) ** 2 + (v / 30.0) ** 2) ** 0.5) * 1.3)
    p.dithf(0, 0, 32, 32, None, hp,
            lambda u, v: max(0.0, 0.5 - ((u / 22.0) ** 2 + (v / 22.0) ** 2) ** 0.5) * 1.5)

    # 12 — a plain panel fill for prompt backgrounds.
    p = A.cell32(12)
    p.dith(0, 0, 32, 32, None, dark, 0.5)
    p.dith(0, 0, 32, 32, None, ink, 0.25)

    # 13 — a thin rule, for separating prompt lines.
    p = A.cell32(13)
    p.rect(0, 14, 32, 2, bone)
    p.hline(0, 16, 32, ink)
    p.rect(0, 13, 32, 1, grey)

    # 14 — the cooldown wedge: a ring, quartered.
    p = A.cell32(14)
    p.disc(16, 16, 12, ink, filled=False)
    p.disc(16, 16, 11, track, filled=False)
    p.disc(16, 16, 10, track, filled=False)
    for k in range(11):
        p.px(16 + k, 16, bar)
        p.px(16, 16 - k, bar)
    p.disc(16, 16, 2, white)

    # 15 — the assembly step pip.
    p = A.cell32(15)
    p.rect(9, 9, 14, 14, ink)
    p.rect(10, 10, 12, 12, bar)
    p.rect(11, 11, 10, 3, HH[10])
    p.rect(11, 18, 10, 3, track)
    p.px(12, 12, white)
    return A


# ══════════════════════════════════════════════════════════════════════════════
# The contract
#
# sm_atlas.hpp is the single source of truth for where every region lives.  It
# is parsed rather than mirrored: a mirrored table drifts, and a drifted table
# is exactly the failure this check exists to catch.
# ══════════════════════════════════════════════════════════════════════════════

BUILDERS = [
    ("home_walls", build_home_walls),
    ("home_floor", build_home_floor),
    ("home_furniture", build_home_furniture),
    ("home_window", build_home_window),
    ("home_door", build_home_door),
    ("street_facade", build_street_facade),
    ("street_ground", build_street_ground),
    ("street_props", build_street_props),
    ("street_lamp", build_street_lamp),
    ("light_pool", build_light_pool),
    ("sky", build_sky),
    ("gate", build_gate),
    ("factory_walls", build_factory_walls),
    ("factory_floor", build_factory_floor),
    ("factory_machines", build_factory_machines),
    ("factory_board", build_factory_board),
    ("factory_signs", build_factory_signs),
    ("lamppost_parts", build_lamppost_parts),
    ("kipuchka", build_kipuchka),
    ("smoker", build_smoker),
    ("smoke", build_smoke),
    ("items", build_items),
    ("hands", build_hands),
    ("hud", build_hud),
]

# The header's "actors" section is one layout shared by both enemy sheets.
SECTION_ATLASES = {"actors": ("kipuchka", "smoker")}

_SECTION_RE = re.compile(r"^//\s*[─-]+\s*(\w+)\s*\((\d+)[x×](\d+)")
_REGION_RE = re.compile(r"^constexpr sm_uvrect (SM_UV_\w+)\s*=\s*(sm_cell\w*|sm_uv)\(([^)]*)\);")


def parse_contract(path):
    """{atlas: (w, h, {region: (x0, y0, x1, y1)})} straight out of the header."""
    out = {}
    cur = None
    for raw in open(path, encoding="utf-8"):
        line = raw.strip()
        m = _SECTION_RE.match(line)
        if m:
            names = SECTION_ATLASES.get(m.group(1), (m.group(1),))
            cur = []
            for n in names:
                out.setdefault(n, [int(m.group(2)), int(m.group(3)), {}])
                cur.append(out[n])
            continue
        m = _REGION_RE.match(line)
        if not m or cur is None:
            continue
        name, fn, args = m.group(1), m.group(2), [a.strip() for a in m.group(3).split(",")]
        if fn == "sm_uv":
            x0, y0, x1, y1 = (int(a) for a in args)
        else:
            if fn == "sm_cell":
                cols, size, idx = (int(a) for a in args)
            elif fn == "sm_cell64":
                cols, size, idx = 4, 64, int(args[0])
            elif fn == "sm_cell32":
                cols, size, idx = 4, 32, int(args[0])
            elif fn == "sm_cell32x2":
                cols, size, idx = 2, 32, int(args[0])
            else:
                raise ValueError(f"unknown cell helper {fn} in {path}")
            x0, y0 = (idx % cols) * size, (idx // cols) * size
            x1, y1 = x0 + size, y0 + size
        for entry in cur:
            entry[2][name] = (x0, y0, x1, y1)
    return out


def check_atlas(A, contract):
    """Everything the console and the acceptance pass care about, in order."""
    problems = []
    img = A.img
    h, w = img.shape[:2]

    # 1. exact size.
    want = contract.get(A.name)
    if want is None:
        problems.append(f"{A.name}: no section for this atlas in sm_atlas.hpp")
    elif (want[0], want[1]) != (w, h):
        problems.append(f"{A.name}: {w}x{h}, but sm_atlas.hpp says {want[0]}x{want[1]}")

    flat = img.reshape(-1, 4)
    holes = int((flat[:, 3] < 128).sum())
    opaque = flat[flat[:, 3] >= 128]
    uniq = {tuple(int(v) for v in c) for c in np.unique(opaque[:, :3], axis=0)}

    # 2. the palette the baker will actually get.
    budget = 15 if holes else 16
    if len(uniq) > budget:
        problems.append(f"{A.name}: {len(uniq)} opaque colours, budget {budget}"
                        f" ({'entry 0 is the hole' if holes else 'no transparency'})")

    # 3. nothing packs to 0000h, and nothing gets close enough to worry.
    for c in uniq:
        if c[0] < MIN_OPAQUE[0] or c[1] < MIN_OPAQUE[1] or c[2] < MIN_OPAQUE[2]:
            problems.append(f"{A.name}: opaque colour {c} is darker than {MIN_OPAQUE}")

    # A tiling surface with a hole in it is a hole in a wall.
    if not A.alpha and holes:
        problems.append(f"{A.name}: declared opaque but has {holes} transparent texels")

    # 4. every named region carries something.  A flat region is a missing
    #    asset wearing the right rectangle.
    if want:
        for region, (x0, y0, x1, y1) in sorted(want[2].items()):
            if x1 > w or y1 > h:
                problems.append(f"{A.name}: {region} rect {(x0, y0, x1, y1)} is outside the atlas")
                continue
            sub = img[y0:y1, x0:x1].reshape(-1, 4)
            n = len(np.unique(sub, axis=0))
            if n < 2:
                problems.append(f"{A.name}: {region} is a flat fill — nothing was painted there")
    return problems, len(uniq), holes


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    contract = parse_contract(ATLAS_HPP)

    rows = []
    problems = []
    for name, fn in BUILDERS:
        A = fn()
        if A.name != name:
            problems.append(f"{name}: builder produced an atlas called {A.name!r}")
        errs, uniq, holes = check_atlas(A, contract)
        problems.extend(errs)
        path = os.path.join(OUT_DIR, name + ".png")
        Image.fromarray(A.img, "RGBA").save(path, optimize=True)
        rows.append((name, A.w, A.h, uniq, holes, A.w * A.h // 2))

    # The font is generated by its own script; account for it here so the
    # budget line is the whole set rather than most of it.
    font = os.path.join(OUT_DIR, "font.png")
    if os.path.exists(font):
        rows.append(("font", 128, 64, None, None, 128 * 64 // 2))
    else:
        problems.append("assets/tex/font.png is missing — run tools/gen_font.py")

    if problems:
        for p in problems:
            print("FAIL:", p, file=sys.stderr)
        return 1

    total = 0
    print(f"{'texture':<18}{'size':>10}{'colours':>9}{'alpha':>7}{'IDX4 bytes':>12}")
    for name, w, h, uniq, holes, nbytes in rows:
        total += nbytes
        cols = "-" if uniq is None else str(uniq)
        alpha = "-" if holes is None else ("yes" if holes else "no")
        print(f"{name:<18}{f'{w}x{h}':>10}{cols:>9}{alpha:>7}{nbytes:>12}")
    print(f"{'TOTAL':<18}{'':>10}{'':>9}{'':>7}{total:>12}"
          f"   ({total / 1024.0:.1f} KiB of 1 MB VRAM)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
