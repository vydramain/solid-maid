// Solidmaid — the scene machinery: authoring, drawing, collision, queries.
//
// Everything in here is deliberately dumb. A scene is a flat list of quads and
// rectangles; the only cleverness allowed is in sm_scene_slide(), and even
// there the goal is not accuracy but the guarantee stated in sm_scene.hpp: the
// solve "cannot produce a softlock, which is worth more here than any amount of
// generality". Nothing else in the game can strand a player.
//
// Two console facts shape every draw call below and are worth restating where
// the code that obeys them lives:
//
//   * SAMPLE_TEXTURE REPLACES the vertex colour (sm_gfx.hpp). So `tint` only
//     does anything on the untextured paths, and every gradient in this file —
//     the sky, the pool under a burning lamppost — is untextured quad_shaded()
//     geometry with four authored corner colours.
//   * Tessellation SUBDIVIDES a uv rectangle, it does not repeat it. One
//     sm_surface therefore shows one atlas cell stretched across itself,
//     however finely it is cut up; a repeating rhythm has to be authored as
//     several surfaces. tess_metres is a primitive-count and affine-swim
//     control, and nothing else.
#include "sm_scene.hpp"

#include <cmath>

#include "sm_atlas.hpp"

namespace solidmaid {
namespace {

using rv_pdklib::rv_vec3;

constexpr float SM_PI_F = 3.14159265358979323846f;

// ── collision
// ─────────────────────────────────────────────────────────────────

// A wall is authored as a PLANE, and a rectangle with no area cannot push a
// circle out of anything. Both axes of every collider are inflated to at least
// this, centred on the authored rectangle, so a zero-thickness wall still
// behaves like a wall.
constexpr float SM_COLLIDER_MIN_THICKNESS = 0.10f;

// How many push-out passes sm_scene_slide() runs. A corner where two colliders
// meet needs a second pass to settle, a doorway jamb occasionally a third; six
// is far past anything the authored levels contain and still trivially cheap.
constexpr int SM_SLIDE_PASSES = 6;

// Numerical slack on "is the circle inside this rectangle". Small enough to be
// invisible, large enough that the resolver never re-triggers on its own output
// and oscillates.
constexpr float SM_SLIDE_EPSILON = 1.0e-4f;

// ── picking
// ───────────────────────────────────────────────────────────────────

// How far off the view axis an interactable may sit and still be pickable.
// Deliberately far wider than SM_AIM_ASSIST_CONE_DEGREES: aiming a weapon is a
// skill the game asks for, reaching for the brick by the door is not, and the
// only aiming device is a thumbstick (docs/gameplay.md §4).
constexpr float SM_PICK_CONE_DEGREES = 55.0f;

// How much being off-axis costs in the pick ranking, as a multiple of distance.
// Purely a tie-break: the brick and the pipe stand side by side in the hallway
// every single shift, and the player must get the one they are looking at.
constexpr float SM_PICK_ANGLE_WEIGHT = 1.0f;

// ── the lamppost
// ──────────────────────────────────────────────────────────────
//
// The standard РКУ mercury lantern of docs/environments.md, as geometry. The
// pole is never removed at any tier; only the pool below it goes out.
constexpr float SM_LAMP_HEIGHT = 7.0f;
constexpr float SM_LAMP_HALF_WIDTH = 0.16f;
constexpr float SM_LAMP_ARM = 1.35f;
constexpr float SM_LAMP_ARM_DROP = 0.34f;
constexpr float SM_LAMP_HEAD_HALF_W = 0.42f;
constexpr float SM_LAMP_HEAD_HALF_H = 0.20f;
constexpr float SM_LAMP_POOL_RX = 4.6f;
constexpr float SM_LAMP_POOL_RZ = 3.4f;
constexpr float SM_LAMP_POOL_INNER = 0.45f; // fraction of the outer ellipse
constexpr float SM_LAMP_POOL_LIFT =
    0.03f; // off the road, so the OT cannot flicker it

// ── the sky
// ───────────────────────────────────────────────────────────────────
//
// A camera-centred shell just inside SM_FAR_PLANE. It is opaque, so it also
// doubles as the wall that hides the far-clip: geometry past the shell sorts
// behind it and simply is not there, which on a 130 m lane is the difference
// between "overcast distance" and "the road ends in mid air".
constexpr float SM_SKY_RADIUS = 42.0f;
constexpr float SM_SKY_TOP = 30.0f;     // above the eye
constexpr float SM_SKY_BOTTOM = -12.0f; // below the eye
constexpr float SM_SKY_TESS = 16.0f;

// ── the unlit ramp
// ────────────────────────────────────────────────────────────

// Untextured geometry never passes through a palette, so it has to walk down
// the impoverishment ramp by hand. This is the same straight line the palette
// ramp in sm_assets.cpp walks (1.00 at shift 5 down to 0.66 on the final lap) —
// and like it, it never reaches zero, because docs/mechanics.md forbids driving
// the ambient floor to zero on any tier.
float tier_scale(int tier) {
  if (tier < 0)
    tier = 0;
  if (tier > SM_TIER_COUNT - 1)
    tier = SM_TIER_COUNT - 1;
  return 1.0f - 0.068f * static_cast<float>(tier);
}

uint8_t scale_channel(uint8_t value, float k) {
  float v = static_cast<float>(value) * k + 0.5f;
  if (v < 0.0f)
    v = 0.0f;
  if (v > 255.0f)
    v = 255.0f;
  return static_cast<uint8_t>(v);
}

rv_pdk::rv_color shade(rv_pdk::rv_color colour, float k) {
  return rv_pdk::rv_color{scale_channel(colour.r, k),
                          scale_channel(colour.g, k),
                          scale_channel(colour.b, k)};
}

bool finite3(rv_vec3 v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

float clampf(float value, float low, float high) {
  return value < low ? low : (value > high ? high : value);
}

// How far the circle at (x, z) is inside the WORST collider it touches, in
// metres, or zero when it is clear of all of them. A depth rather than a
// yes/no, because the only honest way to choose between two imperfect answers
// is to take the less imperfect one.
float penetration_depth(const sm_scene &scene, float x, float z, float radius) {
  float worst = 0.0f;
  for (const sm_rect &rect : scene.colliders) {
    const float cx = clampf(x, rect.x0, rect.x1);
    const float cz = clampf(z, rect.z0, rect.z1);
    const float dx = x - cx;
    const float dz = z - cz;
    const float overlap = radius - std::sqrt(dx * dx + dz * dz);
    if (overlap > worst)
      worst = overlap;
  }
  return worst;
}

// ── drawing helpers
// ───────────────────────────────────────────────────────────

void draw_sky(sm_gfx &gfx, int tier) {
  // Flat overcast: dirty grey-blue at the horizon, a darker cold blue overhead,
  // and the console's ordered dithering does the rest (docs/environments.md).
  // The sky IS the ambient floor's justification, which is why it is shaded per
  // tier here rather than through a palette: it is untextured geometry.
  const float k = tier_scale(tier);
  const rv_pdk::rv_color horizon = shade(rv_pdk::rv_color{62, 66, 74}, k);
  const rv_pdk::rv_color zenith = shade(rv_pdk::rv_color{27, 31, 44}, k);

  const rv_vec3 eye = gfx.view().eye;
  const float r = SM_SKY_RADIUS;
  const float top = eye.y + SM_SKY_TOP;
  const float bottom = eye.y + SM_SKY_BOTTOM;

  const float px[5] = {-r, r, r, -r, -r};
  const float pz[5] = {-r, -r, r, r, -r};

  for (int i = 0; i < 4; ++i) {
    const rv_vec3 corners[4] = {
        rv_vec3{eye.x + px[i], top, eye.z + pz[i]},
        rv_vec3{eye.x + px[i + 1], top, eye.z + pz[i + 1]},
        rv_vec3{eye.x + px[i], bottom, eye.z + pz[i]},
        rv_vec3{eye.x + px[i + 1], bottom, eye.z + pz[i + 1]}};
    const rv_pdk::rv_color colours[4] = {zenith, zenith, horizon, horizon};
    gfx.quad_shaded(corners, colours, SM_SKY_TESS);
  }

  // The lid. Pitch clamps near ±85°, so the player can and will look up.
  const rv_vec3 lid[4] = {
      rv_vec3{eye.x - r, top, eye.z + r}, rv_vec3{eye.x + r, top, eye.z + r},
      rv_vec3{eye.x - r, top, eye.z - r}, rv_vec3{eye.x + r, top, eye.z - r}};
  const rv_pdk::rv_color lid_colours[4] = {zenith, zenith, zenith, zenith};
  gfx.quad_shaded(lid, lid_colours, SM_SKY_TESS * 1.5f);
}

// The pool of light under a burning lamppost.
//
// This ships as the AUTHORED light_pool decal rather than as shaded geometry,
// and the reason is what the atlas actually contains: a cut-out ordered-dither
// stipple that thins from a solid mercury centre to nothing at the rim. Its
// transparent texels are what let the tier-darkened asphalt show through, so
// the pool dissolves into the road instead of ending on it — the "visible
// dithered edge" docs/environments.md asks for, drawn once by the artist rather
// than approximated per frame.
//
// It is deliberately one of the UNTIERED atlases (sm_assets.cpp), and that is
// correct rather than a compromise: a mercury lamp that is still burning emits
// the same light however many of its neighbours have gone out. The ramp darkens
// the town around it, which is exactly why the surviving pools read as brighter
// as the count falls. A lamp that dies loses its pool entirely — that is the
// countdown, and it happens at the call site, not here.
void draw_light_pool(sm_gfx &gfx, const sm_assets &assets, rv_vec3 centre) {
  const sm_texref pool = assets.ref(SM_TEX_LIGHT_POOL, 0);

  const float y = centre.y;
  const rv_vec3 corners[4] = {
      rv_vec3{centre.x - SM_LAMP_POOL_RX, y, centre.z + SM_LAMP_POOL_RZ},
      rv_vec3{centre.x + SM_LAMP_POOL_RX, y, centre.z + SM_LAMP_POOL_RZ},
      rv_vec3{centre.x - SM_LAMP_POOL_RX, y, centre.z - SM_LAMP_POOL_RZ},
      rv_vec3{centre.x + SM_LAMP_POOL_RX, y, centre.z - SM_LAMP_POOL_RZ}};

  // Tessellated rather than drawn as one quad: it lies on the ground the player
  // walks over, so a corner crosses the near plane constantly, and a whole-
  // polygon near rejection would make the pool blink out underfoot.
  // Biased toward the eye so the pool never trades places with the road it
  // lies on — see sm_gfx::quad.
  gfx.quad(corners, pool, SM_UV_LIGHT_POOL, rv_pdk::rv_color{255, 255, 255},
           2.2f, SM_DEPTH_BIAS_DECAL);
}

void draw_lamp(sm_gfx &gfx, const sm_assets &assets, int tier,
               const sm_lamp &lamp, bool lit) {
  const sm_texref tex = assets.ref(SM_TEX_STREET_LAMP, tier);
  // The bracket always reaches toward the lane, i.e. toward x = 0. No lamppost
  // in the game stands on the centre line, so the sign is never ambiguous.
  const float dir = (lamp.base.x <= 0.0f) ? 1.0f : -1.0f;
  const float x = lamp.base.x;
  const float z = lamp.base.z;
  const float y0 = lamp.base.y;
  const float y1 = y0 + SM_LAMP_HEIGHT;
  const rv_pdk::rv_color white{255, 255, 255};

  // Two crossed cards for the concrete pole: it reads as a pole from every
  // angle for four primitives instead of the twenty a real prism would cost.
  const rv_vec3 pole_a[4] = {rv_vec3{x - SM_LAMP_HALF_WIDTH, y1, z},
                             rv_vec3{x + SM_LAMP_HALF_WIDTH, y1, z},
                             rv_vec3{x - SM_LAMP_HALF_WIDTH, y0, z},
                             rv_vec3{x + SM_LAMP_HALF_WIDTH, y0, z}};
  const rv_vec3 pole_b[4] = {rv_vec3{x, y1, z - SM_LAMP_HALF_WIDTH},
                             rv_vec3{x, y1, z + SM_LAMP_HALF_WIDTH},
                             rv_vec3{x, y0, z - SM_LAMP_HALF_WIDTH},
                             rv_vec3{x, y0, z + SM_LAMP_HALF_WIDTH}};
  if (tex.valid()) {
    gfx.quad(pole_a, tex, SM_UV_LAMP_POLE, white, 4.0f);
    gfx.quad(pole_b, tex, SM_UV_LAMP_POLE, white, 4.0f);
  } else {
    const rv_pdk::rv_color concrete =
        shade(rv_pdk::rv_color{104, 104, 100}, tier_scale(tier));
    gfx.quad_flat(pole_a, concrete, 4.0f);
    gfx.quad_flat(pole_b, concrete, 4.0f);
  }

  const float arm_x = x + dir * SM_LAMP_ARM;
  const rv_vec3 arm[4] = {rv_vec3{x, y1, z}, rv_vec3{arm_x, y1, z},
                          rv_vec3{x, y1 - SM_LAMP_ARM_DROP, z},
                          rv_vec3{arm_x, y1 - SM_LAMP_ARM_DROP, z}};
  if (tex.valid()) {
    gfx.quad(arm, tex, SM_UV_LAMP_BRACKET, white, 4.0f);
  } else {
    gfx.quad_flat(arm, rv_pdk::rv_color{72, 74, 70}, 4.0f);
  }

  const rv_vec3 head_centre{arm_x, y1 - SM_LAMP_ARM_DROP, z};
  if (tex.valid()) {
    gfx.billboard(head_centre, SM_LAMP_HEAD_HALF_W, SM_LAMP_HEAD_HALF_H, tex,
                  lit ? SM_UV_LAMP_HEAD_LIT : SM_UV_LAMP_HEAD_DEAD, white);
  }

  if (!lit)
    return;

  // Turning a streetlight off is not a lighting operation; it is removing this
  // one patch of geometry (docs/gameplay.md §3). Everything else about a dead
  // lamppost — the pole, the bracket, the position — is identical.
  draw_light_pool(gfx, assets, rv_vec3{arm_x, y0 + SM_LAMP_POOL_LIFT, z});
}

} // namespace

// ── authoring
// ─────────────────────────────────────────────────────────────────

void sm_scene::clear() {
  surfaces.clear();
  billboards.clear();
  decals.clear();
  colliders.clear();
  triggers.clear();
  interactables.clear();
  lamps.clear();
  enemy_spawns.clear();
  brick_spawns.clear();

  area = SM_AREA_HOME;
  player_start = rv_pdklib::rv_vec3{0.0f, 0.0f, 0.0f};
  player_yaw = 0.0f;
  floor_y = 0.0f;
  ceiling_y = 2.5f;
  clear_colour = rv_pdk::rv_color{18, 20, 26};
  draws_sky = false;
}

void sm_scene::add_floor(float x0, float z0, float x1, float z1, float y,
                         sm_tex_id tex, sm_uvrect uv, rv_pdk::rv_color tint,
                         float tess) {
  if (x1 < x0) {
    const float t = x0;
    x0 = x1;
    x1 = t;
  }
  if (z1 < z0) {
    const float t = z0;
    z0 = z1;
    z1 = t;
  }

  sm_surface surface{};
  // The PDK's Z order: v0 v1 along the top edge, v2 v3 along the bottom. For a
  // horizontal surface "top" is the far (+z) edge, which puts the quad's split
  // diagonal across it rather than around its rim.
  surface.corners[0] = rv_pdklib::rv_vec3{x0, y, z1};
  surface.corners[1] = rv_pdklib::rv_vec3{x1, y, z1};
  surface.corners[2] = rv_pdklib::rv_vec3{x0, y, z0};
  surface.corners[3] = rv_pdklib::rv_vec3{x1, y, z0};
  surface.texture = tex;
  surface.uv = uv;
  surface.tint = tint;
  surface.tess = tess;
  surfaces.push_back(surface);
}

void sm_scene::add_ceiling(float x0, float z0, float x1, float z1, float y,
                           sm_tex_id tex, sm_uvrect uv, rv_pdk::rv_color tint,
                           float tess) {
  if (x1 < x0) {
    const float t = x0;
    x0 = x1;
    x1 = t;
  }
  if (z1 < z0) {
    const float t = z0;
    z0 = z1;
    z1 = t;
  }

  sm_surface surface{};
  // Mirrored in z against add_floor so the texture is not seen from behind and
  // the split diagonal still runs the short way across the surface.
  surface.corners[0] = rv_pdklib::rv_vec3{x0, y, z0};
  surface.corners[1] = rv_pdklib::rv_vec3{x1, y, z0};
  surface.corners[2] = rv_pdklib::rv_vec3{x0, y, z1};
  surface.corners[3] = rv_pdklib::rv_vec3{x1, y, z1};
  surface.texture = tex;
  surface.uv = uv;
  surface.tint = tint;
  surface.tess = tess;
  surfaces.push_back(surface);
}

void sm_scene::add_wall(float x0, float z0, float x1, float z1, float y0,
                        float y1, sm_tex_id tex, sm_uvrect uv,
                        rv_pdk::rv_color tint, bool solid, float tess) {
  if (y1 < y0) {
    const float t = y0;
    y0 = y1;
    y1 = t;
  }

  sm_surface surface{};
  surface.corners[0] = rv_pdklib::rv_vec3{x0, y1, z0};
  surface.corners[1] = rv_pdklib::rv_vec3{x1, y1, z1};
  surface.corners[2] = rv_pdklib::rv_vec3{x0, y0, z0};
  surface.corners[3] = rv_pdklib::rv_vec3{x1, y0, z1};
  surface.texture = tex;
  surface.uv = uv;
  surface.tint = tint;
  surface.tess = tess;
  surfaces.push_back(surface);

  if (solid)
    add_collider(x0, z0, x1, z1);
}

void sm_scene::add_box(float x0, float z0, float x1, float z1, float y0,
                       float y1, sm_tex_id tex, sm_uvrect uv,
                       rv_pdk::rv_color tint, bool solid, float tess) {
  if (x1 < x0) {
    const float t = x0;
    x0 = x1;
    x1 = t;
  }
  if (z1 < z0) {
    const float t = z0;
    z0 = z1;
    z1 = t;
  }
  if (y1 < y0) {
    const float t = y0;
    y0 = y1;
    y1 = t;
  }

  // Four sides walked around the footprint, then the lid. The underside is
  // never authored: every box in the game stands on a floor.
  add_wall(x0, z0, x1, z0, y0, y1, tex, uv, tint, false, tess);
  add_wall(x1, z0, x1, z1, y0, y1, tex, uv, tint, false, tess);
  add_wall(x1, z1, x0, z1, y0, y1, tex, uv, tint, false, tess);
  add_wall(x0, z1, x0, z0, y0, y1, tex, uv, tint, false, tess);
  add_floor(x0, z0, x1, z1, y1, tex, uv, tint, tess);

  if (solid)
    add_collider(x0, z0, x1, z1);
}

void sm_scene::add_collider(float x0, float z0, float x1, float z1) {
  if (x1 < x0) {
    const float t = x0;
    x0 = x1;
    x1 = t;
  }
  if (z1 < z0) {
    const float t = z0;
    z0 = z1;
    z1 = t;
  }

  // A plane wall has no area in one axis and would let a circle pass straight
  // through. Inflate about the centre rather than from one edge, so a wall
  // stays where it was authored.
  if (x1 - x0 < SM_COLLIDER_MIN_THICKNESS) {
    const float mid = 0.5f * (x0 + x1);
    x0 = mid - 0.5f * SM_COLLIDER_MIN_THICKNESS;
    x1 = mid + 0.5f * SM_COLLIDER_MIN_THICKNESS;
  }
  if (z1 - z0 < SM_COLLIDER_MIN_THICKNESS) {
    const float mid = 0.5f * (z0 + z1);
    z0 = mid - 0.5f * SM_COLLIDER_MIN_THICKNESS;
    z1 = mid + 0.5f * SM_COLLIDER_MIN_THICKNESS;
  }

  sm_rect rect{};
  rect.x0 = x0;
  rect.z0 = z0;
  rect.x1 = x1;
  rect.z1 = z1;
  colliders.push_back(rect);
}

// ── building
// ──────────────────────────────────────────────────────────────────

void sm_build_area(sm_scene &out, sm_area area, const sm_countdown &state) {
  switch (area) {
  case SM_AREA_STREET:
    sm_build_street(out, state);
    return;
  case SM_AREA_FACTORY:
    sm_build_factory(out, state);
    return;
  case SM_AREA_HOME:
  default:
    sm_build_home(out, state);
    return;
  }
}

// ── drawing
// ───────────────────────────────────────────────────────────────────

void sm_scene_render(const sm_scene &scene, sm_gfx &gfx,
                     const sm_assets &assets, const sm_countdown &state) {
  // The tier is the ONLY way the countdown reaches the world's colours, and it
  // is read here, once, from the countdown itself — never stored on the scene.
  const int tier = state.tier();

  if (scene.draws_sky)
    draw_sky(gfx, tier);

  for (const sm_surface &surface : scene.surfaces) {
    if (surface.texture == SM_TEX_COUNT) {
      gfx.quad_flat(surface.corners, surface.tint, surface.tess);
      continue;
    }
    const sm_texref tex = assets.ref(surface.texture, tier);
    if (tex.valid()) {
      gfx.quad(surface.corners, tex, surface.uv, surface.tint, surface.tess);
    } else {
      // A texture that is still being authored draws as flat colour rather
      // than as a hole (sm_assets.cpp: missing is counted, not fatal).
      gfx.quad_flat(surface.corners, surface.tint, surface.tess);
    }
  }

  for (const sm_billboard &board : scene.billboards) {
    if (board.texture == SM_TEX_COUNT)
      continue;
    const sm_texref tex = assets.ref(board.texture, tier);
    if (!tex.valid())
      continue;
    gfx.billboard(board.centre, board.half_width, board.half_height, tex,
                  board.uv, board.tint);
  }

  for (const sm_decal &decal : scene.decals) {
    if (decal.texture == SM_TEX_COUNT)
      continue;
    const sm_texref tex = assets.ref(decal.texture, tier);
    if (!tex.valid())
      continue;
    gfx.decal_ground(decal.centre, decal.half_size, decal.y, tex, decal.uv,
                     decal.tint);
  }

  // Last, and derived: which lamps burn is a pure function of the countdown,
  // read here by index. The scene stores a position and an index and nothing
  // else, so the board and the street cannot get out of sync.
  for (const sm_lamp &lamp : scene.lamps) {
    draw_lamp(gfx, assets, tier, lamp, state.lamp_lit(lamp.index));
  }
}

// ── collision
// ─────────────────────────────────────────────────────────────────

rv_pdklib::rv_vec3 sm_scene_slide(const sm_scene &scene,
                                  rv_pdklib::rv_vec3 from,
                                  rv_pdklib::rv_vec3 to, float radius) {
  // THE HIGHEST-CORRECTNESS FUNCTION IN THE DISC. It is the only thing standing
  // between the player and a softlock, so every path out of it is enumerated:
  // a NaN input, a degenerate radius, a circle that starts inside geometry, and
  // a corner the iteration fails to settle. None of them may return a bad
  // point and none of them may return "you cannot move".
  if (!finite3(to))
    return finite3(from) ? from : rv_pdklib::rv_vec3{0.0f, scene.floor_y, 0.0f};
  if (!finite3(from))
    from = to;
  if (!(radius > 0.0f))
    radius = 0.0f;

  rv_pdklib::rv_vec3 out = to;
  out.y = from.y; // collision is 2D: there is no jump, no crouch and no stairs.
  if (scene.colliders.empty() || radius <= 0.0f)
    return out;

  for (int pass = 0; pass < SM_SLIDE_PASSES; ++pass) {
    bool pushed = false;
    for (const sm_rect &rect : scene.colliders) {
      const float cx = clampf(out.x, rect.x0, rect.x1);
      const float cz = clampf(out.z, rect.z0, rect.z1);
      const float dx = out.x - cx;
      const float dz = out.z - cz;
      const float distance_sq = dx * dx + dz * dz;
      if (distance_sq >= radius * radius)
        continue;

      if (distance_sq > SM_SLIDE_EPSILON * SM_SLIDE_EPSILON) {
        // Outside the rectangle but overlapping it — push straight out
        // along the shortest line to the surface. Along a face that is
        // the face normal, and around a corner it is the radial
        // direction, which is what lets the circle round a door jamb.
        const float distance = std::sqrt(distance_sq);
        const float push = (radius - distance) / distance;
        out.x += dx * push;
        out.z += dz * push;
      } else {
        // The centre is inside the rectangle. Leave by the nearest face:
        // the axis of least penetration is the only exit that cannot
        // teleport the player across the geometry they are standing in.
        const float west = out.x - rect.x0 + radius;
        const float east = rect.x1 - out.x + radius;
        const float south = out.z - rect.z0 + radius;
        const float north = rect.z1 - out.z + radius;

        float best = west;
        int axis = 0;
        if (east < best) {
          best = east;
          axis = 1;
        }
        if (south < best) {
          best = south;
          axis = 2;
        }
        if (north < best) {
          axis = 3;
        }

        if (axis == 0)
          out.x = rect.x0 - radius;
        else if (axis == 1)
          out.x = rect.x1 + radius;
        else if (axis == 2)
          out.z = rect.z0 - radius;
        else
          out.z = rect.z1 + radius;
      }
      pushed = true;
    }
    if (!pushed)
      break;
  }

  if (!finite3(out))
    return from;

  // The escape hatch, stated as "never make it worse". Six passes settle every
  // corner the three levels contain, but a wedge narrower than the player —
  // two props 0.5 m apart — has no valid answer at all, and the resolver can
  // come out of one deeper than it went in. Refusing THIS step is survivable;
  // being left further inside a wall is not.
  //
  // Note what this deliberately does NOT do: if `from` is itself inside
  // geometry (a body pushed into a corner, a spawn that overlaps a prop) the
  // resolver's answer wins, because standing still there is exactly the
  // softlock the whole function exists to prevent. Every step out of a bad
  // position is an improvement, and there are always more steps.
  const float out_depth = penetration_depth(scene, out.x, out.z, radius);
  if (out_depth > SM_SLIDE_EPSILON) {
    if (penetration_depth(scene, from.x, from.z, radius) < out_depth)
      return from;
  }
  return out;
}

bool sm_scene_clear_line(const sm_scene &scene, rv_pdklib::rv_vec3 a,
                         rv_pdklib::rv_vec3 b) {
  if (!finite3(a) || !finite3(b))
    return false;

  const float dx = b.x - a.x;
  const float dz = b.z - a.z;

  for (const sm_rect &rect : scene.colliders) {
    // Slab test in the xz plane, over the parameter range [0, 1] of the
    // segment. A segment that starts inside the rectangle reports blocked,
    // which is the honest answer for a sight line drawn out of a wall.
    float t_enter = 0.0f;
    float t_exit = 1.0f;
    bool separated = false;

    if (dx > -1.0e-6f && dx < 1.0e-6f) {
      if (a.x < rect.x0 || a.x > rect.x1)
        separated = true;
    } else {
      float t0 = (rect.x0 - a.x) / dx;
      float t1 = (rect.x1 - a.x) / dx;
      if (t0 > t1) {
        const float t = t0;
        t0 = t1;
        t1 = t;
      }
      if (t0 > t_enter)
        t_enter = t0;
      if (t1 < t_exit)
        t_exit = t1;
    }

    if (!separated) {
      if (dz > -1.0e-6f && dz < 1.0e-6f) {
        if (a.z < rect.z0 || a.z > rect.z1)
          separated = true;
      } else {
        float t0 = (rect.z0 - a.z) / dz;
        float t1 = (rect.z1 - a.z) / dz;
        if (t0 > t1) {
          const float t = t0;
          t0 = t1;
          t1 = t;
        }
        if (t0 > t_enter)
          t_enter = t0;
        if (t1 < t_exit)
          t_exit = t1;
      }
    }

    if (!separated && t_enter <= t_exit)
      return false;
  }
  return true;
}

// ── queries
// ───────────────────────────────────────────────────────────────────

sm_trigger_id sm_scene_trigger_at(const sm_scene &scene,
                                  rv_pdklib::rv_vec3 position) {
  if (!finite3(position))
    return SM_TRIGGER_NONE;
  for (const sm_trigger &trigger : scene.triggers) {
    if (!trigger.active)
      continue;
    if (trigger.area.contains(position.x, position.z))
      return trigger.id;
  }
  return SM_TRIGGER_NONE;
}

int sm_scene_pick_interactable(const sm_scene &scene, rv_pdklib::rv_vec3 eye,
                               rv_pdklib::rv_vec3 forward, float reach) {
  if (!finite3(eye) || !finite3(forward))
    return -1;

  const rv_pdklib::rv_vec3 axis = rv_pdklib::rv_normalize(forward);
  if (rv_pdklib::rv_dot(axis, axis) < 0.5f)
    return -1; // a zero forward vector

  const float cos_limit = std::cos(SM_PICK_CONE_DEGREES * SM_PI_F / 180.0f);

  int best_index = -1;
  float best_score = 0.0f;

  const int count = static_cast<int>(scene.interactables.size());
  for (int i = 0; i < count; ++i) {
    const sm_interactable &item =
        scene.interactables[static_cast<std::size_t>(i)];
    if (!item.active || item.kind == SM_INTERACT_NONE)
      continue;

    const rv_pdklib::rv_vec3 to = item.position - eye;
    const float distance = rv_pdklib::rv_length(to);
    if (distance > reach + item.radius)
      continue;
    if (distance < 1.0e-3f)
      return i; // standing in it

    const float alignment = rv_pdklib::rv_dot(to, axis) / distance;
    if (alignment < cos_limit)
      continue;

    // Nearest wins, but being off-axis costs distance. The brick and the pipe
    // stand side by side by the front door on every single shift and the
    // player must get the one they are looking at, not the one that happens
    // to be two centimetres closer.
    const float score = (distance - item.radius) +
                        SM_PICK_ANGLE_WEIGHT * distance * (1.0f - alignment);
    if (best_index < 0 || score < best_score) {
      best_index = i;
      best_score = score;
    }
  }
  return best_index;
}

} // namespace solidmaid
