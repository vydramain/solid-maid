// Solidmaid — where every region lives inside every atlas.
//
// This file is a CONTRACT between two jobs that otherwise never meet: the
// generator that paints the PNGs (tools/gen_textures.py) and the level code
// that maps them onto geometry. Both read the rectangles from here, so a region
// cannot move in one without the other noticing.
//
// The scheme is deliberately mechanical — every atlas is a regular grid of
// cells, and a region is one cell:
//
//   256×256 atlas -> 4×4 grid of 64×64 cells
//   128×128 atlas -> 4×4 grid of 32×32 cells
//   128×64  atlas -> 4×2 grid of 32×32 cells
//   64×128  atlas -> 2×4 grid of 32×32 cells
//   64×64   atlas -> 2×2 grid of 32×32 cells
//
// A regular grid costs a little authoring freedom and buys the one thing that
// matters at this scale: a region's rectangle can be derived, checked and
// regenerated without either side hand-copying pixel numbers.
#pragma once

#include "sm_gfx.hpp"

namespace solidmaid {

// Cell `index` of a `cols`×`rows` grid whose cells are `size` texels square.
constexpr sm_uvrect sm_cell(int cols, int size, int index) {
  return sm_uvrect{static_cast<uint16_t>((index % cols) * size),
                   static_cast<uint16_t>((index / cols) * size),
                   static_cast<uint16_t>((index % cols) * size + size),
                   static_cast<uint16_t>((index / cols) * size + size)};
}

constexpr sm_uvrect sm_cell64(int index) { return sm_cell(4, 64, index); }
constexpr sm_uvrect sm_cell32(int index) { return sm_cell(4, 32, index); }
constexpr sm_uvrect sm_cell32x2(int index) { return sm_cell(2, 32, index); }

// A tiling surface samples the same cell over and over; RV_TEXWRAP_CLAMP over a
// cell of an atlas would smear its edge, so tiling surfaces repeat the cell by
// being tessellated rather than by TILE mode (TILE repeats the WHOLE texture,
// which on an atlas means the whole atlas). Level code therefore maps one cell
// per sub-quad — see the tess_metres argument on sm_gfx::quad.

// ── home_walls (256×256, 4×4 of 64)
// ───────────────────────────────────────────
constexpr sm_uvrect SM_UV_WALLPAPER = sm_cell64(0); // the room's default paper
constexpr sm_uvrect SM_UV_WALLPAPER_FADED =
    sm_cell64(1); // sun-bleached, window wall
constexpr sm_uvrect SM_UV_WALLPAPER_SEAM =
    sm_cell64(2); // the lifting joint near the corner
constexpr sm_uvrect SM_UV_WALLPAPER_YELLOWED =
    sm_cell64(3); // above the radiator
constexpr sm_uvrect SM_UV_TRACE_TELEVISION =
    sm_cell64(4); // bright unfaded rectangle + dust line
constexpr sm_uvrect SM_UV_TRACE_CARPET =
    sm_cell64(5); // outline + four nail holes
constexpr sm_uvrect SM_UV_TRACE_CALENDAR =
    sm_cell64(6); // smaller rectangle + glue smear
constexpr sm_uvrect SM_UV_TRACE_CLOCK = sm_cell64(7);  // one nail, a pale disc
constexpr sm_uvrect SM_UV_PLASTER_BARE = sm_cell64(8); // state 5: paper gone
constexpr sm_uvrect SM_UV_HALL_PAINT =
    sm_cell64(9); // dull green oil paint + dividing line
constexpr sm_uvrect SM_UV_CEILING_WHITEWASH = sm_cell64(10);
constexpr sm_uvrect SM_UV_CEILING_STAIN = sm_cell64(11); // the neighbour's leak
constexpr sm_uvrect SM_UV_SKIRTING = sm_cell64(12);
constexpr sm_uvrect SM_UV_TRACE_WARDROBE_WALL =
    sm_cell64(13); // clean pale wall + the coat nail

// ── home_floor (128×128, 4×4 of 32)
// ───────────────────────────────────────────
constexpr sm_uvrect SM_UV_LINO = sm_cell32(0);
constexpr sm_uvrect SM_UV_LINO_WORN =
    sm_cell32(1); // the walking line, through to felt
constexpr sm_uvrect SM_UV_LINO_DENTS = sm_cell32(2); // four wardrobe feet
constexpr sm_uvrect SM_UV_LINO_RING =
    sm_cell32(3);                                  // the chalk-white glass ring
constexpr sm_uvrect SM_UV_LINO_BED = sm_cell32(4); // two long depressions
constexpr sm_uvrect SM_UV_CONCRETE_STAIR = sm_cell32(5);

// ── home_furniture (256×256, 4×4 of 64)
// ───────────────────────────────────────
constexpr sm_uvrect SM_UV_TELEVISION = sm_cell64(0);
constexpr sm_uvrect SM_UV_TELEVISION_ON = sm_cell64(1);
constexpr sm_uvrect SM_UV_WARDROBE = sm_cell64(2);
constexpr sm_uvrect SM_UV_TABLE = sm_cell64(3);
constexpr sm_uvrect SM_UV_CHAIR = sm_cell64(4);
constexpr sm_uvrect SM_UV_BED = sm_cell64(5);
constexpr sm_uvrect SM_UV_WALL_CARPET = sm_cell64(6);
constexpr sm_uvrect SM_UV_CALENDAR = sm_cell64(7);
constexpr sm_uvrect SM_UV_PHOTOGRAPH = sm_cell64(8);
constexpr sm_uvrect SM_UV_CLOCK = sm_cell64(9);
constexpr sm_uvrect SM_UV_RADIATOR = sm_cell64(10);
constexpr sm_uvrect SM_UV_CHANDELIER = sm_cell64(11);
constexpr sm_uvrect SM_UV_MATTRESS = sm_cell64(12);
constexpr sm_uvrect SM_UV_TV_STAND = sm_cell64(13);

// ── home_window (128×128, 4×4 of 32)
// ──────────────────────────────────────────
constexpr sm_uvrect SM_UV_WINDOW_FRAME = sm_cell32(0);
constexpr sm_uvrect SM_UV_WINDOW_GLASS_DAY = sm_cell32(1);
constexpr sm_uvrect SM_UV_WINDOW_GLASS_DUSK = sm_cell32(2);
constexpr sm_uvrect SM_UV_WINDOW_GLASS_NIGHT = sm_cell32(3);
constexpr sm_uvrect SM_UV_WINDOW_SILL = sm_cell32(4);
constexpr sm_uvrect SM_UV_CURTAIN = sm_cell32(5);

// ── home_door (64×64, 2×2 of 32)
// ──────────────────────────────────────────────
constexpr sm_uvrect SM_UV_DOOR_ROOM = sm_cell32x2(0);
constexpr sm_uvrect SM_UV_DOOR_FRONT = sm_cell32x2(1); // dermatin padding
constexpr sm_uvrect SM_UV_DOOR_CLOSED =
    sm_cell32x2(2); // the doors that do not open
constexpr sm_uvrect SM_UV_SWITCH = sm_cell32x2(3);

// ── street_facade (256×256, 4×4 of 64)
// ────────────────────────────────────────
constexpr sm_uvrect SM_UV_PANEL = sm_cell64(0); // pebble dash, seams
constexpr sm_uvrect SM_UV_PANEL_RUST =
    sm_cell64(1); // rust streaks from fixings
constexpr sm_uvrect SM_UV_WINDOWS_DARK = sm_cell64(2); // a band of dark windows
constexpr sm_uvrect SM_UV_WINDOWS_LIT =
    sm_cell64(3); // warm yellow, the last colour left
constexpr sm_uvrect SM_UV_BALCONY_OPEN = sm_cell64(4);
constexpr sm_uvrect SM_UV_BALCONY_GLAZED = sm_cell64(5);
constexpr sm_uvrect SM_UV_ENTRANCE =
    sm_cell64(6); // canopy, sprung door, number plate
constexpr sm_uvrect SM_UV_STREET_SIGN = sm_cell64(7); // ул. Заводская
constexpr sm_uvrect SM_UV_ROOF_EDGE = sm_cell64(8);

// ── street_ground (128×128, 4×4 of 32)
// ────────────────────────────────────────
constexpr sm_uvrect SM_UV_ASPHALT = sm_cell32(0);
constexpr sm_uvrect SM_UV_ASPHALT_PATCH = sm_cell32(1);
constexpr sm_uvrect SM_UV_KERB = sm_cell32(2);
constexpr sm_uvrect SM_UV_PUDDLE = sm_cell32(3);
constexpr sm_uvrect SM_UV_MUD = sm_cell32(4);
constexpr sm_uvrect SM_UV_MANHOLE = sm_cell32(5);
constexpr sm_uvrect SM_UV_CROSSING = sm_cell32(6);

// ── street_props (256×256, 4×4 of 64)
// ─────────────────────────────────────────
constexpr sm_uvrect SM_UV_FENCE_PO2 = sm_cell64(0); // the raised diamond panel
constexpr sm_uvrect SM_UV_GARAGE = sm_cell64(1);
constexpr sm_uvrect SM_UV_KIOSK = sm_cell64(2); // shutter, stickers, 24 ЧАСА
constexpr sm_uvrect SM_UV_BENCH = sm_cell64(3);
constexpr sm_uvrect SM_UV_BIN = sm_cell64(4);
constexpr sm_uvrect SM_UV_CARPET_FRAME =
    sm_cell64(5); // ковровыбивалка — the landmark
constexpr sm_uvrect SM_UV_POPLAR = sm_cell64(6);
constexpr sm_uvrect SM_UV_HEATING_MAIN = sm_cell64(7); // galvanised casing
constexpr sm_uvrect SM_UV_HEATING_SUPPORT = sm_cell64(8);
constexpr sm_uvrect SM_UV_SWINGS = sm_cell64(9);
constexpr sm_uvrect SM_UV_BARBED_WIRE = sm_cell64(10);

// ── street_lamp (64×128, 2×4 of 32)
// ───────────────────────────────────────────
constexpr sm_uvrect SM_UV_LAMP_POLE = sm_cell(2, 32, 0);
constexpr sm_uvrect SM_UV_LAMP_BRACKET = sm_cell(2, 32, 1);
constexpr sm_uvrect SM_UV_LAMP_HEAD_LIT =
    sm_cell(2, 32, 2); // hard pale green-cyan mercury
constexpr sm_uvrect SM_UV_LAMP_HEAD_DEAD =
    sm_cell(2, 32, 3); // dark grey-green glass

// ── light_pool (64×64) — one region, the whole texture
// ────────────────────────
constexpr sm_uvrect SM_UV_LIGHT_POOL = sm_uv(0, 0, 64, 64);

// ── sky (128×64) — one region, the whole texture
// ──────────────────────────────
constexpr sm_uvrect SM_UV_SKY = sm_uv(0, 0, 128, 64);

// ── gate (128×128, 4×4 of 32)
// ─────────────────────────────────────────────────
constexpr sm_uvrect SM_UV_CHECKPOINT_BRICK = sm_cell32(0);
constexpr sm_uvrect SM_UV_CHECKPOINT_DOOR = sm_cell32(1);   // green door
constexpr sm_uvrect SM_UV_CHECKPOINT_WINDOW = sm_cell32(2); // lit
constexpr sm_uvrect SM_UV_GATE_STEEL = sm_cell32(3);
constexpr sm_uvrect SM_UV_TURNSTILE = sm_cell32(4);
constexpr sm_uvrect SM_UV_GATE_SIGN = sm_cell32(5);

// ── factory_walls (256×256, 4×4 of 64)
// ────────────────────────────────────────
constexpr sm_uvrect SM_UV_BRICK_PAINTED =
    sm_cell64(0); // dull green to 1.8 m + the line
constexpr sm_uvrect SM_UV_BRICK_WHITEWASH =
    sm_cell64(1); // flaking, above the line
constexpr sm_uvrect SM_UV_COLUMN =
    sm_cell64(2); // hazard band + stencilled number
constexpr sm_uvrect SM_UV_TRUSS = sm_cell64(3);        // riveted, dull silver
constexpr sm_uvrect SM_UV_ROOF_LANTERN = sm_cell64(4); // dirty wired glass
constexpr sm_uvrect SM_UV_CRANE_RAIL = sm_cell64(5);

// ── factory_floor (128×128, 4×4 of 32)
// ────────────────────────────────────────
constexpr sm_uvrect SM_UV_CONCRETE = sm_cell32(0);
constexpr sm_uvrect SM_UV_CONCRETE_OIL = sm_cell32(1);
constexpr sm_uvrect SM_UV_FLOOR_LINE = sm_cell32(2); // the yellow walkway line
constexpr sm_uvrect SM_UV_FLOOR_BOX =
    sm_cell32(3); // yellow machine-area corner
constexpr sm_uvrect SM_UV_COOLANT = sm_cell32(4);

// ── factory_machines (256×256, 4×4 of 64)
// ─────────────────────────────────────
constexpr sm_uvrect SM_UV_ASSEMBLY_BENCH =
    sm_cell64(0); // the assembly table + vice
constexpr sm_uvrect SM_UV_WELDING_SCREEN = sm_cell64(1);
constexpr sm_uvrect SM_UV_CYLINDERS = sm_cell64(2);
constexpr sm_uvrect SM_UV_CONVEYOR = sm_cell64(3);
constexpr sm_uvrect SM_UV_SHELVING = sm_cell64(4);
constexpr sm_uvrect SM_UV_PALLET = sm_cell64(5);
constexpr sm_uvrect SM_UV_DRILL_PRESS = sm_cell64(6);
constexpr sm_uvrect SM_UV_BARREL = sm_cell64(7);
constexpr sm_uvrect SM_UV_GANTRY = sm_cell64(8);
constexpr sm_uvrect SM_UV_KETTLE_STOOL = sm_cell64(9);

// ── factory_board (128×64, 4×2 of 32)
// ─────────────────────────────────────────
//
// The single most important object in the game. The header and the digits are
// drawn as TEXT from the font atlas (sm_text_draw_world) so they stay crisp and
// so the digit can change without a texture swap; this atlas is the panel it is
// painted on.
constexpr sm_uvrect SM_UV_BOARD_PANEL =
    sm_cell(4, 32, 0); // painted steel, dim backlight
constexpr sm_uvrect SM_UV_BOARD_FRAME = sm_cell(4, 32, 1);
constexpr sm_uvrect SM_UV_BOARD_HONOUR =
    sm_cell(4, 32, 2); // socialist-competition board
constexpr sm_uvrect SM_UV_BOARD_ROSTER = sm_cell(4, 32, 3);

// ── factory_signs (128×128, 4×4 of 32)
// ────────────────────────────────────────
constexpr sm_uvrect SM_UV_SIGN_SAFETY = sm_cell32(0);     // СОБЛЮДАЙ ТБ
constexpr sm_uvrect SM_UV_SIGN_NO_SMOKING = sm_cell32(1); // НЕ КУРИТЬ
constexpr sm_uvrect SM_UV_SIGN_NO_ENTRY =
    sm_cell32(2); // ПОСТОРОННИМ ВХОД ВОСПРЕЩЁН
constexpr sm_uvrect SM_UV_WALL_NEWSPAPER = sm_cell32(3);

// ── lamppost_parts (128×64, 4×2 of 32)
// ────────────────────────────────────────
constexpr sm_uvrect SM_UV_PART_STAGE0 =
    sm_cell(4, 32, 0); // laid out on the bench
constexpr sm_uvrect SM_UV_PART_STAGE1 = sm_cell(4, 32, 1);
constexpr sm_uvrect SM_UV_PART_STAGE2 = sm_cell(4, 32, 2);
constexpr sm_uvrect SM_UV_PART_FINISHED =
    sm_cell(4, 32, 3); // rides the conveyor away

// ── actors (64×64, 2×2 of 32)
// ─────────────────────────────────────────────────
constexpr sm_uvrect SM_UV_ACTOR_IDLE = sm_cell32x2(0);
constexpr sm_uvrect SM_UV_ACTOR_WINDUP = sm_cell32x2(1); // the telegraph pose
constexpr sm_uvrect SM_UV_ACTOR_STRIKE = sm_cell32x2(2);
constexpr sm_uvrect SM_UV_ACTOR_DOWN = sm_cell32x2(3);

// ── smoke (64×64, 2×2 of 32)
// ──────────────────────────────────────────────────
constexpr sm_uvrect SM_UV_SMOKE_A = sm_cell32x2(0);
constexpr sm_uvrect SM_UV_SMOKE_B = sm_cell32x2(1);
constexpr sm_uvrect SM_UV_SMOKE_C = sm_cell32x2(2);
constexpr sm_uvrect SM_UV_PREWARM_RING =
    sm_cell32x2(3); // self-lit, untiered, always readable

// ── items (64×64, 2×2 of 32)
// ──────────────────────────────────────────────────
constexpr sm_uvrect SM_UV_BRICK_WORLD = sm_cell32x2(0);
constexpr sm_uvrect SM_UV_BRICK_VIEW = sm_cell32x2(1);
constexpr sm_uvrect SM_UV_PIPE_WORLD = sm_cell32x2(2);
constexpr sm_uvrect SM_UV_PIPE_VIEW = sm_cell32x2(3);

// ── hands (64×64, 2×2 of 32)
// ──────────────────────────────────────────────────
constexpr sm_uvrect SM_UV_HAND_OPEN = sm_cell32x2(0);
constexpr sm_uvrect SM_UV_HAND_FIST = sm_cell32x2(1);
constexpr sm_uvrect SM_UV_HAND_GRIP = sm_cell32x2(2); // closed around a shaft
constexpr sm_uvrect SM_UV_CUFF = sm_cell32x2(3);      // the coat's sleeve

// ── hud (128×128, 4×4 of 32)
// ──────────────────────────────────────────────────
constexpr sm_uvrect SM_UV_CROSSHAIR = sm_cell32(0);
constexpr sm_uvrect SM_UV_CROSSHAIR_HOT = sm_cell32(1); // over an interactable
constexpr sm_uvrect SM_UV_PROMPT_FRAME = sm_cell32(2);
constexpr sm_uvrect SM_UV_HP_ICON = sm_cell32(3);
constexpr sm_uvrect SM_UV_BAR_END = sm_cell32(4);
constexpr sm_uvrect SM_UV_BAR_FILL = sm_cell32(5);
constexpr sm_uvrect SM_UV_ICON_BRICK = sm_cell32(6);
constexpr sm_uvrect SM_UV_ICON_PIPE = sm_cell32(7);
constexpr sm_uvrect SM_UV_VIGNETTE_CORNER =
    sm_cell32(8); // low-HP, drawn four times

} // namespace solidmaid
