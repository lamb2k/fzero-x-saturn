# Source-art conversion

The required target is the original game on Saturn. This ledger distinguishes
asset extraction from native rendering and from owner visual/audio acceptance.
Nothing below completes the technical or playable slice yet.

## Current bounded converter

`tools/import_source_art.py` reads the locally configured `FZX_TEST_ROM`, verifies
the same US rev0 fingerprint as the course importer and checks both pinned source
commits. Run `python3 tools/import_source_art.py`; its default destination is the
ignored `generated/source_art/`. No ROM path is printed or stored in its manifest.

It preserves all five Blue Falcon draw recipes, their source vertex identities,
positions, signed normals, alpha, UVs, ordered triangles and material changes.
The LOD triangle counts are 132, 96, 64, 24 and 11. It is an offline reader for
these specific C functions, not a runtime display-list interpreter.

The export includes seven machine textures, seven basic HUD sheets and seven
Mute City road textures. All retain RGBA8888 data. Original RGBA16 textures also
get big-endian Saturn RGB1555 words with transparent zero and eight-pixel row
padding. Model I4/IA4 alpha remains available to its material combiner; it is not
silently treated as a binary transparency mask. Generated files and source
dependencies are covered by `source_art.receipt`, checked with
`sh tools/check_receipt.sh generated/source_art/source_art.receipt`.
RGBA channel expansion follows the pinned G-Diffuser visual oracle: 5-bit RGB
uses `floor(v*255/31)` and 3-bit intensity uses `v*36` (maximum 252). Exhaustive
synthetic comparisons check all RGBA16 words and all I4/IA4 nibbles against it.
Output is restricted to ignored `generated/` or the configured `FZX_EVIDENCE`
tree, after resolving symlinks; tracked repository destinations are refused.

| Required content | Extraction | Native rendering / acceptance |
| --- | --- | --- |
| Blue Falcon, five authored LODs | Verified retail export and synthetic decoder gates pass | Open: inherited tile-zero setup/look-at material, lighting, source model pose and native texture submission |
| Basic speed/time/lap/energy HUD sheets | Verified retail RGBA16 export; 14,080-byte native atlas | Timer/speed/lap draw on VDP1 from race state; energy, flash/countdown/results, source-reference comparison open |
| Mute City road materials | Verified retail MIO0 decode and seven RGBA16 surfaces | Open: source cross sections, material selection, UV seams and ordered traversal |
| Mute City floor, skyline, clouds, sky | Source locations identified | Open |
| Dash/pit, other effects, countdown and finish | Source coverage incomplete | Open |
| Source camera, entry/results and audio | Not integrated | Open |
| Remaining courses, machines, modes, rivals and persistence | Gate 6 ledger required | Open |

## Source details that matter

- Blue Falcon LOD1 vertices use segment 3 at ROM base `0x17B1E0`; other LODs
  use segment 7 at `0x1E23F0`. `machine_models.yaml` is not the race craft's
  primary geometry source. Source model scale is 0.1 game units per vertex unit;
  the current renderer's course scale is a separate factor of 1/16.
- The Mute City texture ROM span `0x235130..0x239A80` is compressed. Its YAML
  lacks a compression declaration. Stock `Segment_LoadSegment10` decodes MIO0;
  all texture offsets apply to the decoded segment, not the compressed bytes.
- `course.c` owns road cross sections, per-venue UV scales and traversal. Adding
  textures to the existing two-edge placeholder ribbon would not preserve it.
- `background.c` owns the floor, sky, clouds and six angular skyline placements
  using four unique skyline sprites. `common_assets_compressed.yaml` mixes raw
  and individually compressed entries; its filename is not a decoding rule.
- `hud.c` owns layout and digit selection; basic sheets occur in both `hud_gfx`
  and `machine_custom_gfx`. `font.c` owns general glyph widths and mapping.

## Evidence and next gate

HOST synthetic checks cover MIO0 literals/overlap/bounds, channel order, nibble
order, alpha, native row padding, source vertex loads, all five LODs and receipts.
Malformed streams, unknown graphics commands, unloaded vertex references,
wrong ROMs and hand-edited generated output are rejected by negative controls.
The verified retail export passes all 46 Python tests without skips. The source
HUD oracle extracts the pinned timer/speed/lap functions and position tables;
10,010 cases match the native draw list and a wrong-glyph control fails. This is
layout/command equivalence, not an N64-versus-Saturn raster comparison.

## Native HUD slice

`tools/hud_probe/` builds a standalone display test with `FZX_HUD_ART=fixture` or
`retail`. Both use the game's native submission path in `saturn/hud_native.c`.
Fixture sheets are explicitly synthetic; retail mode requires verified generated
art. Receipts cover the header, atlas generator and source HUD. Compilation checks
provenance, and a deliberately edited retail header stops the build.

```
python3 tools/import_source_art.py
SATURN_DIR=tools/hud_probe bash tools/saturn_build.sh FZX_HUD_ART=retail
```

After the original-race build has produced its local course and closure:

```
bash tools/saturn_build.sh FZX_SIM=orig FZX_IMPORTED=1 FZX_ROM_BOOT=1 \
  FZX_CLOSURE=/work/saturn/build-orig/ref_closure.o FZX_SOURCE_HUD=1
```

The separate `saturn/build-orig-hud/` image includes source timer/speed/lap artwork
and the source timer's +5 ms rounding. Its debug text is moved away from these
components. It retains the temporary scene and does not implement the full HUD
state machine: energy, lap flashes, countdown, intervals/results and other modes
remain open. This option defaults off and cannot use the synthetic machine build.

EMULATOR: eight static retail display cases were captured locally, including the
maximum timer/speed and finished-state lap suppression. The integrated image
replayed 9,907 recorded inputs, completed all three laps and matched the host's
121,814 ms finish time. The live timing-dependent autopilot retired in a separate
run; that is not used as a fidelity result. `tools/emu/input_replay.ps1` uploads
and reads back every word, restarts, then holds source state at the exact end
while presentation runs. All input/state/image evidence remains outside git.

Current local memory footprint (SH-2 link map, not hardware high-water evidence):

| Region | Footprint |
| --- | --- |
| HUD texture atlas, resident VDP1 | 14,080 bytes of the default 442,336-byte texture partition |
| Two CPU command buffers | 539 commands each, 34,496 bytes total; includes 24 reserved HUD draws |
| HUD draw count | At most 19 of 24 reserved commands |
| Whole candidate ELF | text 222,400; data 11,376; BSS 740,032 bytes (final check pending) |
| High-work-RAM end / stack top | 0x060f1bf4 / 0x06100000 (58,380-byte gap, not measured stack use) |

Source-reference still/motion comparisons, full scene budgets and physical Saturn
acceptance are unrun. The next art work is the remaining HUD state plus native
Blue Falcon materials, source road geometry/camera, scenery/effects and audio.
