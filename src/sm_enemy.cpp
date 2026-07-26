// Solidmaid — the two archetypes that ship, and the spawner discipline.
//
// ── THE RULE THIS FILE IS BUILT AROUND
// ────────────────────────────────────────
//
// docs/characters.md, about the Midnight Smoker and true of both archetypes:
//
//   "They get more dangerous on late shifts without any stat change. HP,
//   damage, cloud radius, and spawn counts are identical on shift 5 and shift 1
//   — but with the lampposts dead, their grey silhouettes read later against
//   unlit facades and the cloud's true extent is much harder to judge. Their
//   pre-warm ring stays self-lit and unmistakable at every tier: the player
//   always knows the attack is coming, and increasingly cannot tell how far it
//   reaches."
//
// So: NOTHING here reads the tier or shifts_remaining to change behaviour. The
// `tier` argument to render() reaches exactly three expressions — the three
// sm_assets::ref() calls that pick a palette — and nothing else. The encounter
// schedule reads shifts_remaining, but only to select an authored ROW of spawn
// POINTS; every row holds the same kinds, in the same order, in the same count.
//
// ── AND THE ONE IT INHERITS
// ───────────────────────────────────────────────────
//
// The design leans on audio for enemy telegraphs, and sm_enemy_audio() at the
// foot of the sm_enemies section now supplies that half. It is an ADDITION and
// never a substitution: sm_sound is optional, a missing bank is counted rather
// than fatal, and the game must stay finishable in silence. So the two below
// still carry every cue on their own, and are load-bearing rather than
// decorative:
//
//   * THE HURT FLASH is the player's only confirmation that a hit landed.
//     SAMPLE_TEXTURE *replaces* vertex colour on this console (the note at the
//     top of sm_gfx.hpp), so a textured billboard cannot be tinted. The flash
//     is drawn instead as an UNTEXTURED bright card immediately BEHIND the
//     enemy and a little larger than it: the enemy's cut-out silhouette punches
//     black against a lit rectangle. One primitive, no palette involved, and it
//     gets MORE visible as the tiers darken — the card is flat vertex colour,
//     so only the figure in front of it dims.
//
//   * THE PRE-WARM RING is the UNTIERED smoke atlas plus a bright untextured
//     line ring at exactly SM_CLOUD_RADIUS. It cannot lie about where the cloud
//     will be, because the ring IS the cloud: the sm_cloud is filed the instant
//     the windup starts, with a negative age, and the ring is what a cloud
//     looks like before it ignites. The difficulty comes from the dark ground
//     around the ring, never from the ring.
#include "sm_enemy.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "sm_assets.hpp"
#include "sm_atlas.hpp"
#include "sm_combat.hpp"
#include "sm_scene.hpp"

namespace solidmaid {
namespace {

using rv_pdklib::rv_vec3;

constexpr float SM_PI = 3.14159265358979323846f;

// ── presentation-only numbers
// ─────────────────────────────────────────────────
//
// Nothing here is a tuning value the design argues about — every one of those
// lives in sm_common.hpp and is used from there. These are the sizes and beats
// of the DRAWING, and each is either a body measurement or derived from a
// constant that already exists.

// A kipuchka is a small pest; a smoker is a standing figure a shade taller than
// the eye. Widths come from the collision radii, so what the player swings at
// is what the player sees.
constexpr float SM_KIPUCHKA_BODY_HEIGHT = 1.15f;
constexpr float SM_SMOKER_BODY_HEIGHT = 1.80f;

// Aim assist grabs the chest, never the feet (docs/gameplay.md §4): a cone that
// snaps to the ground reads as the assist fighting the player.
constexpr float SM_CHEST_FRACTION = 0.62f;

// How long the hit confirmation burns, and how long a body takes to fall. Both
// are short on purpose: a corpse must never become scenery, and a flash that
// outlasts the hitstop stops reading as an impact.
constexpr float SM_HURT_FLASH_TIME = 0.22f;
constexpr float SM_DEATH_COLLAPSE = 0.40f;

// The flash card: larger than the enemy so it frames the silhouette, and pushed
// a few centimetres further from the eye so the ordering table puts it behind.
constexpr float SM_FLASH_CARD_SCALE = 1.32f;
constexpr float SM_FLASH_CARD_BEHIND = 0.07f;

// The strike pose is carved OUT of the recovery rather than added to it, so the
// kipuchka's total commitment stays exactly WINDUP + RECOVER + COOLDOWN.
constexpr float SM_STRIKE_POSE_FRACTION = 0.25f;

// The pre-warm ring, in segments, plus four short uprights so it still reads
// when the player stands almost in its plane and the ring goes edge-on.
constexpr int SM_RING_SEGMENTS = 24;
constexpr float SM_RING_STAKE_HEIGHT = 0.55f;

// The cloud is five cards: one core and four on the rim. Every card is placed
// so its OUTER edge lands on the current radius and never past it — the ring
// promised that extent, and the smoke has to honour it.
constexpr int SM_CLOUD_PUFFS = 5;
constexpr float SM_PUFF_ANGLE[SM_CLOUD_PUFFS] = {0.0f, 0.55f, 2.05f, 3.55f,
                                                 5.05f};
constexpr float SM_PUFF_OUT[SM_CLOUD_PUFFS] = {0.00f, 0.58f, 0.54f, 0.60f,
                                               0.56f};
constexpr float SM_PUFF_SIZE[SM_CLOUD_PUFFS] = {0.55f, 0.40f, 0.44f, 0.38f,
                                                0.42f};
constexpr float SM_PUFF_LIFT[SM_CLOUD_PUFFS] = {0.05f, 0.00f, 0.12f, 0.02f,
                                                0.08f};

// The kipuchka's weave. The frequency is derived so one lateral excursion is
// about one body across — fast enough to spoil a lead on a thrown brick, slow
// enough that the silhouette never smears.
constexpr float SM_KIPUCHKA_WEAVE_RATE =
    SM_KIPUCHKA_JITTER / (2.0f * SM_KIPUCHKA_RADIUS);

// One kipuchka footfall per 0.8 m of ground covered — at SM_KIPUCHKA_SPEED,
// a step every quarter second, which is the gait of something small moving
// fast. Held as a DISTANCE and converted into weave phase here, so the pace
// stays honest about the ground covered rather than about the clock.
constexpr float SM_KIPUCHKA_STEP_DISTANCE = 0.80f;
constexpr float SM_KIPUCHKA_STEP_PHASE =
    SM_KIPUCHKA_WEAVE_RATE * SM_KIPUCHKA_STEP_DISTANCE / SM_KIPUCHKA_SPEED;

// A footfall is floor noise, not an event. Well under the telegraphs, which it
// must never cover.
constexpr float SM_KIPUCHKA_STEP_GAIN = 0.55f;

// The smoker's orbit reverses about twice per lap of its standoff circle, and
// the cloud rolls at the same rate so it never looks frozen.
constexpr float SM_SMOKER_ORBIT_RATE = SM_SMOKER_SPEED / SM_SMOKER_STANDOFF;
constexpr float SM_CLOUD_ROLL_RATE = SM_SMOKER_SPEED / SM_CLOUD_RADIUS;

// Self-lit colours. Amber, so the ring is never mistaken for the pale
// green-cyan of a mercury lamp that may or may not still be burning.
constexpr rv_pdk::rv_color SM_RING_COLD{150, 108, 42};
constexpr rv_pdk::rv_color SM_RING_HOT{255, 226, 150};
constexpr rv_pdk::rv_color SM_FLASH_COLD{96, 92, 88};
constexpr rv_pdk::rv_color SM_FLASH_HOT{255, 250, 236};
constexpr rv_pdk::rv_color SM_WHITE{255, 255, 255};

// ── small maths
// ───────────────────────────────────────────────────────────────

float clampf(float value, float low, float high) {
  return value < low ? low : (value > high ? high : value);
}

rv_vec3 flatten(rv_vec3 v) { return rv_vec3{v.x, 0.0f, v.z}; }

float xz_length(rv_vec3 v) { return std::sqrt(v.x * v.x + v.z * v.z); }

float xz_distance(rv_vec3 a, rv_vec3 b) { return xz_length(a - b); }

// Horizontal unit vector, or zero when the two points sit on top of each other.
rv_vec3 xz_direction(rv_vec3 from, rv_vec3 to) {
  const rv_vec3 d = flatten(to - from);
  const float len = xz_length(d);
  if (len <= 1e-5f)
    return rv_vec3{0.0f, 0.0f, 0.0f};
  return d * (1.0f / len);
}

// The perpendicular in the xz plane.
rv_vec3 xz_perpendicular(rv_vec3 d) { return rv_vec3{d.z, 0.0f, -d.x}; }

uint8_t mix_channel(uint8_t a, uint8_t b, float t) {
  const float value = static_cast<float>(a) +
                      (static_cast<float>(b) - static_cast<float>(a)) * t;
  return static_cast<uint8_t>(clampf(value + 0.5f, 0.0f, 255.0f));
}

rv_pdk::rv_color mix_colour(rv_pdk::rv_color a, rv_pdk::rv_color b, float t) {
  return rv_pdk::rv_color{mix_channel(a.r, b.r, t), mix_channel(a.g, b.g, t),
                          mix_channel(a.b, b.b, t)};
}

// ── where a sound is, to the ear
// ──────────────────────────────────────────────
//
// The chip gives a voice an L/R volume and nothing else: no distance model, no
// falloff, no listener (sm_sound.hpp). So the two numbers a cue with a world
// position needs are worked out here. SM_AUDIO_RANGE is a shade past the
// smoker's aggro radius, so everything that can act on the player can be heard
// acting; SM_AUDIO_FLOOR keeps the far end of that audible rather than fading
// it to a rumour, because a telegraph nobody can hear is not a telegraph.
//
// A twin of the one in sm_combat.cpp, which is the same arrangement clampf has
// in all three of these files: a few lines of local maths, not a shared
// dependency.
constexpr float SM_AUDIO_RANGE = 22.0f;
constexpr float SM_AUDIO_FLOOR = 0.15f;

void cue_at(sm_feel &feel, sm_sfx id, rv_vec3 at, rv_vec3 listener,
            rv_vec3 forward, float gain) {
  const rv_vec3 to = at - listener;
  const float distance = rv_pdklib::rv_length(to);
  const float level =
      clampf(1.0f - distance / SM_AUDIO_RANGE, SM_AUDIO_FLOOR, 1.0f);

  // The listener's right hand: sm_right(yaw) read straight off the forward
  // vector instead of recovered from it through an angle, flattened so a
  // player looking at their boots still hears which side a thing is on.
  const rv_vec3 ahead = xz_direction(rv_vec3{0.0f, 0.0f, 0.0f}, forward);
  float pan = 0.0f;
  if (xz_length(ahead) > 0.5f) {
    // Divided by the distance, but never by less than a metre: inside arm's
    // reach a direction stops meaning much, and hard-panning it would only
    // make something at the player's feet jump between the ears.
    pan = rv_pdklib::rv_dot(to, xz_perpendicular(ahead)) /
          (distance > 1.0f ? distance : 1.0f);
  }
  feel.cue(id, gain * level, clampf(pan, -1.0f, 1.0f));
}

// ── per-archetype body data
// ───────────────────────────────────────────────────

float body_height(sm_enemy_kind kind) {
  return kind == SM_ENEMY_SMOKER ? SM_SMOKER_BODY_HEIGHT
                                 : SM_KIPUCHKA_BODY_HEIGHT;
}

float body_radius(sm_enemy_kind kind) {
  return kind == SM_ENEMY_SMOKER ? SM_SMOKER_RADIUS : SM_KIPUCHKA_RADIUS;
}

int body_hp(sm_enemy_kind kind) {
  return kind == SM_ENEMY_SMOKER ? SM_SMOKER_HP : SM_KIPUCHKA_HP;
}

float weave_rate(sm_enemy_kind kind) {
  return kind == SM_ENEMY_SMOKER ? SM_SMOKER_ORBIT_RATE
                                 : SM_KIPUCHKA_WEAVE_RATE;
}

rv_vec3 chest_of(const sm_enemy &e) {
  return e.position +
         rv_vec3{0.0f, body_height(e.kind) * SM_CHEST_FRACTION, 0.0f};
}

// A dying enemy is out of the encounter the frame it starts to fall: it is not
// a target for aim assist, not a body for a weapon to hit, and not counted
// against the spawn cap.
bool is_target(const sm_enemy &e) {
  return e.alive && e.stage != SM_STAGE_DYING;
}

// ── movement
// ──────────────────────────────────────────────────────────────────

// Every enemy resolves its frame through sm_scene_slide, even when it means to
// stand still. That is deliberate: knockback is applied as a bare displacement
// (sm_enemy has no velocity field, and damage_*() has no scene to consult), so
// this call is also what repairs a shove that pushed a body into a wall.
// Returns how far it actually got, so a blocked strafe can turn around.
float step_move(const sm_scene &scene, sm_enemy &e, rv_vec3 velocity,
                float dt) {
  rv_vec3 target = e.position + velocity * dt;
  target.y = e.position.y;

  const rv_vec3 before = e.position;
  e.position = sm_scene_slide(scene, before, target, body_radius(e.kind));
  e.position.y = before.y; // collision is 2D; the floor is flat everywhere
  return xz_distance(e.position, before);
}

bool can_see(const sm_scene &scene, const sm_enemy &e,
             rv_vec3 player_position) {
  const rv_vec3 head = player_position + rv_vec3{0.0f, SM_EYE_HEIGHT, 0.0f};
  return sm_scene_clear_line(scene, chest_of(e), head);
}

void face(sm_enemy &e, rv_vec3 towards) {
  const rv_vec3 d = xz_direction(e.position, towards);
  if (xz_length(d) > 0.5f)
    e.yaw = std::atan2(d.x, d.z);
}

// Death: the collapse begins, and any exhale this smoker had not finished
// leaves with it. The pre-warm cloud carries its owner's slot in `tick` (see
// update_smoker), which is what makes that cancel possible without a new field.
void begin_dying(sm_enemy &e, std::vector<sm_cloud> &clouds,
                 std::size_t index) {
  e.stage = SM_STAGE_DYING;
  e.stage_time = 0.0f;
  e.death_time = 0.0f;
  e.hp = 0;

  const float owner = static_cast<float>(index + 1);
  for (std::size_t i = 0; i < clouds.size(); ++i) {
    sm_cloud &cloud = clouds[i];
    if (!cloud.alive || cloud.age >= 0.0f)
      continue;
    if (std::fabs(cloud.tick - owner) < 0.5f)
      cloud.alive = false;
  }
}

// ── the kipuchka
// ──────────────────────────────────────────────────────────────
//
//   APPROACH ──in range, off cooldown, past grace──▶ WINDUP (0.34 s, STOPPED)
//        ▲                                              │
//        │                                              ▼
//        └── + SM_KIPUCHKA_COOLDOWN ── RECOVER ◀── STRIKE (the one damage
//        frame)
//
// The windup is the whole contract with the player: the pest stops dead and
// changes pose, and the strike only lands if the player is still inside
// SM_KIPUCHKA_ATTACK_RANGE when the strike frame arrives. Committing to a swing
// the player can walk out of is the point of the archetype.
void update_kipuchka(sm_enemy &e, float dt, const sm_scene &scene,
                     rv_vec3 player_position,
                     std::vector<sm_enemy_damage> &out_damage) {
  const float distance = xz_distance(e.position, player_position);

  switch (e.stage) {
  case SM_STAGE_APPROACH: {
    // Aggro needs a line of sight to START a chase; without one the pest
    // holds where it is rather than pathing through a wall it cannot see
    // past. Nothing in the game has pathfinding, and nothing needs it:
    // the street is one lane.
    const bool aggroed =
        distance <= SM_KIPUCHKA_AGGRO && can_see(scene, e, player_position);
    rv_vec3 velocity{0.0f, 0.0f, 0.0f};

    if (aggroed) {
      face(e, player_position);
      const rv_vec3 towards = xz_direction(e.position, player_position);
      const rv_vec3 lateral = xz_perpendicular(towards);
      // Straight in, plus a lateral weave: the silhouette never holds
      // still long enough to be a comfortable lead for a thrown brick.
      velocity = towards * SM_KIPUCHKA_SPEED +
                 lateral * (SM_KIPUCHKA_JITTER * std::sin(e.jitter_phase));
    }

    step_move(scene, e, velocity, dt);

    const bool committed =
        aggroed && e.grace <= 0.0f && e.cooldown <= 0.0f &&
        xz_distance(e.position, player_position) <= SM_KIPUCHKA_ATTACK_RANGE;
    if (committed) {
      e.stage = SM_STAGE_WINDUP;
      e.stage_time = 0.0f;
      face(e, player_position);
    }
    break;
  }

  case SM_STAGE_WINDUP: {
    // Stopped. Not slowed — stopped, with the windup pose up, because the
    // telegraph has to survive a 320x240 frame at the darkest tier with
    // no sound to help it.
    step_move(scene, e, rv_vec3{0.0f, 0.0f, 0.0f}, dt);
    if (e.stage_time >= SM_KIPUCHKA_WINDUP) {
      e.stage = SM_STAGE_STRIKE;
      e.stage_time = 0.0f;
      // The strike frame. Range is re-tested here and nowhere else, so
      // backing off during the windup genuinely beats the swing.
      if (xz_distance(e.position, player_position) <=
          SM_KIPUCHKA_ATTACK_RANGE) {
        sm_enemy_damage hit{};
        hit.amount = SM_KIPUCHKA_DAMAGE;
        hit.from = e.position;
        hit.from_cloud = false; // a direct hit; i-frames apply
        out_damage.push_back(hit);
      }
    }
    break;
  }

  case SM_STAGE_STRIKE: {
    step_move(scene, e, rv_vec3{0.0f, 0.0f, 0.0f}, dt);
    if (e.stage_time >= SM_KIPUCHKA_RECOVER * SM_STRIKE_POSE_FRACTION) {
      e.stage = SM_STAGE_RECOVER;
      e.stage_time = 0.0f;
    }
    break;
  }

  case SM_STAGE_RECOVER: {
    step_move(scene, e, rv_vec3{0.0f, 0.0f, 0.0f}, dt);
    if (e.stage_time >=
        SM_KIPUCHKA_RECOVER * (1.0f - SM_STRIKE_POSE_FRACTION)) {
      e.stage = SM_STAGE_APPROACH;
      e.stage_time = 0.0f;
      e.cooldown = SM_KIPUCHKA_COOLDOWN;
    }
    break;
  }

  case SM_STAGE_DYING:
  default:
    break;
  }
}

// ── the midnight smoker
// ───────────────────────────────────────────────────────
//
//   APPROACH (holds SM_SMOKER_STANDOFF, strafes)
//        │  line of sight, off cooldown, past grace
//        ▼
//   WINDUP (0.85 s, stopped; the pre-warm ring is already on the ground at the
//           cloud's exact future centre and the exact SM_CLOUD_RADIUS)
//        │
//        ▼
//   STRIKE (the exhale, held for SM_CLOUD_GROW_TIME while the cloud opens)
//        │
//        └──▶ APPROACH + SM_SMOKER_COOLDOWN
//
// It is pressure, not a chaser: it never closes past its standoff, and it keeps
// strafing all through its cooldown so the denied ground keeps moving.
// Where to centre an exhale committed this frame: ahead of the player along
// their own drift, but never through a wall. The retreat is coarse on purpose —
// three tries and then the feet — because the only thing that must never happen
// is a cloud opening on the far side of a facade, where the ring is invisible
// and the damage is not.
rv_vec3 exhale_centre(const sm_scene &scene, rv_vec3 player_position,
                      rv_vec3 drift) {
  const float speed = xz_length(drift);
  if (speed < 0.25f)
    return player_position; // standing still: the feet ARE where they'll be
  const rv_vec3 heading = drift * (1.0f / speed);
  float lead = speed * SM_CLOUD_LEAD_TIME;
  if (lead > SM_CLOUD_LEAD_MAX)
    lead = SM_CLOUD_LEAD_MAX;

  const rv_vec3 head = player_position + rv_vec3{0.0f, SM_EYE_HEIGHT, 0.0f};
  for (int attempt = 0; attempt < 3; ++attempt) {
    const rv_vec3 point = player_position + heading * lead;
    if (sm_scene_clear_line(scene, head,
                            point + rv_vec3{0.0f, SM_EYE_HEIGHT, 0.0f}))
      return point;
    lead *= 0.5f;
  }
  return player_position;
}

void update_smoker(sm_enemy &e, std::size_t index, float dt,
                   const sm_scene &scene, rv_vec3 player_position,
                   rv_vec3 player_drift, std::vector<sm_cloud> &clouds) {
  const float distance = xz_distance(e.position, player_position);
  const bool aggroed = distance <= SM_SMOKER_AGGRO;
  const bool sighted = aggroed && can_see(scene, e, player_position);

  switch (e.stage) {
  case SM_STAGE_APPROACH: {
    rv_vec3 velocity{0.0f, 0.0f, 0.0f};
    bool strafing = false;

    if (aggroed) {
      face(e, player_position);
      const rv_vec3 towards = xz_direction(e.position, player_position);

      if (!sighted) {
        // Area denial needs a line to deny. Without one it closes,
        // and only until it has one.
        velocity = towards * SM_SMOKER_SPEED;
      } else if (distance > SM_SMOKER_STANDOFF + SM_SMOKER_RADIUS) {
        velocity = towards * SM_SMOKER_SPEED;
      } else if (distance < SM_SMOKER_STANDOFF - SM_SMOKER_RADIUS) {
        velocity = towards * -SM_SMOKER_SPEED; // backs off; never brawls
      } else {
        const float sign = std::sin(e.jitter_phase) >= 0.0f ? 1.0f : -1.0f;
        velocity = xz_perpendicular(towards) * (SM_SMOKER_SPEED * sign);
        strafing = true;
      }
    }

    const float intended = xz_length(velocity) * dt;
    const float moved = step_move(scene, e, velocity, dt);
    // A strafe into a wall turns around instead of grinding along it.
    if (strafing && intended > 1e-4f && moved < intended * 0.5f)
      e.jitter_phase += SM_PI;

    // It exhales when the player is within a cloud's reach of the
    // distance it means to hold. Committing from across the street would
    // burn a whole 0.85 s + 0.7 s + 3.4 s cycle on ground the player was
    // never going to be standing on.
    const bool in_reach =
        distance <= SM_SMOKER_STANDOFF + SM_CLOUD_RADIUS + SM_CLOUD_COMMIT_BONUS;
    if (sighted && in_reach && e.grace <= 0.0f && e.cooldown <= 0.0f) {
      e.stage = SM_STAGE_WINDUP;
      e.stage_time = 0.0f;
      face(e, player_position);

      // The cloud is filed NOW, with a negative age. Until that age
      // reaches zero it is the pre-warm ring and nothing else: it does
      // not grow and it does not tick. Its centre is where the player is
      // HEADED at the moment of commitment, so the ring opens in front of
      // them where they can see it — and the ring is still an honest
      // promise, because the lead is applied here, once, and the drawing
      // reads the same centre the damage does.
      sm_cloud cloud{};
      cloud.centre = exhale_centre(scene, player_position, player_drift);
      // One dt of head start, because the cloud loop runs after this
      // one and would otherwise ignite a frame before the exhale pose.
      cloud.age = -SM_SMOKER_WINDUP - dt;
      // `tick` is unused until ignition, so it carries the owner's slot
      // (+1). That is what lets a smoker killed mid-windup take its
      // unfinished exhale with it, with no extra field in the header.
      cloud.tick = static_cast<float>(index + 1);
      cloud.alive = true;

      bool filed = false;
      for (std::size_t i = 0; i < clouds.size(); ++i) {
        if (clouds[i].alive)
          continue;
        clouds[i] = cloud;
        filed = true;
        break;
      }
      if (!filed)
        clouds.push_back(cloud);
    }
    break;
  }

  case SM_STAGE_WINDUP: {
    step_move(scene, e, rv_vec3{0.0f, 0.0f, 0.0f}, dt);
    if (e.stage_time >= SM_SMOKER_WINDUP) {
      e.stage = SM_STAGE_STRIKE;
      e.stage_time = 0.0f;
    }
    break;
  }

  case SM_STAGE_STRIKE: {
    // The exhale, held for as long as the cloud takes to open, so the
    // pose and the effect read as one event.
    step_move(scene, e, rv_vec3{0.0f, 0.0f, 0.0f}, dt);
    if (e.stage_time >= SM_CLOUD_GROW_TIME) {
      e.stage = SM_STAGE_APPROACH;
      e.stage_time = 0.0f;
      e.cooldown = SM_SMOKER_COOLDOWN;
    }
    break;
  }

  case SM_STAGE_RECOVER:
  case SM_STAGE_DYING:
  default:
    // The smoker has no recovery of its own — its cooldown is its
    // recovery, and it strafes through it.
    e.stage = SM_STAGE_APPROACH;
    break;
  }
}

// ── the cloud
// ─────────────────────────────────────────────────────────────────

// What the cloud denies right now. A negative age is the pre-warm, where the
// ring already shows the FULL radius: the extent is told to the player before a
// single texel of smoke exists.
float cloud_radius_at(const sm_cloud &cloud) {
  if (cloud.age <= 0.0f)
    return SM_CLOUD_RADIUS;
  return SM_CLOUD_RADIUS * clampf(cloud.age / SM_CLOUD_GROW_TIME, 0.0f, 1.0f);
}

// ── encounter authoring
// ───────────────────────────────────────────────────────
//
// docs/mechanics.md: "Placement is keyed by chunk index and shifts_remaining,
// so encounters are authored per tier, not scaled." The row below is chosen by
// shifts_remaining and chooses spawn POINTS ONLY. Every row is the same length,
// holds the same kinds in the same order, and therefore spawns the same five
// enemies. Nothing about an enemy is read from the row.

constexpr int SM_SHIFT_ROWS = 5;

// Three encounters across a 50-70 s commute: one in the courtyard, one at the
// seam between the chunks, one on the approach to the gate.
constexpr int SM_STREET_SLOTS = 5;
constexpr float SM_STREET_AT[SM_STREET_SLOTS] = {8.0f, 24.0f, 25.5f, 42.0f,
                                                 43.5f};
constexpr sm_enemy_kind SM_STREET_KIND[SM_STREET_SLOTS] = {
    SM_ENEMY_KIPUCHKA, SM_ENEMY_SMOKER, SM_ENEMY_KIPUCHKA, SM_ENEMY_SMOKER,
    SM_ENEMY_KIPUCHKA};
constexpr int SM_STREET_POINT[SM_SHIFT_ROWS][SM_STREET_SLOTS] = {
    {0, 2, 3, 5, 6}, // shift 5 — the honest introduction, all of it in the open
    {1, 3, 4, 6, 7}, // shift 4 — the same beats, one lamp further along
    {0, 3, 5, 7, 2}, // shift 3
    {2, 4, 6, 1, 7}, // shift 2
    {1, 5, 3, 6, 0}, // shift 1 — the darkest street, the same five enemies
};
// A little drift so the commute does not metronome. Same count, same kinds.
constexpr float SM_STREET_NUDGE[SM_SHIFT_ROWS] = {0.0f, 1.5f, -1.0f, 2.0f,
                                                  -1.5f};

// The hall: a light presence to make the room feel occupied while the player
// reads it from the doorway.
constexpr int SM_FACTORY_SLOTS = 2;
constexpr float SM_FACTORY_AT[SM_FACTORY_SLOTS] = {2.5f, 7.0f};
constexpr sm_enemy_kind SM_FACTORY_KIND[SM_FACTORY_SLOTS] = {SM_ENEMY_KIPUCHKA,
                                                             SM_ENEMY_SMOKER};
constexpr int SM_FACTORY_POINT[SM_SHIFT_ROWS][SM_FACTORY_SLOTS] = {
    {0, 2}, {1, 3}, {2, 0}, {3, 1}, {0, 3},
};

// The single ordinary escalation, at assembly step 2. No boss, no phase change,
// no gimmick — three more of exactly what the player has been fighting all
// game.
constexpr int SM_WAVE_SLOTS = 3;
constexpr float SM_WAVE_STAGGER[SM_WAVE_SLOTS] = {0.0f, 0.9f, 1.8f};
constexpr sm_enemy_kind SM_WAVE_KIND[SM_WAVE_SLOTS] = {
    SM_ENEMY_KIPUCHKA, SM_ENEMY_KIPUCHKA, SM_ENEMY_SMOKER};
constexpr int SM_WAVE_POINT[SM_SHIFT_ROWS][SM_WAVE_SLOTS] = {
    {1, 3, 4}, {0, 2, 5}, {1, 4, 3}, {0, 5, 2}, {2, 3, 5},
};

// shifts_remaining 5..1 -> row 0..4. This selects WHERE, never WHAT.
int shift_row(int shifts_remaining) {
  int row = SM_SHIFTS_START - shifts_remaining;
  if (row < 0)
    row = 0;
  if (row > SM_SHIFT_ROWS - 1)
    row = SM_SHIFT_ROWS - 1;
  return row;
}

// Spawn points are authored by sm_scene; the schedule wraps into however many
// that area happens to have, so a level edit can never index off the end.
rv_vec3 spawn_point(const sm_scene &scene, int index) {
  const std::size_t count = scene.enemy_spawns.size();
  if (count == 0)
    return rv_vec3{0.0f, 0.0f, 0.0f};
  const std::size_t wrapped = static_cast<std::size_t>(index) % count;
  return scene.enemy_spawns[wrapped];
}

} // namespace

// ── sm_enemies
// ────────────────────────────────────────────────────────────────

void sm_enemies::reset() {
  player_previous_ = rv_vec3{0.0f, 0.0f, 0.0f};
  player_drift_ = rv_vec3{0.0f, 0.0f, 0.0f};
  player_tracked_ = false;
  enemies_.clear();
  clouds_.clear();
}

int sm_enemies::active_count() const {
  int count = 0;
  for (std::size_t i = 0; i < enemies_.size(); ++i) {
    if (is_target(enemies_[i]))
      ++count;
  }
  return count;
}

bool sm_enemies::spawn(sm_enemy_kind kind, rv_vec3 position, rv_vec3 player) {
  // docs/mechanics.md, spawner discipline. Both refusals are the caller's cue
  // to WAIT — never to go looking for somewhere else to put this one.
  if (active_count() >= SM_ENEMY_CAP)
    return false;
  if (xz_distance(position, player) < SM_SPAWN_MIN_DISTANCE)
    return false;

  std::size_t slot = enemies_.size();
  for (std::size_t i = 0; i < enemies_.size(); ++i) {
    if (!enemies_[i].alive) {
      slot = i;
      break;
    }
  }
  if (slot == enemies_.size())
    enemies_.push_back(sm_enemy{});

  sm_enemy &e = enemies_[slot];
  e = sm_enemy{};
  e.kind = kind;
  e.stage = SM_STAGE_APPROACH;
  e.position = position;
  e.hp = body_hp(kind);
  // Harmless for SM_SPAWN_GRACE: the grace is what stops an enemy
  // materialising mid-swing next to the player.
  e.grace = SM_SPAWN_GRACE;
  // A deterministic phase per slot, so a pair spawned together does not weave
  // in lockstep and read as one animated object.
  e.jitter_phase = static_cast<float>((slot * 2654435761u) % 6283u) * 0.001f;
  e.alive = true;
  face(e, player);
  return true;
}

void sm_enemies::update(float dt, const sm_scene &scene,
                        rv_vec3 player_position,
                        std::vector<sm_enemy_damage> &out_damage) {
  // "What the enemies did to the player this frame" — so the frame starts
  // empty rather than accumulating across frames in the caller's vector.
  out_damage.clear();

  // The player's drift, measured. Smoothed hard because the smoker commits on
  // ONE frame and a raw per-frame difference is noisy enough — a wall slide, a
  // strafe reversal — to aim an exhale at ground the player never meant to go
  // to. An area change teleports the player, so the first frame after a reset
  // seeds instead of measuring, and anything faster than a sprint is discarded
  // as exactly that teleport.
  if (dt > 1e-5f) {
    const rv_vec3 delta = player_position - player_previous_;
    rv_vec3 velocity{delta.x / dt, 0.0f, delta.z / dt};
    if (!player_tracked_ || xz_length(velocity) > SM_PLAYER_WALK_SPEED * 3.0f)
      velocity = rv_vec3{0.0f, 0.0f, 0.0f};
    const float blend = 0.25f;
    player_drift_ = player_drift_ * (1.0f - blend) + velocity * blend;
    player_previous_ = player_position;
    player_tracked_ = true;
  }

  for (std::size_t i = 0; i < enemies_.size(); ++i) {
    sm_enemy &e = enemies_[i];
    if (!e.alive)
      continue;

    if (e.hurt_flash > 0.0f) {
      e.hurt_flash -= dt;
      if (e.hurt_flash < 0.0f)
        e.hurt_flash = 0.0f;
    }

    if (e.stage == SM_STAGE_DYING) {
      // The collapse, and then gone: a corpse is not a collider and not a
      // target for one frame longer than the fall takes.
      e.death_time += dt;
      if (e.death_time >= SM_DEATH_COLLAPSE)
        e.alive = false;
      continue;
    }

    if (e.grace > 0.0f)
      e.grace -= dt;
    if (e.cooldown > 0.0f)
      e.cooldown -= dt;
    e.stage_time += dt;

    e.jitter_phase += weave_rate(e.kind) * dt;
    if (e.jitter_phase > 2.0f * SM_PI)
      e.jitter_phase -= 2.0f * SM_PI;

    if (e.kind == SM_ENEMY_SMOKER) {
      update_smoker(e, i, dt, scene, player_position, player_drift_, clouds_);
    } else {
      update_kipuchka(e, dt, scene, player_position, out_damage);
    }
  }

  for (std::size_t i = 0; i < clouds_.size(); ++i) {
    sm_cloud &cloud = clouds_[i];
    if (!cloud.alive)
      continue;

    const bool was_prewarm = cloud.age < 0.0f;
    cloud.age += dt;

    if (cloud.age < 0.0f)
      continue; // still the ring: no growth, no damage
    if (was_prewarm)
      cloud.tick = 0.0f; // ignition: `tick` stops being the owner

    if (cloud.age >= SM_CLOUD_LIFETIME) {
      cloud.alive = false;
      continue;
    }

    // The cloud keeps its own rhythm whether or not anyone is standing in
    // it: stepping in does not reset the clock, and stepping out does not
    // bank a free tick.
    cloud.tick += dt;
    if (cloud.tick < SM_CLOUD_TICK_PERIOD)
      continue;
    cloud.tick -= SM_CLOUD_TICK_PERIOD;

    // The drawn edge is the truth, and the player's centre is what the
    // crosshair sits over — so the test is against the radius exactly.
    if (xz_distance(player_position, cloud.centre) <= cloud_radius_at(cloud)) {
      sm_enemy_damage hit{};
      hit.amount = SM_CLOUD_TICK_DAMAGE;
      hit.from = cloud.centre;
      hit.from_cloud = true; // chip damage bypasses i-frames
      out_damage.push_back(hit);
    }
  }
}

// ── the enemies' voices
// ───────────────────────────────────────────────────────
//
// Every cue an enemy owes the player: the two telegraphs, the smoker's exhale,
// a body going down, and the kipuchka's approach. All of it once per frame,
// read off the state sm_enemies::update() has just left behind.
//
// WHY IT IS A FREE FUNCTION. update() is handed no sm_feel, and neither its
// signature nor sm_enemy.hpp is this change's to alter. sm_combat::update() is
// called on the next line of sm_game's frame, inside the same `step > 0`
// block, and already holds the listener, its facing, these enemies and the dt
// they were advanced by — so it pumps this, and nothing is heard late.
//
// WHY IT NEEDS NO STATE. A stage entered THIS frame is one whose stage_time is
// still exactly 0.0f: update() assigns that literal on every transition and
// only ever adds a strictly positive dt to it afterwards, so the window is
// exactly one frame wide and the comparison is against a value that was
// stored, never computed. A collapse is read off death_time the same way —
// update() skips stage_time for a dying body but always advances death_time,
// and this runs after the weapons have filed their kills, so the frame a body
// starts falling is the frame death_time is still zero.
void sm_enemy_audio(const sm_enemies &enemies, const sm_scene &scene,
                    rv_vec3 listener, rv_vec3 forward, float dt,
                    sm_feel &feel) {
  if (dt <= 0.0f)
    return;

  // One voice per distinct sample per frame. sm_sound.hpp: two copies of one
  // sample started on the same frame sum IN PHASE and "read as one loud hit
  // rather than as two" — so the second is a wasted voice and 6 dB the mix did
  // not ask for. Which is feel.impact()'s MAX rule, one channel across.
  bool spoken[SM_SFX_COUNT] = {};
  const auto say = [&](sm_sfx id, rv_vec3 at, float gain) {
    if (spoken[id])
      return;
    spoken[id] = true;
    cue_at(feel, id, at, listener, forward, gain);
  };

  const std::vector<sm_enemy> &all = enemies.enemies();
  for (std::size_t i = 0; i < all.size(); ++i) {
    const sm_enemy &e = all[i];
    if (!e.alive)
      continue;

    // From the chest, like aim assist and for the same reason: a voice coming
    // out of the floor pans harder than the figure making it does.
    const rv_vec3 mouth = chest_of(e);

    if (e.stage == SM_STAGE_DYING) {
      if (e.death_time == 0.0f)
        say(SM_SFX_ENEMY_DOWN, mouth, 1.0f);
      continue;
    }

    if (e.stage_time == 0.0f) {
      if (e.stage == SM_STAGE_WINDUP) {
        // THE TELEGRAPH'S OTHER HALF, and the most load-bearing cue in the
        // file. The pose and the pre-warm ring already say an attack is
        // coming; this says WHERE FROM, which is the half a player cannot get
        // out of a 320x240 silhouette on an unlit street — and locating it is
        // exactly what docs/mechanics.md is asking the telegraph to buy.
        say(e.kind == SM_ENEMY_SMOKER ? SM_SFX_SMOKER_PREWARM
                                      : SM_SFX_KIPUCHKA_WINDUP,
            mouth, 1.0f);
      } else if (e.stage == SM_STAGE_STRIKE && e.kind == SM_ENEMY_SMOKER) {
        // The exhale. The cloud it opens is centred where the player STOOD, so
        // the smoker's own position is the honest place to hear it from.
        say(SM_SFX_SMOKER_ATTACK, mouth, 1.0f);
      }
      // A stage was entered this frame, so nothing was walked this frame.
      continue;
    }

    // ── a body on the move ────────────────────────────────────────────────
    if (e.stage != SM_STAGE_APPROACH)
      continue;
    const bool kipuchka = e.kind == SM_ENEMY_KIPUCHKA;

    // Only while it is genuinely closing. This is update_kipuchka's own aggro
    // test, made against the eye rather than the feet because the eye IS the
    // head can_see() builds: a pest holding still behind a wall is silent, and
    // one running at the player is not.
    if (xz_distance(e.position, listener) >
        (kipuchka ? SM_KIPUCHKA_AGGRO : SM_SMOKER_AGGRO))
      continue;
    if (!sm_scene_clear_line(scene, mouth, listener))
      continue;

    // THE PACE, WITHOUT A NEW FIELD. jitter_phase is the only per-enemy clock
    // sm_enemy has to spare, and it advances at a fixed rate, so a footfall is
    // the frame that crosses a multiple of SM_KIPUCHKA_STEP_PHASE. The phase
    // it crossed FROM is recomputed from this frame's dt rather than
    // remembered, which is what keeps this stateless; the wrap at 2π falls out
    // of it for free, since a negative previous phase floors to a different
    // mark than a small positive one. Each enemy is seeded with its own phase
    // in spawn(), so a pair never steps in lockstep — which also keeps them
    // off each other's frame and out of sm_sound.hpp's in-phase summing.
    //
    // The smoker walks the same way with its own numbers: it moves at less than
    // half the pest's speed, so the same ground takes it far longer and its
    // tread comes out slow and deliberate. Both share the one step sample the
    // bank has — a dedicated smoker footfall would read better and is on the
    // list of what is still unrecorded.
    const float rate = kipuchka ? SM_KIPUCHKA_WEAVE_RATE : SM_SMOKER_ORBIT_RATE;
    const float mark = rate * SM_KIPUCHKA_STEP_DISTANCE /
                       (kipuchka ? SM_KIPUCHKA_SPEED : SM_SMOKER_SPEED);
    const float previous = e.jitter_phase - rate * dt;
    if (std::floor(e.jitter_phase / mark) == std::floor(previous / mark))
      continue;

    // From the feet, this one.
    say(SM_SFX_KIPUCHKA_STEP, e.position,
        kipuchka ? SM_KIPUCHKA_STEP_GAIN : SM_KIPUCHKA_STEP_GAIN * 0.8f);
  }
}

int sm_enemies::damage_sphere(rv_vec3 centre, float radius, int damage,
                              rv_vec3 knockback, sm_feel &feel) {
  int hits = 0;
  for (std::size_t i = 0; i < enemies_.size(); ++i) {
    sm_enemy &e = enemies_[i];
    if (!is_target(e))
      continue;

    // Closest point on the enemy's standing segment, so a brick that arcs
    // into a head and one that clips a shin both count.
    const float y =
        clampf(centre.y, e.position.y, e.position.y + body_height(e.kind));
    const rv_vec3 to = centre - rv_vec3{e.position.x, y, e.position.z};
    const float reach = radius + body_radius(e.kind);
    if (rv_pdklib::rv_dot(to, to) > reach * reach)
      continue;

    rv_vec3 shove = flatten(knockback);
    if (xz_length(shove) <= 1e-4f)
      shove = xz_direction(centre, e.position) * SM_PIPE_KNOCKBACK;
    // sm_enemy has no velocity field, so the knockback is that impulse
    // integrated over the hitstop this same impact fires: one visible pop,
    // landing on the frame of the flash. Anything it pushes into geometry is
    // repaired by the next step_move(), which always slides.
    e.position = e.position + shove * SM_HITSTOP_HEAVY;

    e.hp -= damage;
    e.hurt_flash = SM_HURT_FLASH_TIME;
    ++hits;
    if (e.hp <= 0)
      begin_dying(e, clouds_, i);
  }

  // Hitstop and shake, on the frame of the flash. NO CUE: the brick is the
  // only thing that calls this, and sm_combat already fires its impact at the
  // point of contact, where it has the listener to pan against. A second
  // sample here would be the same event twice, summing in phase.
  if (hits > 0)
    feel.impact(SM_HITSTOP_HEAVY, SM_SHAKE_IMPACT);
  return hits;
}

int sm_enemies::damage_arc(rv_vec3 origin, rv_vec3 forward, float range,
                           float half_arc_degrees, int damage, sm_feel &feel) {
  const rv_vec3 axis =
      xz_direction(rv_vec3{0.0f, 0.0f, 0.0f}, flatten(forward));
  if (xz_length(axis) <= 0.5f)
    return 0;
  const float cos_limit =
      std::cos(clampf(half_arc_degrees, 0.0f, 180.0f) * SM_PI / 180.0f);

  int hits = 0;
  rv_vec3 contact{}; // the first body the arc caught, for the cue below
  for (std::size_t i = 0; i < enemies_.size(); ++i) {
    sm_enemy &e = enemies_[i];
    if (!is_target(e))
      continue;

    // The swing is a 2D arc: every floor in the game is flat and there is no
    // jump, so a height test would only ever refuse a hit the player earned.
    const rv_vec3 to = flatten(e.position - origin);
    const float distance = xz_length(to);
    if (distance > range + body_radius(e.kind))
      continue;
    if (distance > 1e-4f &&
        rv_pdklib::rv_dot(to * (1.0f / distance), axis) < cos_limit) {
      continue;
    }

    const rv_vec3 shove =
        (distance > 1e-4f ? to * (1.0f / distance) : axis) * SM_PIPE_KNOCKBACK;
    e.position = e.position + shove * SM_HITSTOP_LIGHT;

    e.hp -= damage;
    e.hurt_flash = SM_HURT_FLASH_TIME;
    if (hits == 0)
      contact = chest_of(e);
    ++hits;
    if (e.hp <= 0)
      begin_dying(e, clouds_, i);
  }

  if (hits > 0) {
    feel.impact(SM_HITSTOP_LIGHT, SM_SHAKE_IMPACT);
    // ONE cue, however many bodies the arc caught: this is the pipe landing,
    // not each enemy answering it. `origin` is the eye and `forward` is where
    // it is looking, so the hit is panned at the first body the swing found —
    // catching something off to one side is heard off to that side.
    cue_at(feel, SM_SFX_PIPE_HIT, contact, origin, forward, 1.0f);
  }
  return hits;
}

bool sm_enemies::nearest_target(rv_vec3 eye, rv_vec3 forward, float range,
                                float half_cone_degrees,
                                rv_vec3 &out_centre) const {
  const rv_vec3 axis = rv_pdklib::rv_normalize(forward);
  if (rv_pdklib::rv_length(axis) <= 0.5f)
    return false;
  const float cos_limit =
      std::cos(clampf(half_cone_degrees, 0.0f, 180.0f) * SM_PI / 180.0f);

  bool found = false;
  float best = 0.0f;
  for (std::size_t i = 0; i < enemies_.size(); ++i) {
    const sm_enemy &e = enemies_[i];
    if (!is_target(e))
      continue;

    const rv_vec3 centre = chest_of(e);
    const rv_vec3 to = centre - eye;
    const float distance = rv_pdklib::rv_length(to);
    if (distance > range || distance <= 1e-4f)
      continue;
    if (rv_pdklib::rv_dot(to * (1.0f / distance), axis) < cos_limit)
      continue;

    if (!found || distance < best) {
      best = distance;
      out_centre = centre;
      found = true;
    }
  }
  return found;
}

void sm_enemies::render(sm_gfx &gfx, const sm_assets &assets, int tier) const {
  // THE ONLY PLACE `tier` IS READ IN THIS FILE, and it does exactly one thing:
  // pick a palette. The smoke atlas is untiered in sm_assets.cpp, so ref()
  // discards the argument for it and the pre-warm ring keeps its authored
  // brightness on shift 1 and on the final lap.
  const sm_texref kipuchka_tex = assets.ref(SM_TEX_KIPUCHKA, tier);
  const sm_texref smoker_tex = assets.ref(SM_TEX_SMOKER, tier);
  const sm_texref smoke_tex = assets.ref(SM_TEX_SMOKE, tier);

  const rv_vec3 eye = gfx.view().eye;

  for (std::size_t i = 0; i < enemies_.size(); ++i) {
    const sm_enemy &e = enemies_[i];
    if (!e.alive)
      continue;

    // The pose IS the telegraph: at 320x240 in an unlit street the player
    // reads a shape changing, not a colour.
    sm_uvrect uv = SM_UV_ACTOR_IDLE;
    switch (e.stage) {
    case SM_STAGE_WINDUP:
      uv = SM_UV_ACTOR_WINDUP;
      break;
    case SM_STAGE_STRIKE:
      uv = SM_UV_ACTOR_STRIKE;
      break;
    case SM_STAGE_DYING:
      uv = SM_UV_ACTOR_DOWN;
      break;
    case SM_STAGE_APPROACH:
    case SM_STAGE_RECOVER:
    default:
      uv = SM_UV_ACTOR_IDLE;
      break;
    }

    float half_width = body_radius(e.kind);
    float half_height = body_height(e.kind) * 0.5f;
    float base = e.position.y;

    if (e.stage == SM_STAGE_DYING) {
      // The collapse: the card loses most of its height and sinks, so a
      // dead enemy stops occupying the space a live one did.
      const float t = clampf(e.death_time / SM_DEATH_COLLAPSE, 0.0f, 1.0f);
      half_height *= 1.0f - 0.72f * t;
      half_width *= 1.0f + 0.25f * t;
      base -= 0.10f * t;
    }

    const rv_vec3 centre{e.position.x, base + half_height, e.position.z};

    if (e.hurt_flash > 0.0f) {
      // THE HIT CONFIRMATION. Untextured, so `tint` is honoured (a
      // textured draw would have its vertex colour replaced — see
      // sm_gfx.hpp) and so the palette ramp cannot dim it. Filed BEHIND the
      // figure and a third again as large: the cut-out silhouette punches
      // black out of a lit card, which is the loudest thing this
      // rasterizer can say without a sound to say it with.
      const float glow = clampf(e.hurt_flash / SM_HURT_FLASH_TIME, 0.0f, 1.0f);
      const rv_vec3 away = xz_direction(eye, centre);
      const rv_vec3 card = centre + away * SM_FLASH_CARD_BEHIND;
      gfx.billboard(card, half_width * SM_FLASH_CARD_SCALE,
                    half_height * SM_FLASH_CARD_SCALE, sm_texref{},
                    SM_UV_ACTOR_IDLE,
                    mix_colour(SM_FLASH_COLD, SM_FLASH_HOT, glow));
    }

    gfx.billboard(centre, half_width, half_height,
                  e.kind == SM_ENEMY_SMOKER ? smoker_tex : kipuchka_tex, uv,
                  SM_WHITE);
  }

  for (std::size_t i = 0; i < clouds_.size(); ++i) {
    const sm_cloud &cloud = clouds_[i];
    if (!cloud.alive)
      continue;

    if (cloud.age < 0.0f) {
      // ── the pre-warm ring ─────────────────────────────────────────────
      // Drawn at the cloud's exact centre and the exact SM_CLOUD_RADIUS.
      // Never fudged, never shrunk, and never lead HERE: the lead was
      // applied once, when the smoker committed, and this reads back the
      // same centre the damage does. The whole difficulty curve depends on
      // the player being told the truth about the extent and then being
      // made to judge it against unlit ground.
      const float charge =
          clampf(1.0f + cloud.age / SM_SMOKER_WINDUP, 0.0f, 1.0f);
      const rv_pdk::rv_color hot =
          mix_colour(SM_RING_COLD, SM_RING_HOT, charge);

      // The untiered decal carries the shape. Its tint is ignored (textured
      // draws replace vertex colour), which is exactly why the atlas is
      // untiered: the authored brightness is the only brightness it has,
      // and no dead lamppost can take it away.
      gfx.decal_ground(cloud.centre, SM_CLOUD_RADIUS, cloud.centre.y, smoke_tex,
                       SM_UV_PREWARM_RING, SM_WHITE);

      // The geometric ring is untextured, so it IS tintable, and it pulses
      // from cold to hot across the windup. It also guarantees the edge is
      // exact regardless of what the decal texture does at its rim.
      const float y = cloud.centre.y + 0.03f;
      rv_vec3 previous{};
      for (int step = 0; step <= SM_RING_SEGMENTS; ++step) {
        const float angle = 2.0f * SM_PI * static_cast<float>(step) /
                            static_cast<float>(SM_RING_SEGMENTS);
        const rv_vec3 point{cloud.centre.x + std::sin(angle) * SM_CLOUD_RADIUS,
                            y,
                            cloud.centre.z + std::cos(angle) * SM_CLOUD_RADIUS};
        if (step > 0)
          gfx.line3(previous, point, hot);
        previous = point;

        // Four uprights, so the ring still reads when the player stands
        // almost in its plane and the circle goes edge-on.
        if (step % (SM_RING_SEGMENTS / 4) == 0 && step < SM_RING_SEGMENTS) {
          gfx.line3(point,
                    point + rv_vec3{0.0f, SM_RING_STAKE_HEIGHT * charge, 0.0f},
                    hot);
        }
      }
      continue;
    }

    // ── the cloud itself ──────────────────────────────────────────────────
    // A few cut-out cards at different sizes, so it reads as volume rather
    // than as one sprite. No ring any more: from here on the extent is the
    // player's to judge, which is the difficulty the darkness is for.
    const float radius = cloud_radius_at(cloud);
    if (radius <= 0.01f)
      continue;
    const float roll = cloud.age * SM_CLOUD_ROLL_RATE;

    for (int puff = 0; puff < SM_CLOUD_PUFFS; ++puff) {
      const float angle = SM_PUFF_ANGLE[puff] + roll;
      const float out = SM_PUFF_OUT[puff] * radius;
      const float half_w = SM_PUFF_SIZE[puff] * radius;
      const float half_h = half_w * 0.8f;

      const rv_vec3 point{cloud.centre.x + std::sin(angle) * out,
                          cloud.centre.y + half_h + SM_PUFF_LIFT[puff] * radius,
                          cloud.centre.z + std::cos(angle) * out};

      sm_uvrect uv = SM_UV_SMOKE_A;
      if (puff % 3 == 1)
        uv = SM_UV_SMOKE_B;
      if (puff % 3 == 2)
        uv = SM_UV_SMOKE_C;

      gfx.billboard(point, half_w, half_h, smoke_tex, uv, SM_WHITE);
    }
  }
}

// ── sm_encounters
// ─────────────────────────────────────────────────────────────

void sm_encounters::begin_street(const sm_scene &scene, int shifts_remaining) {
  waves_.clear();
  clock_ = 0.0f;
  if (scene.enemy_spawns.empty())
    return;

  const int row = shift_row(shifts_remaining);
  for (int slot = 0; slot < SM_STREET_SLOTS; ++slot) {
    sm_wave wave{};
    wave.at = SM_STREET_AT[slot] + SM_STREET_NUDGE[row];
    if (wave.at < 0.0f)
      wave.at = 0.0f;
    wave.kind = SM_STREET_KIND[slot];
    wave.position = spawn_point(scene, SM_STREET_POINT[row][slot]);
    wave.released = true; // the street has nothing gated in it
    waves_.push_back(wave);
  }
}

void sm_encounters::begin_factory(const sm_scene &scene, int shifts_remaining) {
  waves_.clear();
  clock_ = 0.0f;
  if (scene.enemy_spawns.empty())
    return;

  const int row = shift_row(shifts_remaining);

  // A light presence, so the hall is occupied while the player reads it.
  for (int slot = 0; slot < SM_FACTORY_SLOTS; ++slot) {
    sm_wave wave{};
    wave.at = SM_FACTORY_AT[slot];
    wave.kind = SM_FACTORY_KIND[slot];
    wave.position = spawn_point(scene, SM_FACTORY_POINT[row][slot]);
    wave.released = true;
    waves_.push_back(wave);
  }

  // And the one gated wave: the single ordinary escalation at assembly step 2.
  // `at` holds the stagger until release_escalation_wave() turns it into a
  // clock time — three enemies arriving on one frame reads as a spawn bug.
  for (int slot = 0; slot < SM_WAVE_SLOTS; ++slot) {
    sm_wave wave{};
    wave.at = SM_WAVE_STAGGER[slot];
    wave.kind = SM_WAVE_KIND[slot];
    wave.position = spawn_point(scene, SM_WAVE_POINT[row][slot]);
    wave.gated = true;
    waves_.push_back(wave);
  }
}

void sm_encounters::begin_final_lap(const sm_scene &) {
  // "The town is not hostile; it is finished." docs/mechanics.md allows a
  // single non-approaching smoker here, but a smoker that does not approach
  // would have to know it is the final lap — and nothing in this file is
  // permitted to read the countdown to change behaviour. Absent it is, then:
  // the rule is worth more than the silhouette.
  waves_.clear();
  clock_ = 0.0f;
}

void sm_encounters::release_escalation_wave() {
  for (std::size_t i = 0; i < waves_.size(); ++i) {
    sm_wave &wave = waves_[i];
    if (!wave.gated || wave.released)
      continue;
    wave.released = true;
    wave.at += clock_; // the authored stagger, from now
  }
}

void sm_encounters::update(float dt, sm_enemies &enemies,
                           rv_vec3 player_position) {
  clock_ += dt;

  for (std::size_t i = 0; i < waves_.size(); ++i) {
    sm_wave &wave = waves_[i];
    if (wave.spawned || !wave.released)
      continue;
    if (clock_ < wave.at)
      continue;

    // A refusal is the cap or the minimum distance talking. The wave is not
    // dropped and it is not moved somewhere else — it simply tries again next
    // frame, which is what "the caller waits" means in docs/mechanics.md.
    if (enemies.spawn(wave.kind, wave.position, player_position))
      wave.spawned = true;
  }
}

} // namespace solidmaid
