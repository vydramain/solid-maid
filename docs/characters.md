# Characters — Protagonist, Bestiary & Bosses

Character canon is unchanged by the jam re-scope; what changes is **which of it
ships**. Cut entries stay here as canon and are tracked in the
[post-jam backlog](production.md#post-jam-backlog).

## Protagonist — Alkoldun Vasiliusavich

A post-Soviet 1990s factory worker whose mundane routine intersects with faint
occult undertones. ~35, worn by industrial labour, pale, tired, resigned; eyes
permanently hidden behind greasy black hair. Tall, narrow silhouette defined by
a **long matte-brown leather trench coat** over dark-navy factory overalls, a
**tall black wizard hat** with a soft drooping tip, and black combat boots
(*bertsy*). Hands always bare.

The full, fixed visual canon — morphology, outfit, palette, allowed deviations,
forbidden elements, and the T-pose modeling reference — lives in
[`protagonist_profile.md`](protagonist_profile.md). Concept sketch and `.glb`
model are in [`../assets/`](../assets/). **Treat that profile as authoritative**
for any art or modeling work.

In first person the player mostly sees the world and their hands/held tools;
the full silhouette matters for reflections and marketing. There is no
day-summary screen in the jam slice and no cutscene at the end.

He does not comment on the count. He never says the number out loud, never
reacts to the missing furniture, and never acknowledges a dead lamppost. The
player notices; he goes to work.

## Enemy bestiary

Two archetypes ship in the jam slice; the third is post-jam.

- **Kipuchka** *(pickpocket / kikimora)* — **in slice, no-steal.** Small, fast,
  jittery melee pest. Teaches positioning and punishes greed.
  The **component-steal** behaviour (stun, take the lamppost part, flee to an
  alley, recover by chasing it down) is a **stretch goal only** — it needs alley
  geometry, a recovery loop, and a fail-safe, none of which fit a 1.5–2 minute
  shift. Ship the no-steal version; add the steal only if the whole game is done
  and stable.
- **Midnight Smokers** *(courtyard phantoms)* — **in slice.** Gopnik-like
  figures made of cigarette smoke: grey tracksuits, dim window-yellow eyes, a
  whispery *"hey, bro…"*. Exhale a **smoke cloud** that cuts visibility and deals
  chip damage. Area-denial pressure; readable pre-warm before the cloud.
  **They get more dangerous on late shifts without any stat change.** HP,
  damage, cloud radius, and spawn counts are identical on shift 5 and shift 1 —
  but with the lampposts dead, their grey silhouettes read later against unlit
  facades and the cloud's true extent is much harder to judge. Their pre-warm
  ring stays self-lit and unmistakable at every tier: the player always knows the
  attack is coming, and increasingly cannot tell how far it reaches. This is the
  intended difficulty curve of the whole game, and it costs nothing to build.
- **Leshaki** *(low forest spirits)* — **post-jam.** Thick-necked bruisers in
  crimson jackets with gold chains. High HP, heavy knockback, disorienting. No
  new enemy types ship in the jam slice.

Design rules: strong telegraphs (≥ ~300 ms windups), low clutter, loud audio
cues — the software rasterizer and first-person view demand legibility over
detail, and darkness never gets to break it. See combat in
[`mechanics.md`](mechanics.md).

## Bosses (Factory)

Folk-tale figures wearing 90s-institution masks — canon, and **none of them ship
in the jam slice.** The factory escalation is a single ordinary wave at assembly
step 2; there is no boss fight and no boss arena. A two-minute shift repeated
five times has no room for one, and a boss would compete with the board for the
player's attention at exactly the moment the countdown ticks.

- **Warehouse Manager + Zmey Gorynych** — **post-jam, was the slice candidate.**
  A composite boss: a corrugated-pipe body with **three heads — Stock,
  Accounting, Write-off.** Arena clutter spawns, audit debuffs, and item burns
  map to the three heads. Beaten by **baiting the heads into harming each
  other.** Retained in full as canon; the natural home for it is a longer
  post-jam version where a shift can afford 4–6 minutes.
- **Accountant — Baba Yaga** — **post-jam.** Spins in an office chair, curses via
  documents (order / act / certificate) that apply debuffs; can "freeze" the
  screen into tables until a wrong number is found. Vulnerable around the chair
  wheels.
- **Oligarch — Koschei the Deathless** — **post-jam.** Corpulent suit,
  golden-coin eyes (the weak point); heavy ground slams and a grab/split move.

## Casting notes

- The bestiary and bosses are the primary vehicle for the **folklore-as-
  bureaucracy** motif ([`world.md`](world.md)); keep their fiction grounded in
  90s industrial life, with the folk-tale layer showing through behaviour.
- In the jam slice that motif is carried almost entirely by the **board**, not by
  a character. Nothing in the game personifies the countdown, and nothing should
  — there is no antagonist, only a plan being met.
- Every enemy must read at 320×240 with paletted textures, **at the darkest
  playable tier**: silhouette first, one signature colour/effect, one telegraphed
  attack.
