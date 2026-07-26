// Solidmaid — the disc entry point.
//
// The console owns the frame loop; this file is the four hooks it drives
// (pdk/de/rv_de.hpp) and nothing else. Everything the game actually is lives
// behind sm_game; this translation unit only wires the facade to it, validates
// the machine that turned up, and hands the memory back at shutdown.
//
// The last line of the file is what makes this a disc rather than a library.
#include <cstdint>

#include "pdk/cio/rv_cio.hpp"
#include "pdk/cv/rv_cv.hpp"
#include "pdk/de/rv_de.hpp"
#include "pdk/rv_abi.hpp"
#include "pdk/rv_err.hpp"

#include "sm_assets.hpp"
#include "sm_common.hpp"
#include "sm_game.hpp"
#include "sm_gfx.hpp"
#include "sm_input.hpp"

namespace solidmaid {

class rv_dmain : public rv_pdk::rv_de {
public:
  int64_t disc_initialize(rv_pdk::rv_pdko &pdk) override;
  void frame_update(float dt) override;
  void frame_render() override;
  bool disc_release() const override { return release_; }
  void disc_shutdown() override;
  const char *disc_title() const override {
    return "Solidmaid: Alkoldun Vasiliusavich";
  }

private:
  rv_pdk::rv_pdko *pdk_ = nullptr;
  sm_assets assets_;
  sm_gfx gfx_;
  sm_input_reader reader_;
  sm_game game_;
  bool release_ = false;
  bool initialized_ = false;
};

int64_t rv_dmain::disc_initialize(rv_pdk::rv_pdko &pdk) {
  pdk_ = &pdk;

  rv_pdk::rv_cv *cv = pdk.cv();
  rv_pdk::rv_cio *cio = pdk.cio();
  if (!cv || !cio)
    return rv_pdk::RV_ERR_INVAL;

  const int64_t width = cv->screen_width();
  const int64_t height = cv->screen_height();
  const int64_t capacity = cv->frame_capacity();

  // Validate the baked assumptions against the machine that turned up, HERE,
  // rather than discovering them as garbage half a second into play. The
  // numbers are the ones docs/gameplay.md was designed against.
  if (width < 256 || height < 200)
    return rv_pdk::RV_ERR_INVAL;
  if (capacity < 1024)
    return rv_pdk::RV_ERR_INVAL;
  if (cio->iport_count() < 1)
    return rv_pdk::RV_ERR_INVAL;

  const int64_t rc = assets_.load(pdk);
  if (rc < 0)
    return rc;

  gfx_.attach(cv, width, height, capacity);
  game_.initialize(pdk, assets_, gfx_);

  initialized_ = true;
  return rv_pdk::RV_OK;
}

void rv_dmain::frame_update(float dt) {
  if (!initialized_)
    return;

  sm_input input{};
  reader_.sample(pdk_->cio(), game_.injected_pad(), input);

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
  assets_.unload();
  initialized_ = false;
}

} // namespace solidmaid

RV_DISC_EXPORT(solidmaid::rv_dmain)
