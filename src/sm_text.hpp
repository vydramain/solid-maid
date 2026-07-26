// Solidmaid — text, in the alphabet the game is actually written in.
//
// pdklib ships a font, and it is ASCII 32..126. The two strings this game
// absolutely must draw are `ОСТАЛОСЬ:` and `ПЛАН ВЫПОЛНЕН` (docs/content.md),
// so the disc carries its own atlas: 8×8 cells, ASCII plus uppercase Cyrillic,
// baked by tools/gen_font.py into assets/tex/font.png.
//
// Strings in the source stay readable — they are UTF-8, and this decodes them.
// A codepoint with no glyph draws the notdef box rather than nothing, so a
// missing letter is visible on screen instead of silently absent.
#pragma once

#include <cstdint>
#include <string_view>

#include "sm_assets.hpp"
#include "sm_gfx.hpp"

namespace solidmaid {

inline constexpr int SM_GLYPH = 8;         // cell size in texels
inline constexpr int SM_FONT_COLUMNS = 16; // cells per atlas row

// Glyph index for one Unicode codepoint, or the notdef index.
int sm_glyph_index(uint32_t codepoint);

// Decodes one UTF-8 sequence at `text[offset]`, advancing `offset`. Malformed
// bytes advance by one and yield U+FFFD, so a bad string cannot loop forever.
uint32_t sm_utf8_next(std::string_view text, std::size_t &offset);

// Width in pixels of `text` at `scale`.
int sm_text_width(std::string_view text, int scale);

// Draws `text` with its top-left corner at (x, y). Returns the width drawn.
int sm_text_draw(sm_gfx &gfx, const sm_assets &assets, int x, int y,
                 std::string_view text, rv_pdk::rv_color colour, int scale,
                 int32_t depth);

// Same, horizontally centred on `centre_x`.
int sm_text_draw_centred(sm_gfx &gfx, const sm_assets &assets, int centre_x,
                         int y, std::string_view text, rv_pdk::rv_color colour,
                         int scale, int32_t depth);

// Text on a world-space quad — the factory board, which is painted on a wall
// rather than shown in a UI layer (docs/environments.md: "No separate UI is
// ever used for this"). `origin` is the top-left of the first glyph, `right`
// and `down` are the surface's axes in metres per glyph cell.
void sm_text_draw_world(sm_gfx &gfx, const sm_assets &assets,
                        rv_pdklib::rv_vec3 origin, rv_pdklib::rv_vec3 right,
                        rv_pdklib::rv_vec3 down, std::string_view text,
                        rv_pdk::rv_color colour);

} // namespace solidmaid
