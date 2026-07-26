#include "sm_gfx.hpp"

#include <cmath>

#include "pdk/cv/rv_primitives.hpp"
#include "pdk/rv_err.hpp"

namespace solidmaid {
namespace {

using rv_pdklib::rv_vec3;

constexpr float SM_PI = 3.14159265358979323846f;

float clampf(float value, float low, float high) {
  return value < low ? low : (value > high ? high : value);
}

rv_pdk::rv_uv uv_at(sm_uvrect uv, float s, float t) {
  const float u = static_cast<float>(uv.u0) +
                  (static_cast<float>(uv.u1) - static_cast<float>(uv.u0)) * s;
  const float v = static_cast<float>(uv.v0) +
                  (static_cast<float>(uv.v1) - static_cast<float>(uv.v0)) * t;
  return rv_pdk::rv_uv{static_cast<uint16_t>(u + 0.5f),
                       static_cast<uint16_t>(v + 0.5f)};
}

rv_vec3 lerp3(rv_vec3 a, rv_vec3 b, float t) { return a + (b - a) * t; }

uint8_t lerp_channel(uint8_t a, uint8_t b, float t) {
  const float value = static_cast<float>(a) +
                      (static_cast<float>(b) - static_cast<float>(a)) * t;
  return static_cast<uint8_t>(clampf(value + 0.5f, 0.0f, 255.0f));
}

rv_pdk::rv_color lerp_colour(rv_pdk::rv_color a, rv_pdk::rv_color b, float t) {
  return rv_pdk::rv_color{lerp_channel(a.r, b.r, t), lerp_channel(a.g, b.g, t),
                          lerp_channel(a.b, b.b, t)};
}

// Bilinear over the four corner colours, in the same Z order as the geometry.
rv_pdk::rv_color colour_at(const rv_pdk::rv_color colours[4], float s,
                           float t) {
  return lerp_colour(lerp_colour(colours[0], colours[1], s),
                     lerp_colour(colours[2], colours[3], s), t);
}

// Bilinear point on the quad, in the PDK's Z order: corners[0] corners[1] along
// the top edge, corners[2] corners[3] along the bottom.
rv_vec3 quad_point(const rv_vec3 corners[4], float s, float t) {
  return lerp3(lerp3(corners[0], corners[1], s),
               lerp3(corners[2], corners[3], s), t);
}

// `distance` is to the CLOSEST point of the surface, not to its centre: a 4 m
// wall the player is standing against has its centre two metres away, and
// measuring from there gives the coarse tessellation of a distant surface to
// the one place affine warping is most visible.
int subdivisions(float length, float tess_metres, float distance) {
  // Near the eye a surface needs finer cuts for two independent reasons: the
  // whole-triangle near rejection (pdklib/rv_xform.hpp) removes a polygon the
  // moment one corner passes the eye, and affine uv interpolation swims most
  // where the surface is closest. Far away neither matters, and primitives are
  // the scarce resource.
  float target = tess_metres;
  if (distance < 6.0f)
    target *= 0.5f;
  if (distance > 18.0f)
    target *= 2.0f;
  if (target < 0.35f)
    target = 0.35f;

  int n = static_cast<int>(std::ceil(length / target));
  if (n < 1)
    n = 1;
  if (n > 10)
    n = 10;
  return n;
}

// ── near-plane clipping
// ───────────────────────────────────────────────────────
//
// THEOREM: Sutherland-Hodgman against the single plane w > near, in CLIP space,
// before the perspective divide.
//
// pdklib deliberately does not do this and says so (rv_xform.hpp, "HONEST
// LIMITATION"): it rejects the WHOLE polygon the moment any corner passes the
// eye, because everything it keeps is then guaranteed safe to divide. That is
// sound but conservative, and the conservatism is visible — a wall the player
// stands against loses whole quads and shows the background through itself.
//
// Clipping is the era's actual answer. The plane w = near is the only one that
// must be handled, because it is the only one where the divide is undefined: a
// vertex with w <= 0 does not merely fall off screen, it lands MIRRORED through
// the centre and drags its triangle across the whole frame. Off-screen x and y
// are fine — rv_vertex is signed precisely so a clipped corner can be
// expressed, and the console clips them per pixel.
//
// Interpolation is linear in clip space, which is exact for position and, for
// this console, exactly as right as anything else for colour and uv: the
// rasterizer interpolates both affinely in screen space anyway (there is no w
// to correct with), so a linearly interpolated clip-space attribute is
// consistent with what the hardware will do downstream.
struct sm_cvert {
  rv_pdklib::rv_vec4 clip{};
  rv_pdk::rv_color colour{};
  float u = 0.0f;
  float v = 0.0f;
};

sm_cvert lerp_cvert(const sm_cvert &a, const sm_cvert &b, float t) {
  sm_cvert out{};
  out.clip = a.clip + (b.clip - a.clip) * t;
  out.colour = lerp_colour(a.colour, b.colour, t);
  out.u = a.u + (b.u - a.u) * t;
  out.v = a.v + (b.v - a.v) * t;
  return out;
}

// Clips a convex polygon of `count` vertices given in RIM order. A convex
// polygon cut by one plane stays convex and gains at most one vertex, so `out`
// needs count + 1 slots; the callers give it 8 for a quad.
int clip_near(const sm_cvert *in, int count, sm_cvert *out, float limit) {
  int n = 0;
  for (int i = 0; i < count; ++i) {
    const sm_cvert &cur = in[i];
    const sm_cvert &next = in[(i + 1) % count];
    // NaN-safe in the same direction pdklib chose: a NaN w fails the test
    // and is treated as behind the plane.
    const bool cur_in = cur.clip.w > limit;
    const bool next_in = next.clip.w > limit;

    if (cur_in)
      out[n++] = cur;
    if (cur_in != next_in) {
      const float span = next.clip.w - cur.clip.w;
      float t = (span != 0.0f) ? (limit - cur.clip.w) / span : 0.0f;
      if (!(t > 0.0f))
        t = 0.0f;
      if (t > 1.0f)
        t = 1.0f;
      out[n++] = lerp_cvert(cur, next, t);
    }
  }
  return n;
}

rv_pdk::rv_uv uv_of(const sm_cvert &vertex) {
  const float u = vertex.u < 0.0f ? 0.0f : vertex.u;
  const float v = vertex.v < 0.0f ? 0.0f : vertex.v;
  return rv_pdk::rv_uv{static_cast<uint16_t>(u + 0.5f),
                       static_cast<uint16_t>(v + 0.5f)};
}

} // namespace

rv_vec3 sm_forward(float yaw, float pitch) {
  const float cp = std::cos(pitch);
  return rv_vec3{std::sin(yaw) * cp, std::sin(pitch), std::cos(yaw) * cp};
}

rv_vec3 sm_right(float yaw) {
  return rv_vec3{std::cos(yaw), 0.0f, -std::sin(yaw)};
}

void sm_gfx::attach(rv_pdk::rv_cv *cv, int64_t screen_width,
                    int64_t screen_height, int64_t frame_capacity) {
  cv_ = cv;
  screen_width_ = screen_width;
  screen_height_ = screen_height;
  frame_capacity_ = frame_capacity;
}

void sm_gfx::begin(const sm_view &view, rv_pdk::rv_color clear_colour,
                   float far_plane) {
  view_ = view;
  forward_ = sm_forward(view.yaw, view.pitch);
  far_plane_ = far_plane > SM_NEAR_PLANE * 4.0f ? far_plane : SM_FAR_PLANE;

  const float aspect =
      static_cast<float>(screen_width_) / static_cast<float>(screen_height_);
  camera_ = rv_pdklib::rv_camera_make(
      view.eye, view.eye + forward_, rv_vec3{0.0f, 1.0f, 0.0f},
      SM_FOV_Y_DEGREES * SM_PI / 180.0f, aspect, SM_NEAR_PLANE, far_plane_);

  conf_ = rv_pdklib::rv_xform_conf_make(
      rv_pdklib::rv_camera_view_projection(camera_), camera_,
      static_cast<float>(screen_width_), static_cast<float>(screen_height_),
      SM_DEPTH_WORLD_MIN, SM_DEPTH_WORLD_MAX,
      // Authored level geometry is single-sided by hand and a room is only ever
      // seen from inside it, so culling would buy nothing and cost a whole
      // class of "my wall vanished" bugs. Billboards and decals face the camera
      // by construction. Visibility is won back by the range/cone test instead.
      rv_pdklib::RV_CULL_NONE);

  // The widest angle a point can sit off the view axis and still be on screen:
  // the frustum corner. Used by visible() as a cheap cone test.
  const float half_fov_y = SM_FOV_Y_DEGREES * SM_PI / 360.0f;
  const float tan_y = std::tan(half_fov_y);
  const float tan_x = tan_y * aspect;
  cos_half_fov_ = 1.0f / std::sqrt(1.0f + tan_x * tan_x + tan_y * tan_y);

  submitted_ = 0;
  dropped_ = 0;
  cv_->frame_configure(0, clear_colour);
}

void sm_gfx::end() { cv_->frame_flush(); }

bool sm_gfx::put(const rv_pdk::rv_primitive &primitive) {
  if (submitted_ >= frame_capacity_) {
    ++dropped_;
    return false;
  }
  if (cv_->frame_put(primitive) < 0) {
    ++dropped_;
    return false;
  }
  ++submitted_;
  return true;
}

bool sm_gfx::visible(rv_vec3 centre, float radius) const {
  const rv_vec3 to = centre - view_.eye;
  const float along = rv_pdklib::rv_dot(to, forward_);
  if (along > far_plane_ + radius)
    return false;
  if (along < -radius)
    return false;

  const float distance_sq = rv_pdklib::rv_dot(to, to);
  if (distance_sq <= radius * radius)
    return true; // the eye is inside the sphere

  const float distance = std::sqrt(distance_sq);
  // Widen the cone by the sphere's angular radius before comparing.
  const float sin_extra = clampf(radius / distance, 0.0f, 1.0f);
  const float cos_extra = std::sqrt(1.0f - sin_extra * sin_extra);
  const float cos_limit =
      cos_half_fov_ * cos_extra -
      sin_extra *
          std::sqrt(clampf(1.0f - cos_half_fov_ * cos_half_fov_, 0.0f, 1.0f));
  return (along / distance) >= cos_limit;
}

bool sm_gfx::project(rv_vec3 world, float &out_x, float &out_y) const {
  rv_pdklib::rv_vec2 screen{};
  int32_t depth = 0;
  if (!rv_pdklib::rv_xform_point(conf_, world, screen, depth))
    return false;
  out_x = screen.x;
  out_y = screen.y;
  return true;
}

void sm_gfx::quad_raw(const rv_vec3 corners[4], const rv_pdk::rv_uv uv[4],
                      sm_texref texture, rv_pdk::rv_color tint, bool textured) {
  const rv_pdk::rv_color colours[4] = {tint, tint, tint, tint};
  emit_surface(corners, colours, uv, texture, textured);
}

void sm_gfx::emit_surface(const rv_vec3 corners[4],
                          const rv_pdk::rv_color colours[4],
                          const rv_pdk::rv_uv uv[4], sm_texref texture,
                          bool textured) {
  const bool sampling = textured && texture.valid();
  const float limit = conf_.near_plane * 0.999f;

  rv_pdklib::rv_vec4 clip[4];
  bool crosses = false;
  for (int i = 0; i < 4; ++i) {
    clip[i] = rv_pdklib::rv_world_to_clip(conf_.mvp, corners[i]);
    if (!(clip[i].w > limit))
      crosses = true;
  }

  auto fill = [&](rv_pdk::rv_polygon &polygon) {
    if (sampling) {
      polygon.fill_mode = rv_pdk::RV_PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE;
      polygon.addr_texture = texture.texels;
      polygon.addr_palette = texture.palette;
      polygon.mapping = rv_pdk::RV_TEXWRAP_CLAMP;
    } else {
      polygon.fill_mode = rv_pdk::RV_PRIMITIVE_FILL_MODE_FLAT_COLOURED;
      polygon.addr_texture = 0;
      polygon.addr_palette = 0;
      polygon.mapping = rv_pdk::RV_TEXWRAP_CLAMP;
    }
  };

  // The common case: nothing near the eye, so the surface goes out as ONE quad
  // in the PDK's Z order — one primitive instead of two, and the
  // (1,2,3)/(2,3,4) split is the one the console expects.
  if (!crosses) {
    rv_pdk::rv_primitive primitive{};
    primitive.type = rv_pdk::RV_PRIMITIVE_POLYGON;
    primitive.depth = rv_pdklib::rv_xform_depth_key(clip, 4, conf_);

    rv_pdk::rv_polygon &polygon = primitive.data.polygon;
    fill(polygon);
    polygon.vertex_count = 4;
    for (int i = 0; i < 4; ++i) {
      const rv_pdklib::rv_vec2 screen = rv_pdklib::rv_xform_to_screen(
          rv_pdklib::rv_xform_divide(clip[i]), conf_.screen_width,
          conf_.screen_height);
      polygon.vertexes[i] =
          rv_pdklib::rv_xform_vertex_make(screen, colours[i], uv[i]);
    }
    put(primitive);
    return;
  }

  // Something is behind the eye. Clip, and emit what survives.
  //
  // The clipper needs a RIM loop and the caller gave Z order, which walks
  // 0 -> 1 -> 3 -> 2 around the edge. Handing Z order straight to a polygon
  // clipper would cut across the diagonal and produce an hourglass.
  static const int SM_RIM[4] = {0, 1, 3, 2};
  sm_cvert rim[4];
  for (int i = 0; i < 4; ++i) {
    const int k = SM_RIM[i];
    rim[i].clip = clip[k];
    rim[i].colour = colours[k];
    rim[i].u = static_cast<float>(uv[k].u);
    rim[i].v = static_cast<float>(uv[k].v);
  }

  sm_cvert kept[8];
  const int count = clip_near(rim, 4, kept, limit);
  if (count < 3)
    return; // entirely behind the eye

  // ONE depth key for the whole clipped polygon, shared by every triangle it
  // becomes. Keying them individually would let the ordering table interleave
  // the halves of a single wall with something between them, which is a tear
  // along the fan's diagonal — the exact artefact this function exists to stop.
  rv_pdklib::rv_vec4 keys[8];
  for (int i = 0; i < count; ++i)
    keys[i] = kept[i].clip;
  const int32_t depth = rv_pdklib::rv_xform_depth_key(keys, count, conf_);

  rv_pdk::rv_vertex projected[8];
  for (int i = 0; i < count; ++i) {
    const rv_pdklib::rv_vec2 screen =
        rv_pdklib::rv_xform_to_screen(rv_pdklib::rv_xform_divide(kept[i].clip),
                                      conf_.screen_width, conf_.screen_height);
    projected[i] =
        rv_pdklib::rv_xform_vertex_make(screen, kept[i].colour, uv_of(kept[i]));
  }

  // Triangle fan from the first kept vertex. A convex polygon fans correctly
  // from any of its vertices, and triangles are used rather than quads because
  // a fan is not the console's Z order and a 5-gon has no quad form at all.
  for (int i = 1; i + 1 < count; ++i) {
    rv_pdk::rv_primitive primitive{};
    primitive.type = rv_pdk::RV_PRIMITIVE_POLYGON;
    primitive.depth = depth;

    rv_pdk::rv_polygon &polygon = primitive.data.polygon;
    fill(polygon);
    polygon.vertex_count = 3;
    polygon.vertexes[0] = projected[0];
    polygon.vertexes[1] = projected[i];
    polygon.vertexes[2] = projected[i + 1];
    // No byte of a submitted primitive may be indeterminate: it is a union.
    polygon.vertexes[3] = rv_pdk::rv_vertex{};
    put(primitive);
  }
}

void sm_gfx::quad(const rv_vec3 corners[4], sm_texref texture, sm_uvrect uv,
                  rv_pdk::rv_color tint, float tess_metres) {
  const rv_vec3 centre =
      (corners[0] + corners[1] + corners[2] + corners[3]) * 0.25f;
  float radius = 0.0f;
  for (int i = 0; i < 4; ++i) {
    const float d = rv_pdklib::rv_length(corners[i] - centre);
    if (d > radius)
      radius = d;
  }
  if (!visible(centre, radius))
    return;

  float distance = rv_pdklib::rv_length(centre - view_.eye) - radius;
  if (distance < 0.0f)
    distance = 0.0f;
  const float len_u = 0.5f * (rv_pdklib::rv_length(corners[1] - corners[0]) +
                              rv_pdklib::rv_length(corners[3] - corners[2]));
  const float len_v = 0.5f * (rv_pdklib::rv_length(corners[2] - corners[0]) +
                              rv_pdklib::rv_length(corners[3] - corners[1]));

  const int nu = subdivisions(len_u, tess_metres, distance);
  const int nv = subdivisions(len_v, tess_metres, distance);

  for (int j = 0; j < nv; ++j) {
    const float t0 = static_cast<float>(j) / static_cast<float>(nv);
    const float t1 = static_cast<float>(j + 1) / static_cast<float>(nv);
    for (int i = 0; i < nu; ++i) {
      const float s0 = static_cast<float>(i) / static_cast<float>(nu);
      const float s1 = static_cast<float>(i + 1) / static_cast<float>(nu);

      const rv_vec3 cell[4] = {
          quad_point(corners, s0, t0), quad_point(corners, s1, t0),
          quad_point(corners, s0, t1), quad_point(corners, s1, t1)};
      const rv_pdk::rv_uv cell_uv[4] = {uv_at(uv, s0, t0), uv_at(uv, s1, t0),
                                        uv_at(uv, s0, t1), uv_at(uv, s1, t1)};
      quad_raw(cell, cell_uv, texture, tint, true);
    }
  }
}

void sm_gfx::quad_flat(const rv_vec3 corners[4], rv_pdk::rv_color tint,
                       float tess_metres) {
  const rv_pdk::rv_color colours[4] = {tint, tint, tint, tint};
  quad_shaded(corners, colours, tess_metres);
}

void sm_gfx::quad_shaded(const rv_vec3 corners[4],
                         const rv_pdk::rv_color colours[4], float tess_metres) {
  const rv_vec3 centre =
      (corners[0] + corners[1] + corners[2] + corners[3]) * 0.25f;
  float radius = 0.0f;
  for (int i = 0; i < 4; ++i) {
    const float d = rv_pdklib::rv_length(corners[i] - centre);
    if (d > radius)
      radius = d;
  }
  if (!visible(centre, radius))
    return;

  float distance = rv_pdklib::rv_length(centre - view_.eye) - radius;
  if (distance < 0.0f)
    distance = 0.0f;
  const float len_u = 0.5f * (rv_pdklib::rv_length(corners[1] - corners[0]) +
                              rv_pdklib::rv_length(corners[3] - corners[2]));
  const float len_v = 0.5f * (rv_pdklib::rv_length(corners[2] - corners[0]) +
                              rv_pdklib::rv_length(corners[3] - corners[1]));

  const int nu = subdivisions(len_u, tess_metres, distance);
  const int nv = subdivisions(len_v, tess_metres, distance);

  for (int j = 0; j < nv; ++j) {
    const float t0 = static_cast<float>(j) / static_cast<float>(nv);
    const float t1 = static_cast<float>(j + 1) / static_cast<float>(nv);
    for (int i = 0; i < nu; ++i) {
      const float s0 = static_cast<float>(i) / static_cast<float>(nu);
      const float s1 = static_cast<float>(i + 1) / static_cast<float>(nu);

      const rv_vec3 cell[4] = {
          quad_point(corners, s0, t0), quad_point(corners, s1, t0),
          quad_point(corners, s0, t1), quad_point(corners, s1, t1)};

      const float su[4] = {s0, s1, s0, s1};
      const float sv[4] = {t0, t0, t1, t1};
      rv_pdk::rv_color cell_colours[4];
      for (int k = 0; k < 4; ++k)
        cell_colours[k] = colour_at(colours, su[k], sv[k]);

      const rv_pdk::rv_uv no_uv[4] = {};
      emit_surface(cell, cell_colours, no_uv, sm_texref{}, false);
    }
  }
}

void sm_gfx::billboard(rv_vec3 centre, float half_width, float half_height,
                       sm_texref texture, sm_uvrect uv, rv_pdk::rv_color tint) {
  const float radius =
      std::sqrt(half_width * half_width + half_height * half_height);
  if (!visible(centre, radius))
    return;

  // Yaw-only facing: the card stays upright in the world instead of tipping
  // with the camera's pitch, which is what keeps a standing figure standing.
  const rv_vec3 to = centre - view_.eye;
  const float len = std::sqrt(to.x * to.x + to.z * to.z);
  rv_vec3 right{1.0f, 0.0f, 0.0f};
  if (len > 0.0001f)
    right = rv_vec3{to.z / len, 0.0f, -to.x / len};

  const rv_vec3 up{0.0f, half_height, 0.0f};
  const rv_vec3 side = right * half_width;

  const rv_vec3 corners[4] = {centre - side + up, centre + side + up,
                              centre - side - up, centre + side - up};
  const rv_pdk::rv_uv cell_uv[4] = {
      uv_at(uv, 0.0f, 0.0f), uv_at(uv, 1.0f, 0.0f), uv_at(uv, 0.0f, 1.0f),
      uv_at(uv, 1.0f, 1.0f)};
  quad_raw(corners, cell_uv, texture, tint, true);
}

void sm_gfx::decal_ground(rv_vec3 centre, float half_size, float y,
                          sm_texref texture, sm_uvrect uv,
                          rv_pdk::rv_color tint) {
  if (!visible(rv_vec3{centre.x, y, centre.z}, half_size * 1.5f))
    return;

  // Lifted off the floor: one ordering-table key per polygon means a coplanar
  // decal and its floor land in the same bucket and swap frame to frame.
  const float h = y + 0.02f;
  const rv_vec3 corners[4] = {
      rv_vec3{centre.x - half_size, h, centre.z + half_size},
      rv_vec3{centre.x + half_size, h, centre.z + half_size},
      rv_vec3{centre.x - half_size, h, centre.z - half_size},
      rv_vec3{centre.x + half_size, h, centre.z - half_size}};
  const rv_pdk::rv_uv cell_uv[4] = {
      uv_at(uv, 0.0f, 0.0f), uv_at(uv, 1.0f, 0.0f), uv_at(uv, 0.0f, 1.0f),
      uv_at(uv, 1.0f, 1.0f)};
  quad_raw(corners, cell_uv, texture, tint, true);
}

void sm_gfx::line3(rv_vec3 a, rv_vec3 b, rv_pdk::rv_color colour) {
  rv_pdklib::rv_vec2 sa{}, sb{};
  int32_t da = 0, db = 0;
  if (!rv_pdklib::rv_xform_point(conf_, a, sa, da))
    return;
  if (!rv_pdklib::rv_xform_point(conf_, b, sb, db))
    return;

  rv_pdk::rv_primitive primitive{};
  primitive.type = rv_pdk::RV_PRIMITIVE_LINE;
  primitive.depth = da < db ? da : db;
  primitive.data.line.vertexes[0] =
      rv_pdklib::rv_xform_vertex_make(sa, colour, rv_pdk::rv_uv{});
  primitive.data.line.vertexes[1] =
      rv_pdklib::rv_xform_vertex_make(sb, colour, rv_pdk::rv_uv{});
  put(primitive);
}

void sm_gfx::sprite(int x, int y, int w, int h, rv_pdk::rv_color colour,
                    int32_t depth) {
  if (w <= 0 || h <= 0)
    return;

  rv_pdk::rv_primitive primitive{};
  primitive.type = rv_pdk::RV_PRIMITIVE_SPRITE;
  primitive.depth = depth;

  rv_pdk::rv_sprite &s = primitive.data.sprite;
  s.fill_mode = rv_pdk::RV_PRIMITIVE_FILL_MODE_FLAT_COLOURED;
  s.color = colour;
  s.mapping = rv_pdk::RV_TEXWRAP_CLAMP;
  s.x = static_cast<int16_t>(x);
  s.y = static_cast<int16_t>(y);
  s.width = static_cast<uint16_t>(w);
  s.height = static_cast<uint16_t>(h);
  put(primitive);
}

void sm_gfx::sprite_tex(int x, int y, int w, int h, sm_texref texture,
                        sm_uvrect uv, rv_pdk::rv_color tint, int32_t depth) {
  if (w <= 0 || h <= 0 || !texture.valid())
    return;

  // rv_sprite samples from the texture's upper-left corner, so an atlas cell
  // has to be addressed as a quad. Two triangles instead of one rectangle is
  // the price of atlasing, and it is what every UI element here pays.
  const rv_pdk::rv_vertex corners[4] = {
      {static_cast<int16_t>(x), static_cast<int16_t>(y), tint,
       uv_at(uv, 0.0f, 0.0f)},
      {static_cast<int16_t>(x + w), static_cast<int16_t>(y), tint,
       uv_at(uv, 1.0f, 0.0f)},
      {static_cast<int16_t>(x), static_cast<int16_t>(y + h), tint,
       uv_at(uv, 0.0f, 1.0f)},
      {static_cast<int16_t>(x + w), static_cast<int16_t>(y + h), tint,
       uv_at(uv, 1.0f, 1.0f)}};
  quad2d(corners, texture, depth);
}

void sm_gfx::quad2d(const rv_pdk::rv_vertex corners[4], sm_texref texture,
                    int32_t depth) {
  if (!texture.valid())
    return;

  rv_pdk::rv_primitive primitive{};
  primitive.type = rv_pdk::RV_PRIMITIVE_POLYGON;
  primitive.depth = depth;

  rv_pdk::rv_polygon &polygon = primitive.data.polygon;
  polygon.fill_mode = rv_pdk::RV_PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE;
  polygon.addr_texture = texture.texels;
  polygon.addr_palette = texture.palette;
  polygon.mapping = rv_pdk::RV_TEXWRAP_CLAMP;
  polygon.vertex_count = 4;
  for (int i = 0; i < 4; ++i)
    polygon.vertexes[i] = corners[i];
  put(primitive);
}

} // namespace solidmaid
