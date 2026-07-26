// Solidmaid — the shift loop.
//
//   Home -> Street -> Factory -> the board ticks -> Return Home     x5
//                                                        |
//                                                   at zero: the final lap
//
// This is the file that owns the transitions, the death-and-restart rule, and
// the ONE call to sm_countdown::commit_completed_assembly(). Everything the
// player can reach is reachable by walking: no debug teleport is required to
// finish the game, and the autopilot below drives the same buttons a human
// does.
#pragma once

#include <cstdint>
#include <vector>

#include "pdk/cio/rv_isource.hpp"
#include "pdk/rv_pdko.hpp"

#include "sm_assets.hpp"
#include "sm_combat.hpp"
#include "sm_common.hpp"
#include "sm_enemy.hpp"
#include "sm_gfx.hpp"
#include "sm_input.hpp"
#include "sm_player.hpp"
#include "sm_scene.hpp"
#include "sm_state.hpp"
#include "sm_ui.hpp"

namespace solidmaid {

// Where the frame loop is, above the shift itself.
enum sm_mode : int {
  SM_MODE_TITLE = 0,
  SM_MODE_PLAY,
  SM_MODE_FADE_OUT, // walking through a transition
  SM_MODE_FADE_IN,
  SM_MODE_KNOCKOUT, // down; the current shift is about to begin again
  SM_MODE_ENDING,   // ПЛАН ВЫПОЛНЕН has resolved
};

// A scripted controller, used by the test harness to play the game through the
// real code path. It synthesises the same rv_istate a pad would produce, so
// nothing downstream can tell the difference — which is the only way an
// automated playthrough is evidence of anything.
//
// It is compiled in but inert unless SOLIDMAID_AUTOPILOT is set in the
// environment. Normal play never constructs a step.
class sm_autopilot {
public:
  void enable_from_environment();
  bool enabled() const { return enabled_; }

  // Called once per frame with what the autopilot can see. Returns the pad
  // state for this frame.
  const rv_pdk::rv_istate *drive(sm_mode mode, const sm_countdown &state,
                                 const sm_scene &scene, const sm_player &player,
                                 const sm_enemies &enemies,
                                 const sm_combat &combat, float dt);

  bool finished() const { return finished_; }
  int frames() const { return frames_; }

private:
  rv_pdk::rv_istate pad_{};
  bool enabled_ = false;
  bool finished_ = false;
  int frames_ = 0;
  float clock_ = 0.0f;

  // Stuck detection. A scripted walker that wedges itself in a doorway would
  // report a failed playthrough that the game did not actually fail, so it
  // notices it has stopped moving and works its way loose.
  rv_pdklib::rv_vec3 last_position_{};
  float stuck_time_ = 0.0f;
  float unstick_ = 0.0f;

  // The harness is required to lose on purpose exactly once and verify the
  // shift restarts with the countdown untouched.
  int deaths_seen_ = 0;
  bool wants_deliberate_loss_ = false;
};

class sm_game {
public:
  void initialize(rv_pdk::rv_pdko &pdk, sm_assets &assets, sm_gfx &gfx);
  void update(const sm_input &input, float dt);
  void render();
  void shutdown();

  bool wants_release() const { return release_; }
  // Non-null only while the autopilot is driving.
  const rv_pdk::rv_istate *injected_pad() const { return injected_; }

  const sm_countdown &countdown() const { return state_; }
  const sm_player &player() const { return player_; }

private:
  void enter_area(sm_area area);
  void begin_transition(sm_phase next_phase, sm_area next_area);
  void finish_transition();
  void handle_triggers();
  void handle_hands(const sm_input &input, float dt);
  void update_assembly(const sm_input &input, float dt);
  void knock_out();
  void restart_run();
  void restart_shift();
  void build_hud(sm_hud_model &out) const;

  rv_pdk::rv_pdko *pdk_ = nullptr;
  sm_assets *assets_ = nullptr;
  sm_gfx *gfx_ = nullptr;

  sm_countdown state_{};
  sm_scene scene_{};
  sm_player player_{};
  sm_combat combat_{};
  sm_enemies enemies_{};
  sm_encounters encounters_{};
  sm_feel feel_{};
  sm_autopilot autopilot_{};

  sm_mode mode_ = SM_MODE_TITLE;
  sm_area area_ = SM_AREA_HOME;
  sm_area pending_area_ = SM_AREA_HOME;
  sm_phase pending_phase_ = SM_PHASE_HOME;

  float fade_ = 1.0f;
  float mode_time_ = 0.0f;
  float title_pulse_ = 0.0f;

  // Assembly, while the player is holding the bench.
  float assembly_hold_ = 0.0f;
  float assembly_interrupt_flash_ = 0.0f;
  bool assembly_done_ = false;
  float board_clack_ = 0.0f;
  bool return_open_ = false;
  bool escalation_released_ = false;

  // The one comfort interaction in the game, and the first thing the countdown
  // takes away (docs/content.md, apartment state 1).
  bool television_on_ = false;

  sm_prompt prompt_ = SM_PROMPT_NONE;
  int prompt_index_ = -1;
  // The offered brick is lying in the world rather than being one of the
  // scene's authored pickups, so the press routes to sm_combat, not to give().
  bool prompt_world_brick_ = false;

  bool debug_overlay_ = false;
  // Select is two bindings on one button: a tap shows the development
  // overlay, a deliberate hold throws the whole run away. A demo gets shown to
  // strangers, and a single stray press that wipes somebody's progress in
  // front of an audience is worse than one that does nothing.
  float restart_hold_ = 0.0f;
  bool release_ = false;
  bool has_save_ = false;
  float last_dt_ = 0.0f;
  sm_input last_input_{};
  const rv_pdk::rv_istate *injected_ = nullptr;
};

} // namespace solidmaid
