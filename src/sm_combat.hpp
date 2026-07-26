// Solidmaid — the two improvised weapons and the weight behind them.
//
// docs/mechanics.md: "Weight is everything, and it's mostly cheap tricks
// stacked well" — hitstop of 0.06-0.1 s, camera micro-shake, and (in a build
// with audio) a loud sample, all landing on the same frame.
//
// AUDIO IS AN ADDITION HERE, NEVER A SUBSTITUTION. The visual channel still
// carries every confirmation on its own — hitstop, shake, the enemy's hurt
// flash and its knockback — because sm_feel::cue() is a no-op when no bank
// loaded, and the game has to stay finishable in silence. Sound is the third
// channel on top, not the one the others were waiting for.
#pragma once

#include <cstdint>
#include <vector>

#include "pdklib/rv_math.hpp"

#include "sm_common.hpp"
#include "sm_gfx.hpp"
#include "sm_sound.hpp"

namespace solidmaid {

class sm_assets;
struct sm_scene;
class sm_enemies;

// Which tool a hand is holding. The hand button's meaning follows from this
// plus what is in front of the player, and from nothing else.
enum sm_hand_item : int {
  SM_ITEM_NONE = 0,
  SM_ITEM_BRICK,
  SM_ITEM_PIPE,
};

// Screen shake and hitstop, shared by everything that wants to be felt.
// Hitstop scales the simulation's dt rather than skipping frames, so the
// rendering keeps running and the freeze reads as weight rather than a hitch.
struct sm_feel {
  float hitstop = 0.0f;
  float shake = 0.0f;
  // Advanced once per frame by update(); camera_offset() only READS it, which
  // is what lets every call inside one frame agree and a replayed run
  // reproduce. mutable so that read can stay const.
  mutable uint32_t noise = 0x1234567u;

  // The THIRD feedback channel, beside hitstop and shake. It lives here rather
  // than being threaded through every update() because sm_feel is already the
  // "how this lands" object every system with something to confirm receives —
  // and a hit that is felt, seen and heard is one event, not three.
  sm_sound *sound = nullptr;
  void cue(sm_sfx id, float gain = 1.0f, float pan = 0.0f);

  void impact(float hitstop_seconds, float shake_amount);
  void update(float dt);
  // Scales the frame's dt. Returns 0 while a hitstop is running.
  float time_scale() const { return hitstop > 0.0f ? 0.0f : 1.0f; }
  // A small deterministic offset for the camera, in radians.
  void camera_offset(float &out_yaw, float &out_pitch) const;
};

struct sm_brick {
  rv_pdklib::rv_vec3 position{};
  rv_pdklib::rv_vec3 velocity{};
  float spin = 0.0f;
  float life = 0.0f;
  bool airborne = false; // still flying: only an airborne brick damages
  bool alive = false;
};

class sm_combat {
public:
  void reset();

  // The player's hands. The right hand is the throwing hand.
  sm_hand_item right_hand() const { return right_hand_; }
  sm_hand_item left_hand() const { return left_hand_; }
  void give(sm_hand_item item);
  bool has(sm_hand_item item) const;

  // Brick: hold to ready, release to throw. `charge` is 0..1 of hold time.
  void begin_charge();
  float charge() const { return charge_; }
  bool charging() const { return charging_; }
  void release_throw(rv_pdklib::rv_vec3 eye, rv_pdklib::rv_vec3 forward,
                     sm_feel &feel);
  float throw_cooldown() const { return throw_cooldown_; }

  // Pipe: windup, an active window, then recovery. Returns false if a swing is
  // already running or cooling down.
  bool swing(sm_feel &feel);
  bool swinging() const { return swing_time_ > 0.0f; }
  float swing_phase() const; // 0..1 through the whole swing, for the view model

  // Advances bricks, the swing window, and cooldowns. Damage lands on enemies
  // through `enemies`; `scene` stops bricks at walls.
  void update(float dt, const sm_scene &scene, sm_enemies &enemies,
              rv_pdklib::rv_vec3 eye, rv_pdklib::rv_vec3 forward,
              sm_feel &feel);

  // Bricks in the world, so the renderer can draw them and the pickup logic
  // can find a settled one.
  const std::vector<sm_brick> &bricks() const { return bricks_; }
  // Spawns a resting brick — the world always has more (docs/mechanics.md:
  // "Bricks are never limited").
  void scatter_brick(rv_pdklib::rv_vec3 position);

  // Is there a brick lying within `radius` of `from` that could be picked up?
  // Only a settled one counts; a brick still in flight is not a pickup.
  bool brick_within(rv_pdklib::rv_vec3 from, float radius) const;

  // Take that brick off the floor and into the right hand. Returns false if
  // there was none. This is the other half of "bricks are never limited": a
  // thrown brick lands, and a landed brick can be picked up again — otherwise
  // the primary weapon runs out one throw into the shift.
  bool take_brick(rv_pdklib::rv_vec3 from, float radius);

  void render(sm_gfx &gfx, const sm_assets &assets, int tier) const;

private:
  std::vector<sm_brick> bricks_;
  sm_hand_item right_hand_ = SM_ITEM_NONE;
  sm_hand_item left_hand_ = SM_ITEM_NONE;

  bool charging_ = false;
  float charge_ = 0.0f;
  float throw_cooldown_ = 0.0f;

  float swing_time_ = 0.0f; // counts up through windup+active+recover
  float swing_cooldown_ = 0.0f;
  bool swing_landed_ = false; // one hit per swing
};

} // namespace solidmaid
