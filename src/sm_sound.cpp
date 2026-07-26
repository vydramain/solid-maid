#include "sm_sound.hpp"

#include "pdk/ca/rv_ca.hpp"
#include "pdk/cd/rv_cd.hpp"
#include "pdk/rv_err.hpp"

namespace solidmaid {
namespace {

// One second of the console's format: raw S16LE mono at 44 100 Hz.
constexpr int64_t SM_BYTES_PER_SECOND = 44100 * 2;

// Every bar of every melody is exactly 1.000 s, which tools/prep_audio.py
// enforces by never trimming music. That is also exactly 60 frames at 60 fps,
// and it is what lets the sequencer land on the beat instead of near it.
constexpr float SM_BAR_SECONDS = 1.0f;
constexpr int64_t SM_BAR_BYTES = SM_BYTES_PER_SECOND;

// THE SEAM, AND WHY THE NEXT BAR STARTS EARLY.
//
// The contract has no scheduled start: voice_play() begins "now", there is no
// queue, and the mixer runs on its own thread. The finest the game can aim is
// one frame. So a handoff can land late — leaving a hole in the melody — or
// early — overlapping the tail of the outgoing bar by a few milliseconds.
//
// Early is strictly better. A gap is heard as a stumble; an overlap of under a
// frame across a bar line is heard as nothing at all, because the outgoing bar
// is already decaying there. The lead below is a shade over one frame at 60 fps,
// so the melody is never silent between bars.
//
// The clock is also advanced by the IDEAL bar length rather than reset to zero,
// so the error is bounded at one frame instead of accumulating a frame per bar
// into a tempo that visibly drifts.
constexpr float SM_BAR_LEAD = 0.018f;

// Voice allocation over the console's 24. Music takes two — one per slot, so
// the outgoing bar can ring while the incoming one starts. The effects rotate
// through the middle block; the top eight are left alone, which is the headroom
// docs/art-and-audio.md asks for.
constexpr int64_t SM_VOICE_MUSIC_A = 0;
constexpr int64_t SM_VOICE_MUSIC_B = 1;
constexpr int64_t SM_EFFECT_FIRST = 2;
constexpr int64_t SM_EFFECT_COUNT = 12;

// Per-voice levels. The SAMPLES already carry the balance — effects were
// normalised to -2 dBFS and music to -8, half the amplitude — so these are
// headroom against summing, not a second mix.
constexpr int16_t SM_VOLUME_EFFECT = 22000;
constexpr int16_t SM_VOLUME_MUSIC = 10000; // half: the melody sits under the game

struct sm_sfx_desc {
  const char *resource;
};

constexpr sm_sfx_desc SM_EFFECTS[SM_SFX_COUNT] = {
    {"sfx_ui_prompt.pcm"},        {"sfx_pickup.pcm"},
    {"sfx_player_hurt.pcm"},      {"sfx_player_death.pcm"},

    {"sfx_brick_throw.pcm"},      {"sfx_brick_hit_hard.pcm"},
    {"sfx_brick_hit_soft.pcm"},   {"sfx_pipe_swing.pcm"},
    {"sfx_pipe_hit.pcm"},

    {"sfx_enemy_down.pcm"},       {"sfx_kipuchka_windup.pcm"},
    {"sfx_kipuchka_step.pcm"},    {"sfx_smoker_prewarm.pcm"},
    {"sfx_smoker_attack.pcm"},

    {"sfx_assembly_break.pcm"},   {"sfx_board_clack.pcm"},

    {"sfx_step_lino_a.pcm"},      {"sfx_step_lino_b.pcm"},
    {"sfx_step_asphalt_a.pcm"},   {"sfx_step_asphalt_b.pcm"},
    {"sfx_step_concrete_a.pcm"},  {"sfx_step_concrete_b.pcm"},
};

// The melodies, bar by bar, in playing order. They live on the medium — which
// has no size ceiling (specs.md) — and reach sound RAM two at a time.
constexpr const char *SM_SONG_HOME_BARS[] = {
    "mus_home_01.pcm", "mus_home_02.pcm", "mus_home_03.pcm", "mus_home_04.pcm",
    "mus_home_05.pcm", "mus_home_06.pcm", "mus_home_07.pcm", "mus_home_08.pcm",
    "mus_home_09.pcm", "mus_home_10.pcm",
};
constexpr const char *SM_SONG_STREET_BARS[] = {
    "mus_street_01.pcm", "mus_street_02.pcm", "mus_street_03.pcm",
    "mus_street_04.pcm", "mus_street_05.pcm",
};
constexpr const char *SM_SONG_FACTORY_BARS[] = {
    "mus_factory_01.pcm", "mus_factory_02.pcm", "mus_factory_03.pcm",
};

const char *const *bars_of(sm_song song, int &count) {
  switch (song) {
  case SM_SONG_HOME:
    count = 10;
    return SM_SONG_HOME_BARS;
  case SM_SONG_STREET:
    count = 5;
    return SM_SONG_STREET_BARS;
  case SM_SONG_FACTORY:
    count = 3;
    return SM_SONG_FACTORY_BARS;
  default:
    break;
  }
  count = 0;
  return nullptr;
}

int16_t scale_volume(int16_t base, float k) {
  if (!(k > 0.0f))
    return 0;
  float v = static_cast<float>(base) * k;
  if (v > 32767.0f)
    v = 32767.0f;
  return static_cast<int16_t>(v);
}

} // namespace

bool sm_sound::upload(const char *resource, int64_t address, int64_t capacity) {
  rv_pdk::rv_cd *cd = pdk_ ? pdk_->cd() : nullptr;
  if (!cd || !ca_)
    return false;

  const int64_t handle = cd->asset_open(resource);
  if (handle < 0)
    return false;
  const int64_t size = cd->asset_size(handle);
  if (size <= 0 || size > capacity)
    return false;

  scratch_.assign(static_cast<std::size_t>(size), 0);
  // asset_read's return is the authoritative length; asset_size is a hint that
  // may go stale between the two calls (pdk/cd/rv_cd.hpp).
  const int64_t read = cd->asset_read(handle, scratch_.data(), size);
  if (read <= 0)
    return false;

  rv_pdk::rv_sample sample{};
  sample.data = scratch_.data();
  sample.size = read;
  return ca_->sound_asset_write(address, &sample) >= 0;
}

int64_t sm_sound::load(rv_pdk::rv_pdko &pdk) {
  pdk_ = &pdk;
  ca_ = pdk.ca();
  if (!ca_)
    return rv_pdk::RV_ERR_INVAL;

  // Effects first, so they sit at the bottom of the pool and the music slots —
  // the only regions ever rewritten — live above them. Nothing here is freed
  // during play, so the pool never fragments.
  for (int i = 0; i < SM_SFX_COUNT; ++i) {
    rv_pdk::rv_cd *cd = pdk.cd();
    if (!cd)
      break;
    const int64_t handle = cd->asset_open(SM_EFFECTS[i].resource);
    if (handle < 0) {
      ++missing_;
      continue;
    }
    const int64_t size = cd->asset_size(handle);
    if (size <= 0) {
      ++missing_;
      continue;
    }
    const int64_t address = ca_->sound_asset_malloc(size);
    if (address < 0) {
      ++missing_;
      continue;
    }
    if (!upload(SM_EFFECTS[i].resource, address, size)) {
      ca_->sound_asset_free(address);
      ++missing_;
      continue;
    }
    effects_[i] = address;
    sound_bytes_ += size;
  }

  // Two bar-length slots, rewritten in place for the rest of the run.
  for (int i = 0; i < 2; ++i) {
    const int64_t address = ca_->sound_asset_malloc(SM_BAR_BYTES);
    if (address < 0) {
      ++missing_;
      continue;
    }
    slots_[i].address = address;
    slots_[i].voice = (i == 0) ? SM_VOICE_MUSIC_A : SM_VOICE_MUSIC_B;
    sound_bytes_ += SM_BAR_BYTES;
  }

  return rv_pdk::RV_OK;
}

void sm_sound::unload() {
  if (!ca_)
    return;
  stop_song();

  for (int i = 0; i < SM_SFX_COUNT; ++i) {
    if (effects_[i] != 0)
      ca_->sound_asset_free(effects_[i]);
    effects_[i] = 0;
  }
  for (int i = 0; i < 2; ++i) {
    if (slots_[i].address != 0)
      ca_->sound_asset_free(slots_[i].address);
    slots_[i] = sm_slot{};
  }
  sound_bytes_ = 0;
  ca_ = nullptr;
}

void sm_sound::arm_and_play(int64_t voice_mask, int64_t address, float gain,
                            float pan, bool music) {
  if (!ca_ || address == 0)
    return;

  // A music BAR is a one-shot like everything else — the melody is sequenced,
  // not looped — so the level cannot be inferred from loop_type.
  const int16_t base = music ? SM_VOLUME_MUSIC : SM_VOLUME_EFFECT;
  const float left = pan <= 0.0f ? 1.0f : (1.0f - pan);
  const float right = pan >= 0.0f ? 1.0f : (1.0f + pan);

  rv_pdk::rv_voice_conf conf{};
  conf.voice = voice_mask;
  conf.loop_type = rv_pdk::rv_loop::none;
  conf.sample_address = address;
  // ar = 0 opens the envelope on the first frame and sr = 0 holds it there
  // (src/rv_pconsole/ca/rv_pcvoice.cpp): a rate of zero means "arrive
  // immediately", so this is a flat, unshaped playback of exactly the bytes
  // that were authored. The shaping was done in the DAW, where it can be heard.
  conf.ar = 0;
  conf.dr = 0;
  conf.sr = 0;
  conf.rr = 0;
  conf.sl = 32767;
  // The console multiplies these: gain_l = gain_of(volume) * gain_of(volume_l)
  // (src/rv_pconsole/ca/rv_pcvoice.cpp). So `volume` carries the LEVEL and the
  // per-channel pair carries the PAN ALONE, at unity in the centre. Putting the
  // level in both squares it — which is how a sound meant for -8 dBFS came out
  // near -26 and read as silence.
  conf.volume = scale_volume(base, gain);
  conf.volume_l = scale_volume(32767, left);
  conf.volume_r = scale_volume(32767, right);

  if (ca_->voice_setup(&conf) < 0)
    return;
  ca_->voice_play(voice_mask);
}

int64_t sm_sound::free_effect_voice() {
  if (!ca_)
    return 0;

  const int64_t block = ((1LL << SM_EFFECT_COUNT) - 1) << SM_EFFECT_FIRST;
  const int64_t busy = ca_->voice_status(block);
  if (busy >= 0) {
    for (int i = 0; i < SM_EFFECT_COUNT; ++i) {
      const int64_t index = SM_EFFECT_FIRST + i;
      const int64_t bit = 1LL << index;
      if ((busy & bit) == 0)
        return bit;
    }
  }

  // All busy. Steal in rotation rather than dropping the sound: a hit the
  // player does not hear reads as a hit that did not land.
  const int64_t index = SM_EFFECT_FIRST + (next_effect_voice_ % SM_EFFECT_COUNT);
  ++next_effect_voice_;
  return 1LL << index;
}

const char *sm_sound::name_of(sm_sfx id) {
  if (id < 0 || id >= SM_SFX_COUNT)
    return "?";
  return SM_EFFECTS[id].resource;
}

void sm_sound::play(sm_sfx id, float gain, float pan) {
  if (!ca_ || id < 0 || id >= SM_SFX_COUNT)
    return;
  const int64_t address = effects_[id];
  if (address == 0)
    return;
  ++fired_[id];
  arm_and_play(free_effect_voice(), address, gain, pan, false);
}

void sm_sound::footstep(sm_song area, float gain) {
  sm_sfx a = SM_SFX_COUNT;
  switch (area) {
  case SM_SONG_HOME:
    a = SM_SFX_STEP_LINO_A;
    break;
  case SM_SONG_STREET:
    a = SM_SFX_STEP_ASPHALT_A;
    break;
  case SM_SONG_FACTORY:
    a = SM_SFX_STEP_CONCRETE_A;
    break;
  default:
    return;
  }
  // Alternate the pair. With no pitch control the same sample twice in a row is
  // audibly the same sample twice in a row, and a walk cycle is the one place a
  // player would notice.
  const sm_sfx pick = (step_toggle_ & 1) ? static_cast<sm_sfx>(a + 1) : a;
  step_toggle_ ^= 1;
  play(pick, gain);
}

void sm_sound::play_song(sm_song song, int max_bars) {
  if (!ca_ || (song == song_ && max_bars == bar_limit_))
    return;
  stop_song();
  if (song == SM_SONG_NONE)
    return;

  int count = 0;
  const char *const *bars = bars_of(song, count);
  if (!bars || count <= 0 || slots_[0].address == 0 || slots_[1].address == 0)
    return;

  if (max_bars > 0 && max_bars < count)
    count = max_bars < 2 ? 2 : max_bars;
  bar_limit_ = max_bars;

  song_ = song;
  bar_ = 0;
  playing_ = 0;
  clock_ = 0.0f;

  // Fill both slots before the first note: bar 0 to sound now, bar 1 behind it.
  if (!upload(bars[0], slots_[0].address, SM_BAR_BYTES)) {
    song_ = SM_SONG_NONE;
    return;
  }
  if (count > 1)
    upload(bars[1 % count], slots_[1].address, SM_BAR_BYTES);

  arm_and_play(1LL << slots_[0].voice, slots_[0].address, 1.0f, 0.0f, true);
  ++bars_played_;
  started_ = true;
}

void sm_sound::stop_song() {
  if (ca_ && started_) {
    ca_->voice_stop((1LL << slots_[0].voice) | (1LL << slots_[1].voice));
  }
  song_ = SM_SONG_NONE;
  bar_limit_ = 0;
  started_ = false;
  slots_[0].needs_refill = false;
  slots_[1].needs_refill = false;
}

void sm_sound::update(float dt) {
  if (!ca_ || !started_ || song_ == SM_SONG_NONE || dt <= 0.0f)
    return;

  int count = 0;
  const char *const *bars = bars_of(song_, count);
  if (!bars || count <= 0)
    return;
  if (bar_limit_ > 0 && bar_limit_ < count)
    count = bar_limit_ < 2 ? 2 : bar_limit_;

  clock_ += dt;

  if (clock_ >= SM_BAR_SECONDS - SM_BAR_LEAD) {
    const int next = playing_ ^ 1;
    arm_and_play(1LL << slots_[next].voice, slots_[next].address, 1.0f, 0.0f,
                 true);

    // The slot that was sounding is now the one to refill — but not yet: its
    // voice is still ringing out the overlap. Marked here, written below once
    // the console says the region is idle.
    slots_[playing_].needs_refill = true;
    playing_ = next;
    ++bars_played_;
    bar_ = (bar_ + 1) % count;

    // Advance by the IDEAL length rather than resetting, so a frame of lateness
    // is not carried into the next bar and multiplied by the bar after it.
    clock_ -= SM_BAR_SECONDS;
    if (clock_ < 0.0f)
      clock_ = 0.0f;
  }

  // Stream the bar after next into whichever slot has fallen silent. One disc
  // read and one copy of 86 KiB per second, on a frame that is doing nothing
  // else — and never into a region a voice is still reading from.
  for (int i = 0; i < 2; ++i) {
    if (!slots_[i].needs_refill)
      continue;
    const int64_t bit = 1LL << slots_[i].voice;
    const int64_t busy = ca_->voice_status(bit);
    if (busy > 0)
      continue;
    const int ahead = (bar_ + 1) % count;
    upload(bars[ahead], slots_[i].address, SM_BAR_BYTES);
    slots_[i].needs_refill = false;
  }
}

} // namespace solidmaid
