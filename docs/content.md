# Content — Areas, the Countdown & Progression

The game is **five short shifts** across three areas, followed by one final lap.
Content is deliberately minimal and reused; the countdown carries the
progression. Fiction is in [`world.md`](world.md); systems are in
[`mechanics.md`](mechanics.md). This doc is the structure view.

## The one number

```
shifts_remaining : 5 → 0
```

A single integer. **Everything visible derives from it** — the factory board
digit, how many lampposts are lit, which apartment state is dressed, the palette
tier, and how many music layers play. There is no second counter, no HUD
readout, and no explanatory text anywhere in the game.

It decrements **once**, at a single authored moment: when the lamppost assembly
completes at the factory. It never decrements on death, on time, or on kills.

## The three areas

### Home (15–25s)

- One cramped Khrushchyovka apartment room, dressed to the current **apartment
  state** (see below).
- **The brick and the pipe are always by the door**, in the same place, every
  shift. Subtraction never touches what the shift requires.
- Bleak courtyard view through the window — the courtyard lamppost is visible
  from here, lit or dead.
- Exit to the stairwell → Street.
- **After each completed shift the room loses one large object.** The room never
  gains anything.

### Street (50–70s)

- **Two** compact modular chunks, one unmistakable route from the courtyard to
  the factory gate. No branching path, no optional area.
- The same two chunks are reused every shift. What changes is **lighting,
  lamppost state, and encounter placement** — never the geometry.
- **Five lampposts** along the route (see the extinguish order below).
- One or two shallow alcoves off the lane for encounter variety. No alley
  detours in the jam slice (they existed for the Kipuchka steal, which is a
  stretch goal — see [`characters.md`](characters.md)).
- Fight the two archetypes; scavenge spare bricks.
- Ends at the **factory gate**, which is signposted and, until the very end,
  lit.
- Spawns are hooked by **chunk index and `shifts_remaining`** so difficulty is
  placed, not random.

### Factory (45–60s)

- **One** compact hall: assembly table, welding station, a short finishing
  conveyor, and a small open floor. No boss arena in the jam slice.
- The **board** hangs over the conveyor at the end of the hall, visible from the
  entrance: `ОСТАЛОСЬ: N`. See below.
- The **3-step lamppost assembly** under pressure (see
  [`mechanics.md`](mechanics.md)).
- **One escalation** — a single ordinary wave at step 2. No boss, no second
  phase, no gimmick cycle.
- On completion: the finished lamppost rolls off the conveyor, the board ticks
  down, and the **return trigger** opens.

## The factory board

A mechanical flip-digit board over the conveyor, the kind that would carry a
production quota. It reads:

```
ОСТАЛОСЬ: N
```

Rules:

- On shift 1 it reads `ОСТАЛОСЬ: 5` and should parse as a **production plan** —
  units left to build, shifts left in the week, a norm. Nothing contradicts that
  reading.
- It ticks down **diegetically and on screen**: the assembly completes, the
  lamppost leaves the conveyor, the board clacks over one digit. The player is
  looking at it when it happens.
- Its number always equals the lit lampposts on the street and the apartment
  stage. The player is invited to notice; the game never states it.
- **The game never explains what the board counts.** No dialogue, no note, no
  HUD mirror.

## The countdown table (authored, not procedural)

| Shift | Board | Lit lampposts | Dead lamppost (new)   | Apartment state | Object lost      |
| ----- | ----- | ------------- | --------------------- | --------------- | ---------------- |
| 1     | 5     | 5             | —                     | 0 — intact      | —                |
| 2     | 4     | 4             | L1 courtyard          | 1               | television       |
| 3     | 3     | 3             | L2 chunk A            | 2               | wardrobe         |
| 4     | 2     | 2             | L3 chunk A/B seam     | 3               | table + chair    |
| 5     | 1     | 1             | L4 chunk B            | 4               | wall objects     |
| final | 0     | 0             | L5 factory gate       | 5               | bed / remainder  |

A row's state is dressed at the **start** of that shift. The change therefore
lands on the player during the **return home** that ends the previous shift and
the walk out that begins the next one.

### Lamppost extinguish order

Authored, fixed, and read from the courtyard outward toward the factory:

1. **L1 — courtyard**, right outside the entrance. Visible from the apartment
   window. The first light lost is his own doorway.
2. **L2 — chunk A**, mid-lane.
3. **L3 — the seam** between chunk A and chunk B, the route's clearest landmark.
4. **L4 — chunk B**, mid-lane.
5. **L5 — the factory gate.** Still burning on shift 5, when it is the only lamp
   left in town. The workplace keeps its light longest. It goes out for the
   final lap.

A dead lamppost is **visibly dead** — dark glass, no pool of light on the ground
below it, a faint tick of cooling metal as you pass. It is not removed from the
world; the pole stays. The town keeps its posts and loses its light.

### Apartment states

Fixed authored sequence, one object per shift, each large enough to register in
a 20-second visit. **Every removal leaves a trace** — the absence must be
legible, not just an empty spot.

0. **Intact.** Television, wardrobe, table and chair, wall carpet / calendar /
   photograph, bed. The television is **interactable** — flipping it is the
   game's one comfort interaction and it cues the Home music state.
1. **No television.** A bright unfaded rectangle on the wallpaper, a dust
   outline, the cord still hanging out of an empty socket. *The one comfort
   interaction is the first thing the countdown takes.* From here the room has
   only room tone.
2. **No wardrobe.** Four dents pressed into the linoleum, clean pale wall
   behind. The coat now hangs on a nail.
3. **No table, no chair.** A chalk-white ring where a glass stood, the ceiling
   bulb now hanging over bare floor.
4. **Bare walls.** Carpet, calendar, and photograph gone: plaster, nail holes,
   glue ghosts. The pale rectangles are the only pictures left.
5. **Almost empty.** A mattress on the floor, or nothing. Window, door, bulb,
   and the brick and pipe by the door.

Traces are dressing, not props to interact with. The room stops reading as a
refuge somewhere around state 3 and should feel like a corridor by state 5.

## The loop

```
                   ┌─────────────────────────────────────────────────┐
                   ▼                                                 │
  Home ──► Street ──► Factory ──► board ticks down ──► Return Home ───┘   ×5
 (prep)   (commute)  (assembly)   one lamp dies,
                                  one object gone
                                        │
                                   at zero ▼
                    Final lap: Home ──► Street ──► Factory
                    (empty)      (unlit)   (ОСТАЛОСЬ: 0 · ПЛАН ВЫПОЛНЕН)
```

- **Win condition:** complete five shifts, then complete the final lap.
- **Lose condition:** knocked out anywhere → **restart the current shift** from
  Home, with the countdown untouched. Losing costs time, never progress.
- Nothing about the loop escalates through stats. It escalates through **light**.

## The final lap (at zero)

Triggered by the return home that ends shift 5. Roughly 60–90 seconds, using
only spaces and assets that already exist.

- **Home:** apartment state 5. Almost empty. The brick and pipe are still by the
  door; taking them is optional and nothing requires them.
- **Street:** every lamppost dead, including the factory gate. Ambient light
  only — enough to walk the route and read the buildings, no pools of light
  anywhere. **No enemies**, or at most one distant Midnight Smoker that does not
  approach. The town is not hostile; it is finished.
- **Factory:** the hall is quiet, the conveyor is stopped, there is nothing to
  assemble. The board reads `ОСТАЛОСЬ: 0`.
- **End:** on approaching the board, the line **`ПЛАН ВЫПОЛНЕН`** resolves and
  the game ends. No cutscene, no boss, no new location, no explanation.

## Progression & persistence

Progress is not "levels cleared" and not "how strange the day has become" — it
is **how much is left**. The whole save is tiny:

- `shifts_remaining` (5 → 0) — the authoritative countdown.
- `shift_phase` (home / street / factory / returning / final) — for restart.
- `assembly_step` (0–3) — within-shift only, cleared on restart.
- `finished` — the run completed the final lap.

Everything else — lamp states, apartment dressing, palette tier, music layer
count — is **derived** from `shifts_remaining` at load time, never stored
separately. This is a hard rule: two sources of truth is how the board and the
street get out of sync.

Fits one 8 KB memory-card slot many times over; no real save system needed.

## Content status & targets

| Area    | Target for the jam slice                                                     |
| ------- | ---------------------------------------------------------------------------- |
| Home    | One room; 6 dressing states (0–5) with traces; TV interactable in state 0 only; brick + pipe pickups fixed by the door. |
| Street  | 2 greyboxed chunks, one route, 1 gate, 5 lampposts with per-shift lit/dead state, spawns indexed by chunk × `shifts_remaining`. |
| Factory | 1 compact arena, lamppost socket + 3-step assembly, board with 6 digit states, one wave at step 2, return trigger. |
| Global  | `shifts_remaining` state, palette tier ramp, music layer ramp, final lap, `ПЛАН ВЫПОЛНЕН`. |

Buildable levels, tuning tables, and the save schema live in
[`../data/`](../data/). Keep authored surreal events, extra chunks, and bosses in
the [post-jam backlog](production.md#post-jam-backlog) so the slice stays small.
