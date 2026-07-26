// Solidmaid — the drawing layer.
//
// One frame is: begin(), a camera, a pile of world quads and billboards, some
// screen-space sprites and text, end(). Everything the rest of the game draws
// goes through here, and the reasons are all console-shaped:
//
//   * The frame refuses the 4097th primitive (specs.md). One place counts them,
//     so a budget overrun is a number on the debug overlay rather than geometry
//     that silently stops appearing.
//   * A polygon is rejected WHOLE if any corner crosses the near plane
//     (pdklib/rv_xform.hpp), so every surface is tessellated before transform,
//     more finely close to the eye.
//   * Texture mapping is affine, so tessellation is also what keeps a long wall
//     from swimming.
//   * The ordering table sorts by one key per polygon, so draw order is a
//   design
//     concern and the depth ranges live in sm_common.hpp.
//
// ── THE ONE THING TO KNOW BEFORE DRAWING ANYTHING
// ─────────────────────────────
//
// TEXTURE-COMBINE IS NOT IMPLEMENTED ON THIS CONSOLE. SAMPLE_TEXTURE *replaces*
// the vertex colours with the texel; it does not multiply them
// (src/rv_pconsole/cv/rv_pcraster.cpp: "The moment texture-combine (modulation)
// lands..."). Three consequences run through the whole game:
//
//   1. `tint` on a TEXTURED draw does nothing. It is honoured only when the
//      texture is absent, i.e. on the flat and shaded calls below.
//   2. The darkening ramp is therefore PALETTE work and only palette work —
//      which is what docs/art-and-audio.md specifies anyway ("six tiers = six
//      palettes over the same texture atlases"). See sm_assets.
//   3. Anything that needs per-surface shading — a light pool fading into
//      unlit asphalt, the sky gradient, a hit flash — is drawn as UNTEXTURED
//      gouraud geometry with quad_shaded(). Interpolating four vertex colours
//      across a quad is fully supported and costs nothing extra.
#pragma once

#include <cstdint>

#include "pdk/cv/rv_cv.hpp"
#include "pdklib/rv_camera.hpp"
#include "pdklib/rv_math.hpp"
#include "pdklib/rv_xform.hpp"

#include "sm_common.hpp"

namespace solidmaid {

// A texture as the game refers to it: where its texels and its current palette
// sit in video RAM. Copied by value everywhere — it is two addresses and a
// size.
struct sm_texref {
  int64_t texels = 0;
  int64_t palette = 0;
  uint16_t width = 0;
  uint16_t height = 0;

  bool valid() const { return texels != 0; }
};

// A rectangle inside a texture, in texels. Atlases are the norm here, so almost
// nothing draws a whole texture.
struct sm_uvrect {
  uint16_t u0 = 0, v0 = 0, u1 = 0, v1 = 0;
};

constexpr sm_uvrect sm_uv(int u0, int v0, int w, int h) {
  return sm_uvrect{static_cast<uint16_t>(u0), static_cast<uint16_t>(v0),
                   static_cast<uint16_t>(u0 + w),
                   static_cast<uint16_t>(v0 + h)};
}

// Where the eye is and where it looks. Yaw is measured from +z toward +x, so
// yaw 0 faces +z; pitch is positive looking up.
struct sm_view {
  rv_pdklib::rv_vec3 eye{0.0f, SM_EYE_HEIGHT, 0.0f};
  float yaw = 0.0f;
  float pitch = 0.0f;
};

rv_pdklib::rv_vec3 sm_forward(float yaw, float pitch);
rv_pdklib::rv_vec3 sm_right(float yaw);

class sm_gfx {
public:
  // Bind the controller and cache the geometry the disc validated at boot.
  void attach(rv_pdk::rv_cv *cv, int64_t screen_width, int64_t screen_height,
              int64_t frame_capacity);

  // `far_plane` is the area's own, not a global: the ordering-table key is
  // LINEAR in view z across [near, far], so a 4 m room measured against a 46 m
  // far plane spends about 2% of the table on itself and sorts its own walls
  // with a couple of buckets between them. Handing each area a far plane that
  // matches its size is what gives the apartment the resolution it needs — and
  // it is a budget decision, not a darkness one: nothing in any area is beyond
  // its own far plane, so no route landmark is ever clipped away.
  void begin(const sm_view &view, rv_pdk::rv_color clear_color,
             float far_plane = SM_FAR_PLANE);
  void end();

  // ── world space ───────────────────────────────────────────────────────────

  // A quad given in the PDK's Z ORDER — v0 v1 on the top edge, v2 v3 on the
  // bottom, so the console's (1,2,3)/(2,3,4) split runs across it rather than
  // around the rim. Handing corners in rim order produces an hourglass.
  //
  // `tess_metres` is the target size of one sub-quad; the call subdivides to
  // roughly that, more finely when the surface is close to the eye.
  //
  // `depth_bias` nudges the ordering-table key TOWARD THE CAMERA. The console
  // keeps one key per polygon and quantises it into buckets, so a decal lying a
  // couple of centimetres over a road computes almost the same key as the road:
  // the two land in the same bucket, or in neighbouring ones depending on which
  // way the rounding fell that frame, and the pair flickers as the camera
  // moves. Lifting the decal geometrically cannot fix that — the KEY is what is
  // quantised, so the key is what has to move.
  void quad(const rv_pdklib::rv_vec3 corners[4], sm_texref texture,
            sm_uvrect uv, rv_pdk::rv_color tint, float tess_metres = 2.0f,
            int32_t depth_bias = 0);

  // Same, untextured and one colour.
  void quad_flat(const rv_pdklib::rv_vec3 corners[4], rv_pdk::rv_color tint,
                 float tess_metres = 2.0f);

  // Untextured with a colour per corner, interpolated across the surface.
  // This is the console's ONLY shading mechanism (see the note at the top of
  // this file) and the way every gradient in the game is drawn: the pool of
  // light under a burning lamppost, the overcast sky, the fade of a floor into
  // the dark. Colours are interpolated per sub-quad, so tessellation controls
  // how smooth the gradient is.
  void quad_shaded(const rv_pdklib::rv_vec3 corners[4],
                   const rv_pdk::rv_color colours[4], float tess_metres = 2.0f);

  // An axis-aligned camera-facing card at a world point: enemies, props seen
  // from any angle, the smoke cloud, the pre-warm ring, light pools.
  void billboard(rv_pdklib::rv_vec3 centre, float half_width, float half_height,
                 sm_texref texture, sm_uvrect uv, rv_pdk::rv_color tint);

  // A ground decal — a horizontal card at a fixed height, used for the lamp
  // pools and the assembly glow. Drawn slightly above its floor so the
  // ordering table does not flicker it against the surface underneath.
  void decal_ground(rv_pdklib::rv_vec3 centre, float half_size, float y,
                    sm_texref texture, sm_uvrect uv, rv_pdk::rv_color tint);

  // A world-space line, for telegraph rings and debug shapes.
  void line3(rv_pdklib::rv_vec3 a, rv_pdklib::rv_vec3 b,
             rv_pdk::rv_color colour);

  // ── screen space ──────────────────────────────────────────────────────────

  void sprite(int x, int y, int w, int h, rv_pdk::rv_color colour,
              int32_t depth);
  void sprite_tex(int x, int y, int w, int h, sm_texref texture, sm_uvrect uv,
                  rv_pdk::rv_color tint, int32_t depth);
  // A textured screen quad with explicit corners, for the view model's small
  // amount of skew and swing.
  void quad2d(const rv_pdk::rv_vertex corners[4], sm_texref texture,
              int32_t depth);

  // ── queries ───────────────────────────────────────────────────────────────

  // Is a world-space sphere worth transforming at all? Cheap cone-and-range
  // test against the current view; every world draw call runs it first.
  bool visible(rv_pdklib::rv_vec3 centre, float radius) const;

  // Project a world point to the screen. False when it is behind the eye.
  bool project(rv_pdklib::rv_vec3 world, float &out_x, float &out_y) const;

  int width() const { return static_cast<int>(screen_width_); }
  int height() const { return static_cast<int>(screen_height_); }
  const sm_view &view() const { return view_; }

  int submitted() const { return submitted_; }
  int dropped() const { return dropped_; }
  int capacity() const { return static_cast<int>(frame_capacity_); }

private:
  bool put(const rv_pdk::rv_primitive &primitive);
  void quad_raw(const rv_pdklib::rv_vec3 corners[4], const rv_pdk::rv_uv uv[4],
                sm_texref texture, rv_pdk::rv_color tint, bool textured,
                int32_t depth_bias = 0);
  // Transform, CLIP against the near plane, and file. Every world-space
  // surface in the game ends up here; see the theorem in sm_gfx.cpp.
  void emit_surface(const rv_pdklib::rv_vec3 corners[4],
                    const rv_pdk::rv_color colours[4],
                    const rv_pdk::rv_uv uv[4], sm_texref texture, bool textured,
                    int32_t depth_bias = 0);

  rv_pdk::rv_cv *cv_ = nullptr;
  int64_t screen_width_ = 0;
  int64_t screen_height_ = 0;
  int64_t frame_capacity_ = 0;

  sm_view view_{};
  rv_pdklib::rv_vec3 forward_{0.0f, 0.0f, 1.0f};
  rv_pdklib::rv_camera camera_{};
  rv_pdklib::rv_xform_conf conf_{};
  float cos_half_fov_ = 0.0f;
  float far_plane_ = SM_FAR_PLANE;

  int submitted_ = 0;
  int dropped_ = 0;
};

} // namespace solidmaid
