// Solidmaid — the two archetypes that ship, and the spawner discipline.
//
// The rule that governs every number in here is docs/characters.md:
//
//   HP, damage, cloud radius and spawn counts are IDENTICAL on shift 5 and
//   shift 1. Nothing in this file may read the tier. The escalation of the
//   whole game is that there is less light to fight in — the grey silhouettes
//   read later against unlit facades and the cloud's true extent is harder to
//   judge. "This is the intended difficulty curve of the whole game, and it
//   costs nothing to build."
//
// Telegraphs are the other hard rule: windups are >= 300 ms, and the smoker's
// pre-warm ring is drawn from an UNTIERED texture so it stays self-lit at every
// tier. A telegraph must never depend on a lamppost being alive.
#pragma once

#include <vector>

#include "pdklib/rv_math.hpp"

#include "sm_common.hpp"
#include "sm_gfx.hpp"

namespace solidmaid {

class sm_assets;
struct sm_scene;
struct sm_feel;

enum sm_enemy_kind : int {
  SM_ENEMY_KIPUCHKA = 0, // fast, jittery melee pest
  SM_ENEMY_SMOKER = 1,   // area denial: an expanding smoke cloud
};

enum sm_enemy_stage : int {
  SM_STAGE_APPROACH = 0,
  SM_STAGE_WINDUP, // the telegraph
  SM_STAGE_STRIKE,
  SM_STAGE_RECOVER,
  SM_STAGE_DYING,
};

struct sm_enemy {
  sm_enemy_kind kind = SM_ENEMY_KIPUCHKA;
  sm_enemy_stage stage = SM_STAGE_APPROACH;
  rv_pdklib::rv_vec3 position{};
  float yaw = 0.0f;
  int hp = 0;
  float stage_time = 0.0f;
  float cooldown = 0.0f;
  float grace = 0.0f;      // harmless for SM_SPAWN_GRACE after appearing
  float hurt_flash = 0.0f; // visual confirmation of a hit, in place of a sound
  float jitter_phase = 0.0f;
  float death_time = 0.0f;
  bool alive = false;
};

// The smoker's exhaled cloud: grows, denies an area, ticks chip damage.
struct sm_cloud {
  rv_pdklib::rv_vec3 centre{};
  float age = 0.0f;
  float tick = 0.0f;
  bool alive = false;
};

// What the enemies did to the player this frame. Returned rather than applied,
// so the player owns its own health and i-frames.
struct sm_enemy_damage {
  int amount = 0;
  rv_pdklib::rv_vec3 from{};
  bool from_cloud = false; // chip damage bypasses i-frames, direct hits do not
};

class sm_enemies {
public:
  void reset();

  // Places one enemy if the cap and the minimum spawn distance allow it.
  // Returns false when refused — the caller does not retry, it waits.
  bool spawn(sm_enemy_kind kind, rv_pdklib::rv_vec3 position,
             rv_pdklib::rv_vec3 player);

  void update(float dt, const sm_scene &scene,
              rv_pdklib::rv_vec3 player_position,
              std::vector<sm_enemy_damage> &out_damage);

  // Weapons land through these. Both return the number of enemies hit, so the
  // caller knows whether to fire the hitstop.
  int damage_sphere(rv_pdklib::rv_vec3 centre, float radius, int damage,
                    rv_pdklib::rv_vec3 knockback, sm_feel &feel);
  int damage_arc(rv_pdklib::rv_vec3 origin, rv_pdklib::rv_vec3 forward,
                 float range, float half_arc_degrees, int damage,
                 sm_feel &feel);

  void render(sm_gfx &gfx, const sm_assets &assets, int tier) const;

  int active_count() const;
  const std::vector<sm_enemy> &enemies() const { return enemies_; }
  const std::vector<sm_cloud> &clouds() const { return clouds_; }

  // Aim assist needs the nearest target inside a cone; returns false if none.
  bool nearest_target(rv_pdklib::rv_vec3 eye, rv_pdklib::rv_vec3 forward,
                      float range, float half_cone_degrees,
                      rv_pdklib::rv_vec3 &out_centre) const;

private:
  std::vector<sm_enemy> enemies_;
  std::vector<sm_cloud> clouds_;
};

// The enemies' own voices: telegraphs, footfalls and deaths, read off the state
// sm_enemies::update() just produced.
//
// It is a free function rather than a member because sm_enemies::update() is
// handed no sm_feel — enemies report damage as DATA and the player decides what
// it costs, which is a separation worth keeping. So the observer runs one step
// later, from sm_combat::update(), where a feel is already in hand and every
// kill for this frame has been filed.
void sm_enemy_audio(const sm_enemies &enemies, const sm_scene &scene,
                    rv_pdklib::rv_vec3 listener, rv_pdklib::rv_vec3 forward,
                    float dt, sm_feel &feel);

// ── encounters
// ────────────────────────────────────────────────────────────────
//
// "Placement is keyed by chunk index and shifts_remaining, so encounters are
// authored per tier, not scaled" (docs/mechanics.md). The schedule decides
// WHERE and WHEN an enemy appears; it never touches what one is made of.
class sm_encounters {
public:
  void begin_street(const sm_scene &scene, int shifts_remaining);
  void begin_factory(const sm_scene &scene, int shifts_remaining);
  void begin_final_lap(const sm_scene &scene);

  // One ordinary wave, released when the assembly reaches step 2.
  void release_escalation_wave();

  void update(float dt, sm_enemies &enemies,
              rv_pdklib::rv_vec3 player_position);

private:
  struct sm_wave {
    float at = 0.0f;
    sm_enemy_kind kind = SM_ENEMY_KIPUCHKA;
    rv_pdklib::rv_vec3 position{};
    bool released = false;
    bool spawned = false;
    bool gated = false; // waits for release_escalation_wave()
  };

  std::vector<sm_wave> waves_;
  float clock_ = 0.0f;
};

} // namespace solidmaid
