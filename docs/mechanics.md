# Mechanics

First-person, tactile, and readable. Two weapons, a short assembly ritual,
enemies with strong telegraphs, and **one countdown that drives everything
else**. Tuned for the console: gamepad input, a software rasterizer, and a
stable frame.

## Controls (gamepad — console re-target)

**Gamepad only.** No keyboard, no mouse, no fallback — the console's mouse-look
channel exists and is deliberately unused. Five inputs total; full scheme and
feel requirements in [`gameplay.md`](gameplay.md#4-controls).

- **Left stick:** move.
- **Right stick:** look/aim, with a dead zone, a fine-aim zone near centre, a
  pitch clamp, and **mandatory light aim assist** toward targets. Stick aiming is
  the only aiming there is, so this is a correctness requirement, not polish.
- **Two hand buttons only** — right trigger / LMB and left trigger / RMB. There
  is no separate interact, attack, or throw binding: the hand button expresses
  intent, and what is in the hand plus what is in front of the player decides the
  outcome (pick up / throw / swing / hold-to-assemble / nothing).
- **Optional:** sprint / crouch if they earn their place — the slice works
  without them.
- Light head-bob and a subtle low-HP vignette (via palette/dither, not shaders).
  Keep bob subtle and, ideally, adjustable — motion comfort matters in first
  person.

## Countdown state

The whole game runs off one integer.

```
shifts_remaining : 5 → 0
```

Rules, enforced as design constraints:

- **One writer.** `shifts_remaining` decrements at exactly one place in the
  code: the moment the lamppost assembly completes. Nowhere else.
- **Everything else derives.** Board digit, lit-lamp count, apartment dressing
  index, palette tier, and active music layers are all pure functions of
  `shifts_remaining`. They are never stored or advanced independently.
- **Never displayed on the HUD.** The count reaches the player only through the
  factory board, the street lighting, and the apartment. No timer, no
  shift counter, no "Day 3 of 5" card.
- **No second countdown.** Bricks are not limited. Shifts and lampposts are not
  spendable. Health, ammo, and assembly steps are not framed as counts.

### Shift transition

1. Assembly step 3 completes → the finished lamppost leaves the conveyor.
2. The board clacks down one digit, on screen, with audio. Brief beat where the
   player can look at it.
3. Return trigger opens → player walks it → fade.
4. `shifts_remaining` is committed; Home is re-dressed to the new state, the
   street's lamp states and the palette/music tiers are recomputed.
5. Player wakes in the apartment with one thing missing.

At `shifts_remaining == 0` the transition routes to the **final lap** instead of
a normal shift (see [`content.md`](content.md#the-final-lap-at-zero)).

## Light, darkness & readability

Falling light is the difficulty curve. It is also the biggest risk to
legibility, so the rules are strict.

- **The countdown removes light *pools*, never the ambient floor.** Each dead
  lamppost removes its own pool of light from the ground and the surrounding
  facades. A separate ambient floor (sky bounce, window glow, distant industrial
  haze) is tuned once, stays above a readable minimum at every tier, and is
  **never driven to zero** — not even on the final lap.
- **Do not lean on far-clip.** Pulling the far plane in is allowed only as a
  small, gentle contribution to mood. Route landmarks, the factory gate, enemy
  silhouettes, and every attack telegraph must stay readable at
  `ОСТАЛОСЬ: 1`. If a tier fails that check, brighten the floor — do not accept
  a "scary but unplayable" tier.
- **Darkening is done with the cheap tools we already have:** turning light
  sources off, per-tier palette selection, vertex/face colour, environment
  colour, and dithering. No new shaders, no dynamic shadow work — the console is
  a software rasterizer (see [`art-and-audio.md`](art-and-audio.md)).
- **Telegraphs are self-lit.** Pre-warm rings, windup tints, and interaction
  prompts use palette entries that stay bright at every tier. A telegraph must
  never depend on a lamppost being alive.
- **The gate stays lit until the end.** The factory gate lamp (L5) is the last
  one burning, which conveniently doubles as the navigational anchor for the
  darkest playable shift.

## Weapons

- **Brick (throwable — primary).** Hold to ready, release to throw on an arc;
  cooldown between throws. Physics arc, bounce, and a satisfying impact.
  **Bricks are never limited** — spares are scattered in the world and a fresh
  one is always by the apartment door. The brick must not read as a resource
  that counts down.
- **Pipe (melee — backup / gap-maker).** Short, fast swing with a clear
  telegraph and cooldown; used to create space for safer throws and to finish
  weakened enemies.
- **Pillars (ranged — post-jam).** Deferred entirely; see
  [`production.md`](production.md#post-jam-backlog).

## Combat feel

Weight is everything, and it's mostly cheap tricks stacked well:

- **Hitstop** (~0.06–0.1s) on solid hits.
- **Camera micro-shake** on impact.
- **Loud, punchy SFX** (see [`art-and-audio.md`](art-and-audio.md)).
- **Readable telegraphs:** enemy windups **≥ ~300 ms**, low visual clutter, one
  signature tell per attack. The software rasterizer and first-person view demand
  legibility over detail — and this holds at every light tier.

## Enemies (behaviour)

See [`characters.md`](characters.md) for fiction; the gameplay hooks:

- **Kipuchka** — fast, jittery melee. **No-steal behaviour ships.** The
  component-steal is a stretch goal only.
- **Midnight Smoker** — expanding **smoke-cloud AoE** with a pre-warm ring;
  reduces visibility and deals chip damage. Area denial.
- **Escalation comes from darkness, not from stats.** Enemy HP, damage, speed,
  and spawn counts stay **flat across all five shifts**. What changes is that
  there is less light to fight in: the smoker's cloud edge is harder to judge
  against unlit ground, and open lanes stop being obviously safe. The pre-warm
  ring stays self-lit and readable — the player always knows an attack is
  coming, and increasingly cannot tell exactly how far it reaches.
- **Spawner discipline:** cap ~5 active enemies, enforce a minimum spawn distance
  from the player, and a ~1.5s spawn grace. Placement is keyed by chunk index
  and `shifts_remaining`, so encounters are authored per tier, not scaled.

## The assembly ritual (Factory)

The core "under pressure" interaction, and the only thing that moves the
countdown.

- **3-step hold-to-assemble** the lamppost, each step a hold-interaction with a
  progress bar.
- **Interrupt on damage** — taking a hit interrupts the current step, so the
  player must clear space (pipe) before committing. Interrupts lose progress on
  the current step only; completed steps stay done.
- **One escalation at step 2:** a single ordinary wave. No boss, no phase
  change, no gimmick. Never allow a step to be skipped, bought, or traded.
- On completion: the lamppost leaves the conveyor, the **board ticks down**, and
  the return trigger spawns.

## Loop & fail states

- **Win:** finish five shifts, then walk the final lap at zero and reach the
  board.
- **Lose:** knocked out anywhere → **restart the current shift** from Home.
  `shifts_remaining` is unchanged; the board, the lampposts, and the apartment
  stay exactly as they were. Within-shift state (assembly step, cleared
  encounters) resets.
  Losing costs the player ~2 minutes and never costs progress — the countdown
  belongs to the fiction, not to the punishment.
- **No fail state can advance or reset the countdown.** There is no way to lose
  a shift, no way to skip one, and no way to get one back.
- **Persistence:** `shifts_remaining`, `shift_phase`, `assembly_step`, and a
  `finished` flag. Tiny; fits one 8 KB memory-card slot trivially. See
  [`content.md`](content.md#progression--persistence).

## Final lap requirements

At zero the player walks the same route with the work removed:

- No assembly interaction and no return trigger — the factory hall is inert.
- Enemies absent, or a single non-approaching Midnight Smoker at distance.
- All lampposts dead; ambient floor still above the readability minimum.
- The board reads `ОСТАЛОСЬ: 0`; approaching it resolves **`ПЛАН ВЫПОЛНЕН`** and
  ends the game.
- No cutscene, no new geometry, no boss, no dialogue.
- The player must never be able to get stuck: the route is linear, and reaching
  the board is the only objective.

## MVP acceptance (feel checks)

- A new player finishes the **whole game in 8–12 minutes** without reading
  anything, and one shift in **≤ 2 minutes**.
- By shift 3, a playtester who is asked "what's changed?" names the lamps, the
  room, or the board — unprompted, with no text having explained it.
- Nobody is told what the board counts, and nobody needs to be told.
- Hits feel punchy: SFX + micro-hitstop + shake land together.
- Enemy telegraphs are unmistakable at `ОСТАЛОСЬ: 1`; failure is readable and
  fair.
- The frame stays stable through spawns, the AoE cloud, and assembly, at every
  light tier.
- Dying never makes a player feel they lost a shift.
