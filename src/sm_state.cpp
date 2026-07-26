#include "sm_state.hpp"

#include <cstring>

#include "pdk/rv_err.hpp"

namespace solidmaid {
namespace {

void write_u32(uint8_t *p, uint32_t value) {
  p[0] = static_cast<uint8_t>(value & 0xFF);
  p[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  p[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  p[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

uint32_t read_u32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

} // namespace

int sm_countdown::tier() const {
  int t = SM_SHIFTS_START - shifts_remaining;
  if (t < 0)
    t = 0;
  if (t > SM_TIER_COUNT - 1)
    t = SM_TIER_COUNT - 1;
  return t;
}

bool sm_countdown::lamp_lit(int index) const {
  if (index < 0 || index >= 5)
    return false;
  // Exactly `shifts_remaining` lamps are lit, and they are always the LAST
  // ones in the extinguish order — L1 (the courtyard, his own doorway) dies
  // first, L5 (the factory gate) last. So lamp i is lit while i >= 5 - N.
  return index >= (5 - lamps_lit());
}

int sm_countdown::lamps_lit() const {
  if (shifts_remaining < 0)
    return 0;
  if (shifts_remaining > 5)
    return 5;
  return shifts_remaining;
}

int sm_countdown::apartment_state() const { return tier(); }

bool sm_countdown::commit_completed_assembly() {
  // The guard: only a finished three-step assembly moves the number.
  if (assembly_step < SM_ASSEMBLY_STEPS)
    return false;
  if (shifts_remaining <= 0)
    return false;

  --shifts_remaining;
  assembly_step = 0;
  return true;
}

void sm_countdown::restart_current_shift() {
  // shifts_remaining is deliberately NOT touched. "Dying never makes a player
  // feel they lost a shift" (docs/mechanics.md).
  assembly_step = 0;
  phase = SM_PHASE_HOME;
}

void sm_save_encode(const sm_countdown &state, uint8_t out[SM_SAVE_BYTES]) {
  std::memset(out, 0, SM_SAVE_BYTES);
  write_u32(out + 0, SM_SAVE_MAGIC);
  write_u32(out + 4, static_cast<uint32_t>(state.shifts_remaining));
  write_u32(out + 8, static_cast<uint32_t>(state.phase));
  out[12] = static_cast<uint8_t>(state.assembly_step);
  out[13] = state.finished ? 1u : 0u;
}

bool sm_save_decode(const uint8_t data[SM_SAVE_BYTES], sm_countdown &out) {
  if (read_u32(data) != SM_SAVE_MAGIC)
    return false;

  const int32_t shifts = static_cast<int32_t>(read_u32(data + 4));
  const int32_t phase = static_cast<int32_t>(read_u32(data + 8));
  const int step = data[12];

  // Refuse anything out of range rather than clamping it: a blob that does not
  // describe a reachable state is somebody else's save or a corrupted one, and
  // starting fresh is safer than resuming into a shift that cannot exist.
  if (shifts < 0 || shifts > SM_SHIFTS_START)
    return false;
  if (phase < SM_PHASE_HOME || phase > SM_PHASE_FINAL)
    return false;
  if (step < 0 || step > SM_ASSEMBLY_STEPS)
    return false;

  out.shifts_remaining = shifts;
  out.phase = static_cast<sm_phase>(phase);
  out.assembly_step = step;
  out.finished = data[13] != 0;
  return true;
}

bool sm_save_store(rv_pdk::rv_cm *cm, const sm_countdown &state) {
  if (!cm)
    return false;
  if (cm->card_slots() <= SM_SAVE_SLOT)
    return false;
  if (cm->card_slot_size() < SM_SAVE_BYTES)
    return false;

  uint8_t blob[SM_SAVE_BYTES];
  sm_save_encode(state, blob);
  return cm->card_write(SM_SAVE_SLOT, blob, SM_SAVE_BYTES) >= 0;
}

bool sm_save_load(rv_pdk::rv_cm *cm, sm_countdown &out) {
  if (!cm)
    return false;
  if (cm->card_slots() <= SM_SAVE_SLOT)
    return false;

  const int64_t size = cm->card_size(SM_SAVE_SLOT);
  if (size < SM_SAVE_BYTES)
    return false;

  uint8_t blob[SM_SAVE_BYTES];
  if (cm->card_read(SM_SAVE_SLOT, blob, SM_SAVE_BYTES) < 0)
    return false;
  return sm_save_decode(blob, out);
}

} // namespace solidmaid
