# Overview

## Title

**Solidmaid: Alkoldun Vasiliusavich**

## Jam framing

This document describes the **jam slice** built for the theme **Count Down**.
The countdown is not a feature bolted onto the game — it is the game's
structure, its progression, its environment, and its ending. Everything below
is scoped to a demo that plays in **8–12 minutes**.

Longer-term canon that does not fit the jam (bosses, extra enemies, the ranged
unlock) is preserved, not deleted — see [Post-jam backlog](production.md#post-jam-backlog).

## Elevator pitch

You have **five shifts left**. Wake in a cramped Khrushchyovka apartment, walk
the darkening street, assemble one lamppost at the factory, come home. The
factory board counts down. Each finished shift takes one streetlight out of the
town and one object out of your apartment — until the plan is complete, the
street is dark, and the room is empty.

Alkoldun Vasiliusavich keeps producing light while the world around him goes
out.

## Genre

First-person folk-horror shooter with improvised weapons and a ritual under
pressure. Compact, tone-first, one sitting.

## Platform

The **3dmppc console** — a PSX-like fantasy console. The game ships as a single
`.mppcdisc` disc. See [console fit](README.md#console-fit-hard-constraints)
and [`../../../docs/platform/specs.md`](../../../docs/platform/specs.md).
(Originally a Godot 4.5 PC project; re-targeted to the console.)

## Pillars

1. **Count Down is the structure.** One number governs the whole game:
   `shifts_remaining`, 5 → 0. The board, the streetlights, the apartment, the
   palette, and the music all read off it. There is exactly **one** counter.
2. **Subtraction, not accretion.** The world does not grow stranger — it grows
   *smaller*. Progression is what is missing since last time, not what has been
   added.
3. **The counter is diegetic.** A factory board, dead lampposts, and pale
   rectangles on the wallpaper carry the count. No HUD number, no explanatory
   text, no narrator.
4. **Routine as narrative.** The Home → Street → Factory → Home loop *is* the
   story. Repetition is the delivery mechanism; the shift is short enough that
   the player notices what changed.
5. **Tactile improvised combat.** Bricks and pipes over gun fetish. Throws and
   swings land with arc, impact, micro-shake, hitstop.
6. **Readable first-person play under falling light.** Darkness raises tension,
   never illegibility. Silhouettes, telegraphs, and landmarks stay readable at
   `ОСТАЛОСЬ: 1`.
7. **Satirical folk horror, empathetic.** The target is the plan, not the
   worker.

## The central paradox

> The factory produces streetlights. The town loses one every time a shift is
> completed.

The player is never told this. They are shown a number, a street, and a room,
and left to notice that all three agree.

## Core loop (one shift — 1.5–2 min)

1. **Home — Preparation** (15–25s): a room with one thing fewer than last time.
   Take the brick and the pipe from beside the door. Leave.
2. **Street — Commute & Encounter** (50–70s): two modular chunks, one clear
   route to the factory gate. Kipuchka and Midnight Smokers. **Exactly
   `shifts_remaining` lampposts are lit.**
3. **Factory — Work Under Fire** (45–60s): the 3-step lamppost assembly, one
   escalation wave at step 2. On completion the board **ticks down one digit**
   in front of the player.
4. **Return Home** (~10s): the door, the room, one more object gone. The next
   shift begins.

**Win:** finish five shifts, then walk the final lap at zero.
**Lose:** knocked out anywhere → **restart the current shift**. The countdown
does not advance and does not reset; the board, the lamps, and the apartment
stay exactly where they were.

## The countdown, shift by shift

| Shift | Board       | Lit lampposts | Apartment                    |
| ----- | ----------- | ------------- | ---------------------------- |
| 1     | `ОСТАЛОСЬ: 5` | 5           | intact                       |
| 2     | `ОСТАЛОСЬ: 4` | 4           | no television                |
| 3     | `ОСТАЛОСЬ: 3` | 3           | no wardrobe                  |
| 4     | `ОСТАЛОСЬ: 2` | 2           | no table, no chair           |
| 5     | `ОСТАЛОСЬ: 1` | 1           | bare walls                   |
| —     | `ОСТАЛОСЬ: 0` | 0           | almost empty room            |

The last row is the **final lap**: the same spaces, unlit, near-empty, with no
work to do. It ends on the board reading zero and the line **`ПЛАН ВЫПОЛНЕН`**.

Full detail in [`content.md`](content.md).

## Session length

- One shift: **1.5–2 minutes.**
- Five shifts: **~8–10 minutes.**
- Final lap: **~60–90 seconds.**
- Whole game: **8–12 minutes**, one sitting, restartable at any time.

## In scope (jam slice)

- One shift structure, repeated **five** times, plus the final lap at zero.
- One global countdown state; **no second counter anywhere.**
- The factory board `ОСТАЛОСЬ: N`, decrementing diegetically on assembly
  completion.
- Five street lampposts with an authored extinguish order.
- Five authored apartment states with leave-behind traces.
- Two enemy archetypes (Kipuchka — no-steal behaviour; Midnight Smoker).
- Two weapons (brick + pipe).
- One 3-step lamppost assembly with one escalation wave.
- Street cut to **two** modular chunks and one route.
- Factory cut to **one** compact arena.
- A palette/lighting ramp and a music-layer ramp driven by the same number.

## Out of scope (jam slice)

Deferred to [Post-jam backlog](production.md#post-jam-backlog), **not** cut from
canon:

- Any boss fight, including Warehouse Manager + Zmey Gorynych.
- Baba Yaga, Koschei, Leshaki.
- The pillar ranged unlock.
- Kipuchka's component-steal (stretch goal only).
- Extra street chunks, new areas, authored surreal events.
- Progression trees, inventories, firearms, cutscenes.

Explicitly **not** in the design at all — rejected, not deferred:

- Limited bricks or any second resource used as a countdown.
- Spending shifts or lampposts as currency; skipping ritual steps for a day.
- Multiple independent numeric systems.
- Procedural world corruption.

## Guiding principle

Small, finished, and legible beats sprawling and unfinished. One number, shown
three ways, five times. Every system — the street layout, the throwable brick,
the music stack — should reinforce exhaustion, fleeting agency, and a plan being
fulfilled at the world's expense.
