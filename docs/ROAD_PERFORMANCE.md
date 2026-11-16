# Road CPU measurements

`FZX_ROAD_TILE_PROFILE=1` is an optional diagnostic on the existing original-scene
recipe. It requires `FZX_SOURCE_ROAD_TILES=1`, defaults off and changes no assets.
It records exclusive master-SH2 intervals in nine phases. The normal game flow
and the command hash remain unchanged; no timing overlay is added.

The native replay uses `-SourceRoadTiles -RoadTileProfile` and writes counters
with each held view. `tools/road_profile.py:decode_sample` checks completed nesting,
phase sums and paired outer FRT/video-field clocks. A whole missed 16-bit timer
wrap must fail. The field clock assumes the current 60 Hz test mode. This measures
CPU command construction, not VDP1 completion or combined frame time.

## Qualified diagnostic

The image `43819d1951b571a11ffbe8128b6c2c5adc15e6eeea62410190451febfb1fac4a` uses the admitted
original camera, machine, road, sky, skyline, floor/cloud and HUD. Native geometry
reuse flags remain enabled. Thirteen held views span inputs 1-685; this short probe
is not a substitute for the earlier complete 9,907-input correctness replay.

All scene packets and command hashes match the uninstrumented image. Low-RAM/CD
guards pass; stack retains 17,548 bytes, high-RAM end 0x060fa0b4.
All 9,907 source HOST views preserve commands under GCC/Clang UBSan. Nested timing,
rollover, invalid phases, unbalanced scopes and native lost-wrap controls pass.
Profile-off reproduces the entire admitted machine binary. Invalid flag/dependency
settings are rejected before building.

| Thirteen held views | Mean CPU time, MODELLED |
| --- | ---: |
| Original baseline A | 125.92 ms |
| Instrumented B | 197.67 ms |
| Repeated baseline A2 | 125.92 ms |

Maximum A/A2 per-view difference: 0.018 ms. Instrumentation adds
71.75 ms on average, so the following numbers include
measurement work. They identify candidate areas; they are not baseline phase
estimates, measured savings or additive whole-frame costs.

| Exclusive phase | Instrumented mean, MODELLED |
| --- | ---: |
| base | 67.70 ms |
| transform | 13.09 ms |
| project | 27.23 ms |
| cull | 36.06 ms |
| clip | 2.47 ms |
| split | 14.95 ms |
| material | 29.15 ms |
| emit | 4.67 ms |
| hash | 2.36 ms |

`base` includes traversal, record unpacking, setup, edge ordering, subdivision
control and unassigned bookkeeping. `split` times interpolation calls only.
`material` includes fog selection and Gouraud values; `emit` is native command and
shade writes. Instrumentation overhead is distributed across phases and base.

Hashing is a small observed component. The next bounded experiment caches source
vertex transforms, projection and outcodes per frame in low work RAM, then measures
that arm without profiling. It must preserve commands and pass A/B/A timing and
memory checks; this document does not claim a saving in advance. Larger road and
background changes remain necessary to approach the unchanged 16.7 ms scene target.

Private evidence: `emu/road-profile-paired`, `host/road-profile`. Earlier
`emu/road-profile` preserves the diagnostic with mismatched timing scopes; it is
superseded. Images, snapshots and source assets are not published. HARDWARE UNRUN.
