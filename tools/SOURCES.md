# SOURCES — texture provenance

Everything in `assets/tex/` is generated. There are no downloads, no scanned
photographs, no purchased packs and no assets of unknown licence anywhere in
this directory.

Two scripts produce the whole set, and each one is the editable source for its
output — a texture is changed by editing the script and re-running it, never by
touching the PNG:

| Script | Produces |
| --- | --- |
| `tools/gen_font.py` | `font.png` |
| `tools/gen_textures.py` | the other 24 atlases |

```sh
python3 tools/gen_font.py       # font.png first: gen_textures imports its glyphs
python3 tools/gen_textures.py
```

Both are deterministic: same script, same bytes, every run, on any machine. All
randomness goes through `random.Random(seed)` with the seed written into the
call site, and no system font is ever consulted — every glyph is a bitmap
authored in `gen_font.py`.

---

## Reworked from the earlier Godot incarnation

These are the only places where existing art in `assets/` fed into the new
first-person set. In every case what was carried across is a **palette or a
motif**, re-drawn at the new resolution for 3D surfaces. No 2D tilemap sprite
was pasted onto a 3D surface.

| Source file | What was taken | Where it lands |
| --- | --- | --- |
| `assets/protagonist_tex.png` | The canon character palette, unchanged: pale skin `(196,178,160)` / `(158,138,122)`, matte brown leather coat `(82,59,43)` / `(104,76,54)` / `(72,52,39)`, dark navy overalls `(54,60,88)` / `(38,43,64)`. These are already canon-coloured and are the protagonist's fixed palette. | `hands.png` — the whole atlas. Entries 0–3 are the skin ramp, 5–7 the coat, 8–9 the overalls. The first-person hands and the coat cuff are literally the protagonist's own swatches, so the hands in view cannot drift from the model. |
| `assets/projecttiles/brick_1.png`, `brick_2.png` | The two-tone thrown-brick red: base `(100,48,34)`, lit `(119,73,60)`, and the fresher chip colour `(158,99,81)` from `brick_2`. Snapped to the console's 5-bit lattice and re-drawn as a 3D brick with a top bed, a header face and mortar. | `items.png` entries 0–3 (`SM_UV_BRICK_WORLD`, `SM_UV_BRICK_VIEW`), and `hud.png` entry 12 (`SM_UV_ICON_BRICK`), so the icon and the object are the same red. |
| `assets/outside/Skybox_4.png` | The overcast hue only — its cold grey-blue `(85,92,104)` — as the anchor for the sky ramp. The file itself is a dusk gradient that warms to sand at the horizon, which contradicts `environments.md` ("flat overcast, no sun, no stars"), so the warm half was **not** carried across. | `sky.png` — the eight-tone ramp is built around that blue, running dark cold blue overhead to dirty grey-blue at the horizon, ordered-dithered. |
| `assets/enemies/smoke.png` | The motif only: a lumpy multi-lobed cloud rather than a round puff. Redrawn from scratch as three billboard frames, because the original is a soft-edged 2D sprite and this console has one-bit transparency — every edge had to be re-authored as an ordered stipple. | `smoke.png` cells `SM_UV_SMOKE_A/B/C`. |
| `assets/lampposts/*.png`, `assets/outside/fence.png`, `assets/factory/tile_map_factory.png`, `assets/home/tile_map_home_*.png` | **Inspected, not used.** These are 2D side-on tilemap sprites at a resolution and a projection that do not survive being mapped onto 3D surfaces; the objects they depict are re-authored from `docs/environments.md` instead. Recorded here so the next person does not repeat the evaluation. |

## Wholly new

Everything else — all of `home_walls`, `home_floor`, `home_furniture`,
`home_window`, `home_door`, `street_facade`, `street_ground`, `street_props`,
`street_lamp`, `light_pool`, `gate`, `factory_walls`, `factory_floor`,
`factory_machines`, `factory_board`, `factory_signs`, `lamppost_parts`,
`kipuchka`, `smoker`, `font`, and the non-brick half of `items` and `hud` — is
original work written directly into `tools/gen_textures.py` from the physical
descriptions in `docs/environments.md`, `docs/art-and-audio.md` and
`docs/content.md`.

### The font

`font.png` contains 126 hand-authored 5x7 glyph bitmaps: ASCII 33–126, a
notdef box, and Cyrillic uppercase U+0410–U+042F. `Ё` (U+0401) is authored in
the same table for the in-world signage but has no cell in the atlas, because
the 128-cell layout is fixed by `src/sm_atlas.hpp`.

`gen_textures.py` imports that glyph table, so the lettering painted into the
world — `УЛ. ЗАВОДСКАЯ`, `24 ЧАСА`, `СОБЛЮДАЙ ТБ`, `НЕ КУРИТЬ`,
`ВХОД ВОСПРЕЩЁН`, `ДОСКА`, `ГРАФИК`, `ЗАВОД`, `ЦЕХ` — is the same letterform
the board draws at run time. One typeface, one source, no second font to keep
in sync.

## Colour canon

Two values are fixed by `docs/art-and-audio.md` and appear byte-identical in
every atlas that needs them, from named constants at the top of
`gen_textures.py`:

- `MERCURY` — the streetlights' hard pale green-cyan, `(189,255,230)`.
- `WINDOW_YELLOW` — the apartment windows' warm yellow, `(255,214,107)`, also
  the Midnight Smokers' eyes and the HUD's interact colour.

They are never mixed and never interpolated.
