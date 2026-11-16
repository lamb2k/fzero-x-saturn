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
| Blue Falcon, five authored LODs | Converter and synthetic gates implemented; retail run pending | Open: inherited tile-zero setup/look-at material, lighting, source model pose and native texture submission |
| Basic speed/time/lap/energy HUD sheets | RGBA16 conversion implemented; retail run pending | Open: original layout, glyph subrectangles and state-driven drawing |
| Mute City road materials | MIO0 decode and seven RGBA16 surfaces implemented; retail run pending | Open: source cross sections, material selection, UV seams and ordered traversal |
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
No retail art, image, native visual parity or hardware performance is established
by these tests. The next gate is a verified local ROM export, resolving the
inherited material, followed by native rendering and paired source stills.
