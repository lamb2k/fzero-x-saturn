# Native material probe

This standalone image exercises the source material sampler and bounded software
triangle renderer. It is a diagnostic reference, not the racing renderer. Eight
fixed views cover four angles of the highest machine LOD plus the four lower
source LODs. Projection and approximate HLE lighting are prepared offline; race
camera, near-plane clipping, culling and dynamic lighting are not implemented here.

Generate public synthetic input with `python3 tools/model_probe/make_fixture.py`.
The owner's local source-art export supplies the optional retail pack instead.
Both use receipts; stale generated output stops the build.

In the Yaul environment:

```sh
make -C tools/model_probe FZX_MODEL_ART=fixture
make -C tools/model_probe FZX_MODEL_ART=fixture symbols
```

`FZX_MODEL_ART=retail` selects the local source pack. `FZX_MODEL_DIVU=1` enables
the exact signed 64/32 SH-2 divider in covered-pixel interpolation; default is zero.
Build/output directories include both selectors. No game-build flags are changed.

Compile `host.c` with `src/fzx_model.c` and `src/fzx_material.c`, include `include`
and the selected generated directory. Its stdout contains eight big-endian
RGB1555 images; stderr records each view's pixel hash and work counts. Keep retail
output outside git. `tools/emu/model_probe.ps1` consumes an eight-row JSON array
of those hashes, validates repeated draws and saves screenshots and FRT counts.
Supply `FZX_YABA_EXE` and any dependencies needed by that existing emulator build.
Captured pixels are EMULATOR evidence; FRT costs are MODELLED, never console timing.

HOST controls exercise top-left coverage, depth, invalid indices and a valid-range
area-one sliver whose interpolant would overflow signed 64-bit setup. Removing the
guard fails under UBSan. Material controls independently corrupt UV scaling,
filter diagonals and reflection blending against the pinned source shader.
The emulator gate also rejects a deliberately changed expected image hash.

The software raster is deliberately double-sided. Structurally invalid input is
rejected before image writes; unsafe numeric setup may return false after clearing
or partially drawing the output, which callers must discard. No input assets,
screenshots, generated headers or retail-bearing images belong in this directory.
