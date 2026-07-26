// Solidmaid — an area, as built data.
//
// A scene is a flat list of authored quads, billboards, collision rectangles,
// triggers and interactables. It is rebuilt whenever the player enters an area,
// which is cheap (no disc I/O — the geometry is code) and is what makes the
// countdown's re-dressing trivial: the same builder runs again with a different
// shifts_remaining and produces the room with one thing fewer.
//
// The three areas are described physically in docs/environments.md and must be
// built to it: a 2.50 m apartment ceiling against a 7 m factory one, two street
// chunks with one unmistakable route, five lampposts in a fixed order.
//
// COLLISION IS 2D. Every floor in the game is flat and there is no jump, no
// crouch and no stairs (docs/gameplay.md §2), so a circle-versus-rectangle
// solve in the xz plane is the whole physics engine. It cannot produce a
// softlock, which is worth more here than any amount of generality.
#pragma once

#include <vector>

#include "pdklib/rv_math.hpp"

#include "sm_assets.hpp"
#include "sm_common.hpp"
#include "sm_gfx.hpp"
#include "sm_state.hpp"

namespace solidmaid {

enum sm_area : int {
  SM_AREA_HOME = 0,
  SM_AREA_STREET = 1,
  SM_AREA_FACTORY = 2,
};

// What walking into a volume does. One per area transition, plus the board.
enum sm_trigger_id : int {
  SM_TRIGGER_NONE = 0,
  SM_TRIGGER_LEAVE_HOME,     // the flat's front door -> the street
  SM_TRIGGER_ENTER_FACTORY,  // the checkpoint gate -> the hall
  SM_TRIGGER_RETURN_HOME,    // opens once the assembly is done
  SM_TRIGGER_APPROACH_BOARD, // final lap only: resolves ПЛАН ВЫПОЛНЕН
};

// What a hand button does when the player is looking at this thing. There is no
// separate interact binding — see docs/gameplay.md, "Hand intent, not a verb
// list".
enum sm_interact_kind : int {
  SM_INTERACT_NONE = 0,
  SM_INTERACT_BRICK,      // pick up
  SM_INTERACT_PIPE,       // pick up
  SM_INTERACT_TELEVISION, // the game's one comfort interaction, state 0 only
  SM_INTERACT_ASSEMBLY,   // hold to work
};

// One authored quad, in the PDK's Z order (see sm_gfx::quad).
struct sm_surface {
  rv_pdklib::rv_vec3 corners[4];
  sm_tex_id texture = SM_TEX_COUNT; // SM_TEX_COUNT = untextured, flat colour
  sm_uvrect uv{};
  rv_pdk::rv_color tint{255, 255, 255};
  float tess = 2.0f;
};

// A camera-facing card: props that read the same from every angle, and the
// things that are genuinely flat (light pools are decals, see `decals`).
struct sm_billboard {
  rv_pdklib::rv_vec3 centre{};
  float half_width = 0.5f;
  float half_height = 0.5f;
  sm_tex_id texture = SM_TEX_COUNT;
  sm_uvrect uv{};
  rv_pdk::rv_color tint{255, 255, 255};
};

// A horizontal card on the ground: the lamp pools, the assembly glow.
struct sm_decal {
  rv_pdklib::rv_vec3 centre{};
  float half_size = 1.0f;
  float y = 0.0f;
  sm_tex_id texture = SM_TEX_COUNT;
  sm_uvrect uv{};
  rv_pdk::rv_color tint{255, 255, 255};
};

// An axis-aligned rectangle in the xz plane. Walls, furniture, anything solid.
struct sm_rect {
  float x0 = 0.0f, z0 = 0.0f, x1 = 0.0f, z1 = 0.0f;

  bool contains(float x, float z) const {
    return x >= x0 && x <= x1 && z >= z0 && z <= z1;
  }
};

struct sm_trigger {
  sm_rect area{};
  sm_trigger_id id = SM_TRIGGER_NONE;
  bool active = true;
};

struct sm_interactable {
  rv_pdklib::rv_vec3 position{};
  sm_interact_kind kind = SM_INTERACT_NONE;
  float radius = 1.2f;
  bool active = true;
};

// A lamppost. Whether it burns is DERIVED from the countdown by its index; the
// pole is never removed, only the light (docs/environments.md).
struct sm_lamp {
  rv_pdklib::rv_vec3 base{};
  int index = 0; // 0..4 = L1..L5, the authored extinguish order
};

struct sm_scene {
  sm_area area = SM_AREA_HOME;

  std::vector<sm_surface> surfaces;
  std::vector<sm_billboard> billboards;
  std::vector<sm_decal> decals;
  std::vector<sm_rect> colliders;
  std::vector<sm_trigger> triggers;
  std::vector<sm_interactable> interactables;
  std::vector<sm_lamp> lamps;
  std::vector<rv_pdklib::rv_vec3> enemy_spawns;
  std::vector<rv_pdklib::rv_vec3> brick_spawns;

  rv_pdklib::rv_vec3 player_start{0.0f, 0.0f, 0.0f};
  float player_yaw = 0.0f;
  float floor_y = 0.0f;
  float ceiling_y = 2.5f;
  // The area's own far plane. See sm_gfx::begin — this is what decides how
  // much of the ordering table this space gets to spend on itself.
  float far_plane = SM_FAR_PLANE;

  // The clear colour is the ambient floor made literal: it is what the player
  // sees where nothing is drawn, and docs/mechanics.md forbids driving it to
  // zero at any tier, the final lap included.
  rv_pdk::rv_color clear_colour{18, 20, 26};
  bool draws_sky = false;

  void clear();

  // ── authoring helpers ─────────────────────────────────────────────────────
  // Corners are generated in the PDK's Z order by construction, so a level
  // file cannot get the winding wrong and produce an hourglass.

  void add_floor(float x0, float z0, float x1, float z1, float y, sm_tex_id tex,
                 sm_uvrect uv, rv_pdk::rv_color tint, float tess = 2.0f);
  void add_ceiling(float x0, float z0, float x1, float z1, float y,
                   sm_tex_id tex, sm_uvrect uv, rv_pdk::rv_color tint,
                   float tess = 2.0f);
  // A vertical wall from (x0,z0) to (x1,z1), rising from y0 to y1. `solid`
  // also files a collider along it.
  void add_wall(float x0, float z0, float x1, float z1, float y0, float y1,
                sm_tex_id tex, sm_uvrect uv, rv_pdk::rv_color tint, bool solid,
                float tess = 2.0f);
  // An axis-aligned box: four sides and a top. The standard prop primitive.
  void add_box(float x0, float z0, float x1, float z1, float y0, float y1,
               sm_tex_id tex, sm_uvrect uv, rv_pdk::rv_color tint, bool solid,
               float tess = 2.0f);
  void add_collider(float x0, float z0, float x1, float z1);
};

// ── building
// ──────────────────────────────────────────────────────────────────
//
// One builder per area. Each reads the countdown and dresses itself to it —
// that is the ONLY way the tier reaches the world.
void sm_build_home(sm_scene &out, const sm_countdown &state);
void sm_build_street(sm_scene &out, const sm_countdown &state);
void sm_build_factory(sm_scene &out, const sm_countdown &state);
void sm_build_area(sm_scene &out, sm_area area, const sm_countdown &state);

// ── drawing
// ───────────────────────────────────────────────────────────────────

// Draws the static world: sky, surfaces, billboards, then the lamp pools of
// whichever lamps are still burning.
void sm_scene_render(const sm_scene &scene, sm_gfx &gfx,
                     const sm_assets &assets, const sm_countdown &state);

// ── collision and queries
// ─────────────────────────────────────────────────────

// Slide a circle of `radius` from `from` toward `to`, pushed out of every
// collider it would end up inside. Returns the resolved position; never returns
// a point inside geometry, and never refuses to move at all.
rv_pdklib::rv_vec3 sm_scene_slide(const sm_scene &scene,
                                  rv_pdklib::rv_vec3 from,
                                  rv_pdklib::rv_vec3 to, float radius);

// Is the straight segment between two points clear of solid geometry? Used by
// enemies to decide whether they can see the player, and by the brick to stop
// at a wall.
bool sm_scene_clear_line(const sm_scene &scene, rv_pdklib::rv_vec3 a,
                         rv_pdklib::rv_vec3 b);

// The first active trigger containing the point, or SM_TRIGGER_NONE.
sm_trigger_id sm_scene_trigger_at(const sm_scene &scene,
                                  rv_pdklib::rv_vec3 position);

// The interactable the player is closest to being aimed at, within reach.
// Returns an index into `interactables`, or -1.
int sm_scene_pick_interactable(const sm_scene &scene, rv_pdklib::rv_vec3 eye,
                               rv_pdklib::rv_vec3 forward, float reach);

} // namespace solidmaid
