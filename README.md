# solid — disc

*Solidmaid: Alkoldun Vasiliusavich* — the reference game for the 3dmppc console.
This directory is the **buildable content** of the disc; the **design** lives in
[`../../docs/mppcdisc/solid/`](../../docs/mppcdisc/solid/).

## Layout

```
solid/
  assets/     art, models, textures, audio
              - protagonist.obj          (runtime model: PSX-style low-poly,
                                          900 tris, palette-atlas UVs)
              - protagonist_tex.png      (runtime texture: 64x64 palette atlas,
                                          16 swatches, canon colors)
              - protagonist_model.glb    (source: AI-generated high-poly the
                                          low-poly was decimated from)
              - protagonist_scetch.png   (protagonist concept sketch)
  scripts/    gameplay logic (planned: Lua) — stub
  data/       levels, tuning tables, save schema, loop flags — stub
```

Nothing loads these files yet: the console reads disc content through the disc
drive (`rv_cd`), which is still a stub, so no assets are copied into the build
tree. The `.glb` source and the sketch stay here as references either way.

## Status

Early. The protagonist has a runtime-ready low-poly model and texture (derived
from the Godot-era concept art via voxel-remesh → decimate → procedural palette
paint). Everything else is to be built as the console's disc API comes
online. See the design docs for what goes here:

- [`../../docs/mppcdisc/solid/overview.md`](../../docs/mppcdisc/solid/overview.md) — the game
- [`../../docs/mppcdisc/solid/art-and-audio.md`](../../docs/mppcdisc/solid/art-and-audio.md) — asset budget & pipeline
- [`../../docs/mppcdisc/solid/production.md`](../../docs/mppcdisc/solid/production.md) — milestones

## Budget reminder

Assets here must fit the console: 320×240, 16-bit + dithering, **4/8-bit
paletted textures**, 1 MB VRAM, 2 MB RAM. See
[`../../docs/platform/specs.md`](../../docs/platform/specs.md).
