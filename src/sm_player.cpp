// Solidmaid — the first-person body, and the stick-aiming curve it is really
// about.
//
// Two thirds of this file is the camera, because docs/gameplay.md §4 makes
// stick aiming a CORRECTNESS requirement: the pad is the only aiming device the
// design allows, so the response curve and the aim assist are load-bearing
// rather than polish. The movement underneath them is deliberately dull — one
// posture, one speed band, a flat floor and a circle-versus-rectangle solve.
#include "sm_player.hpp"

#include <cmath>

#include "sm_enemy.hpp"
#include "sm_scene.hpp"

namespace solidmaid {
namespace {

using rv_pdklib::rv_vec3;

constexpr float SM_PI = 3.14159265358979323846f;
constexpr float SM_TWO_PI = 2.0f * SM_PI;
constexpr float SM_DEG_TO_RAD = SM_PI / 180.0f;

float clampf(float value, float low, float high) {
  return value < low ? low : (value > high ? high : value);
}

// Shortest signed angle, so "turn toward the target" never takes the long way
// round the back of the player.
float wrap_pi(float angle) {
  while (angle > SM_PI)
    angle -= SM_TWO_PI;
  while (angle < -SM_PI)
    angle += SM_TWO_PI;
  return angle;
}

} // namespace

void sm_player::reset(rv_vec3 position, float yaw) {
  position_ = position;
  velocity_ = rv_vec3{0.0f, 0.0f, 0.0f};
  yaw_ = wrap_pi(yaw);
  pitch_ = 0.0f;
  speed_ = 0.0f;

  // A shift always begins whole: losing costs time, not progress.
  hp_ = SM_PLAYER_MAX_HP;
  iframes_ = 0.0f;
  hurt_flash_ = 0.0f;

  bob_phase_ = 0.0f;
  bob_offset_ = 0.0f;
}

rv_vec3 sm_player::eye() const {
  return rv_vec3{position_.x, position_.y + SM_EYE_HEIGHT + bob_offset_,
                 position_.z};
}

rv_vec3 sm_player::forward() const { return sm_forward(yaw_, pitch_); }

void sm_player::update(const sm_input &input, const sm_scene &scene,
                       const sm_enemies &enemies, sm_feel &feel, float dt) {
  // The body never fires `feel` itself — an impact is something that happens TO
  // the player (damage(), below) or something a weapon lands (sm_combat). A
  // footstep is not an impact, and shaking the camera on one would be noise.
  (void)feel;

  if (dt <= 0.0f)
    return;

  const bool down = dead();

  // ── aim assist, part one: find the target and decide how much help ─────────
  //
  // Queried BEFORE the player's own look is applied, so the slowdown is a
  // property of where the crosshair already is rather than a feedback loop
  // against the stick.
  rv_vec3 target{};
  bool has_target = false;
  const float assist_cone = SM_AIM_ASSIST_CONE_DEGREES * SM_DEG_TO_RAD;
  float assist_t = 1.0f; // 0 = dead on the target, 1 = out at the cone's edge

  if (!down && enemies.nearest_target(eye(), forward(), SM_AIM_ASSIST_RANGE,
                                      SM_AIM_ASSIST_CONE_DEGREES, target)) {
    // A target through a wall is not a target. Without this the camera pulls
    // toward things the player cannot see or hit, which reads as a bug rather
    // than as help.
    if (sm_scene_clear_line(scene, eye(), target)) {
      const rv_vec3 to = target - eye();
      const float distance = rv_pdklib::rv_length(to);
      if (distance > 1e-3f) {
        const float yaw_error = wrap_pi(std::atan2(to.x, to.z) - yaw_);
        const float pitch_error =
            std::asin(clampf(to.y / distance, -1.0f, 1.0f)) - pitch_;
        const float error =
            std::sqrt(yaw_error * yaw_error + pitch_error * pitch_error);
        assist_t = clampf(error / assist_cone, 0.0f, 1.0f);
        has_target = true;
      }
    }
  }

  // Slowdown: full SM_AIM_ASSIST_SLOWDOWN with the crosshair on the target,
  // fading back to unscaled by the edge of the cone. The player keeps every bit
  // of their authority the moment they leave the target.
  const float look_scale =
      has_target
          ? SM_AIM_ASSIST_SLOWDOWN + (1.0f - SM_AIM_ASSIST_SLOWDOWN) * assist_t
          : 1.0f;

  // ── the look curve ────────────────────────────────────────────────────────
  //
  // sm_input has already killed the dead zone and rescaled what is left to
  // 0..1, so `deflection` here is the useful part of the stick's travel.
  //
  // The rate is interpolated from SM_LOOK_RATE_BASE to SM_LOOK_RATE_EDGE
  // through deflection^SM_LOOK_EXPONENT, and then the DEFLECTION ITSELF is the
  // per-axis multiplier. That second step is the one that matters: using the
  // interpolated rate directly would step from 0 to 1.35 rad/s the instant the
  // stick crosses the dead zone, which is exactly the discontinuity the design
  // is complaining about. Multiplying keeps it continuous at the dead-zone
  // edge, gives a genuinely fine zone near centre, and still reaches precisely
  // SM_LOOK_RATE_EDGE at full deflection.
  if (!down) {
    float look_x = input.look_x;
    float look_y = input.look_y;
    float deflection = std::sqrt(look_x * look_x + look_y * look_y);
    if (deflection > 1.0f) {
      look_x /= deflection;
      look_y /= deflection;
      deflection = 1.0f;
    }
    if (deflection > 0.0f) {
      const float curve = std::pow(deflection, SM_LOOK_EXPONENT);
      const float rate =
          SM_LOOK_RATE_BASE + (SM_LOOK_RATE_EDGE - SM_LOOK_RATE_BASE) * curve;
      yaw_ += look_x * rate * look_scale * dt;
      pitch_ += look_y * rate * look_scale * dt; // no inversion
    }
  }

  // ── aim assist, part two: the pull ────────────────────────────────────────
  //
  // Recomputed against the yaw/pitch the player just chose, so the assist is
  // always correcting the CURRENT error rather than arguing with the stick.
  // Strength scales with (error/cone)^2, so it vanishes smoothly as the
  // crosshair arrives — it can never snap, and it never holds the crosshair
  // pinned on a target the player is trying to leave.
  if (has_target) {
    const rv_vec3 to = target - eye();
    const float distance = rv_pdklib::rv_length(to);
    if (distance > 1e-3f) {
      const float yaw_error = wrap_pi(std::atan2(to.x, to.z) - yaw_);
      const float pitch_error =
          std::asin(clampf(to.y / distance, -1.0f, 1.0f)) - pitch_;
      const float error =
          std::sqrt(yaw_error * yaw_error + pitch_error * pitch_error);
      if (error > 1e-5f) {
        const float t = clampf(error / assist_cone, 0.0f, 1.0f);
        float step = SM_AIM_ASSIST_GRAVITY * t * t * dt;
        if (step > error)
          step = error; // never overshoot the target
        yaw_ += (yaw_error / error) * step;
        pitch_ += (pitch_error / error) * step;
      }
    }
  }

  const float pitch_limit = SM_PITCH_LIMIT_DEGREES * SM_DEG_TO_RAD;
  pitch_ = clampf(pitch_, -pitch_limit, pitch_limit);
  yaw_ = wrap_pi(yaw_);

  // ── movement ──────────────────────────────────────────────────────────────
  //
  // Camera-relative, one speed band, no gravity: every floor in the game is
  // flat and there is no jump, so y is pinned rather than integrated.
  float move_x = input.move_x;
  float move_y = input.move_y;
  float push = std::sqrt(move_x * move_x + move_y * move_y);
  if (push > 1.0f) {
    move_x /= push;
    move_y /= push;
    push = 1.0f;
  }

  rv_vec3 wish{0.0f, 0.0f, 0.0f};
  if (!down && push > 0.0f) {
    const rv_vec3 ahead =
        sm_forward(yaw_, 0.0f); // flattened: pitch never drives the feet
    const rv_vec3 across = sm_right(yaw_);
    wish = (across * move_x + ahead * move_y) * SM_PLAYER_WALK_SPEED;
  }

  rv_vec3 delta = wish - velocity_;
  delta.y = 0.0f;
  const float delta_length = rv_pdklib::rv_length(delta);
  const float rate =
      (!down && push > 0.0f) ? SM_PLAYER_ACCEL : SM_PLAYER_FRICTION;
  const float step = rate * dt;
  if (delta_length <= step || delta_length <= 1e-6f) {
    velocity_ = wish;
  } else {
    velocity_ = velocity_ + delta * (step / delta_length);
  }
  velocity_.y = 0.0f;

  const rv_vec3 wanted = position_ + velocity_ * dt;
  rv_vec3 resolved = sm_scene_slide(scene, position_, wanted, SM_PLAYER_RADIUS);
  resolved.y = scene.floor_y;

  // Take the velocity back from what the slide actually allowed. Walking into a
  // wall must not bank speed that fires the player sideways the moment the wall
  // ends, and it is also what makes the head bob stop against a corner.
  rv_vec3 achieved = (resolved - position_) * (1.0f / dt);
  achieved.y = 0.0f;
  const float achieved_speed = rv_pdklib::rv_length(achieved);
  if (achieved_speed > SM_PLAYER_WALK_SPEED) {
    achieved = achieved * (SM_PLAYER_WALK_SPEED / achieved_speed);
  }
  velocity_ = achieved;
  position_ = resolved;
  speed_ = rv_pdklib::rv_length(velocity_);

  // ── head bob ──────────────────────────────────────────────────────────────
  //
  // Driven by actual speed, not by whether a stick is pushed, so it is honest
  // about a player scraping along a wall. Subtle by construction: motion
  // comfort matters more than style in first person.
  const float gait = clampf(speed_ / SM_PLAYER_WALK_SPEED, 0.0f, 1.0f);
  bob_phase_ += SM_BOB_FREQUENCY * gait * dt;
  if (bob_phase_ > SM_TWO_PI)
    bob_phase_ = std::fmod(bob_phase_, SM_TWO_PI);
  bob_offset_ = SM_BOB_AMPLITUDE * std::sin(bob_phase_) * gait;

  // ── timers ────────────────────────────────────────────────────────────────
  if (iframes_ > 0.0f) {
    iframes_ -= dt;
    if (iframes_ < 0.0f)
      iframes_ = 0.0f;
  }
  if (hurt_flash_ > 0.0f) {
    // The flash lasts exactly as long as the invulnerability it announces, so
    // the player learns one duration rather than two.
    hurt_flash_ -= dt / SM_PLAYER_IFRAMES;
    if (hurt_flash_ < 0.0f)
      hurt_flash_ = 0.0f;
  }
}

void sm_player::damage(int amount, rv_vec3 from, bool bypass_iframes,
                       sm_feel &feel) {
  // `from` is carried by the interface for a knockback the design does not give
  // the player: one posture, one speed band, and no shove that could push a
  // body through a wall the slide has already resolved this frame.
  (void)from;

  if (amount <= 0 || dead())
    return;
  if (!bypass_iframes && iframes_ > 0.0f)
    return;

  hp_ -= amount;
  if (hp_ < 0)
    hp_ = 0;

  // Chip damage from a smoke cloud does NOT grant i-frames. Granting them would
  // let the cloud shield the player from the melee that is chasing them into
  // it, which turns area denial into area protection.
  if (!bypass_iframes)
    iframes_ = SM_PLAYER_IFRAMES;

  // This is the whole confirmation of damage in a build with no audio, so it is
  // set to full on EVERY hit, i-frames or not, cloud or fist.
  hurt_flash_ = 1.0f;
  feel.impact(SM_HITSTOP_LIGHT, SM_SHAKE_HURT);
}

sm_view sm_player::view(const sm_feel &feel) const {
  sm_view out{};
  out.eye = eye();
  out.yaw = yaw_;
  out.pitch = pitch_;

  float shake_yaw = 0.0f;
  float shake_pitch = 0.0f;
  feel.camera_offset(shake_yaw, shake_pitch);
  out.yaw += shake_yaw;
  out.pitch += shake_pitch;

  const float pitch_limit = SM_PITCH_LIMIT_DEGREES * SM_DEG_TO_RAD;
  out.pitch = clampf(out.pitch, -pitch_limit, pitch_limit);
  return out;
}

} // namespace solidmaid
