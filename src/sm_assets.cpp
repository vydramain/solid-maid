#include "sm_assets.hpp"

#include <cstring>
#include <vector>

#include "pdk/cd/rv_cd.hpp"
#include "pdk/cv/rv_texture.hpp"
#include "pdk/rv_err.hpp"

namespace solidmaid {
namespace {

struct sm_tex_desc {
  const char *resource;
  bool tiered;
};

// The manifest from docs/art-and-audio.md, in load order. `tiered` is false for
// anything that must stay readable when the town goes dark: the HUD, the font,
// and the smoke atlas (which carries the self-lit pre-warm ring).
constexpr sm_tex_desc SM_TEXTURES[SM_TEX_COUNT] = {
    {"home_walls.mppctex", true},
    {"home_floor.mppctex", true},
    {"home_furniture.mppctex", true},
    {"home_window.mppctex", true},
    {"home_door.mppctex", true},

    {"street_facade.mppctex", true},
    {"street_ground.mppctex", true},
    {"street_props.mppctex", true},
    {"street_lamp.mppctex", true},
    {"light_pool.mppctex", false},
    {"sky.mppctex", true},
    {"gate.mppctex", true},

    {"factory_walls.mppctex", true},
    {"factory_floor.mppctex", true},
    {"factory_machines.mppctex", true},
    {"factory_board.mppctex", false},
    {"factory_signs.mppctex", true},
    {"lamppost_parts.mppctex", true},

    {"kipuchka.mppctex", true},
    {"smoker.mppctex", true},
    {"smoke.mppctex", false},

    {"items.mppctex", true},
    {"hands.mppctex", true},
    {"hud.mppctex", false},
    {"font.mppctex", false},
};

// docs/art-and-audio.md: each tier "drops a little saturation and one warm hue
// from the environment ramp, shifting the street toward cold grey-blue". The
// floor never reaches zero — tier 5 still keeps well over half the light,
// because the countdown removes pools of light, not the ability to see.
constexpr float SM_TIER_LEVEL[SM_TIER_COUNT] = {1.00f, 0.93f, 0.86f,
                                                0.79f, 0.72f, 0.66f};
constexpr float SM_TIER_WARMTH[SM_TIER_COUNT] = {1.00f, 0.95f, 0.90f,
                                                 0.85f, 0.80f, 0.76f};

uint16_t read_u16(const uint8_t *p) {
  return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

void write_u16(uint8_t *p, uint16_t value) {
  p[0] = static_cast<uint8_t>(value & 0xFF);
  p[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

bool read_resource(rv_pdk::rv_cd *cd, const char *name,
                   std::vector<uint8_t> &out) {
  if (!cd)
    return false;
  const int64_t handle = cd->asset_open(name);
  if (handle < 0)
    return false;
  const int64_t size = cd->asset_size(handle);
  if (size < 0)
    return false;

  out.assign(static_cast<std::size_t>(size), 0);
  // asset_read's return value is the authoritative length; asset_size is a
  // hint that may go stale between the two calls (pdk/cd/rv_cd.hpp).
  const int64_t read = cd->asset_read(handle, out.data(), size);
  if (read < 0)
    return false;
  out.resize(static_cast<std::size_t>(read));
  return true;
}

} // namespace

uint16_t sm_palette_tier_entry(uint16_t entry, int tier) {
  if (tier <= 0)
    return entry;
  if (tier >= SM_TIER_COUNT)
    tier = SM_TIER_COUNT - 1;

  // 0000h is the cut-out hole and must survive every tier untouched: darkening
  // a hole would turn it into a drawn pixel and put an opaque square around
  // every sprite in the game.
  if (entry == 0)
    return 0;

  const int r = entry & 0x1F;
  const int g = (entry >> 5) & 0x1F;
  const int b = (entry >> 10) & 0x1F;

  const float level = SM_TIER_LEVEL[tier];
  const float warmth = SM_TIER_WARMTH[tier];

  int nr = static_cast<int>(static_cast<float>(r) * level * warmth + 0.5f);
  int ng = static_cast<int>(static_cast<float>(g) * level + 0.5f);
  int nb = static_cast<int>(static_cast<float>(b) * level + 0.5f);
  if (nr > 31)
    nr = 31;
  if (ng > 31)
    ng = 31;
  if (nb > 31)
    nb = 31;
  if (nr < 0)
    nr = 0;
  if (ng < 0)
    ng = 0;
  if (nb < 0)
    nb = 0;

  uint16_t packed = static_cast<uint16_t>(nr | (ng << 5) | (nb << 10));
  // The black trap (pdk/tools/mppcbaker/README.md): an OPAQUE colour must never
  // encode to 0000h, or the darkest corner of a texture turns into a window.
  // The baker nudges to 0001h at bake time; the ramp has to do it again,
  // because it is the ramp that walks colours toward black.
  if (packed == 0)
    packed = 1;
  return packed;
}

int64_t sm_assets::load(rv_pdk::rv_pdko &pdk) {
  pdk_ = &pdk;
  rv_pdk::rv_cv *cv = pdk.cv();
  rv_pdk::rv_cd *cd = pdk.cd();
  if (!cv || !cd)
    return rv_pdk::RV_ERR_INVAL;

  const int64_t max_w = cv->texture_max_width();
  const int64_t max_h = cv->texture_max_height();

  std::vector<uint8_t> bytes;
  std::vector<uint8_t> palette_bytes;

  for (int i = 0; i < SM_TEX_COUNT; ++i) {
    const sm_tex_desc &desc = SM_TEXTURES[i];
    sm_tex_slot &slot = slots_[i];
    slot.tiered = desc.tiered;

    // A missing texture is counted, not fatal: the drawing layer falls back
    // to flat vertex colour, so the game stays playable while the atlases
    // are still being authored. Shipping with missing() != 0 is what the
    // acceptance check forbids — see tests/ and the debug overlay.
    if (!read_resource(cd, desc.resource, bytes)) {
      ++missing_;
      continue;
    }

    // Header: "MPTX", version, format, width, height, palette_count.
    // Malformed IS fatal, unlike absent: bytes that claim to be a texture
    // and are not mean the pipeline is broken, and guessing past that ships
    // garbage geometry instead of a clear failure on the loading screen.
    if (bytes.size() < 16 || std::memcmp(bytes.data(), "MPTX", 4) != 0)
      return rv_pdk::RV_ERR_INVAL;
    if (read_u16(bytes.data() + 4) != 1)
      return rv_pdk::RV_ERR_INVAL;

    const uint16_t format = read_u16(bytes.data() + 6);
    const uint16_t width = read_u16(bytes.data() + 8);
    const uint16_t height = read_u16(bytes.data() + 10);
    const uint16_t palette_count = read_u16(bytes.data() + 12);

    if (width == 0 || height == 0)
      return rv_pdk::RV_ERR_INVAL;
    // Validate the baked assumption against the machine that turned up,
    // here on the loading screen (pdk/de/rv_de.hpp).
    if (static_cast<int64_t>(width) > max_w ||
        static_cast<int64_t>(height) > max_h) {
      return rv_pdk::RV_ERR_INVAL;
    }

    const std::size_t palette_offset = 16;
    const std::size_t palette_size =
        static_cast<std::size_t>(palette_count) * 2;
    const std::size_t texel_offset = palette_offset + palette_size;
    if (bytes.size() < texel_offset)
      return rv_pdk::RV_ERR_INVAL;
    const std::size_t texel_size = bytes.size() - texel_offset;

    const int64_t addr =
        cv->video_asset_malloc(static_cast<int64_t>(texel_size));
    if (addr < 0)
      return addr;

    rv_pdk::rv_texture texels{};
    texels.format = static_cast<rv_pdk::rv_texfmt>(format);
    texels.data = bytes.data() + texel_offset;
    texels.size = texel_size;
    texels.width = width;
    texels.height = height;
    const int64_t rc = cv->video_asset_write(addr, texels);
    if (rc < 0) {
      cv->video_asset_free(addr);
      return rc;
    }
    slot.texels = addr;
    slot.width = width;
    slot.height = height;
    video_bytes_ += static_cast<int64_t>(texel_size);

    if (palette_count == 0)
      continue;

    // A palette is uploaded as a texture of its own: DIRECT15, width = entry
    // count, height = 1 (pdk/cv/rv_texture.hpp). One per tier for an
    // environment texture, one only for a protected one.
    const int tiers = desc.tiered ? SM_TIER_COUNT : 1;
    palette_bytes.assign(palette_size, 0);
    for (int tier = 0; tier < tiers; ++tier) {
      for (uint16_t e = 0; e < palette_count; ++e) {
        const uint16_t base = read_u16(bytes.data() + palette_offset + e * 2);
        write_u16(palette_bytes.data() + e * 2,
                  sm_palette_tier_entry(base, tier));
      }

      const int64_t paddr =
          cv->video_asset_malloc(static_cast<int64_t>(palette_size));
      if (paddr < 0)
        return paddr;

      rv_pdk::rv_texture clut{};
      clut.format = rv_pdk::RV_TEXFMT_DIRECT15;
      clut.data = palette_bytes.data();
      clut.size = palette_size;
      clut.width = palette_count;
      clut.height = 1;
      const int64_t prc = cv->video_asset_write(paddr, clut);
      if (prc < 0) {
        cv->video_asset_free(paddr);
        return prc;
      }
      slot.palettes[tier] = paddr;
      video_bytes_ += static_cast<int64_t>(palette_size);
    }
    if (!desc.tiered) {
      for (int tier = 1; tier < SM_TIER_COUNT; ++tier)
        slot.palettes[tier] = slot.palettes[0];
    }
  }

  return rv_pdk::RV_OK;
}

void sm_assets::unload() {
  if (!pdk_)
    return;
  rv_pdk::rv_cv *cv = pdk_->cv();
  if (!cv)
    return;

  for (int i = 0; i < SM_TEX_COUNT; ++i) {
    sm_tex_slot &slot = slots_[i];
    if (slot.texels != 0)
      cv->video_asset_free(slot.texels);
    slot.texels = 0;

    const int tiers = slot.tiered ? SM_TIER_COUNT : 1;
    for (int tier = 0; tier < tiers; ++tier) {
      if (slot.palettes[tier] != 0)
        cv->video_asset_free(slot.palettes[tier]);
    }
    for (int tier = 0; tier < SM_TIER_COUNT; ++tier)
      slot.palettes[tier] = 0;
  }
  video_bytes_ = 0;
}

sm_texref sm_assets::ref(sm_tex_id id, int tier) const {
  if (id < 0 || id >= SM_TEX_COUNT)
    return sm_texref{};
  if (tier < 0)
    tier = 0;
  if (tier >= SM_TIER_COUNT)
    tier = SM_TIER_COUNT - 1;

  const sm_tex_slot &slot = slots_[id];
  sm_texref out{};
  out.texels = slot.texels;
  out.palette = slot.palettes[slot.tiered ? tier : 0];
  out.width = slot.width;
  out.height = slot.height;
  return out;
}

} // namespace solidmaid
