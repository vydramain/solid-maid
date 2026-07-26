#include "sm_game.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "pdk/rv_err.hpp"

#include "sm_atlas.hpp"
#include "sm_scene.hpp"
#include "sm_text.hpp"

namespace solidmaid {

// Placed by sm_build_factory(); declared here rather than in a header because
// exactly one caller draws on that panel.
extern const rv_pdklib::rv_vec3 sm_factory_board_text_origin;
extern const rv_pdklib::rv_vec3 sm_factory_board_plan_origin;
extern const rv_pdklib::rv_vec3 sm_factory_board_text_right;
extern const rv_pdklib::rv_vec3 sm_factory_board_text_down;

namespace {

using rv_pdklib::rv_vec3;

constexpr float SM_PI = 3.14159265358979323846f;

// The factory board's text, in world space. The panel geometry and these three
// vectors both live in sm_level_factory.cpp — the lettering is placed by
// whoever built the wall it is painted on, not guessed at from here.
// docs/environments.md: the board is the only number in the game and "No
// separate UI is ever used for this", so it is drawn as world geometry.
constexpr rv_pdk::rv_color SM_BOARD_INK{232, 214, 150};

float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// Digits are the only thing on the board that changes, so the string is built
// by hand rather than dragged in with <format>.
void board_line(int digit, char *out, int size) {
  // "ОСТАЛОСЬ: N" — UTF-8, drawn from the disc's own Cyrillic atlas.
  static const char prefix[] =
      "\xD0\x9E\xD0\xA1\xD0\xA2\xD0\x90\xD0\x9B\xD0\x9E\xD0\xA1\xD0\xAC: ";
  int i = 0;
  for (const char *p = prefix; *p && i < size - 2; ++p)
    out[i++] = *p;
  if (digit < 0)
    digit = 0;
  if (digit > 9)
    digit = 9;
  out[i++] = static_cast<char>('0' + digit);
  out[i] = '\0';
}

// "ПЛАН ВЫПОЛНЕН"
constexpr const char *SM_TEXT_PLAN_DONE =
    "\xD0\x9F\xD0\x9B\xD0\x90\xD0\x9D "
    "\xD0\x92\xD0\xAB\xD0\x9F\xD0\x9E\xD0\x9B\xD0\x9D\xD0\x95\xD0\x9D";

// A trace of what the scripted run actually did, on stderr, and ONLY while the
// autopilot is driving. It is how a headless playthrough becomes evidence
// instead of an exit code: every area entered, every board tick, every knockout
// is a line somebody can read back. Normal play prints nothing.
const char *phase_name(sm_phase phase) {
  switch (phase) {
  case SM_PHASE_HOME:
    return "home";
  case SM_PHASE_STREET:
    return "street";
  case SM_PHASE_FACTORY:
    return "factory";
  case SM_PHASE_RETURNING:
    return "returning";
  case SM_PHASE_FINAL:
    return "final";
  }
  return "?";
}

sm_area area_for_phase(sm_phase phase) {
  switch (phase) {
  case SM_PHASE_STREET:
    return SM_AREA_STREET;
  case SM_PHASE_FACTORY:
    return SM_AREA_FACTORY;
  default:
    return SM_AREA_HOME;
  }
}

} // namespace

// ── autopilot
// ─────────────────────────────────────────────────────────────────
//
// A scripted controller for the test harness. It synthesises the same rv_istate
// a pad produces, so every system downstream — the deadzone, the look curve,
// the aim assist, the hand-intent logic — runs exactly as it does for a human.
// An automated playthrough is only evidence if it goes through the real path.

void sm_autopilot::enable_from_environment() {
  const char *value = std::getenv("SOLIDMAID_AUTOPILOT");
  enabled_ = value != nullptr && value[0] != '\0' && value[0] != '0';
}

const rv_pdk::rv_istate *
sm_autopilot::drive(sm_mode mode, const sm_countdown &state,
                    const sm_scene &scene, const sm_player &player,
                    const sm_enemies &enemies, const sm_combat &combat,
                    float dt) {
  if (!enabled_)
    return nullptr;

  ++frames_;
  clock_ += dt;
  pad_ = rv_pdk::rv_istate{};

  // Harness only: hold Select across a two-second window starting at
  // SOLIDMAID_TEST_RESTART seconds, so the hold-to-restart binding is exercised
  // by a real run rather than trusted because it compiles.
  if (const char *at = std::getenv("SOLIDMAID_TEST_RESTART")) {
    const float when = static_cast<float>(std::atof(at));
    if (when > 0.0f && clock_ >= when && clock_ < when + 2.0f) {
      pad_.buttons |= rv_pdk::RV_ISOURCE_MENU_BTTN_MENU;
      return &pad_;
    }
  }

  if (mode == SM_MODE_TITLE || mode == SM_MODE_ENDING) {
    // Tap, do not hold: a held button on the title would be consumed as the
    // first frame's hand press in play.
    if ((frames_ % 30) < 6)
      pad_.buttons |= rv_pdk::RV_ISOURCE_FRONT_BTTN_SOUTH;
    if (mode == SM_MODE_ENDING)
      finished_ = true;
    return &pad_;
  }
  if (mode == SM_MODE_KNOCKOUT) {
    // The deliberate loss landed. Note it and go back to playing properly.
    deaths_seen_ = 1;
    wants_deliberate_loss_ = false;
    return &pad_;
  }
  if (mode != SM_MODE_PLAY)
    return &pad_;

  // One honest loss, once the first shift is banked, to prove that dying
  // restarts the shift and does not move the count. Recomputed EVERY frame
  // rather than latched: a sticky flag followed the walker out of the street
  // and into the factory, where it then refused both to fight and to work and
  // simply stood at the bench forever. It is attempted on the walk to work of
  // any shift after the first, and abandoned the moment that walk ends.
  wants_deliberate_loss_ = deaths_seen_ == 0 &&
                           state.phase == SM_PHASE_STREET &&
                           state.shifts_remaining < SM_SHIFTS_START;

  // Am I actually getting anywhere?
  const rv_vec3 drift = player.position() - last_position_;
  const float moved = std::sqrt(drift.x * drift.x + drift.z * drift.z);
  last_position_ = player.position();
  if (moved < 0.004f) {
    stuck_time_ += dt;
  } else {
    stuck_time_ = 0.0f;
  }
  if (stuck_time_ > 1.2f) {
    unstick_ = 1.0f;
    stuck_time_ = 0.0f;
  }
  if (unstick_ > 0.0f)
    unstick_ -= dt;

  // ── what am I trying to reach? ────────────────────────────────────────────
  //
  // In priority order: the tools by the door (a shift started without them is a
  // shift this harness cannot finish), then the bench, then whichever door is
  // open. Exactly the order a player works it out in.
  rv_vec3 goal = player.position();
  bool have_goal = false;
  bool work_here = false;
  bool fetch_here = false;
  bool flee_here = false;

  // Standing in a smoke cloud is the one thing that kills this walker outright:
  // the chip damage bypasses i-frames, so waiting it out is not an option. The
  // cloud is area DENIAL — the correct answer is to leave, which is exactly
  // what a player learns to do.
  for (const sm_cloud &cloud : enemies.clouds()) {
    if (!cloud.alive || cloud.age < 0.0f)
      continue;
    const rv_vec3 away = player.position() - cloud.centre;
    const float d = std::sqrt(away.x * away.x + away.z * away.z);
    if (d > SM_CLOUD_RADIUS * 1.15f)
      continue;
    const float scale = d > 0.15f ? ((SM_CLOUD_RADIUS + 2.2f) / d) : 1.0f;
    goal = rv_vec3{cloud.centre.x + away.x * scale, scene.floor_y,
                   cloud.centre.z + away.z * scale};
    have_goal = true;
    flee_here = true;
    break;
  }

  if (!have_goal)
    for (const sm_interactable &item : scene.interactables) {
      if (!item.active)
        continue;
      const bool want_brick =
          item.kind == SM_INTERACT_BRICK && !combat.has(SM_ITEM_BRICK);
      const bool want_pipe =
          item.kind == SM_INTERACT_PIPE && !combat.has(SM_ITEM_PIPE);
      if (want_brick || want_pipe) {
        goal = item.position;
        have_goal = true;
        fetch_here = true;
        break;
      }
    }
  if (!have_goal) {
    for (const sm_interactable &item : scene.interactables) {
      if (!item.active)
        continue;
      if (item.kind == SM_INTERACT_ASSEMBLY && !state.is_final_lap()) {
        goal = item.position;
        have_goal = true;
        work_here = true;
        break;
      }
    }
  }
  // Out of bricks with one on the floor nearby: go and get it before walking
  // into the next encounter empty-handed.
  if (!have_goal && !combat.has(SM_ITEM_BRICK)) {
    float best = 9.0f;
    for (const sm_brick &brick : combat.bricks()) {
      if (!brick.alive || brick.airborne)
        continue;
      const rv_vec3 to = brick.position - player.position();
      const float d = std::sqrt(to.x * to.x + to.z * to.z);
      if (d < best) {
        best = d;
        goal = rv_vec3{brick.position.x, scene.floor_y, brick.position.z};
        have_goal = true;
        fetch_here = true;
      }
    }
  }
  if (!have_goal) {
    for (const sm_trigger &trigger : scene.triggers) {
      if (!trigger.active)
        continue;
      goal = rv_vec3{(trigger.area.x0 + trigger.area.x1) * 0.5f, scene.floor_y,
                     (trigger.area.z0 + trigger.area.z1) * 0.5f};
      have_goal = true;
      break;
    }
  }
  if (!have_goal)
    return &pad_;

  // ── who is trying to stop me? ─────────────────────────────────────────────
  rv_vec3 target{};
  const bool threatened = enemies.nearest_target(
      player.eye(), sm_forward(player.yaw(), player.pitch()), 14.0f, 75.0f,
      target);
  float target_range = 0.0f;
  if (threatened) {
    const rv_vec3 to_target = target - player.eye();
    target_range =
        std::sqrt(to_target.x * to_target.x + to_target.z * to_target.z);
  }
  if (wants_deliberate_loss_ && threatened)
    goal = target;

  const rv_vec3 to_goal = goal - player.position();
  const float distance =
      std::sqrt(to_goal.x * to_goal.x + to_goal.z * to_goal.z);

  // ── steering: aiming and walking are separate, as they are on a pad ───────
  //
  // The right stick goes where the player must LOOK (an enemy, or the thing
  // they are about to press); the left stick goes where they must WALK. A
  // single "turn then move forward" loop cannot fight and travel at once, and
  // a harness that cannot fight dies in the factory every shift.
  float travel_yaw = std::atan2(to_goal.x, to_goal.z);
  // The probe must never reach PAST the goal. A fixed 1.4 m one sees the wall
  // behind a doorway it is standing in front of, calls the direct heading
  // blocked, and steers around a target it had already arrived at — which is
  // exactly how this walker spent a minute oscillating in the hallway.
  const float probe =
      distance < 1.4f ? (distance > 0.35f ? distance : 0.35f) : 1.4f;
  const float offsets[7] = {0.0f, 0.35f, -0.35f, 0.75f, -0.75f, 1.25f, -1.25f};
  for (float offset : offsets) {
    const float yaw = travel_yaw + offset;
    const rv_vec3 step{player.position().x + std::sin(yaw) * probe,
                       scene.floor_y,
                       player.position().z + std::cos(yaw) * probe};
    if (sm_scene_clear_line(scene, player.position(), step)) {
      travel_yaw = yaw;
      break;
    }
  }

  float aim_yaw = std::atan2(to_goal.x, to_goal.z);
  float aim_pitch = 0.0f;
  if (threatened && !wants_deliberate_loss_) {
    const rv_vec3 to_target = target - player.eye();
    aim_yaw = std::atan2(to_target.x, to_target.z);
    aim_pitch =
        std::atan2(to_target.y, target_range > 0.3f ? target_range : 0.3f);
  } else if (fetch_here || work_here) {
    // Look at what we are about to press: sm_scene_pick_interactable only
    // offers a prompt for something the player is actually facing.
    aim_pitch =
        std::atan2(goal.y - player.eye().y, distance > 0.3f ? distance : 0.3f);
  }

  float delta = aim_yaw - player.yaw();
  while (delta > SM_PI)
    delta -= 2.0f * SM_PI;
  while (delta < -SM_PI)
    delta += 2.0f * SM_PI;
  pad_.right_stick.x = clampf(delta * 1.7f, -1.0f, 1.0f);
  pad_.right_stick.y = clampf((aim_pitch - player.pitch()) * 2.5f, -1.0f, 1.0f);
  pad_.buttons |= rv_pdk::RV_ISOURCE_RIGHT_STICK_MOVE;

  // The left stick is camera-relative, so the world travel direction is rotated
  // into the player's frame rather than assumed to be "forward".
  // A trigger goal is never "arrived at": the goal is its CENTRE, the box is
  // only a few tens of centimetres deep, and stopping short of the centre
  // leaves the walker standing outside the volume waiting for something that
  // will never fire. Walk into it and let the transition end the phase.
  const float stop_at = flee_here ? 0.0f
                                  : (work_here ? SM_ASSEMBLY_REACH * 0.55f
                                               : (fetch_here ? 0.85f : 0.0f));
  if (distance > stop_at) {
    float relative = travel_yaw - player.yaw();
    while (relative > SM_PI)
      relative -= 2.0f * SM_PI;
    while (relative < -SM_PI)
      relative += 2.0f * SM_PI;
    pad_.left_stick.x = std::sin(relative);
    pad_.left_stick.y = std::cos(relative);
    pad_.buttons |= rv_pdk::RV_ISOURCE_LEFT_STICK_MOVE;
  }
  if (unstick_ > 0.0f) {
    // Wedged: back off and slide sideways rather than grinding into the wall.
    pad_.left_stick.x = (frames_ % 120) < 60 ? 1.0f : -1.0f;
    pad_.left_stick.y = -0.4f;
    pad_.buttons |= rv_pdk::RV_ISOURCE_LEFT_STICK_MOVE;
  }

  if (wants_deliberate_loss_) {
    // One honest loss: walk into the fight and never raise a hand.
    return &pad_;
  }

  // ── hands ─────────────────────────────────────────────────────────────────
  if (flee_here) {
    // Get out first. Swinging at something while standing in its cloud is
    // how this run died every time before.
  } else if (fetch_here && distance < 1.7f) {
    // Tap, do not hold: a held hand button on a brick would begin a throw the
    // instant the pickup succeeds.
    if ((frames_ % 18) < 5) {
      pad_.right_trigger = 1.0f;
      pad_.buttons |= rv_pdk::RV_ISOURCE_RIGHT_TRIGGER_SOFT_PULL;
    }
  } else if (threatened && target_range < SM_PIPE_RANGE * 0.9f &&
             combat.has(SM_ITEM_PIPE)) {
    // Up close the pipe is the answer, and it is the dependable one: it is
    // never consumed, so the harness cannot disarm itself.
    if ((frames_ % 26) < 6) {
      pad_.left_trigger = 1.0f;
      pad_.buttons |= rv_pdk::RV_ISOURCE_LEFT_TRIGGER_SOFT_PULL;
    }
  } else if (threatened && combat.has(SM_ITEM_BRICK) &&
             std::fabs(delta) < 0.30f) {
    // Charge, then release by simply stopping — the throw is a falling edge.
    // Charge for half the cycle, then let go — the throw is a FALLING edge, so
    // the trigger has to actually come back up.
    if ((frames_ % 54) < 30) {
      pad_.right_trigger = 1.0f;
      pad_.buttons |= rv_pdk::RV_ISOURCE_RIGHT_TRIGGER_SOFT_PULL;
    }
  } else if (work_here && distance < SM_ASSEMBLY_REACH * 0.8f) {
    pad_.right_trigger = 1.0f;
    pad_.buttons |= rv_pdk::RV_ISOURCE_RIGHT_TRIGGER_SOFT_PULL; // hold to work
  }

  return &pad_;
}

// ── game
// ──────────────────────────────────────────────────────────────────────

void sm_game::initialize(rv_pdk::rv_pdko &pdk, sm_assets &assets, sm_gfx &gfx,
                         sm_sound &sound) {
  pdk_ = &pdk;
  assets_ = &assets;
  gfx_ = &gfx;
  sound_ = &sound;
  // Sound joins hitstop and shake as the third channel every system already
  // has a handle on.
  feel_.sound = sound_;

  autopilot_.enable_from_environment();
  if (autopilot_.enabled() && sound_) {
    std::fprintf(stderr,
                 "[play] sound bank: %d missing, %lld bytes of %d KiB sound RAM\n",
                 sound_->missing(),
                 static_cast<long long>(sound_->sound_bytes()), 1024);
  }

  sm_countdown loaded{};
  // Development only: start the run at a chosen point on the countdown.
  // docs/production.md asks for exactly this under "Tooling & perf" —
  // "Tier-jump debug key — start any shift directly; essential for testing five
  // states without playing ten minutes each time". It is an environment
  // variable rather than a binding, so no button in the shipped game can reach
  // it, and nothing about finishing the game depends on it.
  if (const char *jump = std::getenv("SOLIDMAID_START_SHIFTS")) {
    const int value = std::atoi(jump);
    if (value >= 0 && value <= SM_SHIFTS_START) {
      state_.shifts_remaining = value;
      state_.phase = SM_PHASE_HOME;
      state_.assembly_step = 0;
      state_.finished = false;
      mode_ = SM_MODE_TITLE;
      fade_ = 0.0f;
      enter_area(SM_AREA_HOME);
      return;
    }
  }
  has_save_ = sm_save_load(pdk.cm(), loaded);
  if (has_save_ && !loaded.finished) {
    state_ = loaded;
    // Resume the SHIFT, not the phase. The card records where the player was
    // (docs/content.md keeps shift_phase for exactly that), but dropping a
    // returning player straight into the middle of a factory fight, or onto a
    // dark street at ОСТАЛОСЬ: 0, is disorienting in a way the design never
    // asks for — the game's own idiom for picking a shift back up is already
    // "restart the current shift from Home". So a session always opens where
    // a shift opens: in the apartment, which is also where the countdown is
    // most legible, since the room shows what has been taken.
    //
    // The countdown itself is untouched, and that is the part that matters:
    // "progress is how much is left". At most two minutes of walking is
    // repeated, and nothing about the run is replayed differently.
    state_.phase = SM_PHASE_HOME;
    state_.assembly_step = 0; // within-shift only; never resumed mid-ritual
  }

  mode_ = SM_MODE_TITLE;
  fade_ = 0.0f;
  enter_area(area_for_phase(state_.phase));
}

void sm_game::enter_area(sm_area area) {
  area_ = area;
  if (autopilot_.enabled()) {
    std::fprintf(stderr,
                 "[play] enter %s  shifts_remaining=%d tier=%d apartment=%d "
                 "lamps_lit=%d\n",
                 phase_name(state_.phase), state_.shifts_remaining,
                 state_.tier(), state_.apartment_state(), state_.lamps_lit());
  }
  sm_build_area(scene_, area, state_);

  player_.reset(scene_.player_start, scene_.player_yaw);

  // What is IN THE PLAYER'S HANDS survives a doorway. The brick and the pipe
  // are taken from beside the front door and carried through the street to the
  // factory and back — dropping them at every area boundary would make the
  // walk to work an unarmed one and the pickup by the door pointless.
  // sm_combat::reset() clears the hands too, so they are carried across by
  // hand here. A death does clear them (restart_shift), because a shift is
  // meant to begin by picking them up again.
  const sm_hand_item carried_right = combat_.right_hand();
  const sm_hand_item carried_left = combat_.left_hand();
  combat_.reset();
  if (carried_right != SM_ITEM_NONE)
    combat_.give(carried_right);
  if (carried_left != SM_ITEM_NONE)
    combat_.give(carried_left);
  enemies_.reset();
  // Clear the per-area feedback state, but NOT the sound handle: `feel_ =
  // sm_feel{}` wiped it along with the hitstop and the shake, and since every
  // system fires its cues through sm_feel, that one line silenced every effect
  // in the game from the first area onward while the music — which goes
  // straight through sound_ — kept playing and hid it.
  feel_.hitstop = 0.0f;
  feel_.shake = 0.0f;
  feel_.sound = sound_;

  television_on_ = false;
  assembly_hold_ = 0.0f;
  assembly_interrupt_flash_ = 0.0f;
  assembly_done_ = false;
  board_clack_ = 0.0f;
  shift_end_ = 0.0f;
  return_open_ = false;
  escalation_released_ = false;
  prompt_ = SM_PROMPT_NONE;
  prompt_index_ = -1;

  // The melody of the area we just walked into, played whole. The impoverishment
  // ramp reaches the music in exactly one place and no other: at zero there is
  // no melody at all, which is what docs/art-and-audio.md asks for — "no new
  // composition, no final theme, no swell at ПЛАН ВЫПОЛНЕН". Shortening the loop
  // per tier was tried and dropped: it truncates a written phrase mid-thought
  // for a difference almost nobody would hear.
  if (sound_) {
    if (state_.is_final_lap()) {
      sound_->stop_song();
    } else {
      const sm_song song = area == SM_AREA_HOME      ? SM_SONG_HOME
                           : area == SM_AREA_STREET  ? SM_SONG_STREET
                                                     : SM_SONG_FACTORY;
      sound_->play_song(song);
    }
  }
  step_from_ = scene_.player_start;
  step_distance_ = 0.0f;

  // Spare bricks, so the brick never reads as a resource that counts down.
  for (const rv_vec3 &spot : scene_.brick_spawns)
    combat_.scatter_brick(spot);

  // Triggers and interactables that only exist under a condition.
  for (sm_trigger &trigger : scene_.triggers) {
    if (trigger.id == SM_TRIGGER_RETURN_HOME)
      trigger.active = false;
    if (trigger.id == SM_TRIGGER_APPROACH_BOARD)
      trigger.active = state_.is_final_lap();
  }
  if (state_.is_final_lap()) {
    // The hall is inert: nothing to assemble, no return trigger, no work.
    for (sm_interactable &item : scene_.interactables) {
      if (item.kind == SM_INTERACT_ASSEMBLY)
        item.active = false;
    }
  }

  if (area == SM_AREA_STREET) {
    if (state_.is_final_lap()) {
      encounters_.begin_final_lap(scene_);
    } else {
      encounters_.begin_street(scene_, state_.shifts_remaining);
    }
  } else if (area == SM_AREA_FACTORY) {
    if (state_.is_final_lap()) {
      encounters_.begin_final_lap(scene_);
    } else {
      encounters_.begin_factory(scene_, state_.shifts_remaining);
    }
  } else {
    encounters_.begin_final_lap(scene_); // the apartment is never contested
  }
}

void sm_game::begin_transition(sm_phase next_phase, sm_area next_area) {
  if (mode_ != SM_MODE_PLAY)
    return;
  pending_phase_ = next_phase;
  pending_area_ = next_area;
  mode_ = SM_MODE_FADE_OUT;
  mode_time_ = 0.0f;
}

void sm_game::finish_transition() {
  state_.phase = pending_phase_;
  enter_area(pending_area_);
  sm_save_store(pdk_ ? pdk_->cm() : nullptr, state_);
  mode_ = SM_MODE_FADE_IN;
  mode_time_ = 0.0f;
}

void sm_game::knock_out() {
  if (autopilot_.enabled()) {
    std::fprintf(
        stderr,
        "[play] KNOCKED OUT in %s, shifts_remaining=%d (must not change)\n",
        phase_name(state_.phase), state_.shifts_remaining);
  }
  mode_ = SM_MODE_KNOCKOUT;
  mode_time_ = 0.0f;
}

void sm_game::restart_run() {
  // Everything back to the beginning: five shifts, an intact apartment, five
  // lamps burning, and no save left behind for the next person to resume into.
  if (pdk_ && pdk_->cm())
    pdk_->cm()->card_erase(SM_SAVE_SLOT);
  state_ = sm_countdown{};
  has_save_ = false;
  combat_.reset();
  television_on_ = false;
  enter_area(SM_AREA_HOME);
  mode_ = SM_MODE_TITLE;
  mode_time_ = 0.0f;
  fade_ = 0.0f;
  if (autopilot_.enabled())
    std::fprintf(stderr, "[play] RUN RESTARTED from Start\n");
}

void sm_game::restart_shift() {
  // The countdown is untouched. The board, the lampposts and the apartment
  // stay exactly where they were; only the two minutes are lost.
  // A shift begins the way the design says it does: with the work gear still
  // leaning by the door, waiting to be picked up.
  combat_.reset();
  state_.restart_current_shift();
  if (autopilot_.enabled()) {
    std::fprintf(stderr, "[play] shift restarts, shifts_remaining=%d\n",
                 state_.shifts_remaining);
  }
  enter_area(SM_AREA_HOME);
  sm_save_store(pdk_ ? pdk_->cm() : nullptr, state_);
  mode_ = SM_MODE_FADE_IN;
  mode_time_ = 0.0f;
  fade_ = 1.0f;
}

void sm_game::handle_triggers() {
  const sm_trigger_id id = sm_scene_trigger_at(scene_, player_.position());
  switch (id) {
  case SM_TRIGGER_LEAVE_HOME:
    begin_transition(SM_PHASE_STREET, SM_AREA_STREET);
    break;
  case SM_TRIGGER_ENTER_FACTORY:
    begin_transition(SM_PHASE_FACTORY, SM_AREA_FACTORY);
    break;
  case SM_TRIGGER_RETURN_HOME:
    if (return_open_)
      begin_transition(SM_PHASE_HOME, SM_AREA_HOME);
    break;
  case SM_TRIGGER_APPROACH_BOARD:
    if (state_.is_final_lap()) {
      state_.finished = true;
      state_.phase = SM_PHASE_FINAL;
      sm_save_store(pdk_ ? pdk_->cm() : nullptr, state_);
      if (autopilot_.enabled()) {
        std::fprintf(stderr, "[play] ПЛАН ВЫПОЛНЕН — run finished\n");
      }
      mode_ = SM_MODE_ENDING;
      mode_time_ = 0.0f;
    }
    break;
  case SM_TRIGGER_NONE:
  default:
    break;
  }
}

void sm_game::handle_hands(const sm_input &input, float dt) {
  (void)dt;

  const rv_vec3 eye = player_.eye();
  const rv_vec3 forward = player_.forward();

  const sm_prompt previous_prompt = prompt_;
  prompt_index_ =
      sm_scene_pick_interactable(scene_, eye, forward, SM_ASSEMBLY_REACH);
  prompt_ = SM_PROMPT_NONE;

  sm_interact_kind aimed = SM_INTERACT_NONE;
  if (prompt_index_ >= 0 &&
      prompt_index_ < static_cast<int>(scene_.interactables.size())) {
    aimed = scene_.interactables[static_cast<std::size_t>(prompt_index_)].kind;
  }

  // The prompt reflects what the hand button would actually do right now —
  // one at a time, never stacked.
  switch (aimed) {
  case SM_INTERACT_BRICK:
    if (!combat_.has(SM_ITEM_BRICK))
      prompt_ = SM_PROMPT_TAKE_BRICK;
    break;
  case SM_INTERACT_PIPE:
    if (!combat_.has(SM_ITEM_PIPE))
      prompt_ = SM_PROMPT_TAKE_PIPE;
    break;
  case SM_INTERACT_TELEVISION:
    prompt_ = SM_PROMPT_TELEVISION;
    break;
  case SM_INTERACT_ASSEMBLY:
    prompt_ = SM_PROMPT_ASSEMBLE;
    break;
  default:
    break;
  }

  // Nothing authored in front of us, but a brick on the floor within arm's
  // reach is still a brick. This is the other half of "bricks are never
  // limited" (docs/mechanics.md): thrown ones land, scattered spares exist,
  // and both can be picked up again. Without this the primary weapon runs out
  // one throw into the shift and the pipe becomes the only weapon in the game.
  prompt_world_brick_ = false;
  if (prompt_ == SM_PROMPT_NONE && !combat_.has(SM_ITEM_BRICK) &&
      combat_.brick_within(player_.position(), SM_PICKUP_RADIUS)) {
    prompt_ = SM_PROMPT_TAKE_BRICK;
    prompt_world_brick_ = true;
  }

  // The prompt announces itself once, on the frame it appears — "a very quiet
  // tick for the interact prompt" (docs/art-and-audio.md). Fired on the LEVEL it
  // would buzz for as long as the player stood there looking at the thing.
  if (prompt_ != SM_PROMPT_NONE && previous_prompt == SM_PROMPT_NONE)
    feel_.cue(SM_SFX_UI_PROMPT, 0.7f);

  // Right hand: pick up / throw / work.
  if (input.hand_right_pressed) {
    if (prompt_ == SM_PROMPT_TAKE_BRICK) {
      if (prompt_world_brick_) {
        combat_.take_brick(player_.position(), SM_PICKUP_RADIUS);
      } else {
        combat_.give(SM_ITEM_BRICK);
      }
      feel_.cue(SM_SFX_PICKUP);
      if (autopilot_.enabled())
        std::fprintf(stderr, "[play] took the brick\n");
    } else if (prompt_ == SM_PROMPT_TAKE_PIPE) {
      feel_.cue(SM_SFX_PICKUP);
      combat_.give(SM_ITEM_PIPE);
      if (autopilot_.enabled())
        std::fprintf(stderr, "[play] took the pipe\n");
    } else if (prompt_ == SM_PROMPT_TELEVISION) {
      television_on_ = !television_on_;
    } else if (prompt_ != SM_PROMPT_ASSEMBLE && combat_.has(SM_ITEM_BRICK)) {
      combat_.begin_charge();
    }
  }
  if (input.hand_right_released && combat_.charging()) {
    combat_.release_throw(player_.eye(), player_.forward(), feel_);
  }

  // Left hand: the pipe, and the same pickups so neither hand is a dead end.
  if (input.hand_left_pressed) {
    if (prompt_ == SM_PROMPT_TAKE_PIPE) {
      combat_.give(SM_ITEM_PIPE);
    } else if (prompt_ == SM_PROMPT_TAKE_BRICK) {
      if (prompt_world_brick_) {
        combat_.take_brick(player_.position(), SM_PICKUP_RADIUS);
      } else {
        combat_.give(SM_ITEM_BRICK);
      }
      feel_.cue(SM_SFX_PICKUP);
    } else if (prompt_ == SM_PROMPT_TELEVISION) {
      television_on_ = !television_on_;
    } else if (prompt_ != SM_PROMPT_ASSEMBLE && combat_.has(SM_ITEM_PIPE)) {
      combat_.swing(feel_);
    }
  }
}

void sm_game::update_assembly(const sm_input &input, float dt) {
  // The finished-assembly beat is handled BEFORE the final-lap guard, and the
  // order is load-bearing. The fifth assembly is the one that takes the count
  // to zero, so by the time this runs is_final_lap() is already true — guarding
  // on it first meant the board ticked to ОСТАЛОСЬ: 0 and the return trigger
  // then never opened, stranding the player in the factory with the work done
  // and no way out. assembly_done_ can only be set by an assembly that
  // completed during THIS visit, which the final lap has no way to start (its
  // bench is inactive), so putting it first is safe as well as necessary.
  if (assembly_done_) {
    // The beat after the board clacks, where the player gets to look at it.
    if (board_clack_ > 0.0f)
      board_clack_ -= dt;

    // Then the shift simply ends. No return trigger, no door to walk back to:
    // the count is put on screen for a few seconds and the fade takes him home
    // as if he had walked out himself.
    if (shift_end_ > 0.0f) {
      shift_end_ -= dt;
      if (shift_end_ <= 0.0f) {
        shift_end_ = 0.0f;
        begin_transition(SM_PHASE_HOME, SM_AREA_HOME);
      }
    }
    return;
  }
  if (state_.is_final_lap())
    return; // the hall is inert; there is nothing to build

  const bool at_bench = prompt_ == SM_PROMPT_ASSEMBLE;
  const bool holding = input.hand_right || input.hand_left;

  if (assembly_interrupt_flash_ > 0.0f)
    assembly_interrupt_flash_ -= dt;

  if (!at_bench || !holding) {
    // Letting go does not lose the step; only damage does.
    return;
  }

  assembly_hold_ += dt;
  if (assembly_hold_ < SM_ASSEMBLY_STEP_TIME)
    return;

  assembly_hold_ = 0.0f;
  ++state_.assembly_step;

  // One ordinary wave, at step 2. No boss, no phase change, no gimmick.
  if (state_.assembly_step == SM_ASSEMBLY_ESCALATION_STEP &&
      !escalation_released_) {
    escalation_released_ = true;
    encounters_.release_escalation_wave();
  }

  if (state_.assembly_step >= SM_ASSEMBLY_STEPS) {
    // THE ONE WRITER. This is the only call to commit_completed_assembly()
    // in the entire game, and the only place shifts_remaining moves.
    if (state_.commit_completed_assembly()) {
      if (autopilot_.enabled()) {
        std::fprintf(stderr, "[play] BOARD TICKS -> ОСТАЛОСЬ: %d\n",
                     state_.board_digit());
      }
      assembly_done_ = true;
      // The lamppost leaves the conveyor: there is nothing left on the
      // bench to work on, so the bench stops offering work. Without this
      // the player is still prompted to assemble a lamppost they have
      // already finished, and the return trigger never becomes the thing
      // in front of them.
      for (sm_interactable &item : scene_.interactables) {
        if (item.kind == SM_INTERACT_ASSEMBLY)
          item.active = false;
      }
      board_clack_ = SM_BOARD_CLACK_HOLD;
      // The one docs/art-and-audio.md calls the most important sound in the
      // game: the digit turning over, alone.
      feel_.cue(SM_SFX_BOARD_CLACK);
      feel_.impact(0.0f, SM_SHAKE_IMPACT);
      // The shift ends by itself now. The player is told how many are left and
      // walks out on their behalf; there is no door to find.
      shift_end_ = SM_SHIFT_END_HOLD;
      sm_save_store(pdk_ ? pdk_->cm() : nullptr, state_);
    }
  }
}

void sm_game::update(const sm_input &input, float dt) {
  if (dt > 0.1f)
    dt = 0.1f; // a stalled frame must not teleport anything
  last_dt_ = dt;
  mode_time_ += dt;
  title_pulse_ += dt;

  // Start: hold to restart the whole game.
  if (input.menu_held) {
    restart_hold_ += dt;
    if (restart_hold_ >= SM_RESTART_HOLD) {
      restart_run();
      restart_hold_ = 0.0f;
    }
  } else {
    restart_hold_ = 0.0f;
  }

  // Select: the development overlay.
  if (input.view_pressed)
    debug_overlay_ = !debug_overlay_;

  // The REAL dt, deliberately: a hitstop freezes the world, not the music.
  if (sound_)
    sound_->update(dt);

  last_input_ = input;

  switch (mode_) {
  case SM_MODE_TITLE:
    // The left hand abandons a saved run and starts again from five.
    // "Restartable at any time" (docs/gameplay.md §5) needs a way in,
    // and finishing the game was previously the only one.
    if (input.cancel_pressed && has_save_) {
      state_ = sm_countdown{};
      has_save_ = false;
      sm_save_store(pdk_ ? pdk_->cm() : nullptr, state_);
      enter_area(SM_AREA_HOME);
    }
    if (input.confirm_pressed || input.cancel_pressed) {
      if (autopilot_.enabled())
        std::fprintf(stderr, "[play] start\n");
      mode_ = SM_MODE_FADE_IN;
      mode_time_ = 0.0f;
      fade_ = 1.0f;
    }
    break;

  case SM_MODE_FADE_OUT:
    fade_ = clampf(mode_time_ / SM_FADE_TIME, 0.0f, 1.0f);
    if (fade_ >= 1.0f)
      finish_transition();
    break;

  case SM_MODE_FADE_IN:
    fade_ = clampf(1.0f - mode_time_ / SM_FADE_TIME, 0.0f, 1.0f);
    if (fade_ <= 0.0f) {
      mode_ = SM_MODE_PLAY;
      mode_time_ = 0.0f;
    }
    break;

  case SM_MODE_KNOCKOUT:
    fade_ = clampf(mode_time_ / SM_DEATH_HOLD, 0.0f, 1.0f);
    if (mode_time_ >= SM_DEATH_HOLD)
      restart_shift();
    break;

  case SM_MODE_ENDING:
    // The line resolves ON THE BOARD, so the world is held visible for a
    // beat before the curtain comes down — that beat is the ending. Once
    // it is down, a hand button starts a fresh run: a player left on a
    // black screen with no input accepted has been softlocked by the
    // credits, which is not an ending.
    if (mode_time_ > SM_ENDING_HOLD + SM_ENDING_FADE && input.confirm_pressed) {
      state_ = sm_countdown{};
      has_save_ = false;
      sm_save_store(pdk_ ? pdk_->cm() : nullptr, state_);
      enter_area(SM_AREA_HOME);
      mode_ = SM_MODE_TITLE;
      mode_time_ = 0.0f;
      fade_ = 0.0f;
    }
    break;

  case SM_MODE_PLAY: {
    feel_.update(dt);
    const float step = dt * feel_.time_scale();

    handle_hands(input, step);
    player_.update(input, scene_, enemies_, feel_, step);

    // Footfall, paced by METRES WALKED rather than by seconds, so a player
    // scraping along a wall does not march on the spot.
    if (sound_) {
      const rv_vec3 moved = player_.position() - step_from_;
      step_from_ = player_.position();
      step_distance_ += std::sqrt(moved.x * moved.x + moved.z * moved.z);
      if (step_distance_ >= SM_STEP_STRIDE) {
        step_distance_ -= SM_STEP_STRIDE;
        sound_->footstep(area_ == SM_AREA_HOME     ? SM_SONG_HOME
                         : area_ == SM_AREA_STREET ? SM_SONG_STREET
                                                   : SM_SONG_FACTORY);
      }
    }

    if (step > 0.0f) {
      encounters_.update(step, enemies_, player_.position());

      std::vector<sm_enemy_damage> damage;
      enemies_.update(step, scene_, player_.position(), damage);
      for (const sm_enemy_damage &hit : damage) {
        const int before = player_.hp();
        player_.damage(hit.amount, hit.from, hit.from_cloud, feel_);
        if (player_.hp() < before) {
          // "Interrupt on damage" — progress on the CURRENT step
          // only; completed steps stay done.
          if (assembly_hold_ > 0.0f) {
            assembly_hold_ = 0.0f;
            assembly_interrupt_flash_ = 0.6f;
            feel_.cue(SM_SFX_ASSEMBLY_BREAK);
          }
        }
      }

      combat_.update(step, scene_, enemies_, player_.eye(), player_.forward(),
                     feel_);
      update_assembly(input, step);
    }

    handle_triggers();
    if (player_.dead())
      knock_out();
    break;
  }
  }

  if (autopilot_.enabled() && mode_ == SM_MODE_PLAY &&
      (autopilot_.frames() % 180) == 0) {
    const rv_pdklib::rv_vec3 at = player_.position();
    std::fprintf(
        stderr,
        "[trace] t=%5.1fs pos=(%6.2f,%6.2f) yaw=%5.2f pitch=%5.2f prompt=%d "
        "hands=%d/%d hp=%3d enemies=%d prims=%4d/%4d bars=%d\n",
        static_cast<double>(autopilot_.frames()) / 60.0,
        static_cast<double>(at.x), static_cast<double>(at.z),
        static_cast<double>(player_.yaw()),
        static_cast<double>(player_.pitch()), static_cast<int>(prompt_),
        static_cast<int>(combat_.right_hand()),
        static_cast<int>(combat_.left_hand()), player_.hp(),
        enemies_.active_count(), gfx_ ? gfx_->submitted() : 0,
        gfx_ ? gfx_->capacity() : 0, sound_ ? sound_->bars_played() : 0);
  }
  injected_ =
      autopilot_.drive(mode_, state_, scene_, player_, enemies_, combat_, dt);
}

void sm_game::build_hud(sm_hud_model &out) const {
  out.hp = player_.hp();
  out.hurt_flash = player_.hurt_flash();
  out.right_hand = combat_.right_hand();
  out.left_hand = combat_.left_hand();
  out.throw_cooldown =
      clampf(combat_.throw_cooldown() / SM_BRICK_COOLDOWN, 0.0f, 1.0f);
  out.charge = combat_.charge();
  out.prompt = prompt_;
  out.assembly_step = state_.assembly_step;
  out.assembly_interrupted = assembly_interrupt_flash_ > 0.0f;
  out.assembly_progress =
      (prompt_ == SM_PROMPT_ASSEMBLE && !assembly_done_ &&
       !state_.is_final_lap())
          ? clampf(assembly_hold_ / SM_ASSEMBLY_STEP_TIME, 0.0f, 1.0f)
          : -1.0f;
}

void sm_game::render() {
  if (!gfx_ || !assets_)
    return;

  const sm_view view = player_.view(feel_);
  gfx_->begin(view, scene_.clear_colour, scene_.far_plane);

  if (mode_ == SM_MODE_TITLE) {
    sm_ui_draw_title(*gfx_, *assets_, title_pulse_, has_save_);
    gfx_->end();
    return;
  }

  const int tier = state_.tier();
  sm_scene_render(scene_, *gfx_, *assets_, state_);
  enemies_.render(*gfx_, *assets_, tier);
  combat_.render(*gfx_, *assets_, tier);

  // The television, when it is on.
  //
  // SM_UV_TELEVISION_ON is a WHOLE television — case, knobs, legs, doily, and a
  // broadcast on the glass — not a bare screen. So it has to cover exactly the
  // rectangle the unlit cell already covers, or it reads as a small television
  // stuck to the front of a big one. That rectangle is the case's front face,
  // and its corners are taken from the level's own convention rather than
  // guessed: sm_build_home() files the set as
  //   add_box(2.85, 0.36, 3.35, 0.92, 0.50, 1.05, ..., SM_UV_TELEVISION, ...)
  // whose -x side is add_wall(x0, z1, x0, z0, ...), i.e. corners running from
  // z = 0.92 to z = 0.36 across the top and the same across the bottom.
  //
  // A centimetre proud of the case so the ordering table cannot flicker the two
  // coplanar faces against each other, and the same tess as the box (one quad,
  // no subdivision) so the cell maps identically to the one underneath.
  if (area_ == SM_AREA_HOME && television_on_ &&
      state_.apartment_state() == 0) {
    const float x = 2.84f;
    const rv_vec3 screen[4] = {
        rv_vec3{x, 1.05f, 0.92f}, rv_vec3{x, 1.05f, 0.36f},
        rv_vec3{x, 0.50f, 0.92f}, rv_vec3{x, 0.50f, 0.36f}};
    gfx_->quad(screen, assets_->ref(SM_TEX_HOME_FURNITURE, tier),
               SM_UV_TELEVISION_ON, rv_pdk::rv_color{255, 255, 255}, 4.0f);
  }

  // The finished lamppost leaving on the conveyor. docs/environments.md puts
  // this beat before the board ticks and in view: "The finished lamppost leaves
  // on it, in view, before the board ticks." It is the signal that the work is
  // over, delivered where the player is standing and without a word of text —
  // and with audio out of scope it is carrying the whole "you are done" cue,
  // which is why it is a moving object and not a state flag.
  if (area_ == SM_AREA_FACTORY && assembly_done_ && board_clack_ > 0.0f) {
    const float t =
        1.0f - clampf(board_clack_ / SM_BOARD_CLACK_HOLD, 0.0f, 1.0f);
    const float z0 = 8.80f + t * 4.30f;
    const float y = 1.02f;
    const rv_vec3 part[4] = {rv_vec3{-0.70f, y, z0 + 1.60f},
                             rv_vec3{0.70f, y, z0 + 1.60f},
                             rv_vec3{-0.70f, y, z0}, rv_vec3{0.70f, y, z0}};
    gfx_->quad(part, assets_->ref(SM_TEX_LAMPPOST_PARTS, tier),
               SM_UV_PART_FINISHED, rv_pdk::rv_color{255, 255, 255}, 1.2f);
  }

  // The board. The only number in the game, painted on the wall it hangs on.
  if (area_ == SM_AREA_FACTORY) {
    char line[32];
    board_line(state_.board_digit(), line, sizeof(line));

    // While the digit flips, the panel's dim bulbs stutter. The clack is an
    // audio event in the design; this is its visual half, and it is what
    // makes the player look up at the one number in the game at the exact
    // moment it changes.
    rv_pdk::rv_color ink = SM_BOARD_INK;
    if (board_clack_ > 0.0f) {
      const int phase = static_cast<int>(board_clack_ * 9.0f) & 1;
      ink = phase ? rv_pdk::rv_color{255, 248, 214}
                  : rv_pdk::rv_color{150, 132, 82};
    }
    sm_text_draw_world(*gfx_, *assets_, sm_factory_board_text_origin,
                       sm_factory_board_text_right, sm_factory_board_text_down,
                       line, ink);
    if (state_.is_final_lap()) {
      sm_text_draw_world(*gfx_, *assets_, sm_factory_board_plan_origin,
                         sm_factory_board_text_right,
                         sm_factory_board_text_down, SM_TEXT_PLAN_DONE, ink);
    }
  }

  sm_ui_draw_viewmodel(*gfx_, *assets_, combat_, player_.bob_phase(),
                       player_.moving(), tier);

  sm_hud_model hud{};
  build_hud(hud);
  sm_ui_draw_hud(*gfx_, *assets_, hud);

  // The end-of-shift card. "ОСТАЛОСЬ СМЕН: N" — the count, stated in words, for
  // a few seconds after the lamppost leaves the conveyor and before the fade
  // takes him home.
  if (shift_end_ > 0.0f) {
    static const char label[] = "\xD0\x9E\xD0\xA1\xD0\xA2\xD0\x90\xD0\x9B"
                                "\xD0\x9E\xD0\xA1\xD0\xAC \xD0\xA1\xD0\x9C"
                                "\xD0\x95\xD0\x9D: ";
    char line[40];
    int i = 0;
    for (const char *p = label; *p && i < 38; ++p)
      line[i++] = *p;
    line[i++] = static_cast<char>('0' + state_.board_digit());
    line[i] = '\0';

    const int w = gfx_->width();
    const int h = gfx_->height();
    sm_text_draw_centred(*gfx_, *assets_, w / 2, h / 2 - 6, line, SM_BOARD_INK, 2,
                         SM_DEPTH_HUD_TEXT);
  }

  if (mode_ == SM_MODE_KNOCKOUT) {
    sm_ui_draw_knockout(*gfx_, *assets_, fade_);
  } else if (mode_ == SM_MODE_ENDING) {
    sm_ui_draw_ending(
        *gfx_, *assets_,
        clampf((mode_time_ - SM_ENDING_HOLD) / SM_ENDING_FADE, 0.0f, 1.0f));
  } else if (fade_ > 0.0f) {
    sm_ui_draw_fade(*gfx_, fade_, rv_pdk::rv_color{0, 0, 0});
  }

  // The restart hold, drawn as it fills. A control that destroys a run must
  // show that it is happening, and must be abandonable by letting go.
  if (restart_hold_ > 0.0f) {
    const int w = gfx_->width();
    const int width =
        static_cast<int>(static_cast<float>(w - 40) *
                         clampf(restart_hold_ / SM_RESTART_HOLD, 0.0f, 1.0f));
    gfx_->sprite(20, 6, w - 40, 5, rv_pdk::rv_color{28, 26, 22}, SM_DEPTH_HUD);
    gfx_->sprite(20, 6, width, 5, rv_pdk::rv_color{198, 176, 96},
                 SM_DEPTH_HUD + 1);
  }

  if (debug_overlay_) {
    sm_debug_model debug{};
    debug.primitives = gfx_->submitted();
    debug.dropped = gfx_->dropped();
    debug.capacity = gfx_->capacity();
    debug.enemies = enemies_.active_count();
    debug.hp = player_.hp();
    debug.tier = tier;
    debug.shifts_remaining = state_.shifts_remaining;
    debug.phase = state_.phase;
    debug.assembly_step = state_.assembly_step;
    debug.missing_textures = assets_->missing();
    debug.video_bytes = assets_->video_bytes();
    debug.dt = last_dt_;
    debug.raw_buttons = last_input_.raw_buttons;
    debug.left_trigger = last_input_.raw_left_trigger;
    debug.right_trigger = last_input_.raw_right_trigger;
    sm_ui_draw_debug(*gfx_, *assets_, debug);
  }

  gfx_->end();
}

void sm_game::shutdown() {
  if (autopilot_.enabled() && sound_) {
    std::fprintf(stderr, "\n[audit] effects fired this run:\n");
    for (int i = 0; i < SM_SFX_COUNT; ++i) {
      const sm_sfx id = static_cast<sm_sfx>(i);
      const int n = sound_->fired(id);
      std::fprintf(stderr, "[audit] %-28s %6d%s\n", sm_sound::name_of(id), n,
                   n == 0 ? "   <-- NEVER" : "");
    }
  }

  // The card is the last thing touched while the facade is still valid.
  //
  // A run that has not actually started is not worth saving: writing the
  // pristine state would make the next session's title offer to "continue" a
  // shift nobody has walked, which is exactly what a player sees after
  // holding Select to restart and then quitting.
  const bool untouched = state_.shifts_remaining == SM_SHIFTS_START &&
                         state_.phase == SM_PHASE_HOME &&
                         state_.assembly_step == 0 && !state_.finished;
  if (untouched)
    return;
  sm_save_store(pdk_ ? pdk_->cm() : nullptr, state_);
}

} // namespace solidmaid
