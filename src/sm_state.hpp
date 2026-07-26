// Solidmaid — the one number, and everything derived from it.
//
// docs/mechanics.md is unusually strict about this file's job, so the rules are
// repeated here as the contract:
//
//   ONE WRITER. shifts_remaining decrements at exactly one place in the code —
//   the moment the lamppost assembly completes. Nowhere else. Not on death, not
//   on time, not on kills.
//
//   EVERYTHING ELSE DERIVES. Board digit, lit-lamp count, apartment dressing
//   index and palette tier are pure functions of shifts_remaining. They are
//   never stored, never advanced independently, and never cached — "two sources
//   of truth is how the board and the street get out of sync".
//
//   NEVER ON THE HUD. The count reaches the player through the factory board,
//   the street lighting and the apartment. There is no readout anywhere.
#pragma once

#include <cstdint>

#include "pdk/cm/rv_cm.hpp"

#include "sm_common.hpp"

namespace solidmaid {

// The entire save: four fields (docs/content.md, "Progression & persistence").
struct sm_countdown {
  int shifts_remaining = SM_SHIFTS_START;
  sm_phase phase = SM_PHASE_HOME;
  int assembly_step = 0; // 0..3, within-shift only, cleared on restart
  bool finished = false; // the run walked the final lap

  // 0 at shift 5 (brightest) .. 5 on the final lap (darkest).
  int tier() const;

  // Lamp L1..L5 by index 0..4. The extinguish order is authored and fixed,
  // read from the courtyard outward: L1 goes first, the factory gate L5 last
  // and only for the final lap (docs/content.md).
  bool lamp_lit(int index) const;
  int lamps_lit() const;

  // Apartment dressing state 0..5. Same number as the tier by construction —
  // that is the point, and the reason it is derived rather than stored.
  int apartment_state() const;

  // The final lap: same route, work removed, board reading zero.
  bool is_final_lap() const { return shifts_remaining <= 0; }

  // The digit painted on the board. The ONLY number in the game.
  int board_digit() const {
    return shifts_remaining < 0 ? 0 : shifts_remaining;
  }

  // The single decrement point. Returns false and changes nothing if called
  // anywhere other than a completed assembly — the guard is here so the rule
  // is enforced by the type, not by reviewer memory.
  bool commit_completed_assembly();

  // Losing costs time, never progress: within-shift state resets and the
  // count is untouched.
  void restart_current_shift();
};

// Fixed-size little-endian blob, 16 bytes. Version-tagged so a future field is
// an append rather than a silent misread of somebody's old save.
constexpr int SM_SAVE_BYTES = 16;

void sm_save_encode(const sm_countdown &state, uint8_t out[SM_SAVE_BYTES]);
bool sm_save_decode(const uint8_t data[SM_SAVE_BYTES], sm_countdown &out);

// Card I/O. Both are best-effort: a missing or unreadable card starts a fresh
// run rather than refusing to play, because the design has nothing worth
// blocking on (docs/content.md: "no real save system needed").
bool sm_save_store(rv_pdk::rv_cm *cm, const sm_countdown &state);
bool sm_save_load(rv_pdk::rv_cm *cm, sm_countdown &out);

} // namespace solidmaid
