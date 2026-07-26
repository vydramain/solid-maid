// Solidmaid — the texture set and the darkening ramp.
//
// Every texture in the game is listed here once. Two things happen at boot:
// the .mppctex files are read off the medium and uploaded, and each environment
// texture gets SIX palettes derived from its own — one per value of
// shifts_remaining.
//
// That is the whole impoverishment ramp (docs/art-and-audio.md): "six tiers =
// six palettes over the same texture atlases. No post-processing, no colour
// grading pass, no extra memory." An IDX4 palette is 32 bytes, so all six tiers
// of all textures together cost under 5 KiB of the 1 MB pool.
//
// Textures that carry a TELEGRAPH or the HUD are marked untiered and keep their
// authored colours at every tier — docs/mechanics.md makes that a hard rule: "a
// telegraph must never depend on a lamppost being alive".
#pragma once

#include <cstdint>

#include "pdk/rv_pdko.hpp"

#include "sm_common.hpp"
#include "sm_gfx.hpp"

namespace solidmaid {

enum sm_tex_id : int {
  SM_TEX_HOME_WALLS = 0,
  SM_TEX_HOME_FLOOR,
  SM_TEX_HOME_FURNITURE,
  SM_TEX_HOME_WINDOW,
  SM_TEX_HOME_DOOR,

  SM_TEX_STREET_FACADE,
  SM_TEX_STREET_GROUND,
  SM_TEX_STREET_PROPS,
  SM_TEX_STREET_LAMP,
  SM_TEX_LIGHT_POOL,
  SM_TEX_SKY,
  SM_TEX_GATE,

  SM_TEX_FACTORY_WALLS,
  SM_TEX_FACTORY_FLOOR,
  SM_TEX_FACTORY_MACHINES,
  SM_TEX_FACTORY_BOARD,
  SM_TEX_FACTORY_SIGNS,
  SM_TEX_LAMPPOST_PARTS,

  SM_TEX_KIPUCHKA,
  SM_TEX_SMOKER,
  SM_TEX_SMOKE,

  SM_TEX_ITEMS,
  SM_TEX_HANDS,
  SM_TEX_HUD,
  SM_TEX_FONT,

  SM_TEX_COUNT,
};

class sm_assets {
public:
  // Reads and uploads every texture. Returns a negative rv_err if a REQUIRED
  // texture is missing or malformed — a disc that cannot draw its own board
  // should refuse on the loading screen, not halfway through shift three.
  int64_t load(rv_pdk::rv_pdko &pdk);
  void unload();

  // The texture with the palette for `tier` (0 = shift 5, brightest; 5 = the
  // final lap). Untiered textures ignore the argument.
  sm_texref ref(sm_tex_id id, int tier) const;

  // Bytes of video RAM this set is holding.
  int64_t video_bytes() const { return video_bytes_; }
  int missing() const { return missing_; }

private:
  struct sm_tex_slot {
    int64_t texels = 0;
    int64_t palettes[SM_TIER_COUNT] = {0, 0, 0, 0, 0, 0};
    uint16_t width = 0;
    uint16_t height = 0;
    bool tiered = false;
  };

  rv_pdk::rv_pdko *pdk_ = nullptr;
  sm_tex_slot slots_[SM_TEX_COUNT]{};
  int64_t video_bytes_ = 0;
  int missing_ = 0;
};

// Derive tier `tier`'s version of one 15-bit palette entry. Exposed for the
// unit tests, which pin the two properties that matter: an opaque colour never
// becomes the transparent 0000h, and the ramp is monotonically darker.
uint16_t sm_palette_tier_entry(uint16_t entry, int tier);

} // namespace solidmaid
