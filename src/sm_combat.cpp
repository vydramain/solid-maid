// Solidmaid — the two improvised weapons, and the weight behind them.
//
// docs/mechanics.md: "Weight is everything, and it's mostly cheap tricks
// stacked well". In this build the trick stack is hitstop + camera micro-shake
// + the enemy's own hurt flash and knockback + the loud sample the design asks
// for, all landing on the same frame and fired from the same sm_feel, so they
// arrive as ONE event rather than three that agree. The visual half stays
// load-bearing on its own: cue() is a no-op with no bank loaded, and the game
// has to remain finishable in silence.
#include "sm_combat.hpp"

#include <cmath>

#include "sm_assets.hpp"
#include "sm_atlas.hpp"
#include "sm_enemy.hpp"
#include "sm_scene.hpp"

namespace solidmaid {
namespace {

using rv_pdklib::rv_vec3;

float clampf(float value, float low, float high) {
  return value < low ? low : (value > high ? high : value);
}

// The whole swing, windup through recovery. Not a tuning value — it is the sum
// of three of them, and swing_phase() is defined across it.
constexpr float sm_swing_total() {
  return SM_PIPE_WINDUP + SM_PIPE_ACTIVE + SM_PIPE_RECOVER;
}

// How big a brick is ON SCREEN. This is a drawing size, not a tuning value: the
// brick's reach is SM_BRICK_HIT_RADIUS and nothing here may contradict it. A
// half-size of 12 cm gives a card about the size of a real silicate brick.
constexpr float sm_brick_half_size = 0.12f;

// A ceiling on how many bricks the world holds at once. Bricks are never a
// limited RESOURCE — the player can always find one — but the frame budget is
// 4096 primitives and the container should not grow without bound over a long
// shift, so the oldest resting brick is recycled instead. A container bound,
// not a design number.
constexpr int sm_brick_world_cap = 32;

// The classic Numerical-Recipes LCG. Deterministic and stateless-looking, which
// is what the replaying test harness needs from the shake.
uint32_t sm_noise_step(uint32_t state) {
  return state * 1664525u + 1013904223u;
}

// A value in -1..1 from a noise word.
float sm_noise_unit(uint32_t state) {
  return static_cast<float>((state >> 8) & 0xFFFFu) * (1.0f / 32767.5f) - 1.0f;
}

// ── where a sound is, to the ear
// ──────────────────────────────────────────────
//
// The chip gives a voice an L/R volume and nothing else: no distance model, no
// falloff, no listener (sm_sound.hpp). So a cue that happens somewhere in the
// world works its two numbers out here. The range is a shade past the smoker's
// aggro radius, so everything that can act on the player can be heard acting,
// and the floor keeps a far-off telegraph audible rather than fading it to a
// rumour — a windup the player cannot hear is a windup that is not a
// telegraph.
constexpr float SM_AUDIO_RANGE = 22.0f;
constexpr float SM_AUDIO_FLOOR = 0.15f;

void sm_cue_at(sm_feel &feel, sm_sfx id, rv_vec3 at, rv_vec3 listener,
               rv_vec3 forward, float gain) {
  const rv_vec3 to = at - listener;
  const float distance = rv_pdklib::rv_length(to);
  const float level =
      clampf(1.0f - distance / SM_AUDIO_RANGE, SM_AUDIO_FLOOR, 1.0f);

  // The listener's right hand, which is sm_right(yaw) read straight off the
  // forward vector rather than recovered from it through an angle. Flattened
  // and renormalised, so a player looking at their boots still hears which
  // side of them a thing is on.
  rv_vec3 right{forward.z, 0.0f, -forward.x};
  const float span = std::sqrt(right.x * right.x + right.z * right.z);
  float pan = 0.0f;
  if (span > 1e-4f) {
    right = right * (1.0f / span);
    // Divided by the distance, but never by less than a metre: inside arm's
    // reach the direction stops being meaningful and hard-panning it would
    // only make a hit at the player's feet jump between the ears.
    pan = rv_pdklib::rv_dot(to, right) / (distance > 1.0f ? distance : 1.0f);
  }
  feel.cue(id, gain * level, clampf(pan, -1.0f, 1.0f));
}

} // namespace

// ── sm_feel
// ───────────────────────────────────────────────────────────────────

void sm_feel::cue(sm_sfx id, float gain, float pan) {
  if (sound)
    sound->play(id, gain, pan);
}

void sm_feel::impact(float hitstop_seconds, float shake_amount) {
  // MAX, never sum. Two enemies dying on the same frame must not stack into a
  // freeze long enough to read as a hitch, and two shakes must not add up into
  // a camera the player cannot aim.
  if (hitstop_seconds > hitstop)
    hitstop = hitstop_seconds;
  if (shake_amount > shake)
    shake = shake_amount;
}

void sm_feel::update(float dt) {
  if (dt <= 0.0f)
    return;

  // Hitstop is spent in REAL time: it is what scales everyone else's dt, so it
  // cannot be scaled by itself or it would never end.
  if (hitstop > 0.0f) {
    hitstop -= dt;
    if (hitstop < 0.0f)
      hitstop = 0.0f;
  }

  if (shake > 0.0f) {
    shake *= std::exp(-SM_SHAKE_DECAY * dt);
    if (shake < 1e-4f)
      shake = 0.0f;
  }

  // The shake's direction is advanced exactly ONCE per frame, here — not inside
  // camera_offset(). camera_offset() is therefore a pure read, so the camera
  // agrees with itself however many times a frame asks for it, and a replayed
  // run produces the same numbers.
  noise = sm_noise_step(noise);
}

void sm_feel::camera_offset(float &out_yaw, float &out_pitch) const {
  if (shake <= 0.0f) {
    out_yaw = 0.0f;
    out_pitch = 0.0f;
    return;
  }
  const uint32_t a = sm_noise_step(noise);
  const uint32_t b = sm_noise_step(a);
  out_yaw = sm_noise_unit(a) * shake;
  out_pitch = sm_noise_unit(b) * shake;
}

// ── sm_combat
// ─────────────────────────────────────────────────────────────────

namespace {

// A slot for a new brick: reuse a dead one, then grow, then recycle the oldest
// resting brick. An AIRBORNE brick is never recycled — it is mid-flight and the
// player is watching it.
sm_brick *sm_acquire_brick(std::vector<sm_brick> &bricks) {
  for (sm_brick &brick : bricks) {
    if (!brick.alive)
      return &brick;
  }
  if (static_cast<int>(bricks.size()) < sm_brick_world_cap) {
    bricks.push_back(sm_brick{});
    return &bricks.back();
  }
  sm_brick *oldest = nullptr;
  for (sm_brick &brick : bricks) {
    if (brick.airborne)
      continue;
    if (oldest == nullptr || brick.life > oldest->life)
      oldest = &brick;
  }
  return oldest; // null only if every slot is in flight, which the cap prevents
}

} // namespace

void sm_combat::reset() {
  bricks_.clear();
  right_hand_ = SM_ITEM_NONE;
  left_hand_ = SM_ITEM_NONE;

  charging_ = false;
  charge_ = 0.0f;
  throw_cooldown_ = 0.0f;

  swing_time_ = 0.0f;
  swing_cooldown_ = 0.0f;
  swing_landed_ = false;
}

void sm_combat::give(sm_hand_item item) {
  // The right hand throws, the left hand swings — which is also the trigger
  // layout in docs/gameplay.md §4, so the pad and the fiction agree.
  if (item == SM_ITEM_BRICK) {
    right_hand_ = SM_ITEM_BRICK;
    charging_ = false;
    charge_ = 0.0f;
  } else if (item == SM_ITEM_PIPE) {
    left_hand_ = SM_ITEM_PIPE;
  }
}

namespace {

// Nearest settled brick within `radius`, or -1. Airborne bricks are excluded:
// catching one out of the air is not a mechanic this game has.
int sm_nearest_resting(const std::vector<sm_brick> &bricks,
                       rv_pdklib::rv_vec3 from, float radius) {
  int best = -1;
  float best_sq = radius * radius;
  for (std::size_t i = 0; i < bricks.size(); ++i) {
    const sm_brick &brick = bricks[i];
    if (!brick.alive || brick.airborne)
      continue;
    const rv_pdklib::rv_vec3 to = brick.position - from;
    const float d_sq = to.x * to.x + to.y * to.y + to.z * to.z;
    if (d_sq <= best_sq) {
      best_sq = d_sq;
      best = static_cast<int>(i);
    }
  }
  return best;
}

} // namespace

bool sm_combat::brick_within(rv_pdklib::rv_vec3 from, float radius) const {
  return sm_nearest_resting(bricks_, from, radius) >= 0;
}

bool sm_combat::take_brick(rv_pdklib::rv_vec3 from, float radius) {
  if (right_hand_ == SM_ITEM_BRICK)
    return false;
  const int index = sm_nearest_resting(bricks_, from, radius);
  if (index < 0)
    return false;

  bricks_[static_cast<std::size_t>(index)] = sm_brick{}; // off the floor
  give(SM_ITEM_BRICK);
  return true;
}

bool sm_combat::has(sm_hand_item item) const {
  return right_hand_ == item || left_hand_ == item;
}

void sm_combat::begin_charge() {
  if (right_hand_ != SM_ITEM_BRICK)
    return;
  if (throw_cooldown_ > 0.0f)
    return;
  charging_ = true;
  charge_ = 0.0f;
}

void sm_combat::release_throw(rv_vec3 eye, rv_vec3 forward, sm_feel &feel) {
  // A throw is not an impact. The weight of the brick is spent where it lands,
  // not where it leaves the hand — see update(). It does have a voice, though,
  // fired at the bottom of this function once the brick is genuinely in the
  // air: a refused throw must stay silent or the sound stops meaning "gone".
  if (!charging_)
    return;
  charging_ = false;
  if (right_hand_ != SM_ITEM_BRICK)
    return;

  const float charge = clampf(charge_, 0.0f, 1.0f);
  charge_ = 0.0f;

  sm_brick *brick = sm_acquire_brick(bricks_);
  if (brick == nullptr)
    return;

  const rv_vec3 aim = rv_pdklib::rv_normalize(forward);
  const float speed =
      SM_BRICK_SPEED_MIN + (SM_BRICK_SPEED_MAX - SM_BRICK_SPEED_MIN) * charge;

  // Launched a body-radius ahead of the eye so the brick is never spawned
  // inside the thrower's own collision circle.
  *brick = sm_brick{};
  brick->position = eye + aim * SM_PLAYER_RADIUS;
  brick->velocity = aim * speed;
  brick->spin = 0.0f;
  brick->life = 0.0f;
  brick->airborne = true;
  brick->alive = true;

  right_hand_ = SM_ITEM_NONE; // it left the hand; the world has more
  throw_cooldown_ = SM_BRICK_COOLDOWN;

  // In the player's own hands, so it is centred. Louder for a fuller charge:
  // the charge is otherwise a purely visual quantity, and this is the one
  // place the effort behind a throw is audible.
  feel.cue(SM_SFX_BRICK_THROW, 0.70f + 0.30f * charge);
}

bool sm_combat::swing(sm_feel &feel) {
  // Starting a swing is a telegraph, not a hit: nothing is FELT until the
  // active window connects. It is heard, though — the windup is what confirms
  // the input took, a whole SM_PIPE_WINDUP before the arc could land, and it
  // is the player's own arm so it is centred and unattenuated.
  if (left_hand_ != SM_ITEM_PIPE)
    return false;
  if (swinging() || swing_cooldown_ > 0.0f)
    return false;

  swing_time_ = 1e-4f; // strictly positive so swinging() reads true this frame
  swing_landed_ = false;
  feel.cue(SM_SFX_PIPE_SWING);
  return true;
}

float sm_combat::swing_phase() const {
  if (swing_time_ <= 0.0f)
    return 0.0f;
  return clampf(swing_time_ / sm_swing_total(), 0.0f, 1.0f);
}

void sm_combat::update(float dt, const sm_scene &scene, sm_enemies &enemies,
                       rv_vec3 eye, rv_vec3 forward, sm_feel &feel) {
  if (dt <= 0.0f)
    return;

  if (throw_cooldown_ > 0.0f) {
    throw_cooldown_ -= dt;
    if (throw_cooldown_ < 0.0f)
      throw_cooldown_ = 0.0f;
  }
  if (swing_cooldown_ > 0.0f) {
    swing_cooldown_ -= dt;
    if (swing_cooldown_ < 0.0f)
      swing_cooldown_ = 0.0f;
  }

  // ── the readied brick ─────────────────────────────────────────────────────
  if (charging_) {
    charge_ = clampf(charge_ + dt / SM_BRICK_CHARGE_TIME, 0.0f, 1.0f);
  }

  // ── the pipe ──────────────────────────────────────────────────────────────
  if (swing_time_ > 0.0f) {
    const float was = swing_time_;
    swing_time_ += dt;

    const float active_begins = SM_PIPE_WINDUP;
    const float active_ends = SM_PIPE_WINDUP + SM_PIPE_ACTIVE;
    // The window is tested against the whole step rather than against the
    // instant, so a long frame cannot skip straight over a 0.10 s window and
    // silently eat the swing.
    const bool crossed_window =
        was < active_ends && swing_time_ > active_begins;

    if (crossed_window && !swing_landed_) {
      const int hits =
          enemies.damage_arc(eye, forward, SM_PIPE_RANGE,
                             SM_PIPE_HALF_ARC_DEGREES, SM_PIPE_DAMAGE, feel);
      if (hits > 0) {
        swing_landed_ = true; // one hit per swing, however many it caught
        feel.impact(SM_HITSTOP_HEAVY, SM_SHAKE_IMPACT);
      }
    }

    if (swing_time_ >= sm_swing_total()) {
      swing_time_ = 0.0f;
      swing_landed_ = false;
      // There is no SM_PIPE_COOLDOWN in the tuning table; the recovery
      // window is reused as the gap between swings rather than inventing a
      // number here. See the report.
      swing_cooldown_ = SM_PIPE_COOLDOWN;
    }
  }

  // ── bricks in flight ──────────────────────────────────────────────────────
  for (sm_brick &brick : bricks_) {
    if (!brick.alive)
      continue;
    brick.life += dt;
    if (!brick.airborne)
      continue;

    const rv_vec3 from = brick.position;
    brick.velocity.y -= SM_BRICK_GRAVITY * dt;
    const rv_vec3 to = from + brick.velocity * dt;

    const float flight_speed = rv_pdklib::rv_length(brick.velocity);
    brick.spin += flight_speed * dt;

    // Enemies first: a brick that would pass through a body on its way into a
    // wall has hit the body. The radius is deliberately generous — the pad
    // cannot be as precise as a mouse and the design should not pretend so.
    const rv_vec3 knock =
        flight_speed > 1e-4f
            ? brick.velocity * (SM_BRICK_KNOCKBACK / flight_speed)
            : rv_vec3{0.0f, 0.0f, 0.0f};
    const int hits = enemies.damage_sphere(to, SM_BRICK_HIT_RADIUS,
                                           SM_BRICK_DAMAGE, knock, feel);
    if (hits > 0) {
      feel.impact(SM_HITSTOP_HEAVY, SM_SHAKE_IMPACT);
      // A body, not concrete: the dull one, at the point of contact rather
      // than where the brick comes to rest. Same frame as the hitstop, the
      // shake and the enemy's flash, which is the whole point of routing it
      // through sm_feel.
      sm_cue_at(feel, SM_SFX_BRICK_HIT_SOFT, to, eye, forward, 1.0f);
      brick.position = rv_vec3{to.x, scene.floor_y, to.z};
      brick.velocity = rv_vec3{0.0f, 0.0f, 0.0f};
      brick.airborne = false;
      brick.life = 0.0f; // it is a fresh pickup where it fell
      continue;
    }

    // Walls stop it. It drops where it was still clear, so a brick can never
    // come to rest inside geometry the player cannot reach into.
    if (!sm_scene_clear_line(scene, from, to)) {
      // Concrete. Cued where the brick actually struck — the last point it was
      // still clear — and not where it drops to, so a brick thrown at a wall
      // across the street is heard over there.
      sm_cue_at(feel, SM_SFX_BRICK_HIT_HARD, from, eye, forward, 1.0f);
      brick.position = rv_vec3{from.x, scene.floor_y, from.z};
      brick.velocity = rv_vec3{0.0f, 0.0f, 0.0f};
      brick.airborne = false;
      brick.life = 0.0f;
      continue;
    }

    // The floor, or the end of its patience. Either way it becomes a resting,
    // pickable brick — a brick is NEVER consumed, only relocated.
    if (to.y <= scene.floor_y || brick.life >= SM_BRICK_LIFETIME) {
      // The floor is concrete too, so it is the same crack — and it doubles as
      // the "you missed" report, which is worth as much as the hit is.
      sm_cue_at(feel, SM_SFX_BRICK_HIT_HARD, rv_vec3{to.x, scene.floor_y, to.z},
                eye, forward, 1.0f);
      brick.position = rv_vec3{to.x, scene.floor_y, to.z};
      brick.velocity = rv_vec3{0.0f, 0.0f, 0.0f};
      brick.airborne = false;
      brick.life = 0.0f;
      continue;
    }

    brick.position = to;
  }

  // ── the enemies' own voices ───────────────────────────────────────────────
  //
  // Telegraphs, footfalls and bodies going down. They are pumped from here
  // because sm_enemies::update() is handed no sm_feel and its signature is not
  // this change's to alter — while THIS function is called on the very next
  // line of sm_game's frame, inside the same `step > 0` block, and already
  // holds every argument the cues need: the listener, its facing, the enemies
  // and the dt they were just advanced by.
  //
  // Last in the function on purpose. By now the swing and the bricks above
  // have filed their kills, so a death is heard on the frame it happens rather
  // than on the one after it.
  sm_enemy_audio(enemies, scene, eye, forward, dt, feel);
}

void sm_combat::scatter_brick(rv_vec3 position) {
  sm_brick *brick = sm_acquire_brick(bricks_);
  if (brick == nullptr)
    return;

  *brick = sm_brick{};
  brick->position = position;
  brick->velocity = rv_vec3{0.0f, 0.0f, 0.0f};
  brick->spin = 0.0f;
  brick->life = 0.0f;
  brick->airborne = false;
  brick->alive = true;
}

void sm_combat::render(sm_gfx &gfx, const sm_assets &assets, int tier) const {
  const sm_texref items = assets.ref(SM_TEX_ITEMS, tier);
  if (!items.valid())
    return;

  // No tint: SAMPLE_TEXTURE replaces the vertex colour on this console, so a
  // tint on a textured draw is a lie. The brick darkens with the tier through
  // its PALETTE, which is what assets.ref(..., tier) just handed us.
  const rv_pdk::rv_color plain{255, 255, 255};

  for (const sm_brick &brick : bricks_) {
    if (!brick.alive)
      continue;
    // A resting brick sits ON the floor: its centre is half a card up. An
    // airborne one is drawn where it actually is.
    const rv_vec3 centre =
        brick.airborne
            ? brick.position
            : rv_vec3{brick.position.x, brick.position.y + sm_brick_half_size,
                      brick.position.z};
    gfx.billboard(centre, sm_brick_half_size, sm_brick_half_size, items,
                  SM_UV_BRICK_WORLD, plain);
  }

  // A pipe resting in the world is NOT drawn here: sm_combat holds no position
  // for one (the header has no member for it), so the world's pipe belongs to
  // the scene's interactables and is drawn by sm_scene_render. See the report.
}

} // namespace solidmaid
