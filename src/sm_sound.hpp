// Solidmaid — the sound chip, as this game uses it.
//
// The console gives a disc a pool of sound RAM and 24 voices, and nothing else:
// no sequencer, no music player, no mixer state (pdk/ca/rv_ca.hpp — the
// high-level rv_snd layer is DEFERRED). So everything above "upload bytes,
// arm a voice, start it" is here.
//
// Three facts shape the whole file:
//
//   * NO PITCH CONTROL. rv_voice_conf has no rate field; a sample plays only at
//     44 100 Hz. Every distinct pitch is therefore a distinct sample, and two
//     identical samples fired on the same frame sum IN PHASE and read as one
//     loud hit rather than as two — which is why the footsteps ship in pairs
//     and are alternated rather than repeated.
//
//   * LOOP IS WHOLE-SAMPLE. rv_loop::forever repeats the entire sample with no
//     loop points, so a bed has to meet itself. The melody here does not use it
//     at all: it is a run of one-second bars played back to back (see below).
//
//   * VOICES SUM IN int32 AND CLIP AT THE int16 RAILS, undivided (specs.md).
//     Per-voice volume is held well under unity because a hit, two footsteps and
//     a bar of music landing together is an ordinary frame, not a worst case.
#pragma once

#include <cstdint>
#include <vector>

#include "pdk/rv_pdko.hpp"

namespace solidmaid {

// Every sample the game can fire. The order is the load order and the resource
// names live beside it in sm_sound.cpp.
enum sm_sfx : int {
  SM_SFX_UI_PROMPT = 0,
  SM_SFX_PICKUP,
  SM_SFX_PLAYER_HURT,
  SM_SFX_PLAYER_DEATH,

  SM_SFX_BRICK_THROW,
  SM_SFX_BRICK_HIT_HARD,
  SM_SFX_BRICK_HIT_SOFT,
  SM_SFX_PIPE_SWING,
  SM_SFX_PIPE_HIT,

  SM_SFX_ENEMY_DOWN,
  SM_SFX_KIPUCHKA_WINDUP,
  SM_SFX_KIPUCHKA_STEP,
  SM_SFX_SMOKER_PREWARM,
  SM_SFX_SMOKER_ATTACK,

  SM_SFX_ASSEMBLY_BREAK,
  SM_SFX_BOARD_CLACK,

  SM_SFX_STEP_LINO_A,
  SM_SFX_STEP_LINO_B,
  SM_SFX_STEP_ASPHALT_A,
  SM_SFX_STEP_ASPHALT_B,
  SM_SFX_STEP_CONCRETE_A,
  SM_SFX_STEP_CONCRETE_B,

  // The factory's two continuous loops. They are listed here because they load
  // and are audited like every other sample, but they are never fired by
  // play(): a one-shot voice would restart them every time, and what they need
  // is rv_loop::forever on a voice of their own. set_loop() is their only door.
  SM_SFX_LOOP_CONVEYOR,
  SM_SFX_LOOP_ASSEMBLY,

  SM_SFX_COUNT,
};

// The two continuous voices, by what they are rather than by index.
enum sm_loop_id : int {
  SM_LOOP_CONVEYOR = 0,
  SM_LOOP_ASSEMBLY,
  SM_LOOP_COUNT,
};

// Which melody is running. One per area; the final lap runs none.
enum sm_song : int {
  SM_SONG_NONE = 0,
  SM_SONG_HOME,
  SM_SONG_STREET,
  SM_SONG_FACTORY,
};

class sm_sound {
public:
  // Uploads every sample that is resident for the whole run, and reserves the
  // regions that are rewritten during it: two for the music bars, two for the
  // footstep pair of whatever area the player is standing in. A missing sample is
  // counted, never fatal: the game is designed to be finishable in silence
  // (every telegraph and every confirmation has a visual half), so a broken
  // audio bank must not stop somebody playing.
  int64_t load(rv_pdk::rv_pdko &pdk);
  void unload();

  bool ready() const { return ca_ != nullptr; }

  // Put this area's footsteps into the two shared slots. Immediate: it is a
  // copy into regions that already exist, not an allocation, so there is no
  // waiting on the mixer and nothing that can fail for lack of room.
  void set_area(sm_song area);
  sm_song area() const { return area_; }
  int missing() const { return missing_; }
  int64_t sound_bytes() const { return sound_bytes_; }

  // Fire a one-shot. `gain` scales the authored level, `pan` is -1 left to +1
  // right. Picks a free voice; if every effect voice is busy the oldest is
  // stolen, because a missing hit confirmation is worse than a clipped tail.
  void play(sm_sfx id, float gain = 1.0f, float pan = 0.0f);

  // A footstep on the surface of `song`'s area, alternating the two authored
  // variants. Silent if the area has no floor sound.
  void footstep(sm_song area, float gain = 1.0f);

  // A continuous bed, on or off. Idempotent: calling it every frame with the
  // same answer does nothing, which is what lets the game state it as a fact
  // ("the conveyor runs in the factory") instead of tracking edges.
  void set_loop(sm_loop_id id, bool on);
  bool looping(sm_loop_id id) const;

  // Start / stop the melody. Bars are streamed: see the note in sm_sound.cpp.
  // `max_bars` caps how much of the melody is used. docs/art-and-audio.md wants
  // the ramp to read as EMPTIER rather than as quieter — "Nothing is ever added
  // to compensate. Keep perceived loudness consistent as layers thin" — so the
  // count takes material away, never volume.
  void play_song(sm_song song, int max_bars = 0);
  void stop_song();
  sm_song song() const { return song_; }
  // Bars handed to a voice since the run began. The sequencer is invisible
  // otherwise, and a number that climbs once a second is how a headless run
  // proves the streaming loop is alive.
  int bars_played() const { return bars_played_; }
  // How many times each effect actually reached a voice. The audit that matters:
  // a cue that is wired but never fires looks identical to one that was never
  // wired at all, and only a real playthrough tells them apart.
  int fired(sm_sfx id) const {
    return (id >= 0 && id < SM_SFX_COUNT) ? fired_[id] : 0;
  }
  static const char *name_of(sm_sfx id);

  // Advances the sequencer and streams the next bar. Must be called with the
  // REAL frame dt, not one scaled by hitstop — music does not stop when the
  // world does.
  void update(float dt);

private:
  struct sm_slot {
    int64_t address = 0;
    int64_t voice = 0;
    bool needs_refill = false;
  };

  bool upload(const char *resource, int64_t address, int64_t capacity);
  void arm_and_play(int64_t voice_mask, int64_t address, float gain, float pan,
                    bool music);
  int64_t free_effect_voice();

  rv_pdk::rv_pdko *pdk_ = nullptr;
  rv_pdk::rv_ca *ca_ = nullptr;

  int64_t effects_[SM_SFX_COUNT] = {};
  // The two regions the three footstep surfaces take turns living in.
  int64_t step_slot_[2] = {};
  int64_t step_slot_bytes_[2] = {};
  sm_song area_ = SM_SONG_NONE;
  int fired_[SM_SFX_COUNT] = {};
  int missing_ = 0;
  int64_t sound_bytes_ = 0;

  // Music, as two rewritable slots one bar long each.
  sm_slot slots_[2];
  int playing_ = 0;      // which slot is sounding
  int bar_ = 0;          // index into the current song's bar list
  float clock_ = 0.0f;   // seconds into the sounding bar
  sm_song song_ = SM_SONG_NONE;
  int bar_limit_ = 0;
  int bars_played_ = 0;
  bool started_ = false;

  bool loop_on_[SM_LOOP_COUNT] = {};

  std::vector<uint8_t> scratch_;
  int step_toggle_ = 0;
  int next_effect_voice_ = 0;
};

} // namespace solidmaid
