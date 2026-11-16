# Native moving machine experiment

This standalone probe keeps the source mesh and drives projection and lighting
on SH-2. It is not integrated into the race. Four-bit charts preserve sampled
source material colors; point filtering, native quad mapping, painter order and
additive Gouraud lighting are measured adaptations, not exact N64 rendering.

Generate synthetic input, then build in the pinned Yaul environment:

```sh
python3 tools/model_probe/make_fixture.py
python3 tools/palette_probe/make_assets.py --art fixture
make -C tools/palette_probe FZX_PALETTE_ART=fixture
make -C tools/palette_probe FZX_PALETTE_ART=fixture symbols
```

The optional `retail` selector requires the owner's local source-art export.
Every generated header and its source inputs are bound by receipts. Keep all
retail headers, screenshots, images, raw pixels and traces outside git.

`host.c`, built with `src/fzx_machine.c`, `src/fzx_model.c` and
`src/fzx_material.c`, emits 72 big-endian RGB1555 reference images on stdout and
vertex/lighting/image hashes on stderr. Include `include`, the selected generated
palette directory and its source model directory. These are diagnostic cameras:
eight fixed source LOD/angle checks followed by 64 turning/banking poses.

`tools/emu/palette_probe.ps1` checks the vertex and lighting hashes, source
triangle/texture byte counts and Gouraud saturation counts. Its expected JSON
contains the 72 host rows (`vertices`, `lights`, `triangles`, `bytes`, `saturated`).
`-Cases (0..71)` checks all poses; the default checks the first eight. It also
releases the pose hold and requires automatic motion to advance. Captures and
costs stay private. Emulator costs are MODELLED, never physical Saturn evidence.

The software reference retains three-point filtering and perspective/depth
interpolation. Comparing it to these native sprites quantifies the adaptation;
passing the arithmetic/hash checks does not imply visual acceptance or 60 fps.
