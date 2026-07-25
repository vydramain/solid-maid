# Art & Audio

Everything here is bound by the console budget. See
[`../../../docs/platform/specs.md`](../../../docs/platform/specs.md) — the numbers
below are the reference console's answers, and **the video and sound budgets are
contractually enforced**: the pools answer `RV_ERR_NOMEM` when they run out, and
`mppcburner` re-checks them at pack time.

| Budget | Value | Enforced? |
| --- | --- | --- |
| Frame | 320×240, 16-bit + ordered dithering | — |
| Texture formats | 4-bit / 8-bit paletted (no filtering) + 15-bit direct | yes |
| Max texture | 256×256 | yes, at upload and at pack time |
| Video RAM | 1 MB | yes |
| Primitives | 4096 per frame | yes |
| **Sound RAM** | **512 KB** | **yes** |
| Sound format | **raw PCM, S16LE mono, 44 100 Hz** | fixed |
| Voices | 24 | yes |
| Main RAM | 2 MB | **no** — honoured voluntarily |
| Memory card | 16 slots × 8 KB | yes, per slot |

Buildable assets live in [`../assets/`](../assets/).

Two art jobs define this slice: **make a countdown visible without a number on
screen**, and **fit an entire game's audio into six seconds of sound RAM.** Both
are solved below.

---

## Visual direction

- **Low-poly, low-fi industrial 3D.** Readable forms, limited palette, flat
  materials. Detail comes from silhouette and palette, not polygon count or
  texture resolution — which is exactly what the software rasterizer wants.
- **The console's limits are the style.** Affine texture warping, dithered
  gradients, nearest-sampled texels, and ordering-table sorting are all
  deliberate. See [`gameplay.md`](gameplay.md#3-rendering--the-console-is-the-art-direction)
  for what that means when building geometry.
- **Palette:** strongly limited and practical, grounded in 1990s Russian
  industrial texture. No bright accents, no glossy surfaces. Two light colours
  are held apart on purpose: **streetlights are cold mercury green-cyan**,
  **apartment windows are warm yellow**, and they never blend.
- **Readability first.** At 320×240 with paletted textures, every enemy and prop
  must read by silhouette and one signature colour — **at the darkest playable
  tier**, not just on the well-lit first shift.

Physical descriptions of every space are in
[`environments.md`](environments.md); build to that document.

### Protagonist canon

The protagonist's look is **fixed** — morphology, outfit, T-pose, forbidden
elements. Do not reinterpret it here; follow
[`protagonist_profile.md`](protagonist_profile.md). Reference sketch and model:
[`../assets/`](../assets/).

---

## The impoverishment ramp

Six tiers, one per value of `shifts_remaining` (5 → 0). Each tier is a small,
authored step down. Nothing is procedural, and **nothing here costs memory** —
the ramp is palette selection plus which samples the sequencer triggers.

**Per tier, exactly three things change:**

1. **One more lamppost is off.** Its light-pool plane is removed, the surrounding
   geometry swaps to a darker vertex-colour set, the lamp glass goes dark. The
   pole stays.
2. **The palette steps down.** One tier drops a little saturation and one warm
   hue from the environment ramp, shifting the street toward cold grey-blue.
   Done by **selecting a different palette** for the same atlases.
3. **One music layer stops being triggered** (see the audio section).

The apartment additionally loses one large object per tier, with a trace left
behind — sequence in [`content.md`](content.md#apartment-states), physical
detail in [`environments.md`](environments.md#traces-the-important-art-work).

### Hard readability rules

Constraints, not preferences. A tier that violates one is broken, not
atmospheric.

- **The ambient floor never goes to zero**, final lap included. The countdown
  removes **pools of light**, not the ability to see.
- **Do not solve darkness with far-clip.** Route landmarks, the factory gate,
  enemy silhouettes, and every telegraph must stay legible at `ОСТАЛОСЬ: 1`.
- **Telegraph palette entries are protected** — pre-warm rings, windup tints, and
  interact prompts use entries that stay bright in every tier's palette. A
  telegraph must never depend on a lamppost being alive.
- **The yellow factory floor lines and the gate lamp are navigation anchors** and
  stay readable at every tier.

---

## Texture manifest

**Total: ~300 KiB of 1 MB VRAM.** Comfortable, with room for variants.

Everything is **IDX4** (16-colour paletted, ½ byte per texel) unless a texture
needs transparency or a smooth gradient. IDX4 packs two texels per byte with the
**low nibble as the left texel**, and rows pad to a whole byte — so keep widths
even.

**Palettes cost nothing:** an IDX4 palette is 16 entries × 2 bytes = **32 bytes**.
Six tiers across every atlas is under 2 KiB total. This is why the entire
darkening ramp is free.

### Home — 82 KiB

| # | Texture | Size | Fmt | KiB | Contents |
| --- | --- | --- | --- | --- | --- |
| 1 | `home_walls` | 256×256 | IDX4 | 32 | Wallpaper clean / sun-faded / behind-furniture; **bright unfaded rectangle** (TV); carpet ghost + 4 nail holes; calendar ghost; glue smear; bare plaster; hallway green oil paint with dividing line; lifting seam |
| 2 | `home_floor` | 128×128 | IDX4 | 8 | Linoleum wood print; worn grey walking line; **4 wardrobe dents**; **chalk ring**; bed depressions; brown skirting |
| 3 | `home_furniture` | 256×256 | IDX4 | 32 | Television (case, convex screen, doily), wardrobe/`стенка` veneer + glazed section, table with floral oilcloth, chair + stool, bed + grey blanket + pillow, wall carpet, 199x calendar, framed photo, wall clock, glass in `подстаканник` |
| 4 | `home_window` | 128×128 | IDX4 | 8 | Wooden double frame + `форточка`, glass (day/dusk/night), sill with jars and aloe, cast-iron radiator, greyed tulle, heavy drape |
| 5 | `home_door` | 64×64 | IDX4 | 2 | Panelled door, front door with `дерматин`, switch, socket, external wiring channel |

### Street — 100 KiB

| # | Texture | Size | Fmt | KiB | Contents |
| --- | --- | --- | --- | --- | --- |
| 6 | `street_facade` | 256×256 | IDX4 | 32 | Panel seams, grey-cream pebble dash, rust streaks, window **lit** and **dark** variants, taped/cardboard panes, balcony variants (open rail / mismatched glazing / asbestos sheet), entrance canopy, blue house-number plate, street sign |
| 7 | `street_ground` | 128×128 | IDX4 | 8 | Asphalt, darker patch repairs, cracks, concrete kerb, manhole, puddle, mud verge, faded crossing paint |
| 8 | `street_props` | 256×256 | IDX4 | 32 | PO-2 diamond fence panel, metal garages (3 paint colours), kiosk with shutter + stickers + `24 ЧАСА`, bench, bin, **carpet-beating frame**, swings, sandpit, poplar bark, laundry line, heating-main galvanised casing + concrete supports |
| 9 | `street_lamp` | 64×128 | IDX4 | 4 | Concrete pole, steel bracket arm, `РКУ` bowl fixture — **lamp lit (green-cyan)** and **lamp dead (dark grey-green)** |
| 10 | `light_pool` | 64×64 | direct15 | 8 | The elliptical dithered ground pool decal — needs transparency |
| 11 | `sky` | 128×64 | IDX8 | 8 | Overcast vertical gradient, dithered; 6 tier variants live as palettes |
| 12 | `gate` | 128×128 | IDX4 | 8 | Checkpoint building brick + green door + lit window, steel gates, turnstile, barbed wire, signage |

### Factory — 88 KiB

| # | Texture | Size | Fmt | KiB | Contents |
| --- | --- | --- | --- | --- | --- |
| 13 | `factory_walls` | 256×256 | IDX4 | 32 | Silicate brick, dull-green oil paint to 1.8 m + painted line, flaking whitewash, concrete column with black/yellow hazard band + stencilled number, riveted roof truss, dirty roof-lantern glazing |
| 14 | `factory_floor` | 128×128 | IDX4 | 8 | Oil-stained concrete, swarf, coolant puddles, **yellow floor lines** (walkway + machine boxes) |
| 15 | `factory_machines` | 256×256 | IDX4 | 32 | Assembly bench + vice + fixings + mallet, welding screen, helmet, chained gas cylinders, coiled cable, roller conveyor + guard rail, steel shelving, pallets, drill press, barrel, sack trolley, kettle + enamel mugs, overhead crane gantry |
| 16 | `factory_board` | 128×64 | IDX4 | 4 | **The board.** Painted steel panel, stencilled header `ОСТАЛОСЬ:`, flip digits **0–5**, and `ПЛАН ВЫПОЛНЕН`. Own dim backlight baked in |
| 17 | `factory_signs` | 128×128 | IDX4 | 8 | `СОБЛЮДАЙ ТБ`, `НЕ КУРИТЬ`, `ПОСТОРОННИМ ВХОД ВОСПРЕЩЁН`, honour board with faded photos, wall newspaper, duty roster |
| 18 | `lamppost_parts` | 128×64 | IDX4 | 4 | The three assembly stages + the finished lamppost that rides the conveyor |

### Actors — 14 KiB

| # | Texture | Size | Fmt | KiB | Contents |
| --- | --- | --- | --- | --- | --- |
| 19 | `protagonist` | 64×64 | IDX4 | 2 | **Exists** — `../assets/protagonist_tex.png`, 16 swatches, canon colours |
| 20 | `kipuchka` | 64×64 | IDX4 | 2 | Small jittery pest; one signature colour |
| 21 | `smoker` | 64×64 | IDX4 | 2 | Grey tracksuit, **window-yellow eyes** (protected palette entry) |
| 22 | `smoke` | 64×64 | direct15 | 8 | Cloud billboard flipbook + the **self-lit pre-warm ring** — needs transparency |

### Items, hands, UI — 16 KiB

| # | Texture | Size | Fmt | KiB | Contents |
| --- | --- | --- | --- | --- | --- |
| 23 | `items` | 64×64 | IDX4 | 2 | Brick (world + view), pipe (world + view) |
| 24 | `hands` | 64×64 | IDX4 | 2 | First-person bare hands, coat cuffs |
| 25 | `hud` | 128×128 | IDX4 | 8 | Crosshair, HP, cooldown indicator, prompt frame, assembly progress bar |
| 26 | `font` | 128×64 | IDX4 | 4 | Cyrillic + digit stencil subset — must cover `ОСТАЛОСЬ`, `ПЛАН ВЫПОЛНЕН`, and prompts |

### Texture production notes

- Author at final resolution. There is **no filtering** — a downscale is a
  visible, permanent decision.
- Quantise to the tier-0 palette first, then derive the five darker palettes from
  it. Every atlas ships one image and six palettes.
- Check every texture **at the darkest tier** before calling it done.
- Keep important detail off large receding polygons — affine warping will swim
  it. Signage and the board belong on small, front-facing geometry.
- Share atlases aggressively across areas; budget texture memory **per area, not
  per object**.

---

## Audio — read this before writing a single note

### The constraint

Sound RAM is **512 KB**, and the format is **raw PCM, S16LE, mono, 44 100 Hz**.
ADPCM is deferred, so there is no compression.

```
44 100 samples/s × 2 bytes = 88 200 bytes/s  →  86.13 KiB per second of audio
512 KB of sound RAM        ≈ 5.94 SECONDS of audio resident at any moment
```

**This is the single most important fact in this document.** Streamed music is
impossible. Stems are impossible. A 60-second loop would need ten times the
console's entire sound RAM.

Three further limits shape everything:

- **No pitch / playback-rate control** (deferred). A sample plays only at the
  rate it was recorded. **Every distinct musical pitch is a separate sample.**
- **Loop is whole-sample only** — `rv_loop::none` (one-shot) or `rv_loop::forever`
  (repeats the entire sample). There are no loop points, so a looping sample must
  be seamless end-to-start on its own.
- **No reverb, no master volume.** Any sense of space must be baked into the
  sample. Voices sum in `int32` and **clip** at the `int16` rails — they are not
  divided by voice count.

Available per voice: `rv_loop`, linear **ADSR in milliseconds** (`sr == 0` holds
sustain indefinitely), and `volume` / `volume_l` / `volume_r` in `0..32767`.
A voice is mono and is placed in the stereo field by its own L/R volumes.

### The consequence: the score is a sample bank plus a sequencer

The disc uploads a small bank of very short samples and **the game code
sequences them across the 24 voices.** This is tracker music, not recorded music.
It is also, conveniently, exactly the right aesthetic: industrial, rhythmic,
drone-based, repetitive.

Practical rules that follow:

- **Beds are non-rhythmic drones.** A bed loops forever, and because it has no
  beat, its loop length does not have to line up with the tempo. This is what
  makes a 1.2-second loop usable as a whole area's harmonic floor.
- **Rhythm comes from the sequencer** triggering one-shots, never from a loop.
- **Melody is expensive** — each note is its own sample. Budget two or three
  fixed pitches per area and build around drone plus percussion.
- **Ambience folds into the bed.** Wind, room tone, and the transformer hum are
  recorded *into* the area's bed sample rather than costing a second voice and a
  second allocation.
- **The layer ramp is free.** Dropping a layer per tier means the sequencer stops
  triggering that sample. It costs no memory and needs no new asset.

### Bank strategy

One **resident core bank** stays allocated for the whole run; one **area bank**
is freed and re-uploaded at each area transition (the fades between Home, Street,
and Factory are the swap points). Allocate the resident bank first at boot so it
sits at the bottom of the pool, and always `sound_asset_free` the outgoing area
bank *before* allocating the incoming one.

| Bank | Duration | Size | Peak with resident |
| --- | --- | --- | --- |
| Resident core (always) | 1.72 s | 148 KiB | — |
| Home | 3.34 s | 288 KiB | **436 KiB** |
| Street | 3.64 s | 314 KiB | **462 KiB** |
| Factory | 3.94 s | 339 KiB | **487 KiB** |
| Final lap | 2.07 s | 178 KiB | **326 KiB** |

Worst case is the factory at **487 KiB of 512 KiB** — about 25 KiB of headroom.
Treat that as the real ceiling: any sample that grows must be paid for by another
that shrinks.

**Total unique audio to author: ~14.2 seconds across 40 samples.** That is the
entire game.

### Resident core bank — 1.72 s / 148 KiB

Combat, player, and UI. Loaded at boot, never freed.

| Sample | Duration | KiB | Loop | Notes |
| --- | --- | --- | --- | --- |
| `sfx_brick_throw` | 0.16 s | 13.8 | one-shot | Short cloth-and-air whoosh, dry |
| `sfx_brick_hit_hard` | 0.22 s | 18.9 | one-shot | Brick on concrete/metal; a crack with a short rattle of debris |
| `sfx_brick_hit_soft` | 0.18 s | 15.5 | one-shot | Brick on a body; dull, low, no ring |
| `sfx_pipe_swing` | 0.15 s | 12.9 | one-shot | Heavier whoosh than the brick, slight metal ring |
| `sfx_pipe_hit` | 0.20 s | 17.2 | one-shot | Steel on flesh/steel; must land with the hitstop |
| `sfx_enemy_hurt_a` | 0.20 s | 17.2 | one-shot | Short grunt/exhale |
| `sfx_enemy_hurt_b` | 0.20 s | 17.2 | one-shot | Second variation — with no pitch control, variety must be authored |
| `sfx_player_hurt` | 0.26 s | 22.4 | one-shot | Breath in, close-mic'd |
| `sfx_pickup` | 0.10 s | 8.6 | one-shot | Scrape of a brick lifted off the floor |
| `sfx_ui_prompt` | 0.05 s | 4.3 | one-shot | Very quiet tick for the interact prompt |

### Home bank — 3.34 s / 288 KiB

| Sample | Duration | KiB | Loop | Notes |
| --- | --- | --- | --- | --- |
| `mus_home_bed` | 1.40 s | 120.6 | **forever** | Drone + room tone + block hum baked together. Low, close, no beat. The harmonic floor of the apartment |
| `mus_home_note` | 0.50 s | 43.1 | one-shot | One sparse tonal figure the sequencer places rarely. Fixed pitch |
| `sfx_tv_loop` | 0.80 s | 68.9 | **forever** | Muffled broadcast murmur. **Uploaded only in apartment state 0** — from state 1 the bank is literally smaller and nothing replaces it |
| `sfx_step_lino_a` | 0.11 s | 9.5 | one-shot | Soft, dull footfall on linoleum |
| `sfx_step_lino_b` | 0.11 s | 9.5 | one-shot | Variation |
| `sfx_door_open` | 0.30 s | 25.8 | one-shot | Sprung door, handle, a little scrape |
| `sfx_radiator_tick` | 0.12 s | 10.3 | one-shot | Cast iron ticking; placed occasionally by the sequencer |

### Street bank — 3.64 s / 314 KiB

The area that carries the five-layer thinning. All layers are drawn from this
bank; which ones play is a function of `shifts_remaining`.

| Sample | Duration | KiB | Loop | Layer | Notes |
| --- | --- | --- | --- | --- | --- |
| `mus_street_bed` | 1.60 s | 137.8 | **forever** | L1 — always | Drone + wind + distant industrial hum baked together. Never drops out, not even at tier 0 |
| `mus_bass_pulse` | 0.30 s | 25.8 | one-shot | L2 | The heartbeat. Sequenced on the beat |
| `mus_kick` | 0.10 s | 8.6 | one-shot | L3 | Dry, close |
| `mus_clank` | 0.22 s | 18.9 | one-shot | L3 | Industrial metal hit standing in for a snare |
| `mus_hat` | 0.06 s | 5.2 | one-shot | L4 | Short noise tick |
| `mus_note_low` | 0.40 s | 34.5 | one-shot | L5 | Fixed pitch — root |
| `mus_note_high` | 0.40 s | 34.5 | one-shot | L5 | Fixed pitch — a minor interval above the root. These two are the only melodic material in the game |
| `sfx_step_asphalt_a` | 0.12 s | 10.3 | one-shot | — | Hard, gritty |
| `sfx_step_asphalt_b` | 0.12 s | 10.3 | one-shot | — | Variation |
| `sfx_step_puddle` | 0.14 s | 12.1 | one-shot | — | Water |
| `sfx_lamp_tick` | 0.18 s | 15.5 | one-shot | — | **Cooling metal tick as the player passes a dead lamppost.** Almost subliminal — quiet, brief, and never explained |

**Layer schedule** (`shifts_remaining` → which layers play):

| Tier | Layers playing | Character |
| --- | --- | --- |
| 5 | L1 + L2 + L3 + L4 + L5 | Full stack |
| 4 | L1 + L2 + L3 + L4 | Melody gone |
| 3 | L1 + L2 + L3 | Hats gone |
| 2 | L1 + L2 | Percussion gone |
| 1 | L1 + bass on the downbeat only | Almost nothing |
| 0 | L1 only, quieter | Final lap — bed alone |

Nothing is ever added to compensate. Keep perceived loudness consistent as layers
thin so the ramp reads as **emptier**, not merely quieter.

### Factory bank — 3.94 s / 339 KiB (worst case)

| Sample | Duration | KiB | Loop | Notes |
| --- | --- | --- | --- | --- |
| `mus_factory_bed` | 1.20 s | 103.4 | **forever** | Transformer hum + ventilation + a low drone. Baked room space — there is no reverb |
| `mus_factory_pulse` | 0.28 s | 24.1 | one-shot | Machine-rhythm pulse |
| `mus_factory_clank` | 0.25 s | 21.5 | one-shot | Heavier than the street clank; a dropped bar in another bay |
| `mus_factory_note` | 0.35 s | 30.1 | one-shot | One fixed pitch, used sparsely and only during assembly |
| `sfx_step_concrete_a` | 0.12 s | 10.3 | one-shot | Hard, slight room |
| `sfx_step_concrete_b` | 0.12 s | 10.3 | one-shot | Variation |
| **`sfx_board_clack`** | **0.35 s** | **30.1** | one-shot | **The most important sound in the game.** A single mechanical flip-digit turning over. Dry, loud, slightly too final. Everything else ducks for a moment so this lands alone. Spend real time on it |
| `sfx_conveyor_loop` | 0.50 s | 43.1 | **forever** | Rollers; started when the finished lamppost rides away, stopped after |
| `sfx_assembly_step` | 0.20 s | 17.2 | one-shot | One of the three assembly beats completing — mechanical, satisfying |
| `sfx_assembly_complete` | 0.35 s | 30.1 | one-shot | The lamppost finished. Plays into the conveyor and then the clack |
| `sfx_weld_flash` | 0.22 s | 18.9 | one-shot | Arc strike; punctuates the escalation wave |

### Final lap bank — 2.07 s / 178 KiB

The emptiest bank in the game, which is the point. Footsteps are the same source
files already authored for the other areas, re-uploaded here.

| Sample | Duration | KiB | Loop | Notes |
| --- | --- | --- | --- | --- |
| `amb_final_bed` | 1.60 s | 137.8 | **forever** | Near-silent wind and a very distant hum. **No new composition, no final theme, no swell at `ПЛАН ВЫПОЛНЕН`** |
| `sfx_step_asphalt_a` | 0.12 s | 10.3 | one-shot | Re-used |
| `sfx_step_asphalt_b` | 0.12 s | 10.3 | one-shot | Re-used |
| `sfx_step_lino_a` | 0.11 s | 9.5 | one-shot | Re-used |
| `sfx_step_concrete_a` | 0.12 s | 10.3 | one-shot | Re-used |

`sfx_board_clack` is **not** in this bank. At zero the board does not turn over —
it is already at zero, and the silence where the clack should be is the ending.

### Tooling

A linear DAW timeline is the wrong shape for this job — not because it cannot
make the sounds, but because **it cannot show you what the console will
actually play.** The console's model is a sample bank plus a pattern sequencer,
which is precisely the tracker model. Use both tools for what each is good at:

- **DAW (Reaper) — sound design and rendering.** Recording, layering, EQ, baking
  room space into a sample, and rendering exact-length mono files. Keep it.
- **Tracker — arrangement and audition.** A bank of short samples triggered
  across channels with per-channel volume and panning *is* our runtime. What you
  hear in a tracker is close to what the console will do.

Linux-native options: **Schism Tracker** (Impulse Tracker clone, free, closest to
the bank-plus-pattern model), **MilkyTracker** (free), **Furnace** (free, built
around emulating sample-based sound chips, so its constraints feel familiar), or
**Renoise** (paid, native, best sample editing if you want one tool for both).

**The trap, and it is a real one:** trackers exist to pitch-shift samples, and
**this console has no pitch control.** A tracker will happily let you write a
melody that the hardware cannot reproduce. The discipline is absolute:

- Place every sample at its base note only (`C-5` in Impulse-style trackers).
  Never transpose.
- If you want a second pitch, that is a **second sample** and it costs its full
  duration out of the 512 KB budget.
- Use tracker volume columns and panning freely — those map directly to
  `volume` / `volume_l` / `volume_r`.
- Ignore tracker effects that have no console equivalent: portamento, vibrato,
  arpeggio, sample offset, and reverb sends. If a pattern needs them, it is not
  playable here.

If Reaper alone is preferred, it can be made honest: load each sample into
ReaSamplOmatic5000 with its note range set to a **single semitone** so nothing
transposes, one instance per sample, and sequence with MIDI. That is a faithful
simulation of the console's sampler, at the cost of more setup than a tracker.

Either way the authoritative preview is the game itself; treat the tracker
arrangement as the score to be re-implemented by the sequencer, not as the
deliverable.

### Production notes for writing this material

- **Deliver:** mono, 16-bit, 44 100 Hz, raw S16LE (or WAV, header stripped at
  bake). Any other sample rate will play at the wrong speed — there is no
  resampler on the console side.
- **Compose one bar's worth of material, not one track.** Pick a single tempo for
  the whole game so one-shots from different banks line up; the sequencer places
  them, so the samples themselves only need to be shorter than their slot.
- **Loops must be seamless as whole samples.** `rv_loop::forever` repeats the
  entire sample with no crossfade and no loop points. Test the wrap.
- **Bake the space in.** No reverb exists. The factory's size, the apartment's
  deadness, and the street's openness all have to be in the recording.
- **Leave headroom.** Voices sum in `int32` and clip at the `int16` rails without
  being divided by voice count. With a bed, a bass, two percussion hits, and
  several SFX potentially live at once, keep per-voice `volume` well under unity
  and normalise samples to peak around −6 dBFS rather than 0.
- **Envelopes are linear and in milliseconds.** Use `ar`/`rr` for fades in and
  out; `sr == 0` holds sustain indefinitely, which is how a bed stays up.
- **Voice budget of 24:** reserve roughly 8–12 for music, 8 for SFX, and keep
  4 spare. Nothing in the design needs more.
- **Duration is a hard spec, not a target.** Every figure in the tables above is
  load-bearing; overrunning one means another must shrink. Trim at the source
  rather than fading, since a fade still costs full bytes.
- **Verify every render by its file size**, which is the only check that matters:

  ```sh
  # WAV → raw S16LE mono 44.1 kHz
  sox in.wav -t raw -e signed -b 16 -c 1 -r 44100 out.pcm
  # expected size in bytes = duration_seconds × 88200
  stat -c %s out.pcm
  ```

  A file that is the wrong size is the wrong sample rate, the wrong bit depth, or
  stereo — all three of which will play back wrong rather than fail loudly.

---

## Asset pipeline (low-poly)

Per asset:

1. Block out (or AI-generate) a base mesh under low-poly constraints.
2. Clean up in Blender: decimate if needed, fix normals, simple UV unwrap.
3. **One paletted texture atlas** per asset/batch; albedo only.
4. Add simple collision shapes and, for enemies/props, cheap LODs.
5. Import to the console; verify silhouette, scale, and how it dithers at 16-bit
   — **and check it at the darkest tier before calling it done.**

**Rough effort budgets:** prop batch of 3–4 items ≈ 1 session; enemy base mesh +
UV ≈ 1–2 sessions; environment chunk dressing ≈ 1 session per chunk after
blockout; the apartment trace variant set ≈ 1–2 sessions total.

Geometry discipline: the frame refuses the 4097th primitive. Subdivide long
surfaces enough that affine warping reads as character rather than nausea, but no
further.

---

## No shaders — palette & dither instead

The console is a **software rasterizer**: there are no programmable shaders.
Effects that would be shaders elsewhere are done with:

- **Ordered dithering** for gradients and the 16-bit frame.
- **Paletted tricks** — palette swaps for flicker, mood shifts, low-HP vignette,
  and the whole impoverishment ramp; vertex/per-face colour for lighting instead
  of pixel lighting.
- **Unlit textured planes / flipbooks** for VFX (light pools, smoke clouds,
  welding flashes, assembly glow) rather than particle shaders.
- **Vertex or pose animation** for telegraphs (scale / tint / pose), not skeletal
  shader work.

**The darkening ramp introduces no new rendering technique** — it is light-pool
planes removed, vertex colours swapped, and a different palette selected.

---

## Animation

Minimal keyframes. First-person hands (idle / walk / hold / throw / melee), enemy
telegraph + attack sets tied to combat timings, the three assembly beats, the
conveyor, and the board's digit flip. Hook animation events to the hitstop/shake
helpers so feedback lands on frame.

---

## Sourcing hygiene

Track licences/terms for any external or AI-assisted assets (a `SOURCES` note in
the disc). Keep raw vs. edited audio separate — and keep the **pre-trim** version
of every sample, since the durations above will be re-cut at least once.
