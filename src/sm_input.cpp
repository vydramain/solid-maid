#include "sm_input.hpp"

#include <cmath>

#include "sm_common.hpp"

namespace solidmaid {
namespace {

// A trigger reads as pulled well before it bottoms out; the console also raises
// a SOFT_PULL bit at its own threshold, and either is enough.
constexpr float SM_TRIGGER_THRESHOLD = 0.35f;

// Radial dead zone, applied to the vector rather than per axis: a per-axis dead
// zone squares off the diagonals and makes a stick feel notched.
void deadzone(float x, float y, float threshold, float &out_x, float &out_y) {
  const float magnitude = std::sqrt(x * x + y * y);
  if (magnitude <= threshold || magnitude <= 0.0001f) {
    out_x = 0.0f;
    out_y = 0.0f;
    return;
  }
  // Rescale so the first live sample is near zero instead of jumping to the
  // threshold — otherwise the stick has a visible step at the edge of the zone.
  const float scaled = (magnitude - threshold) / (1.0f - threshold);
  const float clamped = scaled > 1.0f ? 1.0f : scaled;
  out_x = x / magnitude * clamped;
  out_y = y / magnitude * clamped;
}

} // namespace

bool sm_hand_right_active(const rv_pdk::rv_istate &state,
                          bool allow_face_button) {
  // Kept in the signature so the keyboard fallback can come back without
  // touching every caller; the body currently answers on the triggers alone.
  (void)allow_face_button;
  if (state.right_trigger >= SM_TRIGGER_THRESHOLD)
    return true;
  if (state.buttons & rv_pdk::RV_ISOURCE_RIGHT_TRIGGER_SOFT_PULL)
    return true;
  return false;
}

bool sm_hand_left_active(const rv_pdk::rv_istate &state,
                         bool allow_face_button) {
  (void)allow_face_button;
  if (state.left_trigger >= SM_TRIGGER_THRESHOLD)
    return true;
  if (state.buttons & rv_pdk::RV_ISOURCE_LEFT_TRIGGER_SOFT_PULL)
    return true;
  return false;
}

void sm_input_reader::sample(rv_pdk::rv_cio *cio,
                             const rv_pdk::rv_istate *injected, sm_input &out) {
  rv_pdk::rv_istate state{};
  if (injected) {
    state = *injected;
  } else if (cio) {
    state = cio->iport_state(0);
  }

  // Ask the port what it can do rather than guessing. A pad advertises its
  // trigger axes; the keyboard overlay advertises none, and only then do the
  // face buttons stand in for them.
  bool allow_face_button = true;
  if (cio) {
    const uint64_t abilities = cio->iport_abilities(0);
    const uint64_t triggers = rv_pdk::RV_ISOURCE_LEFT_TRIGGER_SOFT_PULL |
                              rv_pdk::RV_ISOURCE_RIGHT_TRIGGER_SOFT_PULL;
    allow_face_button = (abilities & triggers) == 0;
  }

  out = sm_input{};
  deadzone(state.left_stick.x, state.left_stick.y, SM_MOVE_DEADZONE, out.move_x,
           out.move_y);
  deadzone(state.right_stick.x, state.right_stick.y, SM_LOOK_DEADZONE,
           out.look_x, out.look_y);

  const bool hand_right = sm_hand_right_active(state, allow_face_button);
  const bool hand_left = sm_hand_left_active(state, allow_face_button);

  out.hand_right = hand_right;
  out.hand_left = hand_left;
  out.hand_right_pressed = hand_right && !previous_hand_right_;
  out.hand_left_pressed = hand_left && !previous_hand_left_;
  out.hand_right_released = !hand_right && previous_hand_right_;
  out.hand_left_released = !hand_left && previous_hand_left_;

  const uint64_t rising = state.buttons & ~previous_buttons_;
  out.confirm_pressed = (rising & rv_pdk::RV_ISOURCE_FRONT_BTTN_SOUTH) != 0;
  out.cancel_pressed = (rising & rv_pdk::RV_ISOURCE_FRONT_BTTN_EAST) != 0;
  out.menu_pressed = (rising & rv_pdk::RV_ISOURCE_MENU_BTTN_MENU) != 0;
  out.menu_held = (state.buttons & rv_pdk::RV_ISOURCE_MENU_BTTN_MENU) != 0;
  out.view_pressed = (rising & rv_pdk::RV_ISOURCE_MENU_BTTN_VIEW) != 0;
  out.view_held = (state.buttons & rv_pdk::RV_ISOURCE_MENU_BTTN_VIEW) != 0;
  out.view_released = ((previous_buttons_ & ~state.buttons) &
                       rv_pdk::RV_ISOURCE_MENU_BTTN_VIEW) != 0;

  out.raw_buttons = state.buttons;
  out.raw_left_trigger = state.left_trigger;
  out.raw_right_trigger = state.right_trigger;

  previous_buttons_ = state.buttons;
  previous_hand_right_ = hand_right;
  previous_hand_left_ = hand_left;
}

} // namespace solidmaid
