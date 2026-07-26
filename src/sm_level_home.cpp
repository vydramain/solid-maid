// Solidmaid — the apartment.
//
// One room and a hallway, dressed to `apartment_state()` (docs/content.md). The
// room is 3.40 x 4.20 m and THE CEILING IS EXACTLY 2.50 m: with the eye at
// 1.65 m that leaves 0.85 m of air, and docs/environments.md calls it "the
// single most important measurement in the game". Every other number here is
// negotiable; that one is not, and it is what the 7 m factory is measured
// against.
//
// The subtraction rule, restated because this file is the only place it is
// implemented: EVERY removal leaves a trace on a surface that already exists.
// The countdown never leaves an empty spot — it leaves a bright rectangle of
// unfaded wallpaper, four dents in the linoleum, a chalk ring, a nail. The room
// gets emptier and stays legible, and nothing in the game comments on it.
//
// Never removed, at any state including 5: the window, the radiator, the
// ceiling fixture, the doors, the skirting, and THE BRICK AND THE PIPE BY THE
// FRONT DOOR.
#include "sm_atlas.hpp"
#include "sm_scene.hpp"

namespace solidmaid {
namespace {

using rv_pdklib::rv_vec3;

// ── the shell
// ─────────────────────────────────────────────────────────────────

constexpr float ROOM_X0 = 0.00f;
constexpr float ROOM_X1 = 3.40f;
constexpr float ROOM_Z0 = 0.00f;
constexpr float ROOM_Z1 = 4.20f;

constexpr float CEILING = 2.50f; // see the file header. Do not round this up.
constexpr float SKIRT = 0.12f;

constexpr float HALL_X0 = 1.00f;
constexpr float HALL_X1 = 2.20f;
constexpr float HALL_Z0 = -2.50f;

constexpr float DOOR_X0 = 1.20f;
constexpr float DOOR_X1 = 2.00f;
constexpr float DOOR_TOP = 2.00f;

// The window on the courtyard wall, and the sill under it.
constexpr float WIN_X0 = 0.95f;
constexpr float WIN_X1 = 2.45f;
constexpr float WIN_Y0 = 0.85f;
constexpr float WIN_Y1 = 2.05f;

// The courtyard beyond the glass. The flat is on the third floor, so the ground
// out there is seven metres down and is barely in view — what IS in view, at
// eye level across the yard, is L1.
constexpr float YARD_BLOCK_Z = 15.00f;
constexpr float YARD_LAMP_Z = 8.00f;
constexpr float YARD_GROUND_Y = -7.00f;

constexpr rv_pdk::rv_color WHITE{255, 255, 255};
// Mercury green-cyan. The streetlights are cold and the apartment's own light
// is warm, and docs/art-and-audio.md holds the two apart on purpose: they never
// mix.
constexpr rv_pdk::rv_color MERCURY{104, 168, 152};

// A small textured card laid flat on an existing surface — a trace, a switch, a
// picture. Everything of this kind is offset off its wall by a hair so the
// ordering table cannot flicker it against the surface it is painted on.
constexpr float DRESSING_OFFSET = 0.025f;
constexpr float PROP_TESS = 4.0f;

} // namespace

void sm_build_home(sm_scene &out, const sm_countdown &state) {
  out.clear();
  out.area = SM_AREA_HOME;
  out.floor_y = 0.0f;
  out.ceiling_y = CEILING;
  // A one-room flat: the longest sightline is the hallway, about 7 m.
  // Far enough to contain THE VIEW, not just the room. The opposite block of
  // the courtyard stands at z = 15 m and rises to 14 m; a far plane tucked in
  // around the flat clipped all of it away and turned the window into a hole
  // with nothing behind it. The depth resolution this costs is the price of
  // the one thing the window exists for.
  out.far_plane = 28.0f;
  out.draws_sky = false;
  // The ambient floor made literal. Dim and slightly warm — the one working
  // bulb — and never zero, on any tier, final lap included.
  out.clear_colour = rv_pdk::rv_color{21, 19, 20};

  // At the bed, looking down the length of the room at the window. The first
  // thing the player sees on every shift is the courtyard, which is where L1
  // is, which is how a dead lamp is noticed from indoors.
  out.player_start = rv_vec3{1.30f, 0.0f, 1.95f};
  out.player_yaw = 0.0f;

  const int dressing = state.apartment_state();
  const bool bare_plaster = dressing >= 5;
  const sm_uvrect paper = bare_plaster ? SM_UV_PLASTER_BARE : SM_UV_WALLPAPER;
  const sm_uvrect paper_faded =
      bare_plaster ? SM_UV_PLASTER_BARE : SM_UV_WALLPAPER_FADED;
  const sm_uvrect paper_seam =
      bare_plaster ? SM_UV_PLASTER_BARE : SM_UV_WALLPAPER_SEAM;
  const sm_uvrect paper_hot =
      bare_plaster ? SM_UV_PLASTER_BARE : SM_UV_WALLPAPER_YELLOWED;

  // ── floor ─────────────────────────────────────────────────────────────────
  //
  // Linoleum in one sheet, worn through to grey felt along the line the flat
  // has actually been walked for thirty years: front door, room, window, bed.
  // The worn strip is authored as its own surfaces rather than as an overlay,
  // so nothing here is coplanar with anything else.
  out.add_floor(0.00f, 0.00f, 0.85f, 4.20f, 0.0f, SM_TEX_HOME_FLOOR, SM_UV_LINO,
                WHITE, 2.0f);
  out.add_floor(0.85f, 0.00f, 1.25f, 1.60f, 0.0f, SM_TEX_HOME_FLOOR, SM_UV_LINO,
                WHITE, 2.0f);
  out.add_floor(0.85f, 1.60f, 1.25f, 2.60f, 0.0f, SM_TEX_HOME_FLOOR,
                SM_UV_LINO_WORN, WHITE, 2.0f);
  out.add_floor(0.85f, 2.60f, 1.25f, 4.20f, 0.0f, SM_TEX_HOME_FLOOR, SM_UV_LINO,
                WHITE, 2.0f);
  out.add_floor(1.25f, 0.00f, 2.05f, 4.20f, 0.0f, SM_TEX_HOME_FLOOR,
                SM_UV_LINO_WORN, WHITE, 2.0f);
  out.add_floor(2.05f, 0.00f, 3.40f, 4.20f, 0.0f, SM_TEX_HOME_FLOOR, SM_UV_LINO,
                WHITE, 2.0f);

  out.add_floor(HALL_X0, HALL_Z0, 1.25f, 0.0f, 0.0f, SM_TEX_HOME_FLOOR,
                SM_UV_LINO, WHITE, 2.0f);
  out.add_floor(1.25f, HALL_Z0, 2.05f, 0.0f, 0.0f, SM_TEX_HOME_FLOOR,
                SM_UV_LINO_WORN, WHITE, 2.0f);
  out.add_floor(2.05f, HALL_Z0, HALL_X1, 0.0f, 0.0f, SM_TEX_HOME_FLOOR,
                SM_UV_LINO, WHITE, 2.0f);

  // ── ceiling ───────────────────────────────────────────────────────────────
  out.add_ceiling(ROOM_X0, ROOM_Z0, ROOM_X1, 3.20f, CEILING, SM_TEX_HOME_WALLS,
                  SM_UV_CEILING_WHITEWASH, WHITE, 2.2f);
  // The neighbour's leak, in the corner nearest the window.
  out.add_ceiling(ROOM_X0, 3.20f, 1.20f, ROOM_Z1, CEILING, SM_TEX_HOME_WALLS,
                  SM_UV_CEILING_STAIN, WHITE, 2.2f);
  out.add_ceiling(1.20f, 3.20f, ROOM_X1, ROOM_Z1, CEILING, SM_TEX_HOME_WALLS,
                  SM_UV_CEILING_WHITEWASH, WHITE, 2.2f);
  out.add_ceiling(HALL_X0, HALL_Z0, HALL_X1, 0.0f, CEILING, SM_TEX_HOME_WALLS,
                  SM_UV_CEILING_WHITEWASH, WHITE, 2.2f);

  // ── room walls ────────────────────────────────────────────────────────────
  //
  // Wallpaper up from the skirting. Sun-bleached on the window wall, yellowed
  // over the radiator, one seam lifting near the corner by the door. At state 5
  // the paper is gone and all of it is bare plaster.
  out.add_wall(ROOM_X0, ROOM_Z0, ROOM_X0, ROOM_Z1, SKIRT, CEILING,
               SM_TEX_HOME_WALLS, paper, WHITE, true, 2.4f);
  out.add_wall(ROOM_X1, ROOM_Z1, ROOM_X1, ROOM_Z0, SKIRT, CEILING,
               SM_TEX_HOME_WALLS, paper, WHITE, true, 2.4f);

  // The window wall, in four pieces around the opening.
  out.add_wall(ROOM_X0, ROOM_Z1, WIN_X0, ROOM_Z1, SKIRT, CEILING,
               SM_TEX_HOME_WALLS, paper_faded, WHITE, false, 2.4f);
  out.add_wall(WIN_X1, ROOM_Z1, ROOM_X1, ROOM_Z1, SKIRT, CEILING,
               SM_TEX_HOME_WALLS, paper_faded, WHITE, false, 2.4f);
  out.add_wall(WIN_X0, ROOM_Z1, WIN_X1, ROOM_Z1, SKIRT, WIN_Y0,
               SM_TEX_HOME_WALLS, paper_hot, WHITE, false, 2.4f);
  out.add_wall(WIN_X0, ROOM_Z1, WIN_X1, ROOM_Z1, WIN_Y1, CEILING,
               SM_TEX_HOME_WALLS, paper_faded, WHITE, false, 2.4f);
  out.add_collider(ROOM_X0, ROOM_Z1, ROOM_X1, ROOM_Z1);

  // The door wall, in three pieces around the doorway plus its lintel.
  out.add_wall(ROOM_X0, ROOM_Z0, 0.35f, ROOM_Z0, SKIRT, CEILING,
               SM_TEX_HOME_WALLS, paper_seam, WHITE, false, 2.4f);
  out.add_wall(0.35f, ROOM_Z0, DOOR_X0, ROOM_Z0, SKIRT, CEILING,
               SM_TEX_HOME_WALLS, paper, WHITE, false, 2.4f);
  out.add_wall(DOOR_X1, ROOM_Z0, ROOM_X1, ROOM_Z0, SKIRT, CEILING,
               SM_TEX_HOME_WALLS, paper, WHITE, false, 2.4f);
  out.add_wall(DOOR_X0, ROOM_Z0, DOOR_X1, ROOM_Z0, DOOR_TOP, CEILING,
               SM_TEX_HOME_WALLS, paper, WHITE, false, 2.4f);
  out.add_collider(ROOM_X0, ROOM_Z0, DOOR_X0, ROOM_Z0);
  out.add_collider(DOOR_X1, ROOM_Z0, ROOM_X1, ROOM_Z0);

  // Brown-painted wooden skirting, one length of it coming away from the wall.
  out.add_wall(ROOM_X0, ROOM_Z0, ROOM_X0, ROOM_Z1, 0.0f, SKIRT,
               SM_TEX_HOME_WALLS, SM_UV_SKIRTING, WHITE, false, PROP_TESS);
  out.add_wall(ROOM_X1, ROOM_Z1, ROOM_X1, ROOM_Z0, 0.0f, SKIRT,
               SM_TEX_HOME_WALLS, SM_UV_SKIRTING, WHITE, false, PROP_TESS);
  out.add_wall(ROOM_X0, ROOM_Z1, ROOM_X1, ROOM_Z1, 0.0f, SKIRT,
               SM_TEX_HOME_WALLS, SM_UV_SKIRTING, WHITE, false, PROP_TESS);
  out.add_wall(ROOM_X0, ROOM_Z0, DOOR_X0, ROOM_Z0, 0.0f, SKIRT,
               SM_TEX_HOME_WALLS, SM_UV_SKIRTING, WHITE, false, PROP_TESS);
  out.add_wall(DOOR_X1, ROOM_Z0, ROOM_X1, ROOM_Z0, 0.0f, SKIRT,
               SM_TEX_HOME_WALLS, SM_UV_SKIRTING, WHITE, false, PROP_TESS);

  // ── hallway ───────────────────────────────────────────────────────────────
  //
  // Not papered: dull green oil paint with a painted dividing line, glossy by
  // the switch where thirty years of hands have touched it. Dark, no window.
  out.add_wall(HALL_X0, HALL_Z0, HALL_X0, 0.0f, 0.0f, CEILING,
               SM_TEX_HOME_WALLS, SM_UV_HALL_PAINT, WHITE, true, 2.4f);
  out.add_wall(HALL_X1, 0.0f, HALL_X1, HALL_Z0, 0.0f, CEILING,
               SM_TEX_HOME_WALLS, SM_UV_HALL_PAINT, WHITE, true, 2.4f);
  out.add_wall(HALL_X0, HALL_Z0, DOOR_X0, HALL_Z0, 0.0f, CEILING,
               SM_TEX_HOME_WALLS, SM_UV_HALL_PAINT, WHITE, false, 2.4f);
  out.add_wall(DOOR_X1, HALL_Z0, HALL_X1, HALL_Z0, 0.0f, CEILING,
               SM_TEX_HOME_WALLS, SM_UV_HALL_PAINT, WHITE, false, 2.4f);
  out.add_wall(DOOR_X0, HALL_Z0, DOOR_X1, HALL_Z0, DOOR_TOP, CEILING,
               SM_TEX_HOME_WALLS, SM_UV_HALL_PAINT, WHITE, false, 2.4f);
  out.add_collider(HALL_X0, HALL_Z0, HALL_X1, HALL_Z0);

  // The front door itself: padded dermatin, and it stays shut — walking into it
  // is what fires SM_TRIGGER_LEAVE_HOME.
  out.add_wall(DOOR_X0, HALL_Z0 + 0.03f, DOOR_X1, HALL_Z0 + 0.03f, 0.0f,
               DOOR_TOP, SM_TEX_HOME_DOOR, SM_UV_DOOR_FRONT, WHITE, false,
               PROP_TESS);
  // The kitchen and the bathroom: doors that do not open.
  out.add_wall(HALL_X0 + 0.03f, -2.10f, HALL_X0 + 0.03f, -1.30f, 0.0f, DOOR_TOP,
               SM_TEX_HOME_DOOR, SM_UV_DOOR_CLOSED, WHITE, false, PROP_TESS);
  out.add_wall(HALL_X1 - 0.03f, -0.80f, HALL_X1 - 0.03f, -1.60f, 0.0f, DOOR_TOP,
               SM_TEX_HOME_DOOR, SM_UV_DOOR_CLOSED, WHITE, false, PROP_TESS);
  // The room door, standing open flat against the room side of its wall.
  out.add_wall(2.05f, 0.05f, 2.85f, 0.05f, 0.0f, DOOR_TOP, SM_TEX_HOME_DOOR,
               SM_UV_DOOR_ROOM, WHITE, false, PROP_TESS);
  // Soviet rocker switches, one each side of the room door.
  out.add_wall(2.04f, 0.03f, 2.20f, 0.03f, 1.35f, 1.52f, SM_TEX_HOME_DOOR,
               SM_UV_SWITCH, WHITE, false, PROP_TESS);
  out.add_wall(HALL_X0 + 0.03f, -0.35f, HALL_X0 + 0.03f, -0.15f, 1.35f, 1.52f,
               SM_TEX_HOME_DOOR, SM_UV_SWITCH, WHITE, false, PROP_TESS);

  // ── the window ────────────────────────────────────────────────────────────
  //
  // No glass plane is drawn over the opening: there is no alpha blending on
  // this console, so an opaque pane would hide the courtyard. The wooden double
  // frame is the cut-out card, and the yard is built behind it.
  out.add_wall(WIN_X0, ROOM_Z1 - 0.02f, WIN_X1, ROOM_Z1 - 0.02f, WIN_Y0, WIN_Y1,
               SM_TEX_HOME_WINDOW, SM_UV_WINDOW_FRAME, WHITE, false, PROP_TESS);
  out.add_floor(0.90f, 4.02f, 2.50f, ROOM_Z1, WIN_Y0, SM_TEX_HOME_WINDOW,
                SM_UV_WINDOW_SILL, WHITE, PROP_TESS);
  out.add_wall(0.80f, 4.10f, 1.15f, 4.10f, 0.80f, 2.25f, SM_TEX_HOME_WINDOW,
               SM_UV_CURTAIN, WHITE, false, PROP_TESS);
  out.add_wall(2.25f, 4.10f, 2.60f, 4.10f, 0.80f, 2.25f, SM_TEX_HOME_WINDOW,
               SM_UV_CURTAIN, WHITE, false, PROP_TESS);
  // Cast-iron radiator under the sill. Never removed.
  out.add_box(1.05f, 4.02f, 2.35f, 4.16f, 0.18f, 0.75f, SM_TEX_HOME_FURNITURE,
              SM_UV_RADIATOR, WHITE, true, PROP_TESS);

  // ── the courtyard, seen through it ────────────────────────────────────────
  //
  // A flat card set, not a place. What it exists for is L1: the pole across the
  // yard sits at about the third floor's eye level, so its head is dead centre
  // in the window and the player cannot miss the shift it stops burning.
  out.add_wall(-14.0f, ROOM_Z1, -14.0f, YARD_BLOCK_Z, -10.0f, 16.0f,
               SM_TEX_STREET_FACADE, SM_UV_PANEL, WHITE, false, 8.0f);
  out.add_wall(18.0f, YARD_BLOCK_Z, 18.0f, ROOM_Z1, -10.0f, 16.0f,
               SM_TEX_STREET_FACADE, SM_UV_PANEL, WHITE, false, 8.0f);
  out.add_floor(-14.0f, ROOM_Z1, 18.0f, YARD_BLOCK_Z, YARD_GROUND_Y,
                SM_TEX_STREET_GROUND, SM_UV_ASPHALT, WHITE, 8.0f);
  out.add_wall(-14.0f, YARD_BLOCK_Z, 18.0f, YARD_BLOCK_Z, -10.0f, -1.00f,
               SM_TEX_STREET_FACADE, SM_UV_PANEL, WHITE, false, 8.0f);
  out.add_wall(-14.0f, YARD_BLOCK_Z, 18.0f, YARD_BLOCK_Z, -1.00f, 12.00f,
               SM_TEX_STREET_FACADE, SM_UV_WINDOWS_DARK, WHITE, false, 8.0f);
  out.add_wall(-14.0f, YARD_BLOCK_Z, 18.0f, YARD_BLOCK_Z, 12.00f, 14.00f,
               SM_TEX_STREET_FACADE, SM_UV_ROOF_EDGE, WHITE, false, 8.0f);
  // The two or three windows still lit over there — warm yellow, and the only
  // colour in the view once the mercury goes out.
  out.add_wall(4.20f, YARD_BLOCK_Z - 0.06f, 5.80f, YARD_BLOCK_Z - 0.06f, 2.20f,
               3.90f, SM_TEX_STREET_FACADE, SM_UV_WINDOWS_LIT, WHITE, false,
               PROP_TESS);
  out.add_wall(-6.40f, YARD_BLOCK_Z - 0.06f, -4.80f, YARD_BLOCK_Z - 0.06f,
               5.40f, 7.10f, SM_TEX_STREET_FACADE, SM_UV_WINDOWS_LIT, WHITE,
               false, PROP_TESS);

  const bool l1_lit = state.lamp_lit(0);
  out.add_wall(0.42f, YARD_LAMP_Z, 0.68f, YARD_LAMP_Z, YARD_GROUND_Y, 0.95f,
               SM_TEX_STREET_LAMP, SM_UV_LAMP_POLE, WHITE, false, PROP_TESS);
  out.add_wall(0.60f, YARD_LAMP_Z - 0.03f, 1.05f, YARD_LAMP_Z - 0.03f, 0.62f,
               0.92f, SM_TEX_STREET_LAMP, SM_UV_LAMP_BRACKET, WHITE, false,
               PROP_TESS);
  out.add_wall(0.82f, YARD_LAMP_Z - 0.06f, 1.66f, YARD_LAMP_Z - 0.06f, 0.30f,
               0.72f, SM_TEX_STREET_LAMP,
               l1_lit ? SM_UV_LAMP_HEAD_LIT : SM_UV_LAMP_HEAD_DEAD, WHITE,
               false, PROP_TESS);
  if (l1_lit) {
    // The halo, behind the head. Untextured, because SAMPLE_TEXTURE replaces
    // the vertex colour and this is the only way to put a colour on screen
    // that is not in a palette (sm_gfx.hpp).
    out.add_wall(0.50f, YARD_LAMP_Z + 0.08f, 1.98f, YARD_LAMP_Z + 0.08f, -0.20f,
                 1.22f, SM_TEX_COUNT, sm_uvrect{}, MERCURY, false, PROP_TESS);
  }

  // ── the ceiling fixture ───────────────────────────────────────────────────
  //
  // Three arms, one working bulb, hung off centre, so the corners of the room
  // stay dark at every state. Never removed.
  {
    sm_billboard chandelier{};
    chandelier.centre = rv_vec3{1.70f, 2.26f, 2.05f};
    chandelier.half_width = 0.30f;
    chandelier.half_height = 0.22f;
    chandelier.texture = SM_TEX_HOME_FURNITURE;
    chandelier.uv = SM_UV_CHANDELIER;
    chandelier.tint = WHITE;
    out.billboards.push_back(chandelier);
  }

  // ── state 0: the television ───────────────────────────────────────────────
  if (dressing < 1) {
    out.add_box(2.80f, 0.30f, 3.38f, 0.98f, 0.0f, 0.50f, SM_TEX_HOME_FURNITURE,
                SM_UV_TV_STAND, WHITE, true, PROP_TESS);
    out.add_box(2.85f, 0.36f, 3.35f, 0.92f, 0.50f, 1.05f, SM_TEX_HOME_FURNITURE,
                SM_UV_TELEVISION, WHITE, false, PROP_TESS);

    sm_interactable television{};
    television.position = rv_vec3{3.05f, 0.80f, 0.64f};
    television.kind = SM_INTERACT_TELEVISION;
    television.radius = 1.2f;
    out.interactables.push_back(television);
  } else {
    // The trace. A rectangle of wallpaper in its original unfaded colour with
    // a fuzz of dust along its bottom line, and the socket the cord is still
    // plugged into. "The one comfort interaction is the first thing the
    // countdown takes" — so it has to be the most legible absence in the room.
    out.add_wall(ROOM_X1 - DRESSING_OFFSET, 1.02f, ROOM_X1 - DRESSING_OFFSET,
                 0.26f, 0.48f, 1.22f, SM_TEX_HOME_WALLS, SM_UV_TRACE_TELEVISION,
                 WHITE, false, PROP_TESS);
    out.add_wall(ROOM_X1 - DRESSING_OFFSET, 0.44f, ROOM_X1 - DRESSING_OFFSET,
                 0.28f, 0.16f, 0.32f, SM_TEX_HOME_DOOR, SM_UV_SWITCH, WHITE,
                 false, PROP_TESS);
  }

  // ── state 1: the wardrobe ─────────────────────────────────────────────────
  if (dressing < 2) {
    out.add_box(2.85f, 1.10f, ROOM_X1, 3.10f, 0.0f, 2.05f,
                SM_TEX_HOME_FURNITURE, SM_UV_WARDROBE, WHITE, true, PROP_TESS);
  } else {
    // Clean pale wall behind it, and the coat now on a nail hammered straight
    // into the paper.
    out.add_wall(ROOM_X1 - DRESSING_OFFSET, 3.10f, ROOM_X1 - DRESSING_OFFSET,
                 1.10f, SKIRT, 2.10f, SM_TEX_HOME_WALLS,
                 SM_UV_TRACE_WARDROBE_WALL, WHITE, false, 2.4f);
    // Four dark compressed dents in the linoleum where the feet stood.
    for (int i = 0; i < 2; ++i) {
      sm_decal dent{};
      dent.centre = rv_vec3{3.12f, 0.0f, i == 0 ? 1.35f : 2.85f};
      dent.half_size = 0.30f;
      dent.y = 0.0f;
      dent.texture = SM_TEX_HOME_FLOOR;
      dent.uv = SM_UV_LINO_DENTS;
      dent.tint = WHITE;
      out.decals.push_back(dent);
    }
  }

  // ── state 2: the table and the chair ──────────────────────────────────────
  //
  // Both stand in the far corner by the window rather than out in the middle:
  // the room is 3.40 m wide, the bed and the wardrobe eat 1.40 m of that, and a
  // table in the centre would leave two 0.5 m gaps that a 0.64 m player cannot
  // walk through. A flat that reads as furnished and cannot be crossed is worse
  // than a flat with the table where a real one would be.
  if (dressing < 3) {
    out.add_box(2.35f, 3.30f, 3.35f, 4.00f, 0.0f, 0.75f, SM_TEX_HOME_FURNITURE,
                SM_UV_TABLE, WHITE, true, PROP_TESS);
    out.add_box(1.75f, 3.05f, 2.15f, 3.45f, 0.0f, 0.90f, SM_TEX_HOME_FURNITURE,
                SM_UV_CHAIR, WHITE, true, PROP_TESS);
  } else {
    // The chalk-white ring where the glass always sat, on bare linoleum now.
    // The single working bulb is suddenly hanging over nothing.
    sm_decal ring{};
    ring.centre = rv_vec3{2.85f, 0.0f, 3.65f};
    ring.half_size = 0.42f;
    ring.y = 0.0f;
    ring.texture = SM_TEX_HOME_FLOOR;
    ring.uv = SM_UV_LINO_RING;
    ring.tint = WHITE;
    out.decals.push_back(ring);
  }

  // ── state 3: the wall objects ─────────────────────────────────────────────
  if (dressing < 4) {
    out.add_wall(ROOM_X0 + DRESSING_OFFSET, 1.10f, ROOM_X0 + DRESSING_OFFSET,
                 2.90f, 0.62f, 1.90f, SM_TEX_HOME_FURNITURE, SM_UV_WALL_CARPET,
                 WHITE, false, 2.4f);
    out.add_wall(0.50f, ROOM_Z0 + DRESSING_OFFSET, 0.95f,
                 ROOM_Z0 + DRESSING_OFFSET, 1.34f, 1.92f, SM_TEX_HOME_FURNITURE,
                 SM_UV_CALENDAR, WHITE, false, PROP_TESS);
    out.add_wall(2.36f, ROOM_Z0 + DRESSING_OFFSET, 2.76f,
                 ROOM_Z0 + DRESSING_OFFSET, 1.50f, 1.86f, SM_TEX_HOME_FURNITURE,
                 SM_UV_PHOTOGRAPH, WHITE, false, PROP_TESS);
    out.add_wall(ROOM_X1 - DRESSING_OFFSET, 4.05f, ROOM_X1 - DRESSING_OFFSET,
                 3.70f, 1.60f, 1.95f, SM_TEX_HOME_FURNITURE, SM_UV_CLOCK, WHITE,
                 false, PROP_TESS);
  } else {
    // The carpet's outline in clean wallpaper with four nail holes; the
    // calendar's smaller rectangle and its smear of glue; the clock's single
    // nail and the pale disc around it. The pale rectangles are the only
    // pictures left in the flat.
    out.add_wall(ROOM_X0 + DRESSING_OFFSET, 1.10f, ROOM_X0 + DRESSING_OFFSET,
                 2.90f, 0.62f, 1.90f, SM_TEX_HOME_WALLS, SM_UV_TRACE_CARPET,
                 WHITE, false, 2.4f);
    out.add_wall(0.50f, ROOM_Z0 + DRESSING_OFFSET, 0.95f,
                 ROOM_Z0 + DRESSING_OFFSET, 1.34f, 1.92f, SM_TEX_HOME_WALLS,
                 SM_UV_TRACE_CALENDAR, WHITE, false, PROP_TESS);
    out.add_wall(2.36f, ROOM_Z0 + DRESSING_OFFSET, 2.76f,
                 ROOM_Z0 + DRESSING_OFFSET, 1.50f, 1.86f, SM_TEX_HOME_WALLS,
                 SM_UV_TRACE_CALENDAR, WHITE, false, PROP_TESS);
    out.add_wall(ROOM_X1 - DRESSING_OFFSET, 4.05f, ROOM_X1 - DRESSING_OFFSET,
                 3.70f, 1.60f, 1.95f, SM_TEX_HOME_WALLS, SM_UV_TRACE_CLOCK,
                 WHITE, false, PROP_TESS);
  }

  // ── state 4: the bed ──────────────────────────────────────────────────────
  if (dressing < 5) {
    out.add_box(ROOM_X0, 0.90f, 0.85f, 2.90f, 0.0f, 0.55f,
                SM_TEX_HOME_FURNITURE, SM_UV_BED, WHITE, true, PROP_TESS);
  } else {
    // A mattress on the floor. Two long depressions in the linoleum where the
    // bed stood, and the darker band along the skirting behind it that was
    // never washed. By here the room reads as a corridor.
    out.add_box(0.12f, 0.95f, 0.92f, 2.85f, 0.0f, 0.14f, SM_TEX_HOME_FURNITURE,
                SM_UV_MATTRESS, WHITE, false, PROP_TESS);
    for (int i = 0; i < 2; ++i) {
      sm_decal hollow{};
      hollow.centre = rv_vec3{0.44f, 0.0f, i == 0 ? 1.30f : 2.50f};
      hollow.half_size = 0.40f;
      hollow.y = 0.0f;
      hollow.texture = SM_TEX_HOME_FLOOR;
      hollow.uv = SM_UV_LINO_BED;
      hollow.tint = WHITE;
      out.decals.push_back(hollow);
    }
  }

  // ── the work gear ─────────────────────────────────────────────────────────
  //
  // The brick and the pipe, leaning by the front door. Same two places on every
  // shift, in every state, including state 5 when they are the only things left
  // in the flat with a purpose. Subtraction never touches what the shift needs.
  //
  // They stand a metre back from the door rather than against it, and the leave
  // trigger below starts past them: reaching for the pipe must never be the
  // same act as walking out without it.
  {
    sm_billboard brick{};
    brick.centre = rv_vec3{1.16f, 0.16f, -1.35f};
    brick.half_width = 0.16f;
    brick.half_height = 0.14f;
    brick.texture = SM_TEX_ITEMS;
    brick.uv = SM_UV_BRICK_WORLD;
    brick.tint = WHITE;
    out.billboards.push_back(brick);

    sm_billboard pipe{};
    pipe.centre = rv_vec3{2.04f, 0.52f, -1.42f};
    pipe.half_width = 0.10f;
    pipe.half_height = 0.52f;
    pipe.texture = SM_TEX_ITEMS;
    pipe.uv = SM_UV_PIPE_WORLD;
    pipe.tint = WHITE;
    out.billboards.push_back(pipe);

    sm_interactable brick_pickup{};
    brick_pickup.position = rv_vec3{1.16f, 0.16f, -1.35f};
    brick_pickup.kind = SM_INTERACT_BRICK;
    brick_pickup.radius = 1.0f;
    out.interactables.push_back(brick_pickup);

    sm_interactable pipe_pickup{};
    pipe_pickup.position = rv_vec3{2.04f, 0.52f, -1.42f};
    pipe_pickup.kind = SM_INTERACT_PIPE;
    pipe_pickup.radius = 1.0f;
    out.interactables.push_back(pipe_pickup);
  }

  // ── the way out ───────────────────────────────────────────────────────────
  {
    sm_trigger leave{};
    leave.area.x0 = HALL_X0 + 0.05f;
    leave.area.z0 = HALL_Z0 + 0.20f;
    leave.area.x1 = HALL_X1 - 0.05f;
    leave.area.z1 = HALL_Z0 + 0.60f;
    leave.id = SM_TRIGGER_LEAVE_HOME;
    leave.active = true;
    out.triggers.push_back(leave);
  }
}

} // namespace solidmaid
