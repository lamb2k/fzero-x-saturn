# Original race countdown

The original countdown asset conversion passes HOST checks. Native drawing,
live animation, lighting, local scene comparison and sound remain unimplemented.
This is dependency-safe C8 work while C3 waits for its clipping contract.

`tools/countdown_assets.py` reads the stock US rev0 race countdown's three
bounded source recipes. It retains 92 loaded vertex rows, all 46 triangles
and their authored 23 quad pairs: 20 frame/grate faces, two Mr. Zero panels
and one countdown panel. Positions, scaled UVs, normals, material state,
source addresses and source order remain in the private conversion manifest.
Unsupported recipe shapes, references, spans and quad topology fail explicitly.
The tool reuses the existing offline primitive resolver; no display-list
interpreter is linked into the game.

Nine original texture loads convert to 20,480 bytes of Saturn RGB1555.
The first frame load is 64x32 and spans two adjacent authored 32x32 images;
its native layout follows the load dimensions. The other eight are 32x32.
The two mouth states and four countdown states remain separate source images.
Binary transparency is retained. No replacement art or synthetic content enters
the original asset pack.

Run locally with `FZX_TEST_ROM` set privately:

```sh
python3 tools/countdown_assets.py
bash tools/check_receipt.sh generated/countdown/countdown.receipt
python3 tests/test_countdown_assets.py
```

Outputs stay in ignored `generated/countdown/` or the configured private evidence
tree. The receipt binds the generator, its dependencies, source metadata and
output bytes. This is a conversion intermediate; no native build consumes it yet.

HOST evidence is in `$FZX_EVIDENCE/host/countdown/source-qualification.json`.
An independent primitive traversal agrees on all 46 triangles, including original
positions, scaled UVs and normals. Direct source RGBA16 bit conversion agrees
on all 10,240 native pixels. A changed source vertex fails the comparator.
ROM-free tests pass on Windows and Linux and reject unsupported calls, early
termination, bad spans, missing vertices and changed quad topology.

Integration must read the existing source `unk_21988[0]` sign matrix and
`gRaceIntroTimer`. `Racer_Draw` selects the original 3/2/1/GO panels at timer
220/160/100/40; values above 220 use a black panel, and zero hides the sign.
The mouth opens over 210..220, 150..160, 90..100 and 1..40. These are simulation
timer values, not presentation frames. Sound triggers and pause behavior remain
source integration work. Preserve the source's existing matrix timing rather
than reconstructing its animation in the native renderer.

Remaining gates: native chart/lighting conversion, guarded residency, default-off
renderer and knob-off identity, source timer/matrix checks, local still/motion
comparison, sound, and combined memory/performance. No EMULATOR, MODELLED or
HARDWARE result is claimed by this asset conversion.
