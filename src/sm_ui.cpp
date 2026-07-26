// Solidmaid — the HUD, the view model, the transition curtain, and the two end
// states.
//
// The hard rule, from docs/gameplay.md §2 and repeated in docs/mechanics.md:
// the HUD is "minimal and diegetically silent: crosshair, health, tool
// cooldown, interaction prompt. It NEVER shows the countdown. The only number
// in the game is painted on a wall in the factory."
//
// That rule still governs the in-game HUD: sm_ui_draw_hud() has no counter, no
// timer, and no mirror of the board. Two screens outside it do show the count —
// sm_ui_draw_title(), by explicit design decision (2026-07-26), and
// sm_ui_draw_debug(), a development tool behind the VIEW button and off by
// default. Neither number may migrate into sm_ui_draw_hud().
//
// ── COSTS ────────────────────────────────────────────────────────────────────
//
// The frame refuses the 4097th primitive, and the HUD shares that ceiling with
// the whole world render. Every function below is written against a budget:
//
//   sm_ui_draw_hud        31 primitives worst case (every element lit at once)
//   sm_ui_draw_viewmodel   6
//   sm_ui_draw_fade      150 worst case, 1 when fully closed  (see the note
//   there) sm_ui_draw_knockout  154 worst case (the curtain plus four vignette
//   corners) sm_ui_draw_title      ~72 sm_ui_draw_debug     ~130, development
//   only
//
// ── COLOUR ───────────────────────────────────────────────────────────────────
//
// SAMPLE_TEXTURE replaces the vertex colour, so nothing textured here can be
// tinted and no element's colour is used to carry meaning that a textured draw
// would have to express. Everything that has to change colour — bars, plates,
// the hurt border, the curtain — is drawn UNTEXTURED with sm_gfx::sprite(),
// where the colour is honoured.
#include "sm_ui.hpp"

#include <cmath>
#include <cstdio>

#include "sm_atlas.hpp"
#include "sm_text.hpp"

namespace solidmaid {
namespace {

// ── palette
// ───────────────────────────────────────────────────────────────────
//
// Deliberately desaturated and few: the HUD is not allowed to be the brightest
// thing on screen, because the brightest thing on screen is supposed to be a
// lamppost, a pre-warm ring, or the board.
constexpr rv_pdk::rv_color SM_COL_BLACK{0, 0, 0};
constexpr rv_pdk::rv_color SM_COL_PLATE{16, 16, 14};
constexpr rv_pdk::rv_color SM_COL_TRACK{38, 40, 36};
constexpr rv_pdk::rv_color SM_COL_HEALTH{168, 62, 46};
constexpr rv_pdk::rv_color SM_COL_CHARGE{206, 190, 116};
constexpr rv_pdk::rv_color SM_COL_COOLDOWN{86, 90, 82};
constexpr rv_pdk::rv_color SM_COL_STEP_DONE{120, 132, 104};
constexpr rv_pdk::rv_color SM_COL_STEP_RUN{198, 200, 168};
constexpr rv_pdk::rv_color SM_COL_WARN{196, 76, 44};
constexpr rv_pdk::rv_color SM_COL_HURT{146, 28, 22};
constexpr rv_pdk::rv_color SM_COL_TITLE_BG{12, 12, 12};

// The one piece of state this file keeps. The interrupted-assembly flash needs
// a clock and sm_hud_model carries none; the HUD is drawn exactly once per
// frame, so counting calls IS the frame counter. Nothing else reads it.
uint32_t g_hud_frame = 0;

float clamp01(float value) {
  return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

int16_t to_i16(float value) {
  if (value < -32768.0f)
    value = -32768.0f;
  if (value > 32767.0f)
    value = 32767.0f;
  return static_cast<int16_t>(value);
}

// Mirroring an atlas cell has to step INSIDE the cell on the flipped edge. A
// cell's rectangle is half-open — u1 is the FIRST texel of the neighbouring
// cell — so a naive swap would sample the neighbour at s = 0 and drag a column
// of the wrong glyph into the corner. Starting at u1 - 1 keeps every sample in
// range.
sm_uvrect uv_mirror(sm_uvrect uv, bool flip_x, bool flip_y) {
  sm_uvrect out = uv;
  if (flip_x) {
    out.u0 = static_cast<uint16_t>(uv.u1 - 1);
    out.u1 = uv.u0;
  }
  if (flip_y) {
    out.v0 = static_cast<uint16_t>(uv.v1 - 1);
    out.v1 = uv.v0;
  }
  return out;
}

rv_pdk::rv_uv uv_corner(sm_uvrect uv, bool far_u, bool far_v) {
  return rv_pdk::rv_uv{far_u ? uv.u1 : uv.u0, far_v ? uv.v1 : uv.v0};
}

// A screen-space card with a rotation — the whole of the view model's animation
// vocabulary. There is no skeletal animation on this console; a few quads moved
// and skewed by hand is what a first-person view model IS here, and it is what
// the era did.
void draw_card(sm_gfx &gfx, sm_texref texture, sm_uvrect uv, float cx, float cy,
               float half_w, float half_h, float angle, int32_t depth) {
  if (!texture.valid())
    return;

  const float c = std::cos(angle);
  const float s = std::sin(angle);
  const float ox[4] = {-half_w, half_w, -half_w, half_w};
  const float oy[4] = {-half_h, -half_h, half_h, half_h};
  const bool far_u[4] = {false, true, false, true};
  const bool far_v[4] = {false, false, true, true};

  rv_pdk::rv_vertex corners[4];
  for (int i = 0; i < 4; ++i) {
    corners[i].x = to_i16(cx + ox[i] * c - oy[i] * s);
    corners[i].y = to_i16(cy + ox[i] * s + oy[i] * c);
    corners[i].color = SM_COL_PLATE; // discarded: SAMPLE_TEXTURE replaces it
    corners[i].uv = uv_corner(uv, far_u[i], far_v[i]);
  }
  gfx.quad2d(corners, texture, depth);
}

// A horizontal meter: an untextured track with an untextured fill. Two
// primitives, both colourable, and no dependence on the HUD atlas having
// loaded.
void draw_meter(sm_gfx &gfx, int x, int y, int w, int h, float fill,
                rv_pdk::rv_color track, rv_pdk::rv_color ink, int32_t depth) {
  gfx.sprite(x, y, w, h, track, depth);
  const int inner = w - 2;
  const int filled =
      static_cast<int>(static_cast<float>(inner) * clamp01(fill) + 0.5f);
  if (filled > 0)
    gfx.sprite(x + 1, y + 1, filled, h - 2, ink, depth);
}

// The low-HP vignette: one authored corner, drawn four times, mirrored into
// place. Four primitives for the whole effect and no post-processing, which is
// the only way this console has of doing it (docs/gameplay.md §3).
void draw_vignette(sm_gfx &gfx, sm_texref hud, int width, int height, int size,
                   int32_t depth) {
  if (size <= 0 || !hud.valid())
    return;
  gfx.sprite_tex(0, 0, size, size, hud, SM_UV_VIGNETTE_CORNER, SM_COL_BLACK,
                 depth);
  gfx.sprite_tex(width - size, 0, size, size, hud,
                 uv_mirror(SM_UV_VIGNETTE_CORNER, true, false), SM_COL_BLACK,
                 depth);
  gfx.sprite_tex(0, height - size, size, size, hud,
                 uv_mirror(SM_UV_VIGNETTE_CORNER, false, true), SM_COL_BLACK,
                 depth);
  gfx.sprite_tex(width - size, height - size, size, size, hud,
                 uv_mirror(SM_UV_VIGNETTE_CORNER, true, true), SM_COL_BLACK,
                 depth);
}

// The prompt's payload. One prompt at a time — "no context-sensitive prompts
// stacking" (docs/gameplay.md §4) — and as few letters as the design can bear,
// because this is "a game a player should finish in ten minutes without reading
// anything". Where the HUD atlas has an icon, the icon carries it alone.
sm_uvrect prompt_icon(sm_prompt prompt, bool &out_has_icon) {
  out_has_icon = true;
  switch (prompt) {
  case SM_PROMPT_TAKE_BRICK:
    return SM_UV_ICON_BRICK;
  case SM_PROMPT_TAKE_PIPE:
    return SM_UV_ICON_PIPE;
  case SM_PROMPT_TELEVISION:
  case SM_PROMPT_ASSEMBLE:
  case SM_PROMPT_NONE:
  default:
    break;
  }
  out_has_icon = false;
  return SM_UV_ICON_BRICK;
}

std::string_view prompt_text(sm_prompt prompt) {
  switch (prompt) {
  case SM_PROMPT_TELEVISION:
    return "ТВ";
  case SM_PROMPT_ASSEMBLE:
    return "СБОРКА - A";
  case SM_PROMPT_TAKE_BRICK:
  case SM_PROMPT_TAKE_PIPE:
  case SM_PROMPT_NONE:
  default:
    break;
  }
  return std::string_view{};
}

// ── the dither curtain
// ────────────────────────────────────────────────────────
//
// The classic 4×4 ordered (Bayer) matrix. There is no alpha blending on this
// console, so a half-closed curtain cannot be a translucent rectangle: it is a
// pattern of fully opaque cells, exactly as docs/gameplay.md asks
// ("Gradients are all dither patterns, and they are visible").
constexpr int SM_BAYER[16] = {0, 8,  2, 10, 12, 4, 14, 6,
                              3, 11, 1, 9,  15, 7, 13, 5};
constexpr int SM_FADE_COLUMNS = 20; // 320 / 20 = 16 px cells
constexpr int SM_FADE_ROWS = 15;    // 240 / 15 = 16 px cells

void draw_viewmodel_hand(sm_gfx &gfx, sm_texref hands, sm_texref items,
                         sm_hand_item item, const sm_combat &combat,
                         float base_x, float base_y, float side, float sway_x,
                         float sway_y) {
  float dx = 0.0f;
  float dy = 0.0f;
  float angle = 0.0f;

  if (item == SM_ITEM_PIPE && combat.swinging()) {
    // The pipe's arc, in the proportions sm_common.hpp already fixed:
    // 0.11 s windup, 0.10 s active, 0.24 s recovery.
    const float total = SM_PIPE_WINDUP + SM_PIPE_ACTIVE + SM_PIPE_RECOVER;
    const float wind_end = SM_PIPE_WINDUP / total;
    const float active_end = (SM_PIPE_WINDUP + SM_PIPE_ACTIVE) / total;
    const float phase = clamp01(combat.swing_phase());

    if (phase < wind_end) {
      // Cocked back and up: the telegraph the player reads on themselves.
      const float k = phase / wind_end;
      dx = 30.0f * k * side;
      dy = -26.0f * k;
      angle = -0.75f * k * side;
    } else if (phase < active_end) {
      // The sweep. Fast, and the only part that has a hitbox behind it.
      const float k = (phase - wind_end) / (active_end - wind_end);
      dx = (30.0f - 156.0f * k) * side;
      dy = -26.0f + 44.0f * k;
      angle = (-0.75f + 2.15f * k) * side;
    } else {
      // Recovery: drift back to rest from where the sweep ended.
      const float k = (phase - active_end) / (1.0f - active_end);
      const float rest = 1.0f - k;
      dx = -126.0f * rest * side;
      dy = 18.0f * rest;
      angle = 1.40f * rest * side;
    }
  } else if (item == SM_ITEM_BRICK) {
    if (combat.charging()) {
      // Hold to ready: the arm draws back and down, so the amount of wind
      // is visible on the hand rather than on a number.
      const float k = clamp01(combat.charge());
      dx = 26.0f * k * side;
      dy = 20.0f * k;
      angle = 0.55f * k * side;
    } else if (combat.throw_cooldown() > 0.0f) {
      // Release: the follow-through settles over the cooldown, which is
      // also the whole of the cooldown's readout on the hands.
      const float k = clamp01(combat.throw_cooldown() / SM_BRICK_COOLDOWN);
      dx = -34.0f * k * side;
      dy = -22.0f * k;
      angle = -0.40f * k * side;
    }
  }

  const float x = base_x + dx + sway_x * side;
  const float y = base_y + dy + sway_y;
  const bool mirrored = side < 0.0f;

  // The coat's cuff, behind the hand, anchoring it to an arm the player never
  // sees the rest of.
  draw_card(gfx, hands, uv_mirror(SM_UV_CUFF, mirrored, false),
            x + 26.0f * side, y + 34.0f, 26.0f, 20.0f, angle,
            SM_DEPTH_VIEWMODEL);

  const sm_uvrect grip =
      uv_mirror(item == SM_ITEM_NONE ? SM_UV_HAND_OPEN : SM_UV_HAND_GRIP,
                mirrored, false);

  if (item == SM_ITEM_PIPE) {
    // The fist closes AROUND a shaft, so the pipe goes down first.
    draw_card(gfx, items, uv_mirror(SM_UV_PIPE_VIEW, mirrored, false),
              x - 12.0f * side, y - 26.0f, 46.0f, 46.0f, angle,
              SM_DEPTH_VIEWMODEL);
    draw_card(gfx, hands, grip, x, y, 30.0f, 30.0f, angle, SM_DEPTH_VIEWMODEL);
  } else if (item == SM_ITEM_BRICK) {
    // A brick sits in the palm, in front of the fingers.
    draw_card(gfx, hands, grip, x, y, 30.0f, 30.0f, angle, SM_DEPTH_VIEWMODEL);
    draw_card(gfx, items, uv_mirror(SM_UV_BRICK_VIEW, mirrored, false),
              x - 6.0f * side, y - 14.0f, 24.0f, 20.0f, angle,
              SM_DEPTH_VIEWMODEL);
  } else {
    draw_card(gfx, hands, grip, x, y, 30.0f, 30.0f, angle, SM_DEPTH_VIEWMODEL);
  }
}

} // namespace

void sm_ui_draw_hud(sm_gfx &gfx, const sm_assets &assets,
                    const sm_hud_model &model) {
  ++g_hud_frame;

  const sm_texref hud = assets.ref(SM_TEX_HUD, 0); // the HUD atlas is untiered
  const int w = gfx.width();
  const int h = gfx.height();

  // ── low health ───────────────────────────────────────────────────────────
  // Drawn FIRST, and one depth step under the rest. Two different depths can
  // land in the same ordering bucket (docs/gameplay.md §3), where submission
  // order decides — so both mechanisms have to agree, or the vignette would
  // darken the very health readout the player is looking at while it closes.
  //
  // A vignette rather than a flashing number: it starts at 40 % and tightens
  // as the bar empties. Four primitives, no shader, no post pass.
  const float hp_fraction = clamp01(static_cast<float>(model.hp) /
                                    static_cast<float>(SM_PLAYER_MAX_HP));
  if (hp_fraction < 0.40f) {
    const float closeness = clamp01((0.40f - hp_fraction) / 0.40f);
    const int size = 56 + static_cast<int>(60.0f * closeness);
    draw_vignette(gfx, hud, w, h, size, SM_DEPTH_HUD - 1);
  }

  // ── crosshair ────────────────────────────────────────────────────────────
  // Hot over anything the hand button would act on. That swap IS the "you can
  // touch this" feedback; the prompt below only says WHAT.
  const bool hot = model.prompt != SM_PROMPT_NONE;
  const int cross = 11;
  gfx.sprite_tex((w - cross) / 2, (h - cross) / 2, cross, cross, hud,
                 hot ? SM_UV_CROSSHAIR_HOT : SM_UV_CROSSHAIR, SM_COL_BLACK,
                 SM_DEPTH_HUD);

  // ── health ───────────────────────────────────────────────────────────────
  // TURNED OFF. Health is reported by the hurt flash, the vignette that closes
  // in as it falls, and the view model — the bar and its icon said the same
  // thing a third time and put furniture in the corner of a frame that is only
  // 320x240 to begin with. The vignette below still reads the same number.
  (void)hp_fraction;

  // ── the tool's charge and cooldown ───────────────────────────────────────
  // The ICON is off too: the hands are on screen holding the thing, which is a
  // better answer to "what am I carrying" than a pictogram of it. The meter
  // stays, because a throw's wind-up and its cooldown have no other tell.
  const sm_hand_item tool =
      model.right_hand != SM_ITEM_NONE ? model.right_hand : model.left_hand;
  if (tool != SM_ITEM_NONE) {
    if (model.charge > 0.0f) {
      draw_meter(gfx, w - 96, h - 19, 68, 8, model.charge, SM_COL_TRACK,
                 SM_COL_CHARGE, SM_DEPTH_HUD);
    } else if (model.throw_cooldown > 0.0f) {
      draw_meter(gfx, w - 96, h - 19, 68, 8, model.throw_cooldown, SM_COL_TRACK,
                 SM_COL_COOLDOWN, SM_DEPTH_HUD);
    }
  }

  // ── the assembly ritual ──────────────────────────────────────────────────
  // Three segments, because there are three steps and the player is entitled
  // to know which one is running and that the finished ones stay finished
  // ("interrupts lose progress on the current step only", docs/mechanics.md).
  // This is not a countdown: it resets every shift and never carries.
  if (model.assembly_progress >= 0.0f) {
    const int seg_w = 42;
    const int seg_gap = 3;
    const int total =
        SM_ASSEMBLY_STEPS * seg_w + (SM_ASSEMBLY_STEPS - 1) * seg_gap;
    const int x0 = (w - total) / 2;
    const int y0 = h - 64;

    int step = model.assembly_step;
    if (step < 0)
      step = 0;
    if (step > SM_ASSEMBLY_STEPS - 1)
      step = SM_ASSEMBLY_STEPS - 1;

    // Interrupted: the running segment flashes. ~2.5 Hz at 30 fps, which is
    // fast enough to read as "that just went wrong" and slow enough not to
    // strobe.
    const bool flash_on = ((g_hud_frame / 6u) & 1u) != 0u;
    for (int i = 0; i < SM_ASSEMBLY_STEPS; ++i) {
      const int x = x0 + i * (seg_w + seg_gap);
      float fill = 0.0f;
      rv_pdk::rv_color ink = SM_COL_STEP_DONE;
      if (i < step) {
        fill = 1.0f;
      } else if (i == step) {
        fill = clamp01(model.assembly_progress);
        ink = SM_COL_STEP_RUN;
        if (model.assembly_interrupted) {
          ink = flash_on ? SM_COL_WARN : SM_COL_TRACK;
          if (fill < 0.06f)
            fill = 0.06f; // the flash must have something to colour
        }
      }
      if (i > step) {
        gfx.sprite(x, y0, seg_w, 7, SM_COL_TRACK, SM_DEPTH_HUD);
      } else {
        draw_meter(gfx, x, y0, seg_w, 7, fill, SM_COL_TRACK, ink, SM_DEPTH_HUD);
      }
    }
  }

  // ── the interaction prompt ───────────────────────────────────────────────
  // ONE at a time, by construction: sm_prompt is a single value, not a set.
  if (model.prompt != SM_PROMPT_NONE) {
    const int frame_w = 94;
    const int frame_h = 24;
    const int fx = (w - frame_w) / 2;
    const int fy = h / 2 + 20;
    gfx.sprite_tex(fx, fy, frame_w, frame_h, hud, SM_UV_PROMPT_FRAME,
                   SM_COL_BLACK, SM_DEPTH_HUD);

    bool has_icon = false;
    const sm_uvrect icon = prompt_icon(model.prompt, has_icon);
    if (has_icon) {
      gfx.sprite_tex(fx + (frame_w - 16) / 2, fy + 4, 16, 16, hud, icon,
                     SM_COL_BLACK, SM_DEPTH_HUD_TEXT);
    } else {
      sm_text_draw_centred(gfx, assets, w / 2, fy + 8,
                           prompt_text(model.prompt), SM_COL_PLATE, 1,
                           SM_DEPTH_HUD_TEXT);
    }
  }

  // ── the moment of being hit ──────────────────────────────────────────────
  // Four untextured edge bars. Colour is honoured on a flat sprite, so this is
  // the one place a "flash" can actually be red.
  if (model.hurt_flash > 0.0f) {
    const float flash = clamp01(model.hurt_flash);
    const int thickness = 2 + static_cast<int>(6.0f * flash);
    gfx.sprite(0, 0, w, thickness, SM_COL_HURT, SM_DEPTH_HUD_TEXT);
    gfx.sprite(0, h - thickness, w, thickness, SM_COL_HURT, SM_DEPTH_HUD_TEXT);
    gfx.sprite(0, 0, thickness, h, SM_COL_HURT, SM_DEPTH_HUD_TEXT);
    gfx.sprite(w - thickness, 0, thickness, h, SM_COL_HURT, SM_DEPTH_HUD_TEXT);
  }
}

void sm_ui_draw_viewmodel(sm_gfx &gfx, const sm_assets &assets,
                          const sm_combat &combat, float bob_phase, bool moving,
                          int tier) {
  const sm_texref hands = assets.ref(SM_TEX_HANDS, tier);
  const sm_texref items = assets.ref(SM_TEX_ITEMS, tier);

  // Walking sway is a lateral figure-of-eight against the head bob, at the same
  // frequency, so the hands read as attached to the body rather than pinned to
  // the camera. Standing still, they breathe.
  float sway_x = 0.0f;
  float sway_y = 0.0f;
  if (moving) {
    sway_x = std::sin(bob_phase) * 5.0f;
    sway_y = std::fabs(std::cos(bob_phase)) * -4.0f;
  } else {
    sway_y = std::sin(bob_phase * 0.22f) * 2.0f;
  }

  const int w = gfx.width();
  const int h = gfx.height();

  draw_viewmodel_hand(gfx, hands, items, combat.left_hand(), combat,
                      static_cast<float>(w) * 0.22f,
                      static_cast<float>(h) - 32.0f, -1.0f, sway_x, sway_y);
  draw_viewmodel_hand(gfx, hands, items, combat.right_hand(), combat,
                      static_cast<float>(w) * 0.78f,
                      static_cast<float>(h) - 32.0f, 1.0f, sway_x, sway_y);
}

void sm_ui_draw_fade(sm_gfx &gfx, float amount, rv_pdk::rv_color colour) {
  if (amount <= 0.0f)
    return;

  const int w = gfx.width();
  const int h = gfx.height();

  // Fully closed is one sprite, and it must be exactly opaque — a dither
  // pattern at amount = 1 would still show its own seams.
  if (amount >= 0.995f) {
    gfx.sprite(0, 0, w, h, colour, SM_DEPTH_FADE);
    return;
  }

  // ── budgeting the curtain ────────────────────────────────────────────────
  //
  // A 20×15 grid is 300 cells, and drawing one sprite per lit cell would cost
  // 300 primitives at the worst moment of a fade — 7 % of the whole frame,
  // spent on a rectangle. The brief suggests growing the cell size as `amount`
  // rises to hold that down; RUN-LENGTH MERGING is strictly better and is what
  // this does: horizontally adjacent lit cells become ONE sprite.
  //
  // The result is identical on screen, the pattern never changes size (so
  // there is no visible pop halfway through a 0.55 s transition), and the cost
  // is bounded by the most broken-up a row can be. With a 4×4 Bayer matrix a
  // row of 20 cells alternates at worst, giving 10 runs; 15 rows × 10 = 150
  // primitives, at amount ≈ 0.5. Above and below that the runs merge and the
  // count falls away toward 1.
  const int cell_w = (w + SM_FADE_COLUMNS - 1) / SM_FADE_COLUMNS;
  const int cell_h = (h + SM_FADE_ROWS - 1) / SM_FADE_ROWS;
  const float level = amount * 16.0f;

  for (int row = 0; row < SM_FADE_ROWS; ++row) {
    int run_start = -1;
    for (int col = 0; col <= SM_FADE_COLUMNS; ++col) {
      const bool lit =
          col < SM_FADE_COLUMNS &&
          static_cast<float>(SM_BAYER[(row & 3) * 4 + (col & 3)]) < level;
      if (lit) {
        if (run_start < 0)
          run_start = col;
        continue;
      }
      if (run_start >= 0) {
        gfx.sprite(run_start * cell_w, row * cell_h, (col - run_start) * cell_w,
                   cell_h, colour, SM_DEPTH_FADE);
        run_start = -1;
      }
    }
  }
}

void sm_ui_draw_knockout(sm_gfx &gfx, const sm_assets &assets, float amount) {
  // No text. Nothing explains the rule, because the rule does not need
  // explaining: the shift simply begins again, and the board, the lampposts
  // and the apartment are exactly where they were. Telling the player they did
  // not lose progress would make them wonder whether they could.
  const float a = clamp01(amount);

  // The corners close first, like eyes; the curtain follows on a squared curve
  // so the two are legibly separate events rather than one grey wash.
  const sm_texref hud = assets.ref(SM_TEX_HUD, 0);
  const int size = 64 + static_cast<int>(96.0f * a);
  draw_vignette(gfx, hud, gfx.width(), gfx.height(), size, SM_DEPTH_FADE - 1);
  sm_ui_draw_fade(gfx, a * a, SM_COL_BLACK);
}

void sm_ui_draw_ending(sm_gfx &gfx, const sm_assets &assets, float amount) {
  // БАНКРОТ has already resolved on the board, in the board's own lettering,
  // painted on a wall in the factory. This is what follows it, and it arrives
  // only once the curtain is fully down: two lines, no card, no border, no
  // number, no "thanks for playing".
  const float a = clamp01(amount);
  sm_ui_draw_fade(gfx, a, SM_COL_BLACK);
  if (a < 0.999f)
    return;

  // Filed ABOVE the fade — the curtain is one opaque sprite at SM_DEPTH_FADE
  // and would otherwise be drawn over the top of this.
  const int w = gfx.width();
  sm_text_draw_centred(gfx, assets, w / 2, 100, "ТЫ БАНКРОТ", SM_COL_BLACK, 2,
                       SM_DEPTH_OVER_FADE);
  sm_text_draw_centred(gfx, assets, w / 2, 130, "И ТВОЙ ЗАВОД ТОЖЕ",
                       SM_COL_BLACK, 1, SM_DEPTH_OVER_FADE);
}

void sm_ui_draw_title(sm_gfx &gfx, const sm_assets &assets, float pulse,
                      bool has_save, int shifts_left) {
  const int w = gfx.width();
  const int h = gfx.height();

  gfx.sprite(0, 0, w, h, SM_COL_TITLE_BG, SM_DEPTH_HUD);

  // The title says what the game is called, and under it how much of the run is
  // left. The count is READ, never stored: `shifts_left` arrives as
  // sm_countdown::board_digit(), so the title and the factory board are the
  // same number by construction and a resumed run opens on its own figure
  // rather than on 5.
  sm_text_draw_centred(gfx, assets, w / 2, 62, "АЛКОЛДУН", SM_COL_TITLE_BG, 3,
                       SM_DEPTH_HUD_TEXT);
  char shifts[32];
  std::snprintf(shifts, sizeof(shifts), "Осталось смен: %d", shifts_left);
  sm_text_draw_centred(gfx, assets, w / 2, 100, shifts, SM_COL_TITLE_BG, 1,
                       SM_DEPTH_HUD_TEXT);

  // Colour cannot pulse — SAMPLE_TEXTURE would throw the tint away — so the
  // invitation blinks instead, which is what a machine of this era would have
  // done anyway. ~60 % duty so it reads as an invitation, not as an alarm.
  if (std::sin(pulse * 6.2831853f) > -0.35f) {
    sm_text_draw_centred(gfx, assets, w / 2, 168, "НАЖМИТЕ A", SM_COL_TITLE_BG,
                         1, SM_DEPTH_HUD_TEXT);
  }

  // A saved run is resumable, so say so — carefully. "The shift continues" is
  // true, is period-appropriate, and gives away nothing about how many there
  // are or how many are left.
  if (has_save) {
    sm_text_draw_centred(gfx, assets, w / 2, 190, "СМЕНА ПРОДОЛЖАЕТСЯ",
                         SM_COL_TITLE_BG, 1, SM_DEPTH_HUD_TEXT);
    // Which hand does which. Still no number, still nothing about how many
    // shifts there are — only that the left hand starts the week over.
    sm_text_draw_centred(gfx, assets, w / 2, 206, "B - СНАЧАЛА",
                         SM_COL_TITLE_BG, 1, SM_DEPTH_HUD_TEXT);
  }
}

void sm_ui_draw_debug(sm_gfx &gfx, const sm_assets &assets,
                      const sm_debug_model &model) {
  // DEVELOPMENT ONLY, behind the VIEW button, off by default. This is the only
  // place the RAW shifts_remaining may appear — unclamped, beside the phase and
  // the assembly step — and it may appear here only because a build the player
  // sees never opens it. Nothing in this overlay may migrate into
  // sm_ui_draw_hud().
  char buffer[512];
  std::snprintf(buffer, sizeof(buffer),
                "PRIM %d/%d DROP %d\n"
                "ENEMIES %d  HP %d\n"
                "TIER %d  SHIFTS %d\n"
                "PHASE %d  STEP %d\n"
                "MISSING TEX %d\n"
                "VRAM %lld B\n"
                "DT %d MS",
                model.primitives, model.capacity, model.dropped, model.enemies,
                model.hp, model.tier, model.shifts_remaining,
                static_cast<int>(model.phase), model.assembly_step,
                model.missing_textures,
                static_cast<long long>(model.video_bytes),
                static_cast<int>(model.dt * 1000.0f + 0.5f));

  // Over the HUD, under the fade: a dropped-primitive count is worth reading
  // through a transition.
  sm_text_draw(gfx, assets, 5, 5, buffer, SM_COL_PLATE, 1,
               SM_DEPTH_HUD_TEXT + 100);
  // The live pad, named. If a button labelled TRIANGLE lights SOUTH here, the
  // controller's SDL mapping is putting it there — the disc binds SOUTH and
  // EAST only, and never NORTH or WEST.
  {
    static const struct {
      uint64_t bit;
      const char *name;
    } SM_PAD_BITS[] = {
        {rv_pdk::RV_ISOURCE_FRONT_BTTN_SOUTH, "SOUTH"},
        {rv_pdk::RV_ISOURCE_FRONT_BTTN_EAST, "EAST"},
        {rv_pdk::RV_ISOURCE_FRONT_BTTN_WEST, "WEST"},
        {rv_pdk::RV_ISOURCE_FRONT_BTTN_NORTH, "NORTH"},
        {rv_pdk::RV_ISOURCE_BUMPER_LEFT, "LB"},
        {rv_pdk::RV_ISOURCE_BUMPER_RIGHT, "RB"},
        {rv_pdk::RV_ISOURCE_LEFT_TRIGGER_SOFT_PULL, "LT"},
        {rv_pdk::RV_ISOURCE_RIGHT_TRIGGER_SOFT_PULL, "RT"},
        {rv_pdk::RV_ISOURCE_MENU_BTTN_MENU, "MENU"},
        {rv_pdk::RV_ISOURCE_MENU_BTTN_VIEW, "VIEW"},
    };

    char line[96];
    int at = 0;
    line[at++] = 'P';
    line[at++] = 'A';
    line[at++] = 'D';
    line[at++] = ' ';
    for (const auto &entry : SM_PAD_BITS) {
      if (!(model.raw_buttons & entry.bit))
        continue;
      for (const char *p = entry.name; *p && at < 88; ++p)
        line[at++] = *p;
      if (at < 88)
        line[at++] = ' ';
    }
    line[at] = '\0';
    sm_text_draw(gfx, assets, 4, gfx.height() - 20, line, SM_COL_PLATE, 1,
                 SM_DEPTH_HUD_TEXT + 100);

    char triggers[48];
    std::snprintf(triggers, sizeof(triggers), "LT %.2f  RT %.2f",
                  static_cast<double>(model.left_trigger),
                  static_cast<double>(model.right_trigger));
    sm_text_draw(gfx, assets, 4, gfx.height() - 10, triggers, SM_COL_PLATE, 1,
                 SM_DEPTH_HUD_TEXT + 100);
  }
}

} // namespace solidmaid
