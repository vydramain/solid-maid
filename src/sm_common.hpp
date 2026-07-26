// Solidmaid — shared vocabulary and the game's tuning table.
//
// Everything a tuning value could be argued about lives HERE, in one place, so
// the argument happens once. Systems read these constants; they never define
// their own copy of a number that also appears in another file.
//
// Units are SI: metres, seconds, metres per second. The world is left-handed
// (+x right, +y up, +z forward), matching pdklib/rv_math.hpp.
#pragma once

#include <cstdint>

namespace solidmaid {

// ── the countdown
// ─────────────────────────────────────────────────────────────

// docs/content.md: shifts_remaining runs 5 -> 0 and is the ONLY counter in the
// game. Six tiers of dressing hang off it, tier = SM_SHIFTS_START - remaining.
inline constexpr int SM_SHIFTS_START = 5;
inline constexpr int SM_TIER_COUNT = 6;

// Where the player is inside one shift. Persisted (docs/content.md), so the
// numbers are save-format ABI: append, never renumber.
enum sm_phase : int32_t {
  SM_PHASE_HOME = 0,      // the apartment, dressed to the current state
  SM_PHASE_STREET = 1,    // courtyard -> factory gate
  SM_PHASE_FACTORY = 2,   // the assembly hall
  SM_PHASE_RETURNING = 3, // walked into the return trigger, fading home
  SM_PHASE_FINAL = 4,     // ПЛАН ВЫПОЛНЕН has resolved; the run is over
};

// ── camera and view
// ───────────────────────────────────────────────────────────

// docs/gameplay.md §2: eye at ~1.65 m under a 2.50 m ceiling, ~70-75° at 4:3,
// pitch clamped near ±85°, no crouch and no zoom.
inline constexpr float SM_EYE_HEIGHT = 1.65f;
inline constexpr float SM_FOV_Y_DEGREES = 58.0f; // ~72° horizontal at 4:3
inline constexpr float SM_PITCH_LIMIT_DEGREES = 85.0f;
inline constexpr float SM_NEAR_PLANE = 0.12f;

// The far plane is a BUDGET, not a darkness tool: docs/mechanics.md forbids
// solving darkness with far-clip. It is the same at every tier and sits past
// the longest sightline in the game (the factory hall's diagonal, ~28 m).
inline constexpr float SM_FAR_PLANE = 46.0f;

// ── the ordering table
// ────────────────────────────────────────────────────────
//
// The console never says how many buckets it has (specs.md: 1024, deliberately
// hidden), so the disc picks a range and stays inside it. World geometry gets
// the bulk of it; everything drawn "over" the world is stacked above.
//
// THE RANGE HAS TO FIT IN INT16, AND THAT IS NOT OPTIONAL.
//
// The contract lets a disc hand any int32 and promises only that out-of-range
// values "clamp to the nearest bucket" (pdk/cv/rv_primitives.hpp). What it does
// NOT expose is the window those buckets span — and on the reference console
// that window is depth_min = -32768 .. depth_max = 32767, i.e. 65536 keys over
// 1024 buckets (src/rv_pconsole/rv_pconsole_conf.hpp). The clamp saturates, so
// EVERY key at or above 32767 lands in the same, nearest bucket.
//
// The first version of this file spent 0..900000 on the world. Inside a 4 m
// room every surface keys out around 780000, so the whole apartment — walls,
// floor, ceiling, the courtyard beyond the window, the HUD and the view model —
// collapsed into bucket 1023 together and was drawn in submission order alone.
// The visible result was walls that vanish and show whatever was filed after
// them. A depth range is not a scale the disc may choose freely; it is a
// hardware window the disc has to fit inside, and there is no call to ask how
// wide it is. Keep every constant below comfortably inside int16.
inline constexpr int32_t SM_DEPTH_SKY = -32600;       // behind everything
inline constexpr int32_t SM_DEPTH_WORLD_MIN = -32000; // at the far plane
inline constexpr int32_t SM_DEPTH_WORLD_MAX = 26000;  // at the near plane
inline constexpr int32_t SM_DEPTH_VIEWMODEL =
    28000; // the hands and the held tool
inline constexpr int32_t SM_DEPTH_HUD = 29500;
inline constexpr int32_t SM_DEPTH_HUD_TEXT = 30500;
inline constexpr int32_t SM_DEPTH_FADE =
    32600; // the transition curtain, over everything

// ── player
// ────────────────────────────────────────────────────────────────────

inline constexpr float SM_PLAYER_RADIUS = 0.32f;
inline constexpr float SM_PLAYER_WALK_SPEED = 2.6f; // ~130 m route in ~55 s
inline constexpr float SM_PLAYER_ACCEL =
    22.0f; // m/s^2, reaches speed in ~0.12 s
inline constexpr float SM_PLAYER_FRICTION = 26.0f;
inline constexpr int SM_PLAYER_MAX_HP = 100;
inline constexpr float SM_PLAYER_IFRAMES = 0.55f; // after taking a hit

// docs/gameplay.md §4: dead zone, a fine-aim zone near centre, a higher rate
// toward the edge. A single linear sensitivity "will feel bad and no amount of
// tuning the number will fix it".
inline constexpr float SM_LOOK_DEADZONE = 0.18f;
inline constexpr float SM_LOOK_RATE_BASE = 1.35f; // rad/s at the fine end
inline constexpr float SM_LOOK_RATE_EDGE = 3.30f; // rad/s at full deflection
inline constexpr float SM_LOOK_EXPONENT = 2.4f;   // curve between the two
inline constexpr float SM_MOVE_DEADZONE = 0.22f;

// Aim assist is a correctness requirement on a thumbstick, not polish.
inline constexpr float SM_AIM_ASSIST_CONE_DEGREES = 11.0f;
inline constexpr float SM_AIM_ASSIST_RANGE = 16.0f;
inline constexpr float SM_AIM_ASSIST_GRAVITY = 2.1f; // rad/s pull toward centre
inline constexpr float SM_AIM_ASSIST_SLOWDOWN =
    0.55f; // look rate scale over a target

// One pace. Matched to the bob so the footfall and the camera agree: at walking
// speed this is about 2.7 steps a second, which is a tired man in a hurry.
inline constexpr float SM_STEP_STRIDE = 0.95f;

// Head bob: subtle, tied to footfall (docs/gameplay.md §2).
inline constexpr float SM_BOB_FREQUENCY = 8.6f;   // rad/s at walk speed
inline constexpr float SM_BOB_AMPLITUDE = 0.035f; // metres

// ── weapons
// ───────────────────────────────────────────────────────────────────

// Brick: hold to ready, release to throw on a visibly parabolic arc. Never a
// limited resource (docs/mechanics.md) — the world always has more.
inline constexpr float SM_BRICK_SPEED_MIN = 9.0f;
inline constexpr float SM_BRICK_SPEED_MAX = 15.5f;
inline constexpr float SM_BRICK_CHARGE_TIME = 0.45f; // to reach SPEED_MAX
inline constexpr float SM_BRICK_GRAVITY =
    13.5f; // exaggerated, so the arc reads
inline constexpr float SM_BRICK_COOLDOWN = 0.42f;
inline constexpr float SM_BRICK_HIT_RADIUS =
    0.62f; // generous: the pad is not a mouse
inline constexpr int SM_BRICK_DAMAGE = 55;
inline constexpr float SM_BRICK_LIFETIME = 4.0f;
inline constexpr float SM_BRICK_KNOCKBACK = 2.6f;
inline constexpr float SM_PICKUP_RADIUS = 1.9f;

// Pipe: short, fast, telegraphed swing that makes space for the next throw.
// Windup / active / recovery are the numbers the project's earlier prototype
// actually shipped (pipe_melee_profile.tres); they were tuned by hand once and
// there is no reason to re-guess them.
inline constexpr float SM_PIPE_WINDUP = 0.10f;
inline constexpr float SM_PIPE_ACTIVE = 0.16f;
inline constexpr float SM_PIPE_RECOVER = 0.17f;
inline constexpr float SM_PIPE_COOLDOWN =
    0.14f; // the gap after recovery, before the next swing
inline constexpr float SM_PIPE_RANGE = 2.15f;
inline constexpr float SM_PIPE_HALF_ARC_DEGREES = 52.0f;
inline constexpr int SM_PIPE_DAMAGE = 40;
inline constexpr float SM_PIPE_KNOCKBACK = 3.4f;

// Impact: hitstop + micro-shake, landing on the same frame (docs/gameplay.md).
inline constexpr float SM_HITSTOP_LIGHT = 0.06f;
inline constexpr float SM_HITSTOP_HEAVY = 0.10f;
inline constexpr float SM_SHAKE_IMPACT = 0.055f; // ~3 degrees
// Read as radians against a 58-degree vertical FOV, 0.12 was nearly 7 degrees
// of jitter on both axes — punchy for two frames and nauseating after that,
// which is the one thing docs/gameplay.md asks us not to do to a first-person
// camera.
inline constexpr float SM_SHAKE_HURT = 0.075f;
inline constexpr float SM_SHAKE_DECAY = 5.5f;

// ── enemies
// ───────────────────────────────────────────────────────────────────
//
// docs/characters.md: HP, damage, speed and spawn counts are IDENTICAL on shift
// 5 and shift 1. The escalation is that there is less light to fight in. No
// value below may be scaled by the tier — that is the whole difficulty design.
inline constexpr int SM_ENEMY_CAP = 5; // active at once
inline constexpr float SM_SPAWN_MIN_DISTANCE = 9.0f;
inline constexpr float SM_SPAWN_GRACE =
    1.5f; // harmless for this long after spawn

inline constexpr int SM_KIPUCHKA_HP = 60;
inline constexpr float SM_KIPUCHKA_SPEED = 3.15f;
inline constexpr float SM_KIPUCHKA_JITTER = 1.9f; // lateral weave, m/s
inline constexpr float SM_KIPUCHKA_AGGRO = 17.0f;
inline constexpr float SM_KIPUCHKA_ATTACK_RANGE = 1.35f;
inline constexpr float SM_KIPUCHKA_WINDUP = 0.34f; // >= 300 ms floor
inline constexpr float SM_KIPUCHKA_RECOVER = 0.55f;
inline constexpr float SM_KIPUCHKA_COOLDOWN = 1.05f;
inline constexpr int SM_KIPUCHKA_DAMAGE = 12;
inline constexpr float SM_KIPUCHKA_RADIUS = 0.38f;

inline constexpr int SM_SMOKER_HP = 85;
inline constexpr float SM_SMOKER_SPEED = 1.45f;
inline constexpr float SM_SMOKER_AGGRO = 21.0f;
inline constexpr float SM_SMOKER_STANDOFF =
    6.5f; // area denial: keeps its distance
inline constexpr float SM_SMOKER_WINDUP = 0.85f; // the self-lit pre-warm ring
inline constexpr float SM_SMOKER_COOLDOWN = 3.4f;
inline constexpr float SM_SMOKER_RADIUS = 0.42f;
inline constexpr float SM_CLOUD_RADIUS = 3.3f;
inline constexpr float SM_CLOUD_GROW_TIME = 0.7f;
inline constexpr float SM_CLOUD_LIFETIME = 3.6f;
inline constexpr int SM_CLOUD_TICK_DAMAGE = 5;
inline constexpr float SM_CLOUD_TICK_PERIOD = 0.55f;

// ── the assembly ritual
// ───────────────────────────────────────────────────────

inline constexpr int SM_ASSEMBLY_STEPS = 3;
inline constexpr float SM_ASSEMBLY_STEP_TIME = 3.2f; // hold, per step
inline constexpr float SM_ASSEMBLY_REACH = 2.4f;
inline constexpr int SM_ASSEMBLY_ESCALATION_STEP =
    2; // one ordinary wave, at step 2
// Long enough for the finished lamppost to ride the conveyor out of the hall
// and for the digit to flip where the player can see it happen.
inline constexpr float SM_BOARD_CLACK_HOLD = 2.8f;

// After the lamppost is finished the shift ends on a clock rather than on a
// walk back to the door: the count is put on screen and then the fade carries
// him home. NOTE this is the one place the number appears outside the factory
// board, which docs/mechanics.md otherwise forbids — an explicit, requested
// departure from "the count reaches the player only through the board, the
// street lighting, and the apartment".
inline constexpr float SM_SHIFT_END_HOLD = 5.0f;

// ── transitions
// ───────────────────────────────────────────────────────────────

inline constexpr float SM_FADE_TIME = 0.55f;
// Hold Select this long to throw the run away. Long enough that it cannot
// happen by accident, short enough that it does not feel broken.
inline constexpr float SM_RESTART_HOLD = 1.1f;

inline constexpr float SM_DEATH_HOLD =
    1.8f; // knocked out, before the shift restarts

// The ending is the board, held. docs/content.md: "on approaching the board,
// the line ПЛАН ВЫПОЛНЕН resolves and the game ends. No cutscene, no boss, no
// new location, no explanation." So the world stays on screen long enough to
// read the panel, and only then does the curtain come down.
inline constexpr float SM_ENDING_HOLD = 3.2f;
inline constexpr float SM_ENDING_FADE = 2.0f;

// ── memory card
// ───────────────────────────────────────────────────────────────

inline constexpr int64_t SM_SAVE_SLOT = 0;
inline constexpr uint32_t SM_SAVE_MAGIC = 0x534D4131u; // "SMA1"

} // namespace solidmaid
