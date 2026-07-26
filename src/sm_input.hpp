// Solidmaid — the control surface, and the only place raw pad state is read.
//
// docs/gameplay.md §4 fixes the scheme: two sticks, two hand buttons, pause.
// Five inputs, nothing else bound, and the console's mouse-look channel
// deliberately unused. "Hand intent, not a verb list" — there is no separate
// interact / attack / throw binding; a hand button says *what the hand does*,
// and what is in the hand plus what is in front of the player decides the rest.
//
// KEYBOARD, AND THE ONE PLACE THE FACE BUTTONS ARE BOUND.
//
// The design is explicit: five inputs, and nothing else bound. The console,
// however, overlays a keyboard onto port 0, and that overlay cannot report a
// trigger at all — it advertises face buttons, the D-pad and the two sticks and
// no trigger source whatsoever (RV_PCHOST_KEYBOARD_ABILITIES in
// src/rv_pconsole/rv_pchost.cpp). A strictly trigger-only disc is therefore
// unplayable on the console's own fallback input.
//
// So the fallback is CONDITIONAL, and the condition is asked of the hardware
// rather than assumed: a hand accepts a face button only on a port that does
// NOT advertise triggers. On a real gamepad the scheme is exactly the five
// inputs the design asks for and the face buttons do nothing; on the keyboard
// overlay, Space/Z and X stand in for the two triggers.
//
// To change this, there is exactly one place: sm_hand_right_active() and
// sm_hand_left_active() in sm_input.cpp.
#pragma once

#include <cstdint>

#include "pdk/cio/rv_cio.hpp"

namespace solidmaid {

// One frame of intent, already deadzoned, curved and edge-detected. Systems
// read this; nothing downstream touches rv_istate.
struct sm_input {
  float move_x = 0.0f; // strafe, -1 left .. +1 right
  float move_y = 0.0f; // forward, -1 back .. +1 forward
  float look_x = 0.0f; // raw deflection, the curve is applied by the player
  float look_y = 0.0f;

  bool hand_right = false; // held this frame
  bool hand_left = false;
  bool hand_right_pressed = false; // rising edge
  bool hand_left_pressed = false;
  bool hand_right_released = false; // falling edge
  bool hand_left_released = false;

  // A / cross and B / circle, as themselves. The two hands are the triggers and
  // only the triggers (docs/gameplay.md §4); these exist for the menus, where a
  // face button is what a player reaches for.
  bool confirm_pressed = false;
  bool cancel_pressed = false;

  bool menu_pressed = false;
  bool menu_held = false;     // level, for the hold-to-restart
  bool view_pressed = false;  // rising edge
  bool view_held = false;     // level, for the hold-to-restart
  bool view_released = false; // falling edge

  // The untouched snapshot, for the debug overlay only. Nothing in the game
  // reads these — they exist so a controller that reports the wrong button can
  // be identified in one press instead of by argument.
  uint64_t raw_buttons = 0;
  float raw_left_trigger = 0.0f;
  float raw_right_trigger = 0.0f;
};

// Snapshot-diff edge detection lives here: rv_cio reports the current LEVEL of
// every source and never an event, so "pressed this frame" is (now & ~was).
class sm_input_reader {
public:
  // Reads port 0. `injected` overrides the pad entirely when non-null — that
  // is how the test harness drives a real playthrough through the real game
  // code without a physical controller attached.
  void sample(rv_pdk::rv_cio *cio, const rv_pdk::rv_istate *injected,
              sm_input &out);

private:
  uint64_t previous_buttons_ = 0;
  bool previous_hand_right_ = false;
  bool previous_hand_left_ = false;
};

// Shared by the reader and the test harness, so a synthesised pad state goes
// through exactly the same interpretation as a physical one.
// `allow_face_button` is false whenever the port has real triggers — see the
// note above.
bool sm_hand_right_active(const rv_pdk::rv_istate &state,
                          bool allow_face_button);
bool sm_hand_left_active(const rv_pdk::rv_istate &state,
                         bool allow_face_button);

} // namespace solidmaid
