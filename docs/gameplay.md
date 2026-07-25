# Gameplay — Genre, View, Rendering & Controls

What kind of game this is, how it is seen, how it is drawn, and how it is played.
Systems detail lives in [`mechanics.md`](mechanics.md); this document is the
frame around it and the place where the console's rendering character is treated
as **design**, not as a technical footnote.

---

## 1. Genre

**First-person folk-horror shooter with improvised weapons**, built as a
short-form jam game around the theme *Count Down*.

More precisely, it is three things at once:

- **An arena-lite FPS.** Throw a brick, swing a pipe, manage space against two
  enemy archetypes with readable telegraphs. Combat is tactile and short.
- **A walking loop.** The same 1.5–2 minute route five times over. The
  repetition is the delivery mechanism — the player learns the space so well
  that a single dead lamp registers immediately.
- **An environmental horror.** No jump scares, no chase, no monster in the
  apartment. The horror is arithmetic: the board goes down, the lights go out,
  the room empties, and nothing in the game acknowledges it.

Not a roguelite, not procedural, not systemic. Every change across the five
shifts is authored by hand.

**Reference points for tone and feel:** PSX-era survival horror for the camera
and the grain; *Cruelty Squad* / *Dusk* for improvised low-fi FPS combat weight;
the "same route, worse each time" structure of short narrative horror.

---

## 2. View

- **First person, single player.** The player sees the world, their hands, and
  the held tool. The protagonist's full silhouette exists as canon
  ([`protagonist_profile.md`](protagonist_profile.md)) but is not shown in
  gameplay — there is no mirror, no summary screen, and no cutscene.
- **Camera height ~1.65 m**, standing only. This is measured against the
  apartment's 2.50 m ceiling and the factory's 7 m one: the game is built so the
  player physically feels the difference between the two rooms.
- **Field of view:** ~70–75° horizontal at 4:3. Wide enough to navigate,
  narrow enough that the street corridor stays claustrophobic. Fixed — no FOV
  slider in the jam build.
- **Head bob:** subtle, tied to footfall, with a comfort toggle. Motion comfort
  matters more than style in first person.
- **No zoom, no aim-down-sights, no crouch, no lean.** One posture, one speed
  band (walk, optional sprint if it earns its place).
- **HUD is minimal and diegetically silent:** crosshair, health, tool cooldown,
  interaction prompt. **It never shows the countdown.** The only number in the
  game is painted on a wall in the factory.

---

## 3. Rendering — the console is the art direction

The game runs on the 3dmppc console: a **software rasterizer** with a fixed
virtual budget. Its limitations are not obstacles to hide — they are the visual
identity, and the design leans into every one of them. Authoritative numbers are
in [`../../../docs/platform/specs.md`](../../../docs/platform/specs.md).

### Frame and colour

- **320×240**, 4:3, **16-bit colour with ordered dithering.** Gradients — the sky,
  a lamp's pool of light, the fade into darkness — are all dither patterns, and
  they are visible. Author with that in mind rather than fighting it.
- **Textures are 4-bit or 8-bit paletted with no filtering**, up to 256×256, plus
  a 15-bit direct format where transparency is needed. Nearest sampling means
  texels stay crisp and crawl when the camera moves. Wanted.
- **The whole impoverishment ramp is palette work.** Six tiers = six palettes over
  the same texture atlases. No post-processing, no colour grading pass, no extra
  memory.

### Affine texture mapping — the signature

`rv_vertex` carries no `w`, so UVs are interpolated in screen space and
perspective correction is **not merely skipped, it is impossible on the game's
side of the contract.** Textures visibly swim on surfaces receding into depth,
and quads break along their split diagonal.

This is the PSX signature and it is embraced, but it has real consequences for
level building:

- **Subdivide long surfaces.** Floors, road, corridor walls, and the concrete
  fence must be split into several quads along their length or the warping
  becomes nauseating rather than characterful.
- **Keep important information off big receding polygons.** The board's text, the
  yellow floor lines, and signage should sit on smaller, more front-facing
  geometry so they stay readable.

### Depth sorting

Primitives are sorted through a **1024-bucket ordering table** across the depth
window. Two different depths can land in the same bucket, where submission order
decides — so **draw order is a design concern**, not an engine detail. Expect and
plan for:

- Sorting flicker between coplanar or nearly coplanar surfaces. Keep decals (the
  lamp's light pool, floor markings, the traces on the wallpaper) slightly offset
  and submitted in a deliberate order.
- Per-pixel Z is available where the residue actually matters
  (`RV_PIPELINE_BUFFER_CONFIG_TYPE_Z`); use it sparingly, not as a default.

### Geometry budget

- **4096 primitives per frame**, hard: the frame refuses anything past it. That
  is the real ceiling on scene complexity, and it is why the street is two chunks
  and the factory is one room.
- Low-poly throughout. The protagonist's runtime model is ~900 triangles; that is
  the scale for everything. Detail comes from silhouette and palette.

### Lighting — there are none

There are **no programmable shaders and no dynamic lights.** Everything that
looks like lighting is one of:

- **Vertex / per-face colour** baked into the geometry per tier.
- **Unlit textured planes** for the light pools under lampposts, welding flashes,
  the smoke cloud, and the assembly glow.
- **Palette selection** for mood, low-health vignette, and the darkening ramp.

Which means **turning a streetlight off is not a lighting operation.** It is:
remove the light-pool plane, swap the surrounding geometry to the darker
vertex-colour set, and select the tier's palette. Cheap, deterministic, and
authored — exactly what the countdown needs.

### Readability floor

Because darkness here is authored rather than simulated, it can be made exactly
as dark as we want — which is why the hard rules in
[`mechanics.md`](mechanics.md#light-darkness--readability) exist. The ambient
floor never reaches zero, telegraph colours live in protected palette entries,
and every tier is validated by playing it.

---

## 4. Controls

**Gamepad only.** The console exposes two input ports with a full controller
surface (sticks, triggers, bumpers, trackpads, rear buttons) and also a relative
mouse-look channel (`rv_imouse`) — **the mouse channel is deliberately not used.**
There is no keyboard or mouse support, no fallback scheme, and no rebinding to
one. The game is designed, tuned, and playtested on a pad; a player without one
is not a target.

This is a design decision, not a limitation, and it has teeth: everything below
about aim assist, throw forgiveness, and look tuning exists *because* the only
aiming device is a thumbstick.

### Scheme

| Action | Binding |
| --- | --- |
| Move | Left stick |
| Look | Right stick |
| Right hand — primary | Right trigger |
| Left hand — offhand | Left trigger |
| Pause | Menu button |

Five inputs total. Nothing else is bound.

### Hand intent, not a verb list

Carried over from the original design and worth keeping: **there are only hand
buttons.** There is no separate `interact`, `attack`, `throw`, or `pickup`
binding. Pressing a hand button expresses intent; what happens is decided by what
is in that hand and what is in front of the player:

- Empty hand + a brick on the ground → pick it up.
- Brick in hand + press → throw it on an arc.
- Pipe in hand + press → swing.
- Empty hand + the assembly bench → hold to work.
- Empty hand + nothing → nothing happens, and that is a valid outcome, not an
  error.

This keeps the control surface tiny — two buttons and two sticks — which is
exactly right for a game a player should finish in ten minutes without reading
anything.

### Feel requirements — all of them are stick-aiming requirements

- **Look curve:** a dead zone that kills stick drift, a low-sensitivity zone for
  fine aim near centre, and a higher rate toward the edge. A single linear
  sensitivity value will feel bad and no amount of tuning the number will fix it.
- **Pitch clamped** around ±85°. No look inversion by default, with a toggle.
- **Aim assist is mandatory, not optional.** Light gravity toward enemy centres
  inside a modest cone, plus a slight slowdown as the crosshair crosses a target.
  Without it, throwing a brick at a moving Kipuchka on a thumbstick is not fun,
  and at `ОСТАЛОСЬ: 1` it is not even fair.
- **Throw:** hold to ready, release to throw. The arc must be visibly
  parabolic — the player aims by learning the arc, not by a reticle. Be generous
  with the brick's hit radius; the pad cannot be as precise as a mouse and the
  design should not pretend otherwise.
- **Enemy telegraphs get more windup room than a mouse game would need** — the
  ≥300 ms floor in [`mechanics.md`](mechanics.md) assumes the player has to swing
  a stick to respond, not flick a wrist.
- **Impact:** hitstop of 0.06–0.1 s plus camera micro-shake plus a loud sample,
  all landing on the same frame. Weight is the whole point of improvised weapons.
- **Haptics** are available (`ohaptic`) and worth one pass: impact, taking a hit,
  and the board's clack. Cheap, and the pad is the only thing the player is
  holding.
- **No context-sensitive prompts stacking.** One prompt at a time, one action per
  hand.

---

## 5. Session shape

- One shift: **1.5–2 minutes** — Home, Street, Factory, home again.
- Five shifts plus a final lap: **8–12 minutes total.**
- **Death restarts the current shift only.** The countdown never moves backward
  or forward on failure; losing costs about two minutes and no progress.
- The whole run is one sitting. The memory card holds four small fields so a run
  can be resumed, not so it can be replayed differently.
