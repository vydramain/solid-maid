// Solidmaid — the first-person body and the camera on top of it.
//
// docs/gameplay.md §4 makes the aiming feel a CORRECTNESS requirement rather
// than polish, because a thumbstick is the only aiming device the design
// allows:
//
//   * a look curve with a dead zone, a low-sensitivity zone near centre and a
//     higher rate toward the edge — "a single linear sensitivity value will
//     feel bad and no amount of tuning the number will fix it";
//   * pitch clamped near ±85°, no inversion by default;
//   * aim assist is MANDATORY — light gravity toward enemy centres inside a
//     modest cone plus a slowdown as the crosshair crosses a target.
#pragma once

#include "pdklib/rv_math.hpp"

#include "sm_combat.hpp"
#include "sm_common.hpp"
#include "sm_gfx.hpp"
#include "sm_input.hpp"

namespace solidmaid {

struct sm_scene;
class sm_enemies;

class sm_player {
public:
  // Places the body and clears everything within-shift. Health is restored:
  // a shift always begins whole (docs/mechanics.md — losing costs time, not
  // progress).
  void reset(rv_pdklib::rv_vec3 position, float yaw);

  void update(const sm_input &input, const sm_scene &scene,
              const sm_enemies &enemies, sm_feel &feel, float dt);

  // Applies damage subject to i-frames. Cloud chip damage sets
  // `bypass_iframes` so an area-denial effect is not neutralised by them.
  void damage(int amount, rv_pdklib::rv_vec3 from, bool bypass_iframes,
              sm_feel &feel);

  sm_view view(const sm_feel &feel) const;
  rv_pdklib::rv_vec3 position() const { return position_; }
  rv_pdklib::rv_vec3 eye() const;
  rv_pdklib::rv_vec3 forward() const;
  float yaw() const { return yaw_; }
  float pitch() const { return pitch_; }

  int hp() const { return hp_; }
  bool dead() const { return hp_ <= 0; }
  float hurt_flash() const { return hurt_flash_; }
  bool moving() const { return speed_ > 0.4f; }
  float speed() const { return speed_; }
  // The footfall phase. The view model syncs the hands to it, so the sway and
  // the walk are the same motion rather than two that drift apart.
  float bob_phase() const { return bob_phase_; }

private:
  rv_pdklib::rv_vec3 position_{};
  rv_pdklib::rv_vec3 velocity_{};
  float yaw_ = 0.0f;
  float pitch_ = 0.0f;
  float speed_ = 0.0f;

  int hp_ = SM_PLAYER_MAX_HP;
  float iframes_ = 0.0f;
  float hurt_flash_ = 0.0f;

  float bob_phase_ = 0.0f;
  float bob_offset_ = 0.0f;
};

} // namespace solidmaid
