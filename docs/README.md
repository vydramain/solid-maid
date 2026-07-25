# Solidmaid: Alkoldun Vasiliusavich — the reference disc

The main game for the [3dmppc console](../../../docs/platform/), shipping as a
`.mppcdisc` package. A first-person folk-horror shooter set in the post-Soviet
1990s.

**Current version: the jam slice, built for the theme _Count Down_.**

You have five shifts left. Live the **Home → Street → Factory → Home** day loop,
fight with improvised tools (brick, pipe), assemble one lamppost per shift under
pressure — and watch the factory board count down while the town loses a
streetlight and your apartment loses an object every single time.

> He keeps producing light, and the world keeps getting darker.

The whole game runs off **one integer**, `shifts_remaining` (5 → 0). The board,
the streetlights, the apartment, the palette, and the music all derive from it.
There is no HUD counter and no second countdown anywhere.

It is also the project by which we **prove the console**: the first real
disc. Once it runs well, the console and the game get split into separate
repositories — so keep this design free of console *implementation* detail
(that belongs in [`../../../docs/platform/`](../../../docs/platform/)).

This directory holds only the **design**. The buildable content of the game
(assets, gameplay code, data) lives alongside it in the disc — see
[`../`](../).

## Documents

| Document                              | Contents                                                  |
| ------------------------------------- | --------------------------------------------------------- |
| [`overview.md`](overview.md)          | Pitch, pillars, the countdown, the shift loop, jam scope. |
| [`gameplay.md`](gameplay.md)          | Genre, camera/view, the console's rendering character, controls. |
| [`world.md`](world.md)                | Setting, tone, and fiction — and the count as fiction.    |
| [`environments.md`](environments.md)  | Buildable description of the apartment, street, and factory shop. |
| [`characters.md`](characters.md)      | Protagonist, bestiary, and the (post-jam) bosses.         |
| [`mechanics.md`](mechanics.md)        | Controls, countdown state, combat, light rules, assembly. |
| [`content.md`](content.md)            | The three areas, the countdown table, the final lap.      |
| [`art-and-audio.md`](art-and-audio.md)| Visual style, the impoverishment ramp, and the **full texture + audio asset manifest**. |
| [`production.md`](production.md)      | Milestones, backlog, metrics, risks, post-jam backlog.    |
| [`protagonist_profile.md`](protagonist_profile.md) | The protagonist's fixed visual canon (modeling reference). |

Buildable content (art, models, gameplay code, data) lives in the disc:
[`../assets/`](../assets/), [`../src/`](../src/), [`../data/`](../data/) — e.g.
the protagonist `.glb` model and concept sketch are in
[`../assets/`](../assets/).

## Jam scope at a glance

**In:** five shifts + a final lap; one countdown state; the factory board
`ОСТАЛОСЬ: N`; five lampposts going out one per shift; six apartment states with
traces; two enemies; two weapons; one 3-step assembly; two street chunks; one
factory hall; 8–12 minutes total.

**Out (post-jam, still canon):** all bosses including Warehouse Manager + Zmey
Gorynych, Leshaki, the pillar ranged unlock, the Kipuchka steal, extra street
chunks, authored surreal events. See
[post-jam backlog](production.md#post-jam-backlog).

**Rejected outright:** limited bricks, spendable shifts, skippable ritual steps,
any second numeric system, procedural world corruption.

## Console fit (hard constraints)

The hardware limits from
[`../../../docs/platform/specs.md`](../../../docs/platform/specs.md). Every
design decision in this game must fit within them. If an idea can't fit (say,
512×512 textures), that's a proposal to change the *console spec* — take it to
[`../../../docs/platform/`](../../../docs/platform/), don't bend the game docs
around it:

- **Display:** 320×240 (or 256×224), 16-bit color + dithering.
- **Textures:** 4-bit / 8-bit paletted, no filtering (nearest).
- **Memory:** 2 MB RAM, 1 MB VRAM.
- **Input:** 2 ports, full controller surface. **The game is gamepad-only** — the
  console's mouse-look channel exists and is deliberately unused.
- **Audio:** 24 voices; **512 KB sound RAM**; raw PCM S16LE mono 44 100 Hz
  (ADPCM deferred) — that is **under 6 seconds of resident audio**, which is the
  hardest constraint in the project. See
  [`art-and-audio.md`](art-and-audio.md#audio--read-this-before-writing-a-single-note).
- **Save:** memory card, 16 slots × 8 KB — the four countdown state fields fit in one.
