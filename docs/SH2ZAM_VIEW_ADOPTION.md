# SH2ZAM view contract adoption

At the C2/C3 boundary, the submodule moves from `3374d77` to the owner's
annotated `view-contract-1` tag, peeled commit
`01b4c95a8e3e1f32753a98da666150778754ebb3`. No library source was edited.
The update also includes earlier exact division and workload changes, so the
comparison covers the whole dependency change. C3's renderer is not implemented
by this update.

## HOST checks

Both revisions pass GCC and Clang library suites: 10 tests before, 14 after.
Clang uses ASan/UBSan. Both pass the port's 246,388 core checks, including
173 driven frames and 128 external poses. The new library suite includes the
view contract's independent oracle and negative controls.

## Native comparison

Clean isolated builds use port source `f1ab4af`, identical assets and build
settings, and the canonical library source path. The old-library program is
byte-identical to the admitted C0 frame-profile program. A different library
source path changed LTO layout, so that initial build was excluded and the
canonical path restored before collecting evidence.

Both arms replay the same 685 inputs, holding at inputs 1, 300, 301, 500, 501,
542, 583, 624, 664, 665, 674, 684 and 685. The Yaba Sanshiro dynamic recompiler
remained unchanged throughout. Both retain the same final source state.
All 13 camera, sky, skyline, cloud, floor, road and machine packets and command
hashes match. Texture loading, road cache, machine allocation and stack guards
pass; floor uploads remain in vertical blank.

| Subsystem CPU mean, ms MODELLED | Before | After |
| --- | ---: | ---: |
| Road | 112.026 | 112.045 |
| Vehicle | 52.153 | 52.099 |
| Clouds | 42.448 | 42.441 |
| Floor preparation | 14.098 | 14.096 |
| Sky | 1.860 | 1.860 |
| Skyline | 0.952 | 0.954 |

These are scoped per-view timer measurements, not additive whole-frame timing.
The differences do not establish a useful performance gain. Intentional holds
make presentation FPS from this run invalid. C0 remains the full-route baseline.
Untouched stack guard is 6,292 bytes before and 6,132 after, still below C7's
16 KB target. HARDWARE remains UNRUN.

| Artifact | SHA-256 |
| --- | --- |
| Before program | `38169f2ac44509937e46a1d65bf5c58f80ba4bdca2ae7d7c3010d06626c3431b` |
| After program | `5ac56feaa3d16f7d545a026fa969ea265b661deaa0ddea7215d7b108cc89e40d` |
| Before image | `cf1c46139d1115a5bc2ab7c7764729a76452a809dbd23761a1ad84df6018809a` |
| After image | `edb2f736202fb968464e4b48f73afc2432f077f698a2cb8b567f2abbfee5cc54` |

Private evidence is under `host/view-contract-adoption` and
`emu/view-contract-adoption` in the evidence root, including the source manifest,
build/test logs, packets, guards, stills and `comparison.json`.

## C3 contract gap

The planned near-only clipper cannot cover all road pieces. A HOST census of
the tiered reference found 15,032 candidate pieces across 9,907 views with all
vertices in front of the near plane but coordinates outside VDP1's signed
-1024 to 1023 range. A guard-band clipping contract has been requested from
the library owner. The port must not silently drop those pieces, invent a
general kernel, or change the frozen reference renderer to bypass this gap.
Camera conversion and pack integration can proceed independently.
