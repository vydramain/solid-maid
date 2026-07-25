# Production — Milestones, Backlog & Budget

Solo-dev plan for the **jam slice**: five short shifts, one countdown, two
enemies, two weapons, one assembly, **no boss**. Small, finished, and legible
beats sprawling and unfinished.

> The original fine-grained task list targeted Godot scenes/scripts. It was **not
> carried forward verbatim** — the game now runs on the mppc console runtime, so
> tasks are re-framed engine-agnostically below. Adjust as the console's
> disc API firms up in [`../../../docs/platform/`](../../../docs/platform/).

## Cadence & sizing

- **Daily limit:** ~2 hours (one focused session); weekends optional.
- **Sizing rule:** every task fits in 1 session, or it gets split. Anything
  larger becomes a milestone item, not a task.
- **Tracking:** end each session by noting duration, blockers, and the next
  "first click".

## Priority order (do not reorder)

The jam risk is not "not enough content" — it is "the countdown is not readable
by the time the jam ends". Build in this order and the game is shippable at
almost every point along it:

1. One complete short shift: Home → Street → Factory → Home.
2. The global `shifts_remaining` state and the shift transition.
3. The factory board `ОСТАЛОСЬ: N` and its on-screen tick.
4. Lamppost states on the street.
5. Apartment states with traces.
6. Five shifts running end to end.
7. The final lap at zero and `ПЛАН ВЫПОЛНЕН`.
8. Sound, palette tiers, and polish.
9. Extra enemy behaviour — **only** once the whole game plays through.

## Milestones (jam slice)

- **M1 — One shift, end to end.** First-person controller (gamepad look/move,
  pitch clamp), interact ray + prompt, brick throw, pipe swing, hitstop/shake
  helper, both enemies at ship behaviour, spawner discipline, greybox Home + 2
  street chunks + factory hall, 3-step assembly, return trigger. **Exit
  criterion: a 2-minute shift is playable start to finish.**
- **M2 — The countdown exists.** `shifts_remaining` state and single decrement
  point; shift transition and re-dress; the board with 6 digit states and its
  clack-down moment; restart-current-shift on death. **Exit criterion: the board
  reads 5, then 4, and dying does not change it.**
- **M3 — The countdown is visible.** Five lampposts with lit/dead states and the
  authored extinguish order; per-tier light + palette selection; six apartment
  dressing states with traces; TV interactable in state 0 only. **Exit
  criterion: a playtester who is told nothing notices something is missing by
  shift 3.**
- **M4 — Five shifts and an ending.** Encounter placement per tier; full
  five-shift run; the final lap (dark street, inert factory, empty room);
  `ПЛАН ВЫПОЛНЕН`; save/restore of the four state fields. **Exit criterion: a
  full 8–12 minute playthrough.**
- **M5 — Polish.** HUD (HP, cooldowns, prompts — **no counter**); music layer
  ramp and the board/lamp SFX; readability pass at every tier; performance pass;
  QA pass; internal build.

## Backlog (atomic tasks, by area)

Legend: `[S]` ≤1 session, `[M]` ≤2 sessions (split if bigger).

**Countdown (the spine)**
- [S] `shifts_remaining` state, 5 → 0, single decrement on assembly complete.
- [S] Shift transition: commit, re-dress Home, recompute street + tiers.
- [S] Derive-everything helper: tier → lamp states, apartment index, palette,
  music layers (one function, no stored duplicates).
- [S] Restart-current-shift on death without touching the count.
- [S] Save/restore the four state fields to the memory card.

**The board**
- [S] Board mesh + 6 digit states over the conveyor, readable from the entrance.
- [S] Tick-down beat: lamppost leaves conveyor → clack → one-second hold.
- [S] Board SFX (mechanical flip) and its `ОСТАЛОСЬ: 0` state.
- [S] `ПЛАН ВЫПОЛНЕН` resolve on approach at zero → end of game.

**Street & lampposts**
- [S] Greybox 2 chunks, one route, one gate, clear lanes and collisions.
- [S] Five lampposts: lit/dead states, light pool on/off, dark glass.
- [S] Authored extinguish order L1→L5 keyed to the tier.
- [S] Ambient floor tuning: readable at every tier, never zero.
- [S] Encounter placement per chunk × tier (flat stats, placed pressure).

**Home**
- [S] Room blockout; brick + pipe pickups fixed by the door.
- [S] TV interactable + Home music cue — state 0 only.
- [M] Six dressing states with traces (rectangle, dents, ring, nail holes).
- [S] Window view of the courtyard lamppost, lit/dead.

**Factory**
- [S] Compact arena blockout, lamppost socket, spawn points, conveyor.
- [S] 3-step hold-to-assemble with progress bar + interrupt on damage.
- [S] One ordinary escalation wave at step 2.
- [S] Return trigger + state handoff.

**Player, weapons & enemies**
- [S] First-person controller (move + gamepad look, pitch clamp, sensitivity).
- [S] Interact raycast (2–3m) + on-screen prompt.
- [S] Brick: hold/throw, physics arc, cooldown, impact SFX; spares never gated.
- [S] Hitstop helper + camera micro-shake.
- [S] Pipe: short swing, telegraph, cooldown.
- [S] Kipuchka: chase + melee, **no steal**.
- [S] Midnight Smoker: expanding AoE with self-lit pre-warm ring.
- [S] Spawner: cap 5, min distance, 1.5s grace.
- [M] Low-poly enemy meshes + telegraph animations.
- [M] Model/texture brick + pipe; animate swings/impacts.
- [M] Placeholder FP hand + first-person animation set.

**Final lap**
- [S] Zero-state routing: no assembly, no return trigger, no/minimal enemies.
- [S] All-lamps-dead street pass with readability check.
- [S] Apartment state 5 and the inert factory hall.

**UI / audio**
- [S] HUD: HP, cooldowns, prompts. **No countdown readout — ever.**
- [S] Sample-bank sequencer over the 24 voices; one layer dropped per tier.
- [S] Bank swap on area transition (free outgoing, then upload incoming).
- [S] Near-empty ambience for the final lap (no new composition).
- [S] Crosshair + interact tint overlay.

**Art assets** — full manifest with sizes, formats, and durations in
[`art-and-audio.md`](art-and-audio.md#texture-manifest).
- [M] Home atlases (walls with traces, floor, furniture, window, door) — 82 KiB.
- [M] Street atlases (facade, ground, props, lamp, light pool, sky, gate) — 100 KiB.
- [M] Factory atlases (walls, floor, machines, board, signs, lamppost parts) — 88 KiB.
- [S] Actor + item + HUD/font atlases — 30 KiB.
- [S] Six tier palettes per atlas (32 bytes each; the whole ramp is <2 KiB).
- [M] Audio: 40 samples, ~14.2 s total, split into resident + 4 swappable banks.

**Tooling & perf**
- [S] Debug overlay (frame time, active enemies, player HP, current tier).
- [S] Tier-jump debug key — start any shift directly; essential for testing five
  states without playing ten minutes each time.
- [M] Cheap lighting/ambient setup for readability under the palette budget.

## Metrics & validation

- Whole game **8–12 minutes** for a first-time player; one shift **≤ 2 min**.
- **The unprompted-notice test:** by shift 3, a playtester who has been told
  nothing about a countdown remarks on the lamps, the room, or the board. If
  they do not, the visuals are too subtle — fix the dressing, do not add text.
- **The link test:** by the end, a playtester connects the number to the lamps.
  Nobody needed to be told.
- Stable frame through spawns, the AoE cloud, and assembly, at every light tier.
- Enemy telegraphs read at `ОСТАЛОСЬ: 1`.
- 2 successful full runs out of 3 internal attempts.
- No playtester reports feeling they "lost a shift" by dying.

## Risks & mitigation

| Risk | Mitigation |
| ---- | ---------- |
| Countdown is invisible to the player | It is M3, before content. Three redundant channels (board, lamps, room); run the unprompted-notice test early. |
| Darkness makes late shifts unplayable | Ambient floor never goes to zero; telegraphs self-lit; readability check is a per-tier exit criterion, not a polish task. |
| Two sources of truth drift (board says 3, four lamps lit) | Everything is derived from one integer by one function; nothing is stored twice. |
| Repetition reads as filler, not as structure | Shift is 1.5–2 min; something is visibly missing every single time; five is short enough to stay taut. |
| Scope creep back toward the boss | Boss is explicitly post-jam; the factory arena is sized so a boss would not fit. |
| Performance dips (software raster) | Simple paletted materials, capped spawns, shared atlases, low poly counts. |
| Gamepad aiming feels bad — and there is no mouse to fall back on | Build the look curve (dead zone / fine zone / edge rate) and aim assist in M1, not in polish. Playtest on a pad from the first playable build; never evaluate feel with a debug camera. |
| Content drain | Two street chunks reused five times by design; all variation is lighting and placement. |
| Motion comfort | Keep head-bob/FOV subtle; expose a comfort toggle. |

## Open questions

Only the ones that genuinely need a decision; everything else is locked above.

1. Does the board tick down **before** or **after** the return trigger opens?
   (Before reads as cause-and-effect; after reads as a reward. Prototype both —
   this is a 10-minute experiment and it matters.)
2. Does the final lap end at the board, or after one last walk back to the empty
   apartment? Locked as "ends at the board" for now; revisit only if the ending
   plays flat.
3. Minimal assembly VFX that reads "occult" without art burden (palette/dither
   tricks)?

## Post-jam backlog

Canon and good ideas cut from the jam slice. **None of this is deleted** — it is
the natural content of a longer version where a shift can afford more time.

- **Bosses:** Warehouse Manager + Zmey Gorynych (the former slice candidate,
  full three-head design intact), Accountant / Baba Yaga, Oligarch / Koschei.
- **Enemies:** Leshaki; any additional archetype.
- **Kipuchka component-steal** — stretch goal; needs alley geometry and a
  recovery loop.
- **Pillars ranged unlock** — Emitter vs. Piece-weapon; prototype before
  committing.
- **World:** additional street chunks, alleys as real detours, authored surreal
  events, new areas, cars.
- **Structure:** longer shifts (4–6 min), a longer count, a day-summary screen,
  the full narrative arc.
- **Superseded, not backlogged:** the earlier *accretion* progression, in which
  Home gained a new prop / light / picture per loop and progress was tracked as a
  world-corruption level. It is directly inverted by the countdown design and
  should not be reintroduced alongside it — accretion reads as escalation, and
  this game needs subtraction.

## Out of scope (for now)

Distribution/marketing, analytics, localisation, monetisation, and any save
system beyond the four small state fields on the memory card.

**Rejected outright** (not backlog — these were considered and turned down
because they add a second countdown or dilute the single number):

- Limited bricks, or any consumable framed as a count.
- Names on bricks.
- Spending shifts or lampposts as a resource.
- Skipping assembly steps at the cost of a shift.
- Kipuchka stealing a shift/day.
- Any second independent numeric system.
- Procedural world corruption.
