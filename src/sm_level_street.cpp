// Solidmaid — the street: courtyard to factory gate.
//
// Two modular chunks and ONE route, ~132 m of lane, reused on every shift. The
// geometry in this file never changes between shifts — not one quad. What
// changes is which of the five lampposts still burn, and that is derived by the
// renderer from the countdown, never stored here (docs/content.md).
//
// The route reads as one continuous corridor with no branches: courtyard ->
// between the blocks -> along the heating main -> the concrete fence -> the
// checkpoint. The player must never have to ask which way to go, and at
// ОСТАЛОСЬ: 1 they will be doing it in the dark, which is why the heating main
// exists as GEOMETRY rather than as dressing: it is a continuous handrail
// pointing at the factory, it arches over the path where you have to walk under
// it, and it works with every lamp out.
//
// THE PALETTE RULE, because this is the file that could break it: streetlights
// are cold mercury green-cyan and apartment windows are warm yellow, and THE
// TWO NEVER MIX (docs/art-and-audio.md). The lit windows below are the only
// warm colour on the street, and as the mercury goes out they become the last
// colour left in the town.
#include "sm_atlas.hpp"
#include "sm_scene.hpp"

namespace solidmaid {
namespace {

using rv_pdklib::rv_vec3;

constexpr rv_pdk::rv_color WHITE{255, 255, 255};

// ── the route
// ─────────────────────────────────────────────────────────────────

constexpr float A_Z0 = -6.0f;   // the player's own entrance, behind them
constexpr float SEAM_Z = 62.0f; // chunk A / chunk B, and L3
constexpr float B_Z1 = 126.0f;
constexpr float GATE_Z = 122.0f;

constexpr float A_HALF = 9.0f;  // the courtyard is wide
constexpr float B_LEFT = -5.5f; // the industrial approach is not
constexpr float B_RIGHT = 5.5f;
constexpr float ALCOVE_X = -7.0f; // one shallow recess, for encounter variety

constexpr float BLOCK_TOP = 15.0f; // five storeys of 3 m
constexpr float STOREY = 3.0f;
constexpr float FENCE_TOP = 2.40f;
constexpr float WIRE_TOP = 2.78f;

constexpr int A_SEGMENTS = 8;
constexpr float A_SEG = 8.5f; // (62 - -6) / 8
constexpr int B_SEGMENTS = 8;
constexpr float B_SEG = 8.0f; // (126 - 62) / 8

// The heating main: a 0.55 m casing on low supports, running at 1.65 m and
// climbing to 3.95 m where it crosses the lane.
constexpr float MAIN_LOW = 1.65f;
constexpr float MAIN_HIGH = 3.95f;
constexpr float MAIN_THICK = 0.55f;
constexpr float MAIN_LX0 = -3.90f;
constexpr float MAIN_LX1 = -3.35f;
constexpr float MAIN_RX0 = 4.65f;
constexpr float MAIN_RX1 = 5.20f;

constexpr float GROUND_TESS = 4.0f;
constexpr float FACADE_TESS = 3.5f;
constexpr float PROP_TESS = 4.0f;

// Corners straight into the surface list, for the few pieces that are neither
// axis-aligned boxes nor vertical walls — the heating main climbs, and add_box
// cannot express a slope.
void push_surface(sm_scene &out, rv_vec3 c0, rv_vec3 c1, rv_vec3 c2, rv_vec3 c3,
                  sm_tex_id tex, sm_uvrect uv, float tess) {
  sm_surface surface{};
  surface.corners[0] = c0;
  surface.corners[1] = c1;
  surface.corners[2] = c2;
  surface.corners[3] = c3;
  surface.texture = tex;
  surface.uv = uv;
  surface.tint = WHITE;
  surface.tess = tess;
  out.surfaces.push_back(surface);
}

// One run of the heating main between two z stations, climbing from `top0` to
// `top1`: the top, the underside you see when you walk beneath the arch, and
// both flanks.
void add_main_run(sm_scene &out, float x0, float x1, float z0, float top0,
                  float z1, float top1, bool solid) {
  const float b0 = top0 - MAIN_THICK;
  const float b1 = top1 - MAIN_THICK;

  push_surface(out, rv_vec3{x0, top1, z1}, rv_vec3{x1, top1, z1},
               rv_vec3{x0, top0, z0}, rv_vec3{x1, top0, z0},
               SM_TEX_STREET_PROPS, SM_UV_HEATING_MAIN, PROP_TESS);
  push_surface(out, rv_vec3{x0, b0, z0}, rv_vec3{x1, b0, z0},
               rv_vec3{x0, b1, z1}, rv_vec3{x1, b1, z1}, SM_TEX_STREET_PROPS,
               SM_UV_HEATING_MAIN, PROP_TESS);
  push_surface(out, rv_vec3{x0, top0, z0}, rv_vec3{x0, top1, z1},
               rv_vec3{x0, b0, z0}, rv_vec3{x0, b1, z1}, SM_TEX_STREET_PROPS,
               SM_UV_HEATING_MAIN, PROP_TESS);
  push_surface(out, rv_vec3{x1, top1, z1}, rv_vec3{x1, top0, z0},
               rv_vec3{x1, b1, z1}, rv_vec3{x1, b0, z0}, SM_TEX_STREET_PROPS,
               SM_UV_HEATING_MAIN, PROP_TESS);

  if (solid)
    out.add_collider(x0, z0, x1, z1);
}

// A prop that must read from every angle for four primitives: two cut-out cards
// crossed at right angles. The carpet-beating frame, the swings and the poplars
// are all silhouettes, and a silhouette is all the console can afford them.
void add_crossed(sm_scene &out, float cx, float cz, float half_width, float y0,
                 float y1, sm_uvrect uv) {
  out.add_wall(cx - half_width, cz, cx + half_width, cz, y0, y1,
               SM_TEX_STREET_PROPS, uv, WHITE, false, PROP_TESS);
  out.add_wall(cx, cz - half_width, cx, cz + half_width, y0, y1,
               SM_TEX_STREET_PROPS, uv, WHITE, false, PROP_TESS);
}

void add_ground_decal(sm_scene &out, float x, float z, float half,
                      sm_uvrect uv) {
  sm_decal decal{};
  decal.centre = rv_vec3{x, 0.0f, z};
  decal.half_size = half;
  decal.y = 0.0f;
  decal.texture = SM_TEX_STREET_GROUND;
  decal.uv = uv;
  decal.tint = WHITE;
  out.decals.push_back(decal);
}

void add_lamp(sm_scene &out, int index, float x, float z) {
  sm_lamp lamp{};
  lamp.base = rv_vec3{x, 0.0f, z};
  lamp.index = index;
  out.lamps.push_back(lamp);
  // The pole is a solid object at every tier, lit or dead. The town keeps its
  // posts and loses its light.
  out.add_collider(x - 0.18f, z - 0.18f, x + 0.18f, z + 0.18f);
}

} // namespace

void sm_build_street(sm_scene &out, const sm_countdown &state) {
  out.clear();
  out.area = SM_AREA_STREET;
  out.floor_y = 0.0f;
  out.ceiling_y = BLOCK_TOP;
  out.draws_sky = true;
  // Flat overcast is the ambient floor's justification: it is why the street is
  // never pitch black, on any tier, the final lap included.
  out.clear_colour = rv_pdk::rv_color{30, 34, 42};

  // Out of the entrance, facing straight down the lane. There is exactly one
  // direction to walk and the route never branches.
  out.player_start = rv_vec3{-1.0f, 0.0f, -3.0f};
  out.player_yaw = 0.0f;

  // ── chunk A: the courtyard and the blocks ─────────────────────────────────

  // Cracked asphalt patched with darker asphalt. Three strips per segment so no
  // one surface is long enough to swim, and so a segment under the camera can
  // never lose a whole corner to the near plane.
  for (int i = 0; i < A_SEGMENTS; ++i) {
    const float z0 = A_Z0 + A_SEG * static_cast<float>(i);
    const float z1 = z0 + A_SEG;
    const bool patched = (i % 3) == 1;
    const sm_uvrect verge = (i % 4) == 2 ? SM_UV_KERB : SM_UV_ASPHALT;
    out.add_floor(-A_HALF, z0, -3.5f, z1, 0.0f, SM_TEX_STREET_GROUND, verge,
                  WHITE, GROUND_TESS);
    out.add_floor(-3.5f, z0, 3.5f, z1, 0.0f, SM_TEX_STREET_GROUND,
                  patched ? SM_UV_ASPHALT_PATCH : SM_UV_ASPHALT, WHITE,
                  GROUND_TESS);
    out.add_floor(3.5f, z0, A_HALF, z1, 0.0f, SM_TEX_STREET_GROUND,
                  (i % 4) == 3 ? SM_UV_KERB : SM_UV_ASPHALT, WHITE,
                  GROUND_TESS);
  }

  // The raised concrete kerb, as the lip you actually see edge-on.
  out.add_wall(-3.5f, 2.5f, -3.5f, 11.0f, 0.0f, 0.13f, SM_TEX_STREET_GROUND,
               SM_UV_KERB, WHITE, false, PROP_TESS);
  out.add_wall(-3.5f, 11.0f, -3.5f, 19.5f, 0.0f, 0.13f, SM_TEX_STREET_GROUND,
               SM_UV_KERB, WHITE, false, PROP_TESS);
  out.add_wall(3.5f, 11.0f, 3.5f, 2.5f, 0.0f, 0.13f, SM_TEX_STREET_GROUND,
               SM_UV_KERB, WHITE, false, PROP_TESS);
  out.add_wall(3.5f, 19.5f, 3.5f, 11.0f, 0.0f, 0.13f, SM_TEX_STREET_GROUND,
               SM_UV_KERB, WHITE, false, PROP_TESS);

  // Standing water in the depressions, a manhole, mud at the verge, the faded
  // crossing. All ground decals: sm_gfx lifts them off the surface, so the
  // ordering table cannot flicker them against the asphalt.
  add_ground_decal(out, -1.20f, 20.0f, 0.50f, SM_UV_MANHOLE);
  add_ground_decal(out, 2.00f, 14.0f, 1.10f, SM_UV_PUDDLE);
  add_ground_decal(out, -1.60f, 41.0f, 1.20f, SM_UV_PUDDLE);
  add_ground_decal(out, -6.00f, 34.0f, 1.40f, SM_UV_MUD);
  add_ground_decal(out, 0.00f, 57.0f, 1.90f, SM_UV_CROSSING);

  // Five-storey panel blocks, series 1-464, both sides. One quad per storey per
  // segment: tessellation SUBDIVIDES a uv rectangle, it does not repeat it, so
  // the window rhythm has to be authored surface by surface.
  for (int side = 0; side < 2; ++side) {
    const float x = (side == 0) ? -A_HALF : A_HALF;
    for (int i = 0; i < A_SEGMENTS; ++i) {
      const float z0 = A_Z0 + A_SEG * static_cast<float>(i);
      const float z1 = z0 + A_SEG;
      // Wound so both sides face the lane; culling is off, but the split
      // diagonal still runs the short way across each surface.
      const float a = (side == 0) ? z0 : z1;
      const float b = (side == 0) ? z1 : z0;

      out.add_wall(x, a, x, b, 0.0f, STOREY, SM_TEX_STREET_FACADE,
                   ((i + side) % 3) == 0 ? SM_UV_PANEL_RUST : SM_UV_PANEL,
                   WHITE, false, FACADE_TESS);
      for (int storey = 1; storey < 5; ++storey) {
        const float y0 = STOREY * static_cast<float>(storey);
        out.add_wall(x, a, x, b, y0, y0 + STOREY, SM_TEX_STREET_FACADE,
                     SM_UV_WINDOWS_DARK, WHITE, false, FACADE_TESS);
      }
      out.add_wall(x, a, x, b, BLOCK_TOP, BLOCK_TOP + 0.8f,
                   SM_TEX_STREET_FACADE, SM_UV_ROOF_EDGE, WHITE, false,
                   FACADE_TESS);
    }
  }
  out.add_collider(-A_HALF - 0.4f, A_Z0, -A_HALF, SEAM_Z);
  out.add_collider(A_HALF, A_Z0, A_HALF + 0.4f, SEAM_Z);

  // The player's own block, closing the courtyard behind them, with the
  // подъезд they just came out of.
  for (int half = 0; half < 2; ++half) {
    const float x0 = (half == 0) ? -A_HALF : 0.0f;
    const float x1 = (half == 0) ? 0.0f : A_HALF;
    out.add_wall(x0, A_Z0, x1, A_Z0, 0.0f, STOREY, SM_TEX_STREET_FACADE,
                 SM_UV_PANEL, WHITE, false, FACADE_TESS);
    out.add_wall(x0, A_Z0, x1, A_Z0, STOREY, STOREY * 3.0f,
                 SM_TEX_STREET_FACADE, SM_UV_WINDOWS_DARK, WHITE, false,
                 FACADE_TESS);
    out.add_wall(x0, A_Z0, x1, A_Z0, STOREY * 3.0f, BLOCK_TOP,
                 SM_TEX_STREET_FACADE, SM_UV_WINDOWS_DARK, WHITE, false,
                 FACADE_TESS);
  }
  out.add_collider(-A_HALF, A_Z0 - 0.4f, A_HALF, A_Z0);
  out.add_wall(-2.6f, A_Z0 + 0.06f, -0.4f, A_Z0 + 0.06f, 0.0f, 3.0f,
               SM_TEX_STREET_FACADE, SM_UV_ENTRANCE, WHITE, false, PROP_TESS);
  out.add_floor(-3.0f, A_Z0, -0.1f, A_Z0 + 1.4f, 3.0f, SM_TEX_STREET_FACADE,
                SM_UV_PANEL, WHITE, PROP_TESS);

  // Entrances along the lane: concrete canopy, sprung door, number plate.
  {
    const float entrance_z[4] = {6.0f, 38.0f, 16.0f, 46.0f};
    for (int i = 0; i < 4; ++i) {
      const bool left = i < 2;
      const float x = left ? -A_HALF + 0.06f : A_HALF - 0.06f;
      const float z0 = entrance_z[i];
      out.add_wall(x, z0, x, z0 + 2.2f, 0.0f, 3.0f, SM_TEX_STREET_FACADE,
                   SM_UV_ENTRANCE, WHITE, false, PROP_TESS);
      const float cx0 = left ? -A_HALF : A_HALF - 1.5f;
      out.add_floor(cx0, z0 - 0.4f, cx0 + 1.5f, z0 + 2.6f, 3.0f,
                    SM_TEX_STREET_FACADE, SM_UV_PANEL, WHITE, PROP_TESS);
    }
  }
  out.add_wall(-A_HALF + 0.08f, 9.4f, -A_HALF + 0.08f, 10.8f, 2.6f, 3.1f,
               SM_TEX_STREET_FACADE, SM_UV_STREET_SIGN, WHITE, false,
               PROP_TESS);

  // Balconies, glazed inconsistently by each owner.
  {
    const float bx[6] = {-A_HALF + 0.08f, -A_HALF + 0.08f, -A_HALF + 0.08f,
                         A_HALF - 0.08f,  A_HALF - 0.08f,  A_HALF - 0.08f};
    const float bz[6] = {12.0f, 27.0f, 43.0f, 8.0f, 30.0f, 51.0f};
    const float by[6] = {3.4f, 9.4f, 6.4f, 6.4f, 12.4f, 3.4f};
    for (int i = 0; i < 6; ++i) {
      out.add_wall(bx[i], bz[i], bx[i], bz[i] + 1.8f, by[i], by[i] + 1.5f,
                   SM_TEX_STREET_FACADE,
                   (i % 2) == 0 ? SM_UV_BALCONY_OPEN : SM_UV_BALCONY_GLAZED,
                   WHITE, false, PROP_TESS);
    }
  }

  // The lit windows. Warm yellow, authored one by one, and deliberately few:
  // as the mercury dies these are the main remaining source of colour, and then
  // they thin out too.
  {
    const float wx[12] = {-A_HALF + 0.09f, -A_HALF + 0.09f, -A_HALF + 0.09f,
                          -A_HALF + 0.09f, -A_HALF + 0.09f, -A_HALF + 0.09f,
                          A_HALF - 0.09f,  A_HALF - 0.09f,  A_HALF - 0.09f,
                          A_HALF - 0.09f,  A_HALF - 0.09f,  A_HALF - 0.09f};
    const float wz[12] = {0.5f, 9.0f,  18.0f, 31.0f, 44.0f, 52.5f,
                          3.0f, 13.5f, 25.0f, 36.0f, 48.0f, 57.5f};
    const float wy[12] = {4.6f, 10.6f, 7.6f,  4.6f, 13.0f, 7.6f,
                          7.6f, 4.6f,  10.6f, 4.6f, 7.6f,  10.6f};
    for (int i = 0; i < 12; ++i) {
      out.add_wall(wx[i], wz[i], wx[i], wz[i] + 1.5f, wy[i], wy[i] + 1.6f,
                   SM_TEX_STREET_FACADE, SM_UV_WINDOWS_LIT, WHITE, false,
                   PROP_TESS);
    }
  }

  // The carpet-beating frame. A bare steel pipe rectangle on two legs, and the
  // single most identifying object of the setting — it is here because it is
  // the best silhouette landmark in the game and it works with no light at all.
  add_crossed(out, -5.60f, 15.0f, 1.35f, 0.0f, 2.20f, SM_UV_CARPET_FRAME);
  out.add_collider(-6.95f, 14.85f, -6.75f, 15.15f);
  out.add_collider(-4.45f, 14.85f, -4.25f, 15.15f);

  add_crossed(out, -6.40f, 25.5f, 1.30f, 0.0f, 2.40f, SM_UV_SWINGS);
  out.add_collider(-7.70f, 25.35f, -7.50f, 25.65f);
  out.add_collider(-5.30f, 25.35f, -5.10f, 25.65f);

  // Three bare poplars. Late October: no leaves.
  add_crossed(out, -7.60f, 11.0f, 1.40f, 0.0f, 9.0f, SM_UV_POPLAR);
  add_crossed(out, 7.40f, 24.0f, 1.40f, 0.0f, 9.0f, SM_UV_POPLAR);
  add_crossed(out, -7.40f, 44.0f, 1.40f, 0.0f, 9.0f, SM_UV_POPLAR);
  out.add_collider(-7.72f, 10.88f, -7.48f, 11.12f);
  out.add_collider(7.28f, 23.88f, 7.52f, 24.12f);
  out.add_collider(-7.52f, 43.88f, -7.28f, 44.12f);

  out.add_box(-8.70f, 6.40f, -8.00f, 8.00f, 0.0f, 0.45f, SM_TEX_STREET_PROPS,
              SM_UV_BENCH, WHITE, true, PROP_TESS);
  out.add_box(8.00f, 16.40f, 8.70f, 18.00f, 0.0f, 0.45f, SM_TEX_STREET_PROPS,
              SM_UV_BENCH, WHITE, true, PROP_TESS);
  out.add_box(-8.60f, 9.00f, -8.05f, 9.55f, 0.0f, 0.85f, SM_TEX_STREET_PROPS,
              SM_UV_BIN, WHITE, true, PROP_TESS);
  out.add_box(8.10f, 19.00f, 8.65f, 19.55f, 0.0f, 0.85f, SM_TEX_STREET_PROPS,
              SM_UV_BIN, WHITE, true, PROP_TESS);
  // A sandpit with no sand.
  out.add_box(-8.40f, 27.00f, -6.00f, 29.40f, 0.0f, 0.22f, SM_TEX_STREET_GROUND,
              SM_UV_MUD, WHITE, false, PROP_TESS);

  // ── the seam ──────────────────────────────────────────────────────────────
  //
  // The courtyard closes down into the industrial lane. L3 stands here, and
  // docs/content.md calls this "the route's clearest landmark".
  out.add_wall(-A_HALF, SEAM_Z, B_LEFT, SEAM_Z, 0.0f, 4.5f,
               SM_TEX_STREET_FACADE, SM_UV_PANEL, WHITE, true, FACADE_TESS);
  out.add_wall(B_RIGHT, SEAM_Z, A_HALF, SEAM_Z, 0.0f, 4.5f,
               SM_TEX_STREET_FACADE, SM_UV_PANEL, WHITE, true, FACADE_TESS);

  // ── chunk B: the industrial approach ──────────────────────────────────────

  for (int i = 0; i < B_SEGMENTS; ++i) {
    const float z0 = SEAM_Z + B_SEG * static_cast<float>(i);
    const float z1 = z0 + B_SEG;
    out.add_floor(ALCOVE_X - 0.4f, z0, 0.0f, z1, 0.0f, SM_TEX_STREET_GROUND,
                  (i % 3) == 2 ? SM_UV_ASPHALT_PATCH : SM_UV_ASPHALT, WHITE,
                  GROUND_TESS);
    out.add_floor(0.0f, z0, B_RIGHT + 1.1f, z1, 0.0f, SM_TEX_STREET_GROUND,
                  (i % 3) == 0 ? SM_UV_ASPHALT_PATCH : SM_UV_ASPHALT, WHITE,
                  GROUND_TESS);
  }
  add_ground_decal(out, 1.40f, 78.0f, 1.20f, SM_UV_PUDDLE);
  add_ground_decal(out, -4.60f, 105.0f, 1.50f, SM_UV_MUD);
  add_ground_decal(out, 2.60f, 112.0f, 0.50f, SM_UV_MANHOLE);

  // Left: a short run of fence, the alcove, the garages, then fence again.
  out.add_wall(B_LEFT, SEAM_Z, B_LEFT, 68.0f, 0.0f, FENCE_TOP,
               SM_TEX_STREET_PROPS, SM_UV_FENCE_PO2, WHITE, true, PROP_TESS);
  out.add_wall(ALCOVE_X, 68.0f, ALCOVE_X, 75.0f, 0.0f, FENCE_TOP,
               SM_TEX_STREET_PROPS, SM_UV_FENCE_PO2, WHITE, true, PROP_TESS);
  out.add_wall(ALCOVE_X, 68.0f, B_LEFT, 68.0f, 0.0f, FENCE_TOP,
               SM_TEX_STREET_PROPS, SM_UV_FENCE_PO2, WHITE, true, PROP_TESS);

  // Metal garages, corrugated, painted whatever paint was available.
  for (int i = 0; i < 3; ++i) {
    const float z0 = 75.0f + 7.0f * static_cast<float>(i);
    const float z1 = z0 + 7.0f;
    out.add_wall(B_LEFT, z0, B_LEFT, z1, 0.0f, FENCE_TOP, SM_TEX_STREET_PROPS,
                 SM_UV_GARAGE, WHITE, true, PROP_TESS);
    out.add_floor(B_LEFT - 3.5f, z0, B_LEFT, z1, FENCE_TOP, SM_TEX_STREET_PROPS,
                  SM_UV_GARAGE, WHITE, PROP_TESS);
  }
  out.add_wall(ALCOVE_X, 75.0f, B_LEFT, 75.0f, 0.0f, FENCE_TOP,
               SM_TEX_STREET_PROPS, SM_UV_GARAGE, WHITE, true, PROP_TESS);
  out.add_wall(B_LEFT - 3.5f, 96.0f, B_LEFT, 96.0f, 0.0f, FENCE_TOP,
               SM_TEX_STREET_PROPS, SM_UV_GARAGE, WHITE, true, PROP_TESS);

  for (int i = 0; i < 4; ++i) {
    const float z0 = 96.0f + 6.5f * static_cast<float>(i);
    const float z1 = z0 + 6.5f;
    out.add_wall(B_LEFT, z0, B_LEFT, z1, 0.0f, FENCE_TOP, SM_TEX_STREET_PROPS,
                 SM_UV_FENCE_PO2, WHITE, true, PROP_TESS);
    out.add_wall(B_LEFT, z0, B_LEFT, z1, FENCE_TOP, WIRE_TOP,
                 SM_TEX_STREET_PROPS, SM_UV_BARBED_WIRE, WHITE, false,
                 PROP_TESS);
  }

  // Right: the PO-2 concrete fence the whole way, with one narrow gap you can
  // see the works through and cannot walk through.
  for (int i = 0; i < 8; ++i) {
    const float z0 = SEAM_Z + 7.5f * static_cast<float>(i);
    const float z1 = z0 + 7.5f;
    if (i == 4) {
      out.add_wall(B_RIGHT, z0, B_RIGHT, z0 + 4.2f, 0.0f, FENCE_TOP,
                   SM_TEX_STREET_PROPS, SM_UV_FENCE_PO2, WHITE, true,
                   PROP_TESS);
      out.add_wall(B_RIGHT, z0 + 4.75f, B_RIGHT, z1, 0.0f, FENCE_TOP,
                   SM_TEX_STREET_PROPS, SM_UV_FENCE_PO2, WHITE, true,
                   PROP_TESS);
    } else {
      out.add_wall(B_RIGHT, z0, B_RIGHT, z1, 0.0f, FENCE_TOP,
                   SM_TEX_STREET_PROPS, SM_UV_FENCE_PO2, WHITE, true,
                   PROP_TESS);
    }
    out.add_wall(B_RIGHT, z0, B_RIGHT, z1, FENCE_TOP, WIRE_TOP,
                 SM_TEX_STREET_PROPS, SM_UV_BARBED_WIRE, WHITE, false,
                 PROP_TESS);
  }
  // The works behind the gap.
  out.add_wall(9.0f, 88.0f, 9.0f, 104.0f, 0.0f, 9.0f, SM_TEX_STREET_FACADE,
               SM_UV_PANEL, WHITE, false, FACADE_TESS);

  // The kiosk. Closed on every shift.
  out.add_box(3.60f, 70.0f, 5.35f, 72.2f, 0.0f, 2.60f, SM_TEX_STREET_PROPS,
              SM_UV_KIOSK, WHITE, true, PROP_TESS);

  // ── the heating main ──────────────────────────────────────────────────────
  //
  // The route's spine. It runs the left flank at hip-to-shoulder height,
  // climbs, crosses the lane in a shallow arch the player walks under, and
  // continues on the right flank all the way to the gate. Wherever the player
  // is in chunk B, following it is the correct answer, and it does not need a
  // lamppost.
  add_main_run(out, MAIN_LX0, MAIN_LX1, SEAM_Z, MAIN_LOW, 70.0f, MAIN_LOW,
               true);
  add_main_run(out, MAIN_LX0, MAIN_LX1, 70.0f, MAIN_LOW, 78.0f, MAIN_LOW, true);
  add_main_run(out, MAIN_LX0, MAIN_LX1, 78.0f, MAIN_LOW, 84.0f, MAIN_LOW, true);
  add_main_run(out, MAIN_LX0, MAIN_LX1, 84.0f, MAIN_LOW, 87.6f, MAIN_HIGH,
               true);
  // The crossing itself, overhead: no collider, you walk under it.
  out.add_box(MAIN_LX0, 87.6f, MAIN_RX1, 88.15f, MAIN_HIGH - MAIN_THICK,
              MAIN_HIGH, SM_TEX_STREET_PROPS, SM_UV_HEATING_MAIN, WHITE, false,
              PROP_TESS);
  out.add_ceiling(MAIN_LX0, 87.6f, MAIN_RX1, 88.15f, MAIN_HIGH - MAIN_THICK,
                  SM_TEX_STREET_PROPS, SM_UV_HEATING_MAIN, WHITE, PROP_TESS);
  add_main_run(out, MAIN_RX0, MAIN_RX1, 88.15f, MAIN_HIGH, 92.0f, MAIN_LOW,
               true);
  add_main_run(out, MAIN_RX0, MAIN_RX1, 92.0f, MAIN_LOW, 100.0f, MAIN_LOW,
               true);
  add_main_run(out, MAIN_RX0, MAIN_RX1, 100.0f, MAIN_LOW, 108.0f, MAIN_LOW,
               true);
  add_main_run(out, MAIN_RX0, MAIN_RX1, 108.0f, MAIN_LOW, 116.0f, MAIN_LOW,
               true);
  add_main_run(out, MAIN_RX0, MAIN_RX1, 116.0f, MAIN_LOW, GATE_Z, MAIN_LOW,
               true);

  // Low concrete supports under it.
  {
    const float support_z[9] = {64.0f,  72.0f,  80.0f,  94.0f, 101.0f,
                                108.0f, 114.0f, 119.0f, 86.0f};
    for (int i = 0; i < 9; ++i) {
      const bool left = support_z[i] < 87.0f;
      const float x0 = left ? MAIN_LX0 + 0.05f : MAIN_RX0 + 0.05f;
      out.add_wall(x0, support_z[i] - 0.30f, x0, support_z[i] + 0.30f, 0.0f,
                   MAIN_LOW - 0.55f, SM_TEX_STREET_PROPS, SM_UV_HEATING_SUPPORT,
                   WHITE, false, PROP_TESS);
    }
  }

  // ── the checkpoint (проходная) ────────────────────────────────────────────
  out.add_box(-6.50f, GATE_Z, -1.20f, 128.0f, 0.0f, 4.20f, SM_TEX_GATE,
              SM_UV_CHECKPOINT_BRICK, WHITE, true, FACADE_TESS);
  out.add_wall(-3.60f, GATE_Z - 0.05f, -2.60f, GATE_Z - 0.05f, 0.0f, 2.10f,
               SM_TEX_GATE, SM_UV_CHECKPOINT_DOOR, WHITE, false, PROP_TESS);
  out.add_wall(-2.30f, GATE_Z - 0.05f, -1.50f, GATE_Z - 0.05f, 1.20f, 2.00f,
               SM_TEX_GATE, SM_UV_CHECKPOINT_WINDOW, WHITE, false, PROP_TESS);
  out.add_wall(-5.80f, GATE_Z - 0.05f, -3.90f, GATE_Z - 0.05f, 2.60f, 3.30f,
               SM_TEX_GATE, SM_UV_GATE_SIGN, WHITE, false, PROP_TESS);
  out.add_wall(-1.20f, GATE_Z - 0.12f, -0.20f, GATE_Z - 0.12f, 0.0f, 1.20f,
               SM_TEX_GATE, SM_UV_TURNSTILE, WHITE, false, PROP_TESS);
  out.add_wall(-1.20f, GATE_Z, 2.20f, GATE_Z, 0.0f, 2.80f, SM_TEX_GATE,
               SM_UV_GATE_STEEL, WHITE, true, PROP_TESS);
  out.add_wall(2.20f, GATE_Z, 5.60f, GATE_Z, 0.0f, 2.80f, SM_TEX_GATE,
               SM_UV_GATE_STEEL, WHITE, true, PROP_TESS);
  // The works behind the gate, so the lane does not end in open sky.
  out.add_wall(-A_HALF, 129.0f, 0.0f, 129.0f, 0.0f, 11.0f, SM_TEX_FACTORY_WALLS,
               SM_UV_BRICK_WHITEWASH, WHITE, false, FACADE_TESS);
  out.add_wall(0.0f, 129.0f, A_HALF, 129.0f, 0.0f, 11.0f, SM_TEX_FACTORY_WALLS,
               SM_UV_BRICK_WHITEWASH, WHITE, false, FACADE_TESS);
  out.add_wall(B_RIGHT + 1.1f, GATE_Z, B_RIGHT + 1.1f, B_Z1, 0.0f, 4.0f,
               SM_TEX_STREET_PROPS, SM_UV_FENCE_PO2, WHITE, true, PROP_TESS);

  // ── the five lampposts ────────────────────────────────────────────────────
  //
  // Authored order, read from the courtyard outward. L1 is his own doorway and
  // goes first; L5 hangs over the gate and is the last lamp burning in the
  // town. Which of them are lit is NEVER stored — sm_scene_render asks the
  // countdown, by index.
  add_lamp(out, 0, -3.60f, 5.00f);   // L1 — courtyard, outside the entrance
  add_lamp(out, 1, 3.60f, 30.00f);   // L2 — mid chunk A
  add_lamp(out, 2, -3.70f, 60.00f);  // L3 — the seam
  add_lamp(out, 3, -4.80f, 101.00f); // L4 — mid chunk B
  add_lamp(out, 4, -2.40f, 118.50f); // L5 — over the factory gate

  // ── encounters ────────────────────────────────────────────────────────────
  //
  // Off the centre line, in the alcove and behind the props, so a fight starts
  // beside the player rather than in front of them. Which of these are used and
  // when is sm_encounters' business; this file only says where a body can
  // legally stand.
  {
    const float sx[12] = {-7.20f, 7.30f, -7.50f, 6.90f, -6.50f, 7.10f,
                          -6.30f, 4.20f, -4.60f, 3.80f, -4.70f, 3.60f};
    const float sz[12] = {12.0f, 19.5f, 31.0f, 36.5f, 47.0f,  55.0f,
                          71.5f, 78.0f, 88.0f, 99.0f, 109.0f, 116.0f};
    for (int i = 0; i < 12; ++i)
      out.enemy_spawns.push_back(rv_vec3{sx[i], 0.0f, sz[i]});
  }

  // Spare bricks along the route. The brick is never a limited resource — the
  // world always has more (docs/mechanics.md).
  {
    const float bx[5] = {-2.60f, 2.90f, -2.20f, 2.60f, -2.80f};
    const float bz[5] = {16.0f, 40.0f, 64.0f, 84.0f, 106.0f};
    for (int i = 0; i < 5; ++i) {
      out.brick_spawns.push_back(rv_vec3{bx[i], 0.16f, bz[i]});
    }
  }

  // ── the way in ────────────────────────────────────────────────────────────
  {
    sm_trigger enter{};
    enter.area.x0 = -1.10f;
    enter.area.z0 = 119.60f;
    enter.area.x1 = 5.50f;
    enter.area.z1 = 121.80f;
    enter.id = SM_TRIGGER_ENTER_FACTORY;
    enter.active = true;
    out.triggers.push_back(enter);
  }

  // The final lap walks the identical route with every lamp dead. Nothing in
  // the geometry knows about it, which is the point: the town is not hostile,
  // it is finished.
  (void)state;
}

} // namespace solidmaid
