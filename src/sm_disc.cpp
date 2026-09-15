// Solidmaid — the disc entry point.
//
// The console owns the frame loop; this file is the four hooks it drives
// (pdk/de/rv_de.hpp) and nothing else. Everything the game actually is lives
// behind sm_game; this translation unit only wires the facade to it, validates
// the machine that turned up, and hands the memory back at shutdown.
//
// The last line of the file is what makes this a disc rather than a library.
#include <cstdint>

#include "pdk/cio/rv_cio.h"
#include "pdk/cv/rv_cv.h"
#include "pdk/de/rv_de.h"
#include "pdk/de/rv_dv.h"
#include "pdk/rv_err.h"

#include "sm_assets.hpp"
#include "sm_common.hpp"
#include "sm_game.hpp"
#include "sm_gfx.hpp"
#include "sm_input.hpp"
#include "sm_sound.hpp"

namespace solidmaid {

class rv_dmain {
public:
  int64_t disc_initialize(rv_pdko *pdk);
  void frame_update(float dt);
  void frame_render();
  bool disc_release() const { return release_; }
  void disc_shutdown();
  const char *disc_title() const {
    return "Solidmaid: Alkoldun Vasiliusavich";
  }

private:
  rv_pdko *pdk_ = nullptr;
  sm_assets assets_;
  sm_gfx gfx_;
  sm_input_reader reader_;
  sm_sound sound_;
  sm_game game_;
  bool release_ = false;
  bool initialized_ = false;
};

int64_t rv_dmain::disc_initialize(rv_pdko *pdk) {
  pdk_ = pdk;

  rv_cv *cv = rv_pdko_cv(pdk);
  rv_cio *cio = rv_pdko_cio(pdk);
  if (!cv || !cio)
    return RV_ERR_INVAL;

  const int64_t width = rv_cv_screen_width(cv);
  const int64_t height = rv_cv_screen_height(cv);
  const int64_t capacity = rv_cv_frame_capacity(cv);

  // Validate the baked assumptions against the machine that turned up, HERE,
  // rather than discovering them as garbage half a second into play. The
  // numbers are the ones docs/gameplay.md was designed against.
  if (width < 256 || height < 200)
    return RV_ERR_INVAL;
  if (capacity < 1024)
    return RV_ERR_INVAL;
  if (rv_cio_iport_count(cio) < 1)
    return RV_ERR_INVAL;

  const int64_t rc = assets_.load(pdk);
  if (rc < 0)
    return rc;

  // A missing or broken sound bank is NOT fatal. Every telegraph and every
  // confirmation in this game has a visual half by construction, so the disc
  // stays playable in silence rather than refusing to boot over a sample.
  sound_.load(pdk);

  gfx_.attach(cv, width, height, capacity);
  game_.initialize(pdk, assets_, gfx_, sound_);

  initialized_ = true;
  return RV_OK;
}

void rv_dmain::frame_update(float dt) {
  if (!initialized_)
    return;

  sm_input input{};
  reader_.sample(rv_pdko_cio(pdk_), game_.injected_pad(), input);

  game_.update(input, dt);
  if (game_.wants_release())
    release_ = true;
}

void rv_dmain::frame_render() {
  if (!initialized_)
    return;
  game_.render();
}

void rv_dmain::disc_shutdown() {
  if (!pdk_)
    return;
  // The facade is still valid here and this is the LAST moment it is: after
  // this returns the console may unload this code entirely, and a destructor
  // belonging to unmapped code cannot run.
  if (initialized_)
    game_.shutdown();
  sound_.unload();
  assets_.unload();
  initialized_ = false;
}

} // namespace solidmaid

RV_MPPC_DISC_ENTRY_DEF(solidmaid::rv_dmain)
