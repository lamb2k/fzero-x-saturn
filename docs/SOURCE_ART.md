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
| Blue Falcon, five authored LODs | Verified retail export, eight texture slots and bounded race-setup decoder gates pass | Open: native reflection/lighting, source model pose and texture submission |
| Basic speed/time/lap/energy HUD sheets | Verified retail RGBA16 export; 14,080-byte native atlas | Timer/speed/lap/energy and lap flash draw on VDP1; countdown/results and source-reference comparison open |
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
The verified retail export passes all 47 Python tests without skips. The source
HUD oracle extracts the pinned timer/speed/lap/energy functions and tables;
10,286 cases match native sprites and filled pixels; wrong-glyph and wrong-color
controls fail. This is
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

The separate `saturn/build-orig-hud/` image includes source timer/speed/lap/energy artwork
and the source timer's +5 ms rounding. Its debug text is moved away from these
components. It retains the temporary scene and does not implement the full HUD
state machine: countdown, intervals/results and other modes
remain open. This option defaults off and cannot use the synthetic machine build.

EMULATOR: eight static retail display cases were captured locally, including the
maximum timer/speed and finished-state lap suppression. The integrated image
replayed 9,907 recorded inputs, completed all three laps and matched the host's
121,814 ms finish time. The live timing-dependent autopilot retired in a separate
run; that is not used as a fidelity result. `tools/emu/input_replay.ps1` uploads
and reads back every word, restarts, then holds source state at the exact end
while presentation runs. All input/state/image evidence remains outside git.

The first integrated HUD snapshot was `98c7263`: 100 archived tracked files matched their
Git blob identities before building. It excludes the local hitch/clip experiments.
The isolated original-race HUD image and the default synthetic image both build.
The archived Python run passes 45 tests and skips only the hygiene test requiring
a `.git` directory; the actual checkout passes all 46 plus its hygiene scan.
The isolated image repeats the 9,907-input, 121,814 ms finish and has SHA256
`640d63140206f7aeee0f8aca57b0669f5ecb57ea2e4fa8488f4f5939a0daa436`.
Its unchanged reference closure was reused, SHA256
`a3994f200d0fa9c291c47ba27b7d131bce6684dff83d353e1225e00972cf3149`.

Current local memory footprint (SH-2 link map, not hardware high-water evidence):

| Region | Footprint |
| --- | --- |
| HUD texture atlas, resident VDP1 | 14,080 bytes of the default 442,336-byte texture partition |
| Two CPU command buffers | 579 commands each, 37,056 bytes total; includes 64 reserved HUD draws |
| HUD draw count | At most 59 of 64 reserved commands |
| Isolated source 86ea5d3 ELF | text 223,124; data 11,372; BSS 731,968 bytes |
| High-work-RAM end / stack top | 0x060eff54 / 0x06100000 (65,708-byte gap, not measured stack use) |

The energy addition in `3c77a61` retains the source red/green fill words, including
the alternating red pixels in row four, zero-width handling and finish suppression.
The initial source energy equals maxEnergy for this single-human Time Attack
closure; the native snapshot uses its original rounded ratio. The HOST oracle
checks all 69 widths in both color states, including finish suppression. GCC and
Clang compile the native HUD with strict warnings. All 46 checkout Python tests
pass with the local ROM; the isolated archive skips only the git-hygiene test.

EMULATOR: eight isolated probe states include full-width red energy with four speed
digits (59 commands). 1,355 rendered energy pixels match the source 5-bit colors.
The HUD-off program remains byte-identical to `98c7263`, SHA256
`a3a8469475b0d483b07ab3c8db7fbbeb7dc8bfb1718ebd2b6489157b8a07da87`.
These are native fill/layout checks, not a source-game screenshot comparison.
The isolated `3c77a61` image also completes the same 9,907-input replay in 121,814 ms,
matching the host, with unchanged simulation closure. Its ISO SHA256 is
`442a1fcaee2efebc238ad1747deb40102e7bc6f685c3d7baefbe48df6c0c7c14`.

## Lap-flash state

`86ea5d3` adds a read-only `ref_hud` accessor for the source's lap times. They retain
the original fractional crossing calculation; the native layer does not estimate
them from video fields. The Time Attack presentation adapter advances once after
each original logic tick and resets on initialization/retry. A repeated draw or
held diagnostic replay does not advance it. Source raw lap zero and backward or
negative laps do not reset the monotonic displayed lap.

HOST: `test_hud_clock.py` executes the original `Hud_UpdatePlayerHudInfo` against
the adapter through three lap transitions, every flash phase, pause/resume, retry,
intro lap zero and reverse crossings. Changed blink threshold, wrong lap slot and
double advance controls each fail. All 47 checkout tests pass with the ROM; the
isolated archive skips only its unavailable git-hygiene check. GCC/Clang at O0/O2
retain all ten reference trace hashes, including restart/byte-order checks and the
machine-constant mutation control. Runtime gameplay pause and lap audio events
remain unimplemented; the tested pause semantics are a HOST API result.

EMULATOR: the isolated image holds at exact replay ticks around the first lap:
flash counters 179/165/164/159/5/0/0 select shown/shown/blank/shown/shown/blank/shown.
The completed lap is 42,511 ms; visible digits use 42,516 ms (+5 source rounding).
The final zero-counter blank frame is retained before the running timer returns.
Captures were inspected, and clock bytes remain unchanged across repeated draws.
The original-race HUD-off program is byte-identical before/after, SHA256
`4f7ee58e9d6f04afe429e664b6ae8728827ad97c69d3b45eb35505c11dddd9f9`.
After the held flash checks, the same isolated image finishes all three laps at
9,907 inputs in 121,814 ms, matching the host. Image SHA256:
`07adaf7b80b8270bae12ea1b56b3475a28c76970eda5c52178de87e48cd3382c`.
Its newly built source closure SHA256 is
`19c9f86d86052cec29e3bb0a6abbe5b59cfcee530ff6608ef43af5c2e01cebd0`.

Source-reference still/motion comparisons, full scene budgets and physical Saturn
acceptance are unrun. The next art work is the remaining HUD state plus native
Blue Falcon materials, source road geometry/camera, scenery/effects and audio.

## Inherited machine setup

The offline importer now decodes the bounded stock race setup `D_303A6A8` and
resolves its tile-zero image to `D_3004618`, an I4 16x16 cockpit texture. All eight
Blue Falcon texture slots are accounted for: the local export has 22 textures.
The private manifest retains geometry/othermode bits, light count, texture load,
tile addressing and setup hash. Unsupported command sequences, nested lists,
bad tile roles, transfer lengths and metadata mismatches fail explicitly. This
decoder never executes lists and is not a runtime N64 interpreter.

HOST: all 49 Python tests pass with the verified ROM, including synthetic setup
fields and malformed-command controls. No runtime code changes in this slice.
The existing source HUD atlas remains 14,080 bytes; the added I4 image does not
use the binary-alpha RGB1555 conversion intended for HUD/road textures.

The pinned G-Diffuser renderer's FLX path preserves authored UVs and uses the
race LookAtY vector plus `D_800CE3D0` lookup table for vertex reflection alpha.
This is an HLE implementation reference, not instruction-level RSP validation:
[pinned implementation](https://github.com/Zorkats/libultraship/blob/7c2354576a4a0ded4e83a31b558c7165cddc1ecb/src/fast/interpreter.cpp#L2954).
Native lighting/reflection and source-reference motion comparison remain open.
