# Solidmaid: Alkoldun Vasiliusavich — an mppc disc

A first-person folk-horror shooter set in the post-Soviet 1990s, built as a
`.mppcdisc` for the [3dmppc console](https://github.com/vydramain/3dmppc-polymer).

This repository is the whole disc: its **design** in [`docs/`](docs/) and its
**buildable content** in [`assets/`](assets/), [`scripts/`](scripts/), and
[`data/`](data/). The console lives in its own repository and knows nothing
about this game — see
[its README](https://github.com/vydramain/3dmppc-polymer#readme) for how a disc
is burned and run.

## Layout

```
assets/     art, models, textures, audio
            - protagonist.obj          (runtime model: PSX-style low-poly,
                                        900 tris, palette-atlas UVs)
            - protagonist.mtl          (its material)
            - protagonist_tex.png      (runtime texture: 64x64 palette atlas,
                                        16 swatches, canon colors)
            - protagonist_model.glb    (source: AI-generated high-poly the
                                        low-poly was decimated from; Git LFS)
            - protagonist_scetch.png   (protagonist concept sketch)
            - enemies/ home/ outside/ player/ …
                                       (Godot-era pixel art, kept as source
                                        material for the console's textures)
docs/       the design — start at docs/README.md
scripts/    gameplay logic (planned: Lua) — stub
data/       levels, tuning tables, save schema, loop flags — stub
```

Nothing loads these files yet: the disc is not burned against the console's
`pdk/` contract so far, so no `disc.toml` and no `src/` exist here. The `.glb`
source and the sketch stay here as references either way.

## Status

Early. The protagonist has a runtime-ready low-poly model and texture (derived
from the Godot-era concept art via voxel-remesh → decimate → procedural palette
paint). Everything else is to be built as the console's disc API comes
online. See the design docs for what goes here:

- [`docs/overview.md`](docs/overview.md) — the game
- [`docs/art-and-audio.md`](docs/art-and-audio.md) — asset budget & pipeline
- [`docs/production.md`](docs/production.md) — milestones

## Budget reminder

Assets here must fit the console: 320×240, 16-bit + dithering, **4/8-bit
paletted textures**, 1 MB VRAM, 2 MB RAM. See
[the console's `specs.md`](https://github.com/vydramain/3dmppc-polymer/blob/master/docs/platform/specs.md).

## Working on this alongside the console

The console repository ignores symlinked discs, so you can put this checkout on
its shelf and keep both under their own version control:

```sh
ln -s "$PWD" ../3dmppc-polymer/mppcdiscs/solid-maid
```
