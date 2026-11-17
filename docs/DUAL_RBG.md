# Dual rotation background preparation

C5 remains incomplete. The standalone synthetic probe passes in Ymir, including
actual memory/register readbacks and all six distinguishing controls. Yaba has
not progressed beyond its boot entry in hidden runs. The two-emulator gate,
game integration, motion/look comparisons and combined cost remain open.
See [the probe](../tools/dual_rbg_probe/README.md) for exact banks and controls.

The proposed game layout needs the floor bitmap to fit one 128 KB VDP2 bank,
leaving another bank for the shared reciprocal table and B0/B1 for RBG1.
`tools/floor_indexed.py` prepares the original floor as a 512x256 8-bit bitmap
and a 512-byte RGB1555 palette. It repeats the authored 64x32 tile without
resizing, filtering or replacement art.

The original tile has 308 colors. A deterministic offline merge retains 256
original color representatives; 1,996 of 2,048 source pixels stay exact and
52 change by one RGB5 step in one channel. The generator checks every original
member during palette merges so consecutive merges cannot accumulate error.
All 131,072 repeated bitmap pixels pass independent reconstruction checks.
Windows/Linux outputs are identical. A naive top-256 palette control exceeds
the bound with a six-step error and is rejected.

Index zero contains opaque floor art. Native integration must disable RBG0
transparency; treating zero as a transparent pixel would introduce holes.
The receipt binds the generator, input manifest, original floor and all outputs.
Synthetic tests cover opacity, extent, repeat addressing, merge bounds,
unrepresentable inputs and receipt corruption. This is HOST asset preparation;
it is not a runtime change, memory saving or appearance acceptance.

```sh
python3 tools/floor_indexed.py
bash tools/check_receipt.sh generated/floor_indexed/floor_indexed.receipt
python3 tests/test_floor_indexed.py
```

Private evidence: `host/floor-indexed/verification.json`. Generated art remains
outside Git. The current floor/cloud atlases remain resident until the native
replacement and its knob-off, motion, residency and combined-cost gates pass.
