# Environments — Apartment, Street, Factory Shop

Physical, buildable description of the three spaces. This is the reference for
blockout, modeling, and texturing; the fiction is in [`world.md`](world.md), the
structure and timings in [`content.md`](content.md), and the per-shift countdown
states in [`content.md`](content.md#the-countdown-table-authored-not-procedural).

**Period and season are fixed:** a provincial Russian industrial town, autumn
1990s, late October — no leaves, wet asphalt, grey overcast, dark by the time the
shift ends. Everything is worn, repaired rather than replaced, and painted over
several times.

Two rules run through every space:

1. **Grounded, not decorated.** Nothing here is stylised folklore. Dread comes
   from the room being exactly right and one thing being missing.
2. **Built for 320×240.** Every object must read by silhouette at low
   resolution and stay legible when its light goes out. Detail lives in the
   palette and in the layout, not in the texel count.

---

## 1. Home — a one-room Khrushchyovka apartment

### Shell and dimensions

A single-room flat (`однокомнатная`) in a five-storey panel block, third floor.
The player sees a small hallway and the one room; the kitchen and bathroom are
doors that do not open.

- **Room:** ~4.2 × 3.4 m. **Ceiling: 2.50 m** — this is the single most important
  measurement in the game. With the camera at ~1.65 m there is only ~0.85 m of
  air above the player's head. The apartment must feel *low*. Do not round the
  ceiling up to a comfortable 3 m; the compression is the character of the space.
- **Hallway (`прихожая`):** ~1.2 × 2.5 m, dark, no window. Front door at one end,
  room door at the other. This is where the brick and the pipe live.
- **Doorways:** 0.8 × 2.0 m, no thresholds worth tripping on.

### Surfaces

- **Walls — wallpaper (`обои`).** Paper, not vinyl: a small repeating pattern —
  fine geometric lozenges or a muted floral, brown-ochre on cream. Sun-faded on
  the window wall, darker and cleaner behind furniture. Joins are visible and
  slightly mismatched; one seam near the corner is lifting. Above the radiator
  the paper is yellowed by heat.
- **Hallway walls** are painted, not papered: oil paint (`масляная краска`) in
  dull green up to 1.5 m, whitewash above, with a painted dividing line. The
  paint is glossy where hands have touched it by the light switch.
- **Floor — linoleum** in a wood-imitation print, laid in one sheet, worn through
  to grey felt along the walking line between the door, the window, and the bed.
  Brown-painted wooden skirting (`плинтус`), one length of it coming away from
  the wall.
- **Ceiling** — whitewash (`побелка`), slightly uneven, with a rust-brown
  water stain in the corner nearest the window from a neighbour's leak.

### Window

The window is the room's only source of daylight and the player's only view of
the courtyard — **including the L1 lamppost**, which is how the first
extinguished lamp gets noticed from indoors.

- Wooden double frame (`деревянные рамы`), painted white over an older layer of
  blue that shows through the chips. Two casements plus a small hinged
  ventilation pane (`форточка`) in the upper left.
- The gap between the inner and outer frames holds a strip of grey cotton wool
  (`вата`) stuffed in for winter, and dead flies.
- **Wide painted wooden sill:** two three-litre glass jars, an aloe in a tin can,
  a folded newspaper, a box of matches.
- **Cast-iron radiator (`батарея`)** under the sill, painted silver-white over
  many layers so the fins are soft-edged. Socks drying on it.
- Curtains: a thin translucent `тюль`, greyed, plus one heavy drape pushed to one
  side and never closed.

### Light

- **Ceiling fixture:** a three-arm chandelier with pressed-glass shades; **only
  one bulb works**, so the light is off-centre and the corners of the room stay
  dark. Warm, dim, ~2700 K.
- **Daylight** from the window is flat, cold, and grey — it does not brighten the
  room, it only makes the far wall visible.
- Soviet switch (`клавишный выключатель`) by the door, a round socket beside the
  television with the wiring run externally in a plastic channel along the
  skirting.

### Furniture and props (state 0 — intact)

Listed with the countdown stage that removes each; the removal order and the
traces are canon (see [`content.md`](content.md#apartment-states)).

| Object | Description | Removed after shift |
| --- | --- | --- |
| **Television** | A wooden-cased colour set on a low stand with a crocheted doily on top, a vase, and a plastic remote that does not belong to it. The screen is convex and green-grey when dark. **The one interactable object in the game's quiet spaces.** | 1 |
| **Wardrobe** | A polished veneer wardrobe-and-sideboard unit (`стенка`) along the long wall, honey-brown, with a glazed section holding crystal glasses that are never used and a stack of documents. | 2 |
| **Table + chair** | A small table under an oilcloth (`клеёнка`) with a faded floral print, sticky at one corner; one wooden chair and one stool. On the table: a glass in a metal tea-glass holder (`подстаканник`), a jar of sugar. | 3 |
| **Wall objects** | A dark-red geometric carpet (`ковёр`) hung on the wall above the bed; a 199x wall calendar with a photograph of a mountain; a framed family photograph; a wall clock. | 4 |
| **Bed / remainder** | A folding sofa-bed or a panel bed with a grey blanket and one flattened pillow. | 5 |

**Never removed** — these persist into the final lap: the window, the radiator,
the ceiling fixture, the door, the skirting, and **the brick and the pipe leaning
by the front door**. By state 5 the work gear is the only thing left with a
purpose.

### Traces (the important art work)

Each removal must leave evidence on a surface that already exists. Author these
as texture regions on the wallpaper and floor atlases, not as new props.

- **Television:** a rectangle of wallpaper in its original unfaded colour, sharp
  edged; a fuzz of dust along its bottom line; the empty socket with the cord
  still plugged in and lying on the floor.
- **Wardrobe:** four dark compressed dents in the linoleum where the feet stood;
  clean pale wall behind; the coat now on a nail hammered directly into the
  wallpaper.
- **Table + chair:** a chalk-white ring where the glass always sat, on bare
  linoleum now; crumbs; the single working bulb now hanging over nothing.
- **Wall objects:** the carpet's outline in clean wallpaper with four nail holes
  at the corners; the calendar's smaller rectangle; a smear of glue; the clock's
  single nail and a pale disc around it.
- **Bed:** two long depressions in the linoleum and a darker band along the
  skirting where it was never washed behind.

### Sound

Room tone only: the muffled hum of the block, a neighbour's footsteps overhead,
water in a riser pipe, the radiator ticking. On state 0 the television adds its
loop; **from state 1 onward nothing replaces it.** The apartment gets quieter as
it gets emptier — this is deliberate and must not be compensated for.

---

## 2. The Street — courtyard to factory gate

Two modular chunks and one route, reused every shift. The geometry never
changes; the light does. Total walking length ~120–150 m of playable lane.

### Overall shape

The route reads as one continuous corridor with no branches: **courtyard →
between the blocks → along the heating main → the concrete fence → the factory
checkpoint.** The player should never need to ask which way to go, even at
`ОСТАЛОСЬ: 1`.

### Chunk A — the courtyard and the block

- **Five-storey panel blocks (`хрущёвки`)** on both sides, series 1-464: five
  floors, flat roof, visible horizontal and vertical panel seams, panels finished
  in grey-cream pebble dash (`гравийная крошка`) that has darkened unevenly with
  damp. Rust streaks run down from every embedded fixing.
- **Windows:** wooden frames painted white, a grid of small panes, arranged in a
  strict repeating rhythm. **Most are dark; a handful are lit warm yellow**, and
  which ones are lit is authored per shift — as the street goes dark the lit
  windows become the main remaining source of colour, then they too thin out.
  Some panes are patched with tape or cardboard.
- **Balconies:** glazed inconsistently by each owner — some open with a rusted
  rail, some enclosed in mismatched frames, one clad in corrugated asbestos
  sheet. Skis, a bicycle, and jars stored on them.
- **Entrances (`подъезды`):** a concrete canopy, a heavy sprung door, a blue
  enamel house-number plate, a painted street sign `ул. Заводская`. A wooden
  bench by each door, a bin, cigarette ends.
- **Courtyard floor:** cracked asphalt patched with darker asphalt, a raised
  concrete kerb, a manhole cover, standing water in the depressions reflecting
  whatever light is left.
- **Courtyard props:** a **carpet-beating frame** (`ковровыбивалка`) — a bare
  steel pipe rectangle on two legs, the single most identifying object of the
  setting and an excellent silhouette landmark; a sandpit with no sand; two
  bent swings; a laundry line; three bare poplars.
- **L1 lamppost** stands here, visible from the apartment window.

### Chunk B — the industrial approach

- **Metal garages (`ракушки`)** in a row, corrugated, painted whatever paint was
  available — dull green, oxide red, blue — with padlocks and hand-painted
  numbers.
- **The heating main (`теплотраса`)**: insulated pipes in a dented galvanised
  casing, running above ground on low concrete supports, rising over the path in
  a shallow arch you walk under. This is the route's spine — a continuous
  handrail of geometry pointing at the factory, which keeps navigation solid when
  the lamps are gone.
- **A commercial kiosk** (`ларёк`): a small steel box with a shuttered serving
  window, faded chocolate-bar and cigarette stickers, a hand-lettered `24 ЧАСА`.
  Closed on every shift.
- **Concrete fence PO-2** — the standard precast panel with the raised diamond
  pattern, grey, some panels leaning, one gap wide enough to see the works
  behind. Barbed wire on brackets along the top.
- **Road surface:** broken asphalt, a faded painted crossing, a storm drain, tyre
  tracks in mud at the verge.
- **The factory checkpoint (`проходная`)**: a small brick building with a green
  door, a lit window, a turnstile, and a sign. Two steel gates beside it. **L5
  hangs over the gate** — the last lamp burning in the game.

### Lampposts

The countdown's main visual. All five are the same asset in two states.

- **Type:** a concrete pole, slightly tapered, weather-stained, with a steel
  bracket arm and a shallow bowl fixture — the standard `РКУ` mercury lantern.
- **Lit:** the lamp glass is a hard pale **green-cyan** — mercury light, not warm
  sodium. This is a deliberate palette decision: the streetlights are cold and
  faintly sick, the apartment windows are warm, and the two never mix. It casts
  an elliptical pool on the asphalt below with a visible dithered edge.
- **Dead:** the glass is dark grey-green, the bracket reads only as silhouette,
  and **the pool on the ground is gone.** The pole is never removed. The town
  keeps all five posts to the end and loses all five lamps.
- Placement: L1 courtyard · L2 mid chunk A · L3 the seam between chunks ·
  L4 mid chunk B · L5 over the factory gate. Extinguished in that order.

### Sky and ambient

- Flat overcast, no sun, no stars: a shallow vertical gradient from dirty
  grey-blue at the horizon to a darker cold blue overhead, dithered. The sky is
  the ambient floor's justification — it is why the street is never pitch black.
- No cars, no traffic, no pedestrians other than the enemies.

### Sound

Distant industrial hum, wind in the poplars, a dog somewhere, a train coupling
far off, water dripping from the heating main. Footsteps change surface: asphalt,
puddle, mud, metal grating.

---

## 3. The Factory — one assembly shop

A single bay (`пролёт`) of a lamp works. Compact by design: the whole encounter
happens in one room the player can read from the doorway.

- **Footprint:** ~24 × 14 m, **ceiling ~7 m** — the deliberate opposite of the
  apartment. The player steps out of a 2.5 m box into volume, once per shift, and
  it is the only time the game gives them air.

### Structure

- **Columns:** precast reinforced-concrete columns on a regular grid, grey,
  chipped at the corners, painted with a black-and-yellow hazard band at the
  base and a stencilled number.
- **Roof:** exposed steel roof trusses, riveted, painted dull silver, thick with
  dust. Above them a **roof lantern** (`фонарь верхнего света`) of dirty wired
  glass letting in a weak, colourless daylight that reaches nothing on the floor.
- **Overhead crane rail** running the length of the bay with a small gantry
  parked at the far end — it never moves.
- **Walls:** silicate brick, painted in dull green oil paint to 1.8 m with a
  painted line, whitewash above, both flaking. Grime rings around every bracket.
- **Floor:** poured concrete, oil-stained, with metal swarf and puddles of
  coolant. **Yellow painted floor lines** mark the walkway and box out the
  machine areas — these double as the game's clearest readability aid and should
  stay visible at every light tier.

### Equipment (the working set)

- **Assembly table:** a heavy steel bench with a vice, scattered fixings, a
  wooden mallet, and the lamppost parts laid out — this is where the 3-step
  assembly happens.
- **Welding post:** a curtain screen on a frame, a helmet on a hook, gas
  cylinders chained to the wall, a coiled cable, a scorched sheet on the floor.
  Its flashes are the one bright light event in the game and can be used to
  punctuate the escalation wave.
- **Finishing conveyor:** a short roller conveyor running toward the far wall,
  with a guard rail. **The finished lamppost leaves on it** at the end of every
  shift, in view, before the board ticks.
- **Dressing:** a rack of steel shelving with boxes, wooden pallets, coils of
  cable, a drill press, a barrel, a sack trolley, a tea kettle on a stool with
  two enamel mugs.

### The board

The single most important object in the game and the only number in it.

- **What it is:** an information board of the kind that carried production
  figures — a painted steel panel with a hand-lettered header and mechanical
  flip-digits set into it, dim bulbs behind a grimy plastic strip lighting it
  from within.
- **Placement:** high on the far wall directly above the conveyor, facing the
  entrance. **The player sees it the moment they walk in**, every shift, without
  being directed to look.
- **Reads:** `ОСТАЛОСЬ: N` in a plain industrial stencil. On shift 1 it must be
  possible to read this as a work quota and nothing more.
- **Behaviour:** when the assembly completes and the lamppost rolls away, the
  digit flips over with a single dry mechanical clack.
- At zero it reads `ОСТАЛОСЬ: 0`, and the phrase **`ПЛАН ВЫПОЛНЕН`** resolves on
  the same panel, in the same lettering. No separate UI is ever used for this.
- Nearby, as period dressing that supports the reading without explaining it: a
  socialist-competition honour board with faded photographs, a wall newspaper, a
  duty roster, and stencilled signs — `СОБЛЮДАЙ ТБ`, `НЕ КУРИТЬ`,
  `ПОСТОРОННИМ ВХОД ВОСПРЕЩЁН`.

### Light

- Suspended industrial fixtures with dusty diffusers in two rows; **one flickers
  on a slow irregular cycle** at every tier. The shop is always the
  best-lit space in the game — which is the point: the factory keeps its light
  while the street loses it.
- The hall dims slightly per tier, but **less than the street does.** By the last
  shift the walk to work is nearly dark and the workplace is still working.
- On the final lap the shop is lit but inert: no welding flashes, conveyor
  stopped, one fixture out.

### Sound

Transformer hum, a ventilation fan, an air line hissing, water dripping into a
drum, the conveyor's rollers, distant metal being dropped in another bay. During
assembly the layer thickens; when the board clacks, everything else should duck
for a moment so the clack lands alone.
