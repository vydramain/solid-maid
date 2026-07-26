// Solidmaid — text, in the alphabet the game is actually written in.
//
// ── WHAT `colour` DOES, AND WHY IT IS NOT WHAT YOU EXPECT ────────────────────
//
// SAMPLE_TEXTURE *replaces* the vertex colour with the texel (see the note at
// the top of sm_gfx.hpp). A textured glyph therefore cannot be tinted: whatever
// colour is handed to sm_gfx::sprite_tex is discarded by the rasterizer. Ink
// colour is a property of the font atlas's palette and of nothing else.
//
// Rather than accept a parameter that quietly does nothing, the SCREEN-SPACE
// draws spend `colour` on the one surface the console *will* colour: an
// untextured backing plate, one flat sprite per line, filed one depth step
// under the glyphs. That is a real effect — it is what keeps a prompt legible
// over a bright wall — and it is the whole of what `colour` means here.
//
// sm_text_draw_world() has no plate: the board's painted steel panel is already
// the plate, and a second quad coplanar with it would fight it in the ordering
// table. There `colour` is passed through as the quad's tint, which the console
// ignores while a texture is bound and honours only in sm_gfx::quad's
// missing-texture fallback — so it is the colour the lettering degrades to if
// font.mppctex failed to load, and nothing else.
#include "sm_text.hpp"

#include <cstddef>

#include "sm_atlas.hpp"

namespace solidmaid {
namespace {

using rv_pdklib::rv_vec3;

// The font contract, restated as code. font.png is 128×64, IDX4, a 16×8 grid of
// 8×8 cells counted left-to-right then top-to-bottom:
//
//   cells 0..94    ASCII 32..126        index = codepoint - 32
//   cell  95       notdef, a hollow box
//   cells 96..127  Cyrillic А..Я        index = 96 + (codepoint - U+0410)
//
// tools/gen_font.py bakes to exactly this layout; neither side may drift.
constexpr int SM_GLYPH_NOTDEF = 95;
constexpr int SM_GLYPH_CYRILLIC = 96;

// Leading between lines, at scale 1. Small: the glyphs already carry a blank
// bottom row, and screen space at 320×240 is the scarcest thing there is.
constexpr int SM_LINE_LEAD = 2;

int line_step(int scale) { return (SM_GLYPH + SM_LINE_LEAD) * scale; }

sm_uvrect glyph_uv(int index) {
  return sm_cell(SM_FONT_COLUMNS, SM_GLYPH, index);
}

// One line of already-split text, with its glyph count measured by the caller.
// `x` is the left edge; the plate is drawn first so that, if the depth values
// land in the same ordering-table bucket, submission order still puts it
// behind.
void draw_line(sm_gfx &gfx, sm_texref font, int x, int y, std::string_view line,
               int glyphs, rv_pdk::rv_color colour, int scale, int32_t depth) {
  if (glyphs <= 0)
    return;

  const int cell = SM_GLYPH * scale;
  const int pad = scale;
  gfx.sprite(x - pad, y - pad, glyphs * cell + pad * 2, cell + pad * 2, colour,
             depth - 1);

  std::size_t offset = 0;
  int column = 0;
  while (offset < line.size()) {
    const uint32_t codepoint = sm_utf8_next(line, offset);
    const int px = x + column * cell;
    ++column;
    // A space is a blank cell in the atlas; drawing it would spend a
    // primitive on nothing, and primitives are the frame's hard ceiling.
    if (codepoint == ' ')
      continue;
    gfx.sprite_tex(px, y, cell, cell, font, glyph_uv(sm_glyph_index(codepoint)),
                   colour, depth);
  }
}

// Walks `text` line by line. `centred` reads `x` as a centre rather than a left
// edge and centres each line independently, which is what a title card wants.
int draw_block(sm_gfx &gfx, const sm_assets &assets, int x, int y,
               std::string_view text, rv_pdk::rv_color colour, int scale,
               int32_t depth, bool centred) {
  if (scale < 1)
    scale = 1;

  const sm_texref font =
      assets.ref(SM_TEX_FONT, 0); // the font atlas is untiered
  const int cell = SM_GLYPH * scale;
  const int step = line_step(scale);

  int widest = 0;
  std::size_t offset = 0;
  int row = 0;
  for (;;) {
    std::size_t cursor = offset;
    std::size_t line_end = text.size();
    std::size_t next_line = text.size();
    bool has_next = false;
    int glyphs = 0;

    while (cursor < text.size()) {
      const std::size_t start = cursor;
      const uint32_t codepoint = sm_utf8_next(text, cursor);
      if (codepoint == '\n') {
        line_end = start;
        next_line = cursor;
        has_next = true;
        break;
      }
      ++glyphs;
    }

    const int width = glyphs * cell;
    if (width > widest)
      widest = width;

    const int left = centred ? x - width / 2 : x;
    draw_line(gfx, font, left, y + row * step,
              text.substr(offset, line_end - offset), glyphs, colour, scale,
              depth);

    if (!has_next)
      break;
    offset = next_line;
    ++row;
  }
  return widest;
}

} // namespace

int sm_glyph_index(uint32_t codepoint) {
  // ASCII, the atlas's first 95 cells.
  if (codepoint >= 32u && codepoint <= 126u) {
    return static_cast<int>(codepoint) - 32;
  }
  // Ё and ё have no cell of their own and fall back to Е — the diaeresis is
  // routinely dropped in print anyway, so this reads as typography, not as a
  // missing glyph.
  if (codepoint == 0x0401u || codepoint == 0x0451u) {
    return SM_GLYPH_CYRILLIC + static_cast<int>(0x0415u - 0x0410u);
  }
  // Uppercase А..Я.
  if (codepoint >= 0x0410u && codepoint <= 0x042Fu) {
    return SM_GLYPH_CYRILLIC + static_cast<int>(codepoint - 0x0410u);
  }
  // Lowercase а..я share the uppercase cells: the game is lettered in stencil
  // capitals throughout, so a lowercase source string still draws correctly.
  if (codepoint >= 0x0430u && codepoint <= 0x044Fu) {
    return SM_GLYPH_CYRILLIC + static_cast<int>(codepoint - 0x0430u);
  }
  return SM_GLYPH_NOTDEF;
}

uint32_t sm_utf8_next(std::string_view text, std::size_t &offset) {
  if (offset >= text.size())
    return 0u;

  const uint8_t lead = static_cast<uint8_t>(text[offset]);
  if (lead < 0x80u) {
    ++offset;
    return lead;
  }

  int continuations = 0;
  uint32_t codepoint = 0u;
  uint32_t shortest = 0u;
  if ((lead & 0xE0u) == 0xC0u) {
    continuations = 1;
    codepoint = lead & 0x1Fu;
    shortest = 0x80u;
  } else if ((lead & 0xF0u) == 0xE0u) {
    continuations = 2;
    codepoint = lead & 0x0Fu;
    shortest = 0x800u;
  } else if ((lead & 0xF8u) == 0xF0u) {
    continuations = 3;
    codepoint = lead & 0x07u;
    shortest = 0x10000u;
  } else {
    // A continuation byte or an out-of-range lead, standing alone.
    ++offset;
    return 0xFFFDu;
  }

  // EVERY malformed path advances by exactly one byte. That is the property
  // that makes a corrupt string finite rather than a hang on a fantasy console
  // with no way to break out of a draw call.
  if (offset + static_cast<std::size_t>(continuations) >= text.size()) {
    ++offset;
    return 0xFFFDu;
  }
  for (int i = 1; i <= continuations; ++i) {
    const uint8_t byte =
        static_cast<uint8_t>(text[offset + static_cast<std::size_t>(i)]);
    if ((byte & 0xC0u) != 0x80u) {
      ++offset;
      return 0xFFFDu;
    }
    codepoint = (codepoint << 6) | (byte & 0x3Fu);
  }
  // Overlong encodings, UTF-16 surrogates and anything past U+10FFFF are all
  // rejected: they would otherwise alias onto real glyph indices.
  if (codepoint < shortest || codepoint > 0x10FFFFu ||
      (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) {
    ++offset;
    return 0xFFFDu;
  }

  offset += static_cast<std::size_t>(continuations) + 1u;
  return codepoint;
}

int sm_text_width(std::string_view text, int scale) {
  if (scale < 1)
    scale = 1;

  const int cell = SM_GLYPH * scale;
  int widest = 0;
  int run = 0;
  std::size_t offset = 0;
  while (offset < text.size()) {
    const uint32_t codepoint = sm_utf8_next(text, offset);
    if (codepoint == '\n') {
      if (run > widest)
        widest = run;
      run = 0;
      continue;
    }
    run += cell;
  }
  if (run > widest)
    widest = run;
  return widest;
}

int sm_text_draw(sm_gfx &gfx, const sm_assets &assets, int x, int y,
                 std::string_view text, rv_pdk::rv_color colour, int scale,
                 int32_t depth) {
  return draw_block(gfx, assets, x, y, text, colour, scale, depth, false);
}

int sm_text_draw_centred(sm_gfx &gfx, const sm_assets &assets, int centre_x,
                         int y, std::string_view text, rv_pdk::rv_color colour,
                         int scale, int32_t depth) {
  return draw_block(gfx, assets, centre_x, y, text, colour, scale, depth, true);
}

void sm_text_draw_world(sm_gfx &gfx, const sm_assets &assets, rv_vec3 origin,
                        rv_vec3 right, rv_vec3 down, std::string_view text,
                        rv_pdk::rv_color colour) {
  const sm_texref font = assets.ref(SM_TEX_FONT, 0);

  // ── SIZING THE BOARD'S LETTERING ─────────────────────────────────────────
  //
  // `right` and `down` are handed in as metres per glyph CELL, so the caller
  // owns the size — but the size the board needs is not a free choice, and the
  // arithmetic belongs next to the code that draws it.
  //
  // At 320×240 with SM_FOV_Y_DEGREES = 58, the screen's 240 rows span
  // 2·tan(29°) = 1.108 world units at one metre, i.e. 216 pixels per metre per
  // metre of distance. A glyph `H` metres tall at distance `d` is therefore
  // H/d · 216 pixels high. The board hangs on the far wall of a hall whose
  // longest sightline is ~24 m (sm_common.hpp puts the diagonal near 28 m):
  //
  //     H = 0.55 m  ->  5.0 px at 24 m, 9.9 px at 12 m, 24 px at 5 m
  //     H = 0.30 m  ->  2.7 px at 24 m  — a smear, unreadable
  //
  // So the recommended cell is `down` = 0.55 m and `right` = 0.42 m: an 8×8
  // stencil glyph slightly condensed, which is what industrial stencil is
  // anyway. `ОСТАЛОСЬ: 5` is 11 cells = 4.6 m of lettering and `ПЛАН ВЫПОЛНЕН`
  // is 13 cells = 5.5 m, both of which fit a production board on the wall of a
  // 7 m hall and both of which are the size a real one would be painted.
  //
  // Every glyph is ONE quad, front-facing and small. That is deliberate: the
  // affine mapper swims worst on large polygons receding into depth
  // (docs/gameplay.md §3, "keep important information off big receding
  // polygons"), and a 0.55 m quad seen head-on barely swims at all. The large
  // tess_metres below keeps sm_gfx::quad from subdividing, so the cost is
  // exactly one primitive per non-space glyph — 10 for `ОСТАЛОСЬ: 5`.
  std::size_t offset = 0;
  int column = 0;
  int row = 0;
  while (offset < text.size()) {
    const uint32_t codepoint = sm_utf8_next(text, offset);
    if (codepoint == '\n') {
      ++row;
      column = 0;
      continue;
    }
    if (codepoint == ' ') {
      ++column;
      continue;
    }

    const rv_vec3 top_left = origin + right * static_cast<float>(column) +
                             down * static_cast<float>(row);
    // The PDK's Z order: 0 and 1 along the top edge, 2 and 3 along the
    // bottom. Handing them round the rim instead produces an hourglass.
    const rv_vec3 corners[4] = {top_left, top_left + right, top_left + down,
                                top_left + right + down};
    gfx.quad(corners, font, glyph_uv(sm_glyph_index(codepoint)), colour, 8.0f);
    ++column;
  }
}

} // namespace solidmaid
