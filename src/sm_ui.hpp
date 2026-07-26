// Solidmaid — the HUD, the transition curtain, and the two end states.
//
// The hard rule, from docs/gameplay.md §2 and repeated in docs/mechanics.md:
// the HUD is "minimal and diegetically silent: crosshair, health, tool
// cooldown, interaction prompt. It NEVER shows the countdown. The only number
// in the game is painted on a wall in the factory."
//
// So there is no shift counter, no "Day 3 of 5", no timer, and no mirror of the
// board anywhere in this file. Adding one is not a feature — it breaks the
// design's central mechanism.
#pragma once

#include <string_view>

#include "sm_assets.hpp"
#include "sm_combat.hpp"
#include "sm_gfx.hpp"
#include "sm_state.hpp"

namespace solidmaid {

class sm_player;

// What the player is being invited to do right now. One prompt at a time —
// "no context-sensitive prompts stacking" (docs/gameplay.md §4).
enum sm_prompt : int {
  SM_PROMPT_NONE = 0,
  SM_PROMPT_TAKE_BRICK,
  SM_PROMPT_TAKE_PIPE,
  SM_PROMPT_TELEVISION,
  SM_PROMPT_ASSEMBLE,
};

struct sm_hud_model {
  int hp = SM_PLAYER_MAX_HP;
  float hurt_flash = 0.0f;
  sm_hand_item right_hand = SM_ITEM_NONE;
  sm_hand_item left_hand = SM_ITEM_NONE;
  float throw_cooldown = 0.0f; // 0..1 remaining
  float charge = 0.0f;         // 0..1 of a readied throw
  sm_prompt prompt = SM_PROMPT_NONE;
  float assembly_progress = -1.0f; // < 0 hides the bar
  int assembly_step = 0;
  bool assembly_interrupted = false;
};

void sm_ui_draw_hud(sm_gfx &gfx, const sm_assets &assets,
                    const sm_hud_model &model);

// The first-person hands and whatever is in them. Drawn from the `hands` and
// `items` atlases as screen-space quads with a little swing and bob, which is
// what a view model is on a machine with no skeletal animation.
void sm_ui_draw_viewmodel(sm_gfx &gfx, const sm_assets &assets,
                          const sm_combat &combat, float bob_phase, bool moving,
                          int tier);

// A full-screen curtain. `amount` 0 = clear, 1 = opaque.
void sm_ui_draw_fade(sm_gfx &gfx, float amount, rv_pdk::rv_color colour);

// Knocked out. No text explains the rule; the shift simply begins again.
void sm_ui_draw_knockout(sm_gfx &gfx, const sm_assets &assets, float amount);

// ПЛАН ВЫПОЛНЕН, after the board resolves at zero. The line is drawn in the
// board's own lettering; this is the screen that follows it.
void sm_ui_draw_ending(sm_gfx &gfx, const sm_assets &assets, float amount);

// The title card the disc opens on. `shifts_left` is the countdown as the
// player would resume it — sm_countdown::board_digit(), so it is clamped at
// zero and never disagrees with the factory board.
void sm_ui_draw_title(sm_gfx &gfx, const sm_assets &assets, float pulse,
                      bool has_save, int shifts_left);

// Development only, behind the VIEW button: frame budget, active enemies, tier,
// phase, and the count of textures that failed to load.
struct sm_debug_model {
  int primitives = 0;
  int dropped = 0;
  int capacity = 0;
  int enemies = 0;
  int hp = 0;
  int tier = 0;
  int shifts_remaining = 0;
  sm_phase phase = SM_PHASE_HOME;
  int assembly_step = 0;
  int missing_textures = 0;
  int64_t video_bytes = 0;
  float dt = 0.0f;

  // Live pad state, so a mis-mapped controller identifies itself.
  uint64_t raw_buttons = 0;
  float left_trigger = 0.0f;
  float right_trigger = 0.0f;
};

void sm_ui_draw_debug(sm_gfx &gfx, const sm_assets &assets,
                      const sm_debug_model &model);

} // namespace solidmaid
