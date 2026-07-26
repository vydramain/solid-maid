// Solidmaid — the assembly shop.
//
// One bay (пролёт), 24 m across by 14 m deep, CEILING 7 m. That last number is
// the whole point of the file: the player steps out of a 2.50 m box into
// volume, once per shift, and docs/environments.md calls it "the deliberate
// opposite of the apartment... the only time the game gives them air". The
// columns, the trusses and the crane rail all exist to make 7 m legible from
// the floor.
//
// WHY THE BAY IS WIDE RATHER THAN LONG. The doorway is at z = 0 and THE BOARD
// is on the far wall at z = 14, above the conveyor, facing the entrance — "the
// player sees it the moment they walk in, every shift, without being directed
// to look". A 24 m-deep hall would put the board 24 m from the door, where a
// 0.42 m glyph is four pixels tall on a 240-line screen and the only number in
// the game is unreadable. At 13 m it lands at roughly one screen pixel per font
// texel, which is the whole reason the board is drawn as text rather than as a
// texture.
//
// The yellow painted floor lines run the length of the walkway and are the
// game's clearest readability aid. Their bright cores are drawn as UNTEXTURED
// flat colour — the one thing on this disc a palette tier cannot touch — so
// they read identically on shift 5 and on the final lap.
#include "sm_atlas.hpp"
#include "sm_scene.hpp"

namespace solidmaid {
namespace {

using rv_pdklib::rv_vec3;

constexpr rv_pdk::rv_color WHITE{255, 255, 255};
// The painted walkway. Untextured, so it never passes through a tier palette.
constexpr rv_pdk::rv_color LINE_YELLOW{214, 186, 52};

constexpr float HALL_X = 12.0f; // 24 m across
constexpr float HALL_Z0 = 0.0f;
constexpr float HALL_Z1 = 14.0f;     // 14 m deep
constexpr float COLUMN_HALF = 0.30f; // half the precast column's square section
constexpr float COLUMN_BAND_TOP = 1.15f; // where the painted chevron stops
constexpr float HALL_TOP = 7.0f;         // see the file header
constexpr float PAINT_LINE =
    1.80f; // dull green oil paint to here, whitewash above

constexpr float DOOR_HALF = 1.10f;
constexpr float DOOR_TOP = 2.40f;
constexpr float PORCH_Z = -0.60f;

constexpr float WALK_HALF = 1.16f;  // inside edge of the painted lines
constexpr float LINE_WIDTH = 0.14f; // the bright core

constexpr float BOARD_HALF_W = 2.60f;
constexpr float BOARD_Y0 = 3.55f;
constexpr float BOARD_Y1 = 5.05f;

constexpr float WALL_TESS = 3.5f;
constexpr float FLOOR_TESS = 3.0f;
constexpr float PROP_TESS = 4.0f;

// The assembly bench, which everything else in the hall is placed around: the
// escalation wave has to ARRIVE, so no spawn may sit within
// SM_SPAWN_MIN_DISTANCE of it.
constexpr float BENCH_X0 = -8.40f;
constexpr float BENCH_X1 = -5.60f;
constexpr float BENCH_Z0 = 3.40f;
constexpr float BENCH_Z1 = 4.80f;

} // namespace

// ── THE BOARD'S WORLD TRANSFORM
// ───────────────────────────────────────────────
//
// Deliberately declared in NO header: the integrator writes the extern where it
// draws, so nothing else in the disc can quietly start depending on where the
// board hangs.
//
//   extern const rv_pdklib::rv_vec3 sm_factory_board_text_origin;
//   extern const rv_pdklib::rv_vec3 sm_factory_board_plan_origin;
//   extern const rv_pdklib::rv_vec3 sm_factory_board_text_right;
//   extern const rv_pdklib::rv_vec3 sm_factory_board_text_down;
//
//   sm_text_draw_world(gfx, assets, sm_factory_board_text_origin,
//                      sm_factory_board_text_right, sm_factory_board_text_down,
//                      "ОСТАЛОСЬ: 5", ink);
//
// The panel faces the entrance — its outward normal is -z — and a camera
// looking down the hall at yaw 0 has +x on its screen-right, so `right` is +x
// and `down` is -y with no rotation anywhere. One glyph cell is 0.38 x 0.42 m.
//
//   origin      (-2.09, 4.82, 13.90)   top-left of glyph 1, line 1, 11 glyphs
//   plan origin (-2.47, 4.20, 13.90)   top-left of glyph 1, line 2, 13 glyphs
//   right       ( 0.38, 0.00,  0.00)   one glyph column
//   down        ( 0.00,-0.42,  0.00)   one glyph row
//
// Both are centred on the 5.20 m panel, which spans x -2.60..2.60 and
// y 3.55..5.05 at z = 13.94; the text sits 0.04 m proud of it so the ordering
// table cannot swallow it. "ОСТАЛОСЬ: N" is eleven glyphs and "ПЛАН ВЫПОЛНЕН"
// is thirteen; for any other string call sm_factory_board_text_origin_for(),
// which recentres on the same panel and the same two baselines.
extern const rv_pdklib::rv_vec3 sm_factory_board_text_origin;
extern const rv_pdklib::rv_vec3 sm_factory_board_plan_origin;
extern const rv_pdklib::rv_vec3 sm_factory_board_text_right;
extern const rv_pdklib::rv_vec3 sm_factory_board_text_down;

const rv_pdklib::rv_vec3 sm_factory_board_text_origin{-2.09f, 4.82f, 13.90f};
const rv_pdklib::rv_vec3 sm_factory_board_plan_origin{-2.47f, 4.20f, 13.90f};
const rv_pdklib::rv_vec3 sm_factory_board_text_right{0.38f, 0.0f, 0.0f};
const rv_pdklib::rv_vec3 sm_factory_board_text_down{0.0f, -0.42f, 0.0f};

// `line` 0 is the header-and-digit row, 1 is the row ПЛАН ВЫПОЛНЕН resolves on.
rv_pdklib::rv_vec3 sm_factory_board_text_origin_for(int glyph_count, int line) {
  if (glyph_count < 1)
    glyph_count = 1;
  const float width =
      sm_factory_board_text_right.x * static_cast<float>(glyph_count);
  const float y = (line <= 0) ? sm_factory_board_text_origin.y
                              : sm_factory_board_plan_origin.y;
  return rv_pdklib::rv_vec3{-0.5f * width, y, sm_factory_board_text_origin.z};
}

void sm_build_factory(sm_scene &out, const sm_countdown &state) {
  out.clear();
  out.area = SM_AREA_FACTORY;
  out.floor_y = 0.0f;
  out.ceiling_y = HALL_TOP;
  // Doorway to board is 14 m; the bay's diagonal is about 28 m.
  out.far_plane = 34.0f;
  out.draws_sky = false;
  // The shop is always the best-lit space in the game and it dims LESS per tier
  // than the street does: the factory keeps its light while the town loses it.
  out.clear_colour = rv_pdk::rv_color{38, 40, 42};

  // In the doorway, facing the board.
  out.player_start = rv_vec3{0.0f, 0.0f, 1.60f};
  out.player_yaw = 0.0f;

  // ── floor ─────────────────────────────────────────────────────────────────
  //
  // Poured concrete, oil-stained, with the walkway boxed out in yellow. Cut
  // into 3.5 m bands and never wider than about 6 m per quad, so nothing long
  // enough to swim under affine mapping is authored and no single surface can
  // lose a whole corner to the near plane.
  for (int i = 0; i < 4; ++i) {
    const float z0 = 3.5f * static_cast<float>(i);
    const float z1 = z0 + 3.5f;
    const sm_uvrect a = (i % 2) == 0 ? SM_UV_CONCRETE : SM_UV_CONCRETE_OIL;
    const sm_uvrect b = (i % 2) == 0 ? SM_UV_CONCRETE_OIL : SM_UV_CONCRETE;

    out.add_floor(-HALL_X, z0, -6.0f, z1, 0.0f, SM_TEX_FACTORY_FLOOR, a, WHITE,
                  FLOOR_TESS);
    out.add_floor(-6.0f, z0, -WALK_HALF - LINE_WIDTH, z1, 0.0f,
                  SM_TEX_FACTORY_FLOOR, b, WHITE, FLOOR_TESS);
    out.add_floor(WALK_HALF + LINE_WIDTH, z0, 6.0f, z1, 0.0f,
                  SM_TEX_FACTORY_FLOOR, b, WHITE, FLOOR_TESS);
    out.add_floor(6.0f, z0, HALL_X, z1, 0.0f, SM_TEX_FACTORY_FLOOR, a, WHITE,
                  FLOOR_TESS);
    out.add_floor(-WALK_HALF, z0, WALK_HALF, z1, 0.0f, SM_TEX_FACTORY_FLOOR,
                  SM_UV_FLOOR_LINE, WHITE, FLOOR_TESS);
    // The two bright cores. Untextured is not a shortcut here, it is the
    // requirement: a flat-coloured primitive never passes through a palette,
    // so these are exactly as readable at ОСТАЛОСЬ: 0 as at ОСТАЛОСЬ: 5.
    out.add_floor(-WALK_HALF - LINE_WIDTH, z0, -WALK_HALF, z1, 0.01f,
                  SM_TEX_COUNT, sm_uvrect{}, LINE_YELLOW, FLOOR_TESS);
    out.add_floor(WALK_HALF, z0, WALK_HALF + LINE_WIDTH, z1, 0.01f,
                  SM_TEX_COUNT, sm_uvrect{}, LINE_YELLOW, FLOOR_TESS);
  }

  // Yellow boxes around the machine areas, and coolant where it always is.
  {
    const float dx[4] = {-7.00f, 4.60f, -7.00f, 9.90f};
    const float dz[4] = {4.10f, 5.60f, 6.60f, 6.30f};
    const float dh[4] = {2.10f, 1.70f, 0.70f, 0.55f};
    const sm_uvrect du[4] = {SM_UV_FLOOR_BOX, SM_UV_FLOOR_BOX, SM_UV_COOLANT,
                             SM_UV_COOLANT};
    for (int i = 0; i < 4; ++i) {
      sm_decal decal{};
      decal.centre = rv_vec3{dx[i], 0.0f, dz[i]};
      decal.half_size = dh[i];
      decal.y = 0.0f;
      decal.texture = SM_TEX_FACTORY_FLOOR;
      decal.uv = du[i];
      decal.tint = WHITE;
      out.decals.push_back(decal);
    }
  }

  // ── walls ─────────────────────────────────────────────────────────────────
  //
  // Silicate brick: dull green oil paint to 1.80 m with a painted line,
  // flaking whitewash above. Grime rings around every bracket.
  for (int i = 0; i < 2; ++i) {
    const float z0 = 7.0f * static_cast<float>(i);
    const float z1 = z0 + 7.0f;
    out.add_wall(-HALL_X, z0, -HALL_X, z1, 0.0f, PAINT_LINE,
                 SM_TEX_FACTORY_WALLS, SM_UV_BRICK_PAINTED, WHITE, false,
                 WALL_TESS);
    out.add_wall(-HALL_X, z0, -HALL_X, z1, PAINT_LINE, HALL_TOP,
                 SM_TEX_FACTORY_WALLS, SM_UV_BRICK_WHITEWASH, WHITE, false,
                 WALL_TESS);
    out.add_wall(HALL_X, z1, HALL_X, z0, 0.0f, PAINT_LINE, SM_TEX_FACTORY_WALLS,
                 SM_UV_BRICK_PAINTED, WHITE, false, WALL_TESS);
    out.add_wall(HALL_X, z1, HALL_X, z0, PAINT_LINE, HALL_TOP,
                 SM_TEX_FACTORY_WALLS, SM_UV_BRICK_WHITEWASH, WHITE, false,
                 WALL_TESS);
  }
  out.add_collider(-HALL_X - 0.4f, HALL_Z0, -HALL_X, HALL_Z1);
  out.add_collider(HALL_X, HALL_Z0, HALL_X + 0.4f, HALL_Z1);

  // The far wall, which the board hangs on.
  for (int part = 0; part < 3; ++part) {
    const float x0 = -HALL_X + 8.0f * static_cast<float>(part);
    const float x1 = x0 + 8.0f;
    out.add_wall(x0, HALL_Z1, x1, HALL_Z1, 0.0f, PAINT_LINE,
                 SM_TEX_FACTORY_WALLS, SM_UV_BRICK_PAINTED, WHITE, false,
                 WALL_TESS);
    out.add_wall(x0, HALL_Z1, x1, HALL_Z1, PAINT_LINE, HALL_TOP,
                 SM_TEX_FACTORY_WALLS, SM_UV_BRICK_WHITEWASH, WHITE, false,
                 WALL_TESS);
  }
  out.add_collider(-HALL_X, HALL_Z1, HALL_X, HALL_Z1 + 0.4f);

  // The entrance wall, around the doorway.
  out.add_wall(HALL_X, HALL_Z0, DOOR_HALF, HALL_Z0, 0.0f, PAINT_LINE,
               SM_TEX_FACTORY_WALLS, SM_UV_BRICK_PAINTED, WHITE, false,
               WALL_TESS);
  out.add_wall(-DOOR_HALF, HALL_Z0, -HALL_X, HALL_Z0, 0.0f, PAINT_LINE,
               SM_TEX_FACTORY_WALLS, SM_UV_BRICK_PAINTED, WHITE, false,
               WALL_TESS);
  out.add_wall(HALL_X, HALL_Z0, DOOR_HALF, HALL_Z0, PAINT_LINE, HALL_TOP,
               SM_TEX_FACTORY_WALLS, SM_UV_BRICK_WHITEWASH, WHITE, false,
               WALL_TESS);
  out.add_wall(-DOOR_HALF, HALL_Z0, -HALL_X, HALL_Z0, PAINT_LINE, HALL_TOP,
               SM_TEX_FACTORY_WALLS, SM_UV_BRICK_WHITEWASH, WHITE, false,
               WALL_TESS);
  out.add_wall(DOOR_HALF, HALL_Z0, -DOOR_HALF, HALL_Z0, DOOR_TOP, HALL_TOP,
               SM_TEX_FACTORY_WALLS, SM_UV_BRICK_WHITEWASH, WHITE, false,
               WALL_TESS);
  out.add_collider(-HALL_X, HALL_Z0 - 0.4f, -DOOR_HALF, HALL_Z0);
  out.add_collider(DOOR_HALF, HALL_Z0 - 0.4f, HALL_X, HALL_Z0);

  // A shallow vestibule behind the doorway, so the way the player came in is a
  // place and not a hole in the world.
  out.add_wall(DOOR_HALF, PORCH_Z, -DOOR_HALF, PORCH_Z, 0.0f, DOOR_TOP,
               SM_TEX_FACTORY_WALLS, SM_UV_BRICK_PAINTED, WHITE, true,
               PROP_TESS);
  out.add_wall(-DOOR_HALF, PORCH_Z, -DOOR_HALF, HALL_Z0, 0.0f, DOOR_TOP,
               SM_TEX_FACTORY_WALLS, SM_UV_BRICK_PAINTED, WHITE, false,
               PROP_TESS);
  out.add_wall(DOOR_HALF, HALL_Z0, DOOR_HALF, PORCH_Z, 0.0f, DOOR_TOP,
               SM_TEX_FACTORY_WALLS, SM_UV_BRICK_PAINTED, WHITE, false,
               PROP_TESS);
  out.add_ceiling(-DOOR_HALF, PORCH_Z, DOOR_HALF, HALL_Z0, DOOR_TOP,
                  SM_TEX_FACTORY_WALLS, SM_UV_BRICK_WHITEWASH, WHITE,
                  PROP_TESS);
  out.add_floor(-DOOR_HALF, PORCH_Z, DOOR_HALF, HALL_Z0, 0.0f,
                SM_TEX_FACTORY_FLOOR, SM_UV_CONCRETE, WHITE, PROP_TESS);

  // ── structure ─────────────────────────────────────────────────────────────
  //
  // Precast columns on a regular grid, hazard band and stencilled number at the
  // base. tess is the column's FULL height on purpose: tessellation subdivides
  // a uv rectangle, it never repeats it, so one cell maps once over the whole
  // 7 m shaft and the band stays where it was painted.
  {
    const float cxs[4] = {-10.50f, -5.00f, 5.00f, 10.50f};
    const float czs[2] = {3.50f, 10.50f};
    for (int a = 0; a < 4; ++a) {
      for (int b = 0; b < 2; ++b) {
        const float x0 = cxs[a] - COLUMN_HALF;
        const float x1 = cxs[a] + COLUMN_HALF;
        const float z0 = czs[b] - COLUMN_HALF;
        const float z1 = czs[b] + COLUMN_HALF;

        // The cell carries its hazard band across the bottom of its 64
        // texels, so mapping it over the whole 7 m shaft paints a 2.8 m
        // chevron and the column reads as a warning sign rather than as
        // concrete. Split in two: the band at the base, at the height a
        // painter would actually have reached, and plain concrete above.
        out.add_box(x0, z0, x1, z1, 0.0f, COLUMN_BAND_TOP, SM_TEX_FACTORY_WALLS,
                    sm_uv(128, 26, 64, 38), WHITE, true, COLUMN_BAND_TOP);
        out.add_box(x0, z0, x1, z1, COLUMN_BAND_TOP, HALL_TOP,
                    SM_TEX_FACTORY_WALLS, sm_uv(128, 0, 64, 26), WHITE, false,
                    HALL_TOP);
      }
    }
  }

  // The roof: trusses over the flanks, and above the middle the roof lantern of
  // dirty wired glass, letting in a weak colourless daylight that reaches
  // nothing on the floor.
  for (int i = 0; i < 2; ++i) {
    const float z0 = 7.0f * static_cast<float>(i);
    const float z1 = z0 + 7.0f;
    out.add_ceiling(-HALL_X, z0, -4.0f, z1, HALL_TOP, SM_TEX_FACTORY_WALLS,
                    SM_UV_TRUSS, WHITE, WALL_TESS);
    out.add_ceiling(-4.0f, z0, 4.0f, z1, HALL_TOP, SM_TEX_FACTORY_WALLS,
                    SM_UV_ROOF_LANTERN, WHITE, WALL_TESS);
    out.add_ceiling(4.0f, z0, HALL_X, z1, HALL_TOP, SM_TEX_FACTORY_WALLS,
                    SM_UV_TRUSS, WHITE, WALL_TESS);
  }
  // Riveted steel roof trusses seen from below, spanning the bay as cut-outs.
  for (int i = 0; i < 3; ++i) {
    const float tz = 3.5f + 3.5f * static_cast<float>(i);
    out.add_wall(-HALL_X, tz, HALL_X, tz, 6.10f, 6.85f, SM_TEX_FACTORY_WALLS,
                 SM_UV_TRUSS, WHITE, false, WALL_TESS);
  }
  // The overhead crane rail with a small gantry parked at the far end. It never
  // moves, and it is hung high enough to clear the sightline from the doorway
  // to the top of the board.
  for (int side = 0; side < 2; ++side) {
    const float rx = (side == 0) ? -10.0f : 10.0f;
    for (int i = 0; i < 2; ++i) {
      const float z0 = 7.0f * static_cast<float>(i);
      out.add_wall(rx, z0, rx, z0 + 7.0f, 5.60f, 6.00f, SM_TEX_FACTORY_WALLS,
                   SM_UV_CRANE_RAIL, WHITE, false, WALL_TESS);
    }
  }
  out.add_box(-10.0f, 11.40f, 10.0f, 12.70f, 5.15f, 6.30f,
              SM_TEX_FACTORY_MACHINES, SM_UV_GANTRY, WHITE, false, PROP_TESS);
  out.add_ceiling(-10.0f, 11.40f, 10.0f, 12.70f, 5.15f, SM_TEX_FACTORY_MACHINES,
                  SM_UV_GANTRY, WHITE, PROP_TESS);

  // ── the working set ───────────────────────────────────────────────────────

  // The assembly table: a heavy steel bench with a vice and the lamppost parts
  // laid out on it. This is where the whole game happens.
  out.add_box(BENCH_X0, BENCH_Z0, BENCH_X1, BENCH_Z1, 0.0f, 0.95f,
              SM_TEX_FACTORY_MACHINES, SM_UV_ASSEMBLY_BENCH, WHITE, true,
              PROP_TESS);
  out.add_floor(-7.70f, 3.65f, -6.30f, 4.55f, 0.96f, SM_TEX_LAMPPOST_PARTS,
                SM_UV_PART_STAGE0, WHITE, PROP_TESS);
  {
    sm_interactable bench{};
    bench.position = rv_vec3{-7.00f, 0.95f, 4.10f};
    bench.kind = SM_INTERACT_ASSEMBLY;
    bench.radius = 1.60f;
    out.interactables.push_back(bench);
  }

  // The welding post: a curtain screen on a frame, cylinders chained to the
  // wall. Its flashes are the one bright light event in the game.
  out.add_wall(3.40f, 5.00f, 5.55f, 5.00f, 0.20f, 2.00f,
               SM_TEX_FACTORY_MACHINES, SM_UV_WELDING_SCREEN, WHITE, true,
               PROP_TESS);
  out.add_wall(5.55f, 5.00f, 5.55f, 6.80f, 0.20f, 2.00f,
               SM_TEX_FACTORY_MACHINES, SM_UV_WELDING_SCREEN, WHITE, true,
               PROP_TESS);
  out.add_box(6.60f, 4.00f, 7.40f, 4.60f, 0.0f, 1.50f, SM_TEX_FACTORY_MACHINES,
              SM_UV_CYLINDERS, WHITE, true, PROP_TESS);

  // The finishing conveyor, running toward the far wall directly under the
  // board. The finished lamppost leaves on it, in view, before the board ticks.
  out.add_box(-1.40f, 8.50f, 1.40f, 13.60f, 0.55f, 1.00f,
              SM_TEX_FACTORY_MACHINES, SM_UV_CONVEYOR, WHITE, true, PROP_TESS);
  out.add_wall(-1.40f, 8.50f, -1.40f, 13.60f, 1.00f, 1.45f,
               SM_TEX_FACTORY_MACHINES, SM_UV_CONVEYOR, WHITE, false,
               PROP_TESS);
  out.add_wall(1.40f, 13.60f, 1.40f, 8.50f, 1.00f, 1.45f,
               SM_TEX_FACTORY_MACHINES, SM_UV_CONVEYOR, WHITE, false,
               PROP_TESS);
  out.add_floor(-0.70f, 11.00f, 0.70f, 12.60f, 1.02f, SM_TEX_LAMPPOST_PARTS,
                SM_UV_PART_FINISHED, WHITE, PROP_TESS);

  // Dressing: shelving, pallets, a drill press, a barrel, the kettle on a
  // stool.
  out.add_box(-11.90f, 9.00f, -10.90f, 13.00f, 0.0f, 2.40f,
              SM_TEX_FACTORY_MACHINES, SM_UV_SHELVING, WHITE, true, PROP_TESS);
  out.add_box(7.00f, 8.00f, 8.40f, 9.20f, 0.0f, 0.16f, SM_TEX_FACTORY_MACHINES,
              SM_UV_PALLET, WHITE, false, PROP_TESS);
  out.add_box(7.00f, 9.40f, 8.40f, 10.60f, 0.0f, 0.32f, SM_TEX_FACTORY_MACHINES,
              SM_UV_PALLET, WHITE, true, PROP_TESS);
  out.add_box(-11.50f, 2.00f, -10.70f, 2.90f, 0.0f, 1.90f,
              SM_TEX_FACTORY_MACHINES, SM_UV_DRILL_PRESS, WHITE, true,
              PROP_TESS);
  out.add_box(9.60f, 6.00f, 10.20f, 6.60f, 0.0f, 0.90f, SM_TEX_FACTORY_MACHINES,
              SM_UV_BARREL, WHITE, true, PROP_TESS);
  out.add_box(2.60f, 2.00f, 3.15f, 2.55f, 0.0f, 0.75f, SM_TEX_FACTORY_MACHINES,
              SM_UV_KETTLE_STOOL, WHITE, true, PROP_TESS);

  // ── the board ─────────────────────────────────────────────────────────────
  //
  // High on the far wall, directly above the conveyor, facing the entrance. The
  // frame first, the panel a hair in front of it; the header and the digit are
  // TEXT (sm_text_draw_world) so the digit can flip without a texture swap. The
  // exact origin and axes are documented above sm_factory_board_text_origin.
  out.add_wall(-2.95f, HALL_Z1 - 0.03f, 2.95f, HALL_Z1 - 0.03f,
               BOARD_Y0 - 0.28f, BOARD_Y1 + 0.28f, SM_TEX_FACTORY_BOARD,
               SM_UV_BOARD_FRAME, WHITE, false, PROP_TESS);
  out.add_wall(-BOARD_HALF_W, HALL_Z1 - 0.06f, BOARD_HALF_W, HALL_Z1 - 0.06f,
               BOARD_Y0, BOARD_Y1, SM_TEX_FACTORY_BOARD, SM_UV_BOARD_PANEL,
               WHITE, false, PROP_TESS);

  // Period dressing beside it. It supports the reading — a production quota on
  // a works board — without explaining anything, which is the whole trick.
  out.add_wall(5.00f, HALL_Z1 - 0.05f, 7.60f, HALL_Z1 - 0.05f, 2.20f, 3.60f,
               SM_TEX_FACTORY_BOARD, SM_UV_BOARD_HONOUR, WHITE, false,
               PROP_TESS);
  out.add_wall(-7.60f, HALL_Z1 - 0.05f, -5.60f, HALL_Z1 - 0.05f, 2.20f, 3.40f,
               SM_TEX_FACTORY_BOARD, SM_UV_BOARD_ROSTER, WHITE, false,
               PROP_TESS);
  out.add_wall(-HALL_X + 0.05f, 5.60f, -HALL_X + 0.05f, 7.80f, 2.00f, 3.20f,
               SM_TEX_FACTORY_SIGNS, SM_UV_WALL_NEWSPAPER, WHITE, false,
               PROP_TESS);
  out.add_wall(HALL_X - 0.05f, 9.40f, HALL_X - 0.05f, 8.40f, 2.20f, 3.10f,
               SM_TEX_FACTORY_SIGNS, SM_UV_SIGN_SAFETY, WHITE, false,
               PROP_TESS);
  out.add_wall(3.30f, HALL_Z0 + 0.05f, 2.40f, HALL_Z0 + 0.05f, 2.20f, 3.00f,
               SM_TEX_FACTORY_SIGNS, SM_UV_SIGN_NO_SMOKING, WHITE, false,
               PROP_TESS);
  out.add_wall(9.20f, HALL_Z1 - 0.05f, 10.20f, HALL_Z1 - 0.05f, 1.20f, 2.00f,
               SM_TEX_FACTORY_SIGNS, SM_UV_SIGN_NO_ENTRY, WHITE, false,
               PROP_TESS);

  // ── encounters ────────────────────────────────────────────────────────────
  //
  // Around the hall floor, and every one of them further than
  // SM_SPAWN_MIN_DISTANCE from the bench at (-7.00, 4.10): the escalation wave
  // has to arrive, not appear.
  {
    const float sx[6] = {4.50f, 9.80f, 6.50f, -3.00f, 10.60f, -9.50f};
    const float sz[6] = {1.60f, 4.50f, 12.00f, 12.20f, 9.00f, 12.80f};
    for (int i = 0; i < 6; ++i)
      out.enemy_spawns.push_back(rv_vec3{sx[i], 0.0f, sz[i]});
  }

  // ── triggers ──────────────────────────────────────────────────────────────
  //
  // Both start closed. The return opens once the assembly completes and the
  // board has ticked; the board approach opens only on the final lap. Neither
  // decision belongs to a level file.
  {
    sm_trigger back_home{};
    back_home.area.x0 = -DOOR_HALF + 0.05f;
    back_home.area.z0 = PORCH_Z + 0.05f;
    back_home.area.x1 = DOOR_HALF - 0.05f;
    back_home.area.z1 = 0.60f;
    back_home.id = SM_TRIGGER_RETURN_HOME;
    back_home.active = false;
    out.triggers.push_back(back_home);

    sm_trigger board{};
    board.area.x0 = -BOARD_HALF_W;
    board.area.z0 = 6.60f;
    board.area.x1 = BOARD_HALF_W;
    board.area.z1 = 8.40f;
    board.id = SM_TRIGGER_APPROACH_BOARD;
    board.active = false;
    out.triggers.push_back(board);
  }

  // The hall is the one space the countdown does not rearrange: on the final
  // lap it is lit but inert, and that is a palette and two triggers, not
  // geometry.
  (void)state;
}

} // namespace solidmaid
