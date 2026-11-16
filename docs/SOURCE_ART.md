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
| Blue Falcon, five authored LODs | Verified retail export, eight texture slots and bounded race-setup decoder gates pass | Original near model, live source pose/body/lighting and resident charts complete the race; performance and final look remain open |
| Basic speed/time/lap/energy HUD sheets | Verified retail RGBA16 export; 14,080-byte native atlas | Timer/speed/lap/energy and lap flash draw on VDP1; countdown/results and source-reference comparison open |
| Mute City road materials | Verified retail MIO0 decode and seven RGBA16 surfaces | Native shared strips and source geometry complete a full replay locally; performance and final source-reference look remain open |
| Mute City floor, skyline, clouds, sky | Eleven source texture/palette objects verified; six skyline instances retain source bindings | Native sky/skyline admitted; original floor uses RBG0 with source-derived fog, clouds remain sprites; full replay passes, scene budget and final look remain open |
| Dash/pit, other effects, countdown and finish | Source coverage incomplete | Open |
| Source camera | Original race closure and native view/scissor integration, default off | Source-reference visuals, broader mode coverage and performance remain open |
| Entry/results UI and audio | Not integrated | Open |
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

Earlier HUD-only memory footprint (SH-2 link map, not hardware high-water evidence):

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


## Native machine material probe

A standalone SH-2 probe now draws all eight fixed views of the original Blue
Falcon: four highest-LOD angles plus source LODs 2-5 (132/96/64/24/11 triangles).
It preserves signed authored UVs, source texture scale/addressing, three-point
filtering, material color equations and reflection-to-fog blending. Projection
and HLE-inspired lighting are diagnostic offline poses, not the source race camera
or accepted dynamic lighting. Double-sided drawing is intentional for this probe.

HOST: 51 Python tests pass with the verified local ROM. GCC/Clang strict builds,
ASan/UBSan raster checks and the pinned shader sampler oracle pass. Three material
mutations fail (UV scale, filter diagonal, reflection blend); removing the numeric
plane guard fails under UBSan. CI initializes the pinned renderer for this gate.
A pre-existing non-UTF-8 comment in the core was made ASCII so Clang can compile it.

EMULATOR: eight views, four measured draws each, match host RGB1555 hashes with
`FZX_MODEL_DIVU=0` and `=1`; the second divider run repeats all hashes. A changed
expected hash is rejected. Captures were inspected locally. These compare the
native host implementation to SH-2, not to a running source-game screenshot.
All source assets, raw pixels, captures and detailed rows remain local.

MODELLED: same emulator binary and 128x128 target, FRT divide-by-32 with overflow
extension. Software division: mean 201,822-579,451 FRT ticks across the eight views;
hardware divider: about 144,745-459,097 ticks. The exact divider saves 20-28%,
but even this arm needs about 10-32 fields per draw. This software raster is a
reference/diagnostic path and is not integrated into the race. It does not meet
the 60 fps budget. No physical Saturn or source-reference visual acceptance.

Probe-only footprint: software ELF text/data/BSS 81,872/148/140,736 bytes; divider
text 81,808 with data/BSS unchanged. CPU image/depth arrays total 98,304 bytes;
VDP1 image is 32,768 bytes. Divider high-RAM end is 0x0603a5f4, stack top 0x06100000.
These are static sizes, not stack high-water or the full race-scene memory budget.
The game ELF and game recipe are untouched by this standalone diagnostic.


## Native VDP1 chart experiment

The separate `tools/vdp1_probe` bakes RGB1555 charts for all source triangles in
all eight diagnostic poses, then submits each as an A/B/C/C distorted sprite.
Each chart records source triangle/material, corners, dimensions and byte hash.
The sampler evaluates the original UVs, three-point filter, combiner and lighting
at the chart samples. Screen-space integer corners, finite chart sampling and
painter sorting are explicit adaptations. No source triangle is removed.
This is a fixed-pose cost/representation experiment, not a dynamic game renderer.

HOST: GCC/Clang outputs match. Synthetic corner and perspective checks pass;
wrong UV and affine-interpolation mutations fail. Bad reciprocal depths, vertex
indices and material bindings are rejected. ASan/UBSan checks pass. Receipts bind
the generator, material implementation, input model header and output charts.

EMULATOR: eight views, four samples each verify VRAM hashes and retain 132/96/64/
24/11 source triangles by LOD. The wrong-hash control is rejected. Still inspection
and pixel-center comparison against the software probe find silhouette IoU
0.960-0.979 and mean union RGB error 5.94-11.29 on 0-255 channels. These are observed
errors, not passing thresholds or comparison against a running source game.
Source-game still/motion matching and visual acceptance remain open.

MODELLED: command build averages 96-1,081 FRT/32 ticks per view, with a one-field
steady-state sync wait. One-time upload plus VRAM checksum costs 14,492-72,128
ticks; each pose uses 16,000-76,096 texture bytes. Lighting and projection are
baked offline, so these numbers cannot establish whole-game 60 fps or moving
material cost. All eight poses together: ELF text/data/BSS 451,644/128/60,704 bytes;
VDP1 reserves 400 KiB texture space and 16 KiB commands. This experiment does not
change the game image. Next: moving source geometry and materials, measured
against the software oracle before any race-renderer promotion.


## Moving native machine experiment

`tools/palette_probe` projects the original positions and normals on SH-2, applies
its diagnostic light/reflection directions, and updates triangle CLUTs/Gouraud
records each draw. It retains all five original LODs, topology, UVs and material
assignments. Index charts use source point samples and exact combined source
colors; they do not retain the source three-point filter or perspective mapping.
The per-triangle CLUT uses average shade, with an additive vertex residual at a
representative material color. Multiplicative lighting is therefore approximated.
This is a moving representation experiment, not accepted source-game rendering.

HOST: the palette helper matches the original material equation for 16,000
constant-shade colors, and known projection poses and numeric bounds pass under
GCC/Clang and ASan/UBSan. Wrong reflection, rotation and chart UV controls fail.
The indexed-chart corner test rejects a texel offset; unsupported shifts fail.
53 Python tests passed together with the local ROM; the additional indexed-chart
suite passes separately (54 current tests). Synthetic and retail SH-2 builds pass.

EMULATOR: 72 poses (eight LOD/view checks plus 64 turn/bank steps), two samples each,
match host projected-vertex and CLUT/Gouraud hashes. Auto motion advances after
releasing the debug hold. The wrong lighting-hash control fails. Observed screenshot
silhouette IoU is 0.948-0.984; union RGB channel error is 10.52-16.44 versus the
matching software reference, scaled RGB5 by eight. This is not a passing visual
tolerance or a running source-game comparison. At most two Gouraud channels clip
across these poses. OpenGL emulator coverage/shading also contributes to the image
comparison; physical VDP1 and source-reference motion remain unrun.

MODELLED: fixed eight-view projection averages 763-6,594 FRT/32 ticks; lighting,
record building and diagnostic hash work average 1,890-24,976. The highest LOD's
roughly 31,500 CPU ticks is too slow for the scene budget. Native textures are
28,016-94,288 bytes per LOD. ELF text/data/BSS: 386,720/124/81,184 bytes. VDP1
reserves 400 KiB charts, 16 KiB commands, 16,288 bytes CLUTs and 4,072 Gouraud bytes.
None of this promotes the experiment into the race. Next exact work: eliminate
repeated transforms (261 records, only 68 positions and 133 normals at highest
LOD), and repeated palette work, then remeasure before scene integration.


## Exact machine reuse (default off)

`FZX_MACHINE_REUSE=1` reuses bit-identical source positions and normals and skips
redundant Gouraud work for constant-shade triangles. Per-vertex UVs and all source
triangles remain unchanged. Load-time validation rejects wrong or out-of-range
reuse maps. The highest LOD transforms 68 positions and 133 normals instead of
repeating both operations for 261 vertex records. Default remains zero.

HOST: all 72 software images and vertex/lighting hashes match the pre-change
reference with the switch off or on. GCC/Clang, ASan/UBSan and bad-map controls pass.
EMULATOR: the reuse path matches all 72 pose/light checks; the final eight-view
pair also matches command, uploaded texture and CLUT/Gouraud readback hashes.
A first capture differed at one or two shared-edge pixels. Byte readbacks ruled
out submitted-data divergence; six sampled draws per held pose resolve the capture
settling issue. All eight final screenshots are pixel-identical. Early captures
are retained, and the harness now saves both early and final sampled captures.

MODELLED: matched final eight-view off/on runs, six sampled draws per pose, reduce
projection plus build/light work by 18.35-27.90%. The highest LOD views remain
about 25-26k FRT/32 ticks before sync and diagnostic readback work. Mean/p95/max and
image identities are in private comparison.json. The preliminary A/B/A agrees on
the direction of the gain. The reuse caches add 14,336 B BSS. This is a bounded
probe-phase improvement, not a full-race or console frame-rate result; palette
appearance, scene cost and source-reference visual acceptance remain open.


## Source race camera closure

`FZX_SOURCE_CAMERA=1` adds the pinned source `camera.c` after `Racer_Update`, with
`Camera_StartInit` at cold boot and `Camera_Init` after racer init/restart. It
exports float view/projection matrices, source viewport/scissor and camera state.
This is the reference/SH-2 closure; the game still draws its temporary chase view.
It does not mark G5.3 or source-scene integration complete. Default is zero.

The thirteen viewport records are decoded from the verified ROM's compressed
course graphics segment into a separate local, receipted pack. Host and SH-2 byte
order are explicit. Only these objects are mapped; there is no segment or graphics
interpreter. Synthetic tests have their own viewport fixtures. Extraction rejects
changed layout, truncated records, invalid scales and edited generated output.

HOST: GCC/Clang at O0/O2 match both cameras across ten existing scenarios plus a
view-switch/look-back sequence. Racer hashes remain unchanged. Three restart
comparisons match; nonfinite, missing-dependency and missing-input controls fail.
Time Attack's secondary records-entry camera has finite matrices but intentionally
no viewport until the source records UI draws it; the race harness leaves it so.
The only called camera stub is the unimplemented audio-pan sink.

The qualified local 9,907-input replay preserves every traced racer field and
finishes in 121,814 ms. Camera modes change at ticks 300 (race), 7,728 (success)
and 8,173 (spectate). Both cameras remain finite. Spectate calls the shared source
random generator; later scenarios must preserve that call, not force indefinite
camera-off identity. A different same-length input log falls in both arms and is
retained separately; the qualified replay was selected by its saved input hash.

EMULATOR: the 714-tick camera-control probe and ten existing synthetic scenarios
match both complete camera streams and racer state. The first sweep exposed a
pending-overflow race in the timer reader; its timing summary is invalid. The
corrected reader masks interrupts, samples the pending flag and counter coherently,
and retains 32-bit per-tick durations. The corrected repeat passes all 11 scenarios,
15,291 ticks with no timer underflow. MODELLED simulation plus both cameras averages
6,486..7,339 FRT/32 ticks per scenario (about 7.2..8.2 ms); worst tick 15,737
(about 17.6 ms), before rendering. This exceeds the proposed 8 ms master budget.
Hashing is excluded. These are not hardware or full-frame results. Retail
finish/spectate is HOST evidence, not SH-2 retail acceptance.

Probe generation rebuilds the host reference, verifies the committed camera hashes,
and receipts its actual source/header dependencies plus all input schedules. This
prevents a stale host binary from minting a receipt for newer source.

Native presentation must consume the exported float matrices. The upstream packed
matrix helper unconditionally swaps column halves for its little-endian port;
those packed matrices are not a valid big-endian Saturn presentation interface.
Next: connect the source projection/viewport to native rendering, then integrate
the source road and moving-machine presentation. Preserve the draw-time single-player
scissor override (source 12,16..308,224) and camera-dependent road visibility; the
stored camera scissor and existing forward-only ring window are insufficient.


Isolated source-only tree `247cd477`: all camera HOST gates and SH-2 build pass;
its camera-control program is byte-identical to the emulator-tested program,
SHA-256 `0425b1c179d0b411c4208226f40ade4051f01935de5658806bfa121dbfef4a39`.
Changing a contributing input schedule fails the receipt check. Default-off host
reference gates pass. No renderer feature or performance switch is promoted.


## Native source camera integration (default off)

`FZX_SOURCE_CAMERA=1` now supplies the original view/projection matrices to the
race renderer. The adapter transposes source row-vector matrices, reverses camera
Z, retains the course's 1/16 world scale, uses both source focal factors and
projection offsets, and crops eight source pixels from the top. It preserves
`Camera_Draw`'s effective single-player Time Attack scissor, including the ordinary
12/16/308/224 override rather than just the camera's stored rectangle. Native
coordinates are 12/8/308/216, half-open. X changes view; Z looks back.

The initial racer publication waits for the first ordinary source tick before
reading camera matrices; no extra simulation tick is inserted. Rotation uses
Q2.30, positions Q16.16. Clipped intersections use Q0.32 fractions and inward
integer rounding so no output vertex remains outside an already processed plane.
The renderer considers every course ring, then clips it, instead of retaining
only the old forward window. Capacity/range errors set a sticky debugger fault.
This still draws the temporary road/craft; it is not the source track renderer.

HOST: 16,000 distant-world point projections stay within 1/16 pixel of the
independent source matrix equation. A 12,000-polygon crossing corpus stays inside
all six planes; the actual render path respects half-open screen bounds and
flags command overflow. Wrong transpose, crop, focal scale, omitted inward
rounding and restored forward-window controls fail. GCC/Clang and ASan/UBSan
pass. Knob-off frame corpus remains `0027e206`. The isolated suite has 59 tests:
58 pass, with only the checkout-metadata hygiene test skipped in the archive;
the real checkout hygiene scan passes separately. All four compiler/optimization
camera streams and restart controls still pass.

EMULATOR: the isolated source-only image replays 9,907 fixed inputs, including
four view switches and a held look-back, and finishes in 121,814 ms, equal to the
host. Nineteen held camera packets match the host bit for bit across intro, race,
view changes, look-back, finish and spectate. All nineteen native adapter packets
also match independent binary32 conversion, and a wrong focal-byte control fails.
No renderer overflow occurs. A prior unmodified-input run has the same finish
and eleven matching held packets. The final program SHA256 is
`3d761af2e3f4c6326692d1a289e54f30ca74d352f71bcb544629934c490302b8`.
The synthetic-content original-race camera image also builds in the CI recipe.

| Source-camera race memory (link map only) | Footprint |
| --- | --- |
| ELF text / data / BSS | 247,268 / 12,096 / 735,648 bytes |
| High-work-RAM end / stack top | 0x060f6ed4 / 0x06100000; 37,164-byte gap, not measured stack use |
| Measured stand-in allocation in low work RAM | 438,464 bytes |
| HUD atlas / CPU command buffers | 14,080 VDP1 bytes / 37,056 bytes, unchanged |

Frame cost is over budget. The game's legacy 16-bit phase timers can wrap on this
slower path, so their maxima are not a valid performance qualification. The
previous corrected camera-closure probe timings remain the available MODELLED
simulation-plus-camera evidence. Source-scene geometry/materials, source-reference
moving visuals, fractional viewport transitions, full mode coverage, stack
high-water measurement and HARDWARE acceptance remain open. No 60 fps claim.

## Source road export (HOST only)

`python3 tools/road_assets.py` rebuilds a separate host exporter, runs the pinned
source course setup and full-detail shape routines, then resolves their fixed
template set offline. No display-list interpreter is linked into the game.
The capture retains chunk ownership, source triangle order, geometry, modified
UV seams, material commands and texture bindings. The race setup replaces the
course-preview setup explicitly; inherited race/background sampling flags are
included. Every contributing source/header and generated output is receipted.

The local Mute City pack has 300 chunks, 2,413 captured vertices, 2,613 resolved
vertices, 4,966 triangles, nine materials and six texture loads. The loads cover
seven authored images: the tunnel's adjacent 64x16 halves form one 64x32 load.
The source interior pass has 4,806 triangles. Removing the classified 160
conditional outer faces from the complete pack matches that independently
captured pass in geometry, UVs, material and triangle order.

HOST: three synthetic road/tunnel/air/pipe/bank fixtures match across GCC/Clang
at -O0/-O2 and repeated runs. Six decoder tests reject bad references, missing
texture halves, changed UV/material data, unowned triangles and changed interior
geometry/order. This establishes the conversion intermediate only. Native chunk
traversal, racer-dependent interior selection, fog, animated rail color, texture
submission and source-reference moving visuals remain unimplemented. Course
gadgets (pits, dash pads, jumps, landmines and decorations) are separate source
draws and are not in this road pack. No runtime timing or HARDWARE claim.

Isolated source-only tree `69f027a6`: the same gates, all ten default-off race
traces and Python suite pass. Both retail capture passes are byte-identical
across four compiler arms; the complete capture SHA256 is
`ff448f0349456557317a839f1b12055a50b144dc7d521be6d28c535cfff759e1`.
An edited pack fails its receipt check. No Saturn runtime files changed.

## Source road cartridge storage (default off; drawing still pending)

`tools/saturn_road.sh` builds the original race, source camera/HUD and the local
road storage path with `FZX_SOURCE_ROAD=1`. `ROAD.PAK` loads before course setup;
the race loop performs no disc I/O. The temporary road/craft still draw. This is
the storage prerequisite for native source-road rendering, not its acceptance.

The big-endian pack retains signed source positions/UVs, triangle order and
ownership, conditional-face flags, material state and original RGBA16 bytes.
Its 146,120 bytes include 24,576 texture bytes. The image binds to the receipted
pack hash and its course record. The loader rejects wrong identities, lengths,
indices, spans, texture checksums and fixture content in the retail path. It
requires a detected 4 MiB cartridge and checks for bank aliasing before writing.
Disc sectors first enter work RAM; aligned CPU stores copy them to the cartridge.

The installed Yaul 0.3.1 CPU sector transfer reads words at 0x25890000, an alias
the selected Yaba build does not implement. It returned success with zero data,
then CDFS asserted on its volume header. The native loader uses longword PIO at
the sector-data port 0x25818000, bounded sector/ready waits and a volume-header
preflight. It leaves the SDK and emulator unchanged. HARDWARE remains UNRUN.

HOST: GCC/Clang and ASan/UBSan accept the synthetic pack and reject independently
rehashed malformed records, wrong course/pack identities and fixture/retail
mixups. Source texture bytes round-trip against their original SHA256. The
isolated suite has 66 passes and one archive-metadata skip. Road-off program
SHA256 remains `3d761af2e3f4c6326692d1a289e54f30ca74d352f71bcb544629934c490302b8`.

EMULATOR: all 72 disc sectors load; all 146,120 cartridge bytes match the local
pack SHA256 `00c0133202a2d8762af91b9429fae10062a90fed0d7468178c0b743a683f0bf1`.
Changing one pack byte fails before race entry. No cartridge also fails before
race entry and displays the required message. Program SHA256 is
`68d034bcd177c0b155232447327efabbcf8d8db052fd43c6c4f3226b12b4bd6d`.
The 9,907-input whole-race replay finishes in 121,814 ms, matching the host;
all nineteen held source-camera and native-view packets match, with no overflow.

| Storage-enabled race memory | Footprint |
| --- | --- |
| ELF text / data / BSS | 253,520 / 12,096 / 740,352 bytes |
| High-work-RAM end / stack top | 0x060f9994 / 0x06100000; 26,220-byte static gap |
| Cartridge road pack | 146,120 bytes of 4 MiB |

The static gap also hosts SDK user-heap metadata; it is not measured stack
headroom or space for more assets. Stack high-water, native road materials and
traversal, moving source-reference visuals, final scene cost and hardware remain open.

Final source audit `b5403b39` matches all 170 staged source files to the reviewed
tree and the tested program. Editing `ROAD.PAK` also stops the Saturn build at
its receipt check; restoring the original bytes makes that check pass.

## Source road draw planner (HOST; native drawing pending)

`tools/road_plan.py` derives the planner from the pinned stock `Course_Draw`.
It retains camera culling, group construction, far-to-near selection, the
racer's interior/join predicate and fog interpolation. Presentation commands
become a bounded 796-byte packet of group endpoints/directions/depths and
material state. No display-list interpretation or runtime vertex baking occurs.
Source edits require a new review; the generator refuses a changed source hash.

The race-only closure omits `Background_Init` and the graphics main loop, so its
fog and graphics-frame stand-ins are zero. The planner explicitly requires a
course fog color and graphics-frame phase. Binding them to the native game is
still pending; the HOST oracle uses named synthetic color inputs. It temporarily
binds those same inputs to the original draw routine and compares its actual
commands, then restores the source globals.

`python3 ref/road_plan_check.py` passes three synthetic courses, 2,000 ticks each,
GCC/Clang at O0/O2, exact repeats and 24 injected interior/join states. Four
mutations fail: reversed group direction, wrong rail clock, wrong interior fog
scale and inverted outer-face selection. The source racer/camera remain unchanged.
The local 9,907-input route also matches the source planner on every tick, with
up to 19 groups and 3..62 visible chunks; both interior states, 59 synthetic fog
colors and all 32 rail-color phases occur. Its race trace remains byte-identical.
This is planner equivalence, not source-scene visual or performance acceptance.

## Native road planner and environment binding

`FZX_SOURCE_ROAD=1` now publishes that planner packet on SH-2. The receipted
metadata binds the course record's skybox to the original `Background_Init`
course-fog table. A separate presentation phase starts at zero on the first
completed race tick and survives player retries. Debugger replays explicitly
reset that phase; a held simulation does not advance it or rebuild the packet.
The race-only closure's graphics-frame global remains untouched. Menus and the
complete graphics main loop are still outside this partial race integration.

EMULATOR: a 9,907-input race again finishes in 121,814 ms. All nineteen held
796-byte packets match the HOST oracle bit for bit, including source group
depths/order, interior-face selection, selected source fog and animated rail
color. Camera packets also match with zero overflow. Mutating outer selection,
fog, rail color or group direction makes the readback checker fail. The
source-only environment tests reject changed table/layout/initialization wiring.

Program SHA256: `38ad491f0342ba518784a3a7dde30ee94958d5e6bfa879715c4525528259b39f`.
Road-off program identity remains `3d761af2e3f4c6326692d1a289e54f30ca74d352f71bcb544629934c490302b8`.
The isolated suite has 69 passes and one archive-metadata skip; the subsequent
readback parser tick-numbering regression test also passes. The parser fix did
not change the tested image or require a new emulator capture.

| Planner-enabled memory | Footprint |
| --- | --- |
| ELF text / data / BSS | 256,104 / 12,112 / 742,208 bytes |
| High-work-RAM end / stack top | 0x060fab14 / 0x06100000; 21,740-byte static gap |
| Cartridge road pack | 146,120 bytes, unchanged |

The gap includes SDK user-heap metadata and is not a stack high-water result.
Native road geometry/textures are not drawn yet; source-scene look, cost, full
memory use and HARDWARE remain open.

Final source audit `8eaa8445` binds all 179 staged files to the reviewed tree and
tested program. Changing one generated fog-channel value stops the Saturn
build at its receipt gate; restoring the bytes makes the gate pass.

## Native road material admission (HOST)

`fzx_road_material` maps each stored material to its source RGBA16 tile view,
including the tunnel's mirrored first half and separate second half. It rejects
unsupported geometry, sampler, combiner, tile layout and load stride. Resolved
road UVs are already scaled; the native API consumes them without a second scale.
The two course combiners preserve texture/primitive blending against shade,
followed by fog. Coverage stays opaque, including zero-alpha source texels.

GCC and Clang pass 4,500 synthetic samples each, plus undefined-behavior checks;
wrong texture-half, mirror, combiner and opacity controls fail. All nine local
material views match the original texture bytes. This code is not yet linked
into the race; the previously tested image and memory layout are unchanged.

Allocation exploration at nineteen held cameras is HOST modelling only.
Whole-course source-resolution triangle charts need about 60.8 MB; preserving
source diagonals while splitting into repeating UV tiles still needs about
22.4 MB. Visible tile charts reach 2.16 MB. Screen-sized collapsed-triangle
charts reach 449,072 bytes after required size splits, before HUD/commands.
Screen-aligned masked charts of each original triangle's clipped polygon are
estimated at 82,272Ã¢â‚¬â€œ360,176 bytes, plus 14,080 HUD bytes. They are the next
bounded native experiment; sampling/upload cost and raster fidelity are unrun.
No texture downsampling, topology change or performance acceptance follows
from these allocation estimates.

## Masked road charts (HOST admission)

`fzx_road_draw` preserves the stored triangle/material bindings and source group
painter order, clips with UV/shade/fog attributes, and emits opaque RGB1555 pixels
inside each polygon with transparent padding. It does not implement the source's
continuous chunk endpoint LOD or replace the authored geometry with a ribbon.
The caller owns atlas/command limits; failure rejects the partial frame.

GCC/Clang synthetic gates cover analytic coverage and perspective shade,
clip-plane duplicates, extreme UV/skinny-triangle overflow rejection, full int32
interpolation, painter order, outer walls, material changes and capacity failure.
UBSan and six deliberate coverage/perspective/overflow/order/wall/material faults
exercise the relevant controls. At 19 saved race camera/plan packets, HOST draws
at most 419 charts; atlas use is
69,344â€“308,800 bytes and the largest tested rectangle workload is 154,400 pixels.
These local views prove bounded generation, not source-reference visual fidelity.
The new module is not yet linked into the committed race recipe. Native frame
cost, full-route draw/stack limits, source look comparisons and HARDWARE remain open.

### Local native chart cost result: blocked

The uncommitted integration experiment draws the original first held road view
on VDP1. Its complete 69,344-byte texture atlas matches HOST byte for byte, with
13 chunks, 208 source triangles, 120 charts and 12,390 shaded pixels. Source
camera/plan packets match. This is one diagnostic pose, not a complete race gate.
The temporary craft and missing scenery remain visible gaps.

That view takes 165 video fields with software perspective division and 132
with the bounded SH-2 divider (MODELLED, one published interval per arm). Both
atlases have SHA-256 `b5b0fd0fe344342da95ce5a1ce48db6689788c3d23cab6e40d10089f202e9732`.
The divider preserves these bytes but does not close the performance gap. This
per-pixel generation path is retained locally as a fidelity reference, not admitted
to the race recipe. Unblock: native submission must meet the field budget with
source-art checks, then pass full-route capacity/stack and simulation gates.
Next is native shared-texture mapping using the original geometry/materials.

### Native patch allocation probe (HOST, not admitted)

The 4,966 source triangles form 2,483 command pairs with four boundary vertices:
1,148 UV rectangles, 1,324 convex UV trapezoids and 11 primitive-color pairs.
Six original texture sheets occupy 24,576 bytes. Arbitrary repeat phase and
large endpoint skew prevent replacing these with one shared rectangular crop.
The selected material-4 tile is invariant along V; its 80 pairs are a reusable
strip candidate. No source positions, UVs or texture detail were removed.

A second sizing probe samples bilinear four-corner charts with at least one
sample per texel of L1 UV movement along either chart axis. It keeps fractional
phases, uses three-point source filtering, splits at native sprite limits and
deduplicates exact byte-identical charts. This is an allocation experiment,
not preservation of the original triangle interpolation or a runtime pack.
It needs 2,632 unique charts / 32,919,888 RGB1555 bytes; independently zlib-
compressed charts total 6,371,715 bytes. The 19 held views need 366,624 to
2,133,824 uncompressed bytes before HUD/commands. Compression does not reduce
the VDP1 residency requirement, so this whole-patch approach is also blocked.
Report SHA-256: `6efc47ff82baf1512192011e3d0d0ecb49ab2df7ffaf8107d501c7845ae9456e`.

Unblock: shared native tiles/subdivision must retain source topology, UVs and
sampling while fitting the scene's measured memory and field budgets; source
look and full-route gates follow. No downsampling or dropped-content arm is
accepted. Next independent card: source backdrop extraction and bindings.

## Mute City backdrop extraction (HOST)

`python3 tools/backdrop_assets.py` reads the locally configured ROM and writes
only ignored/private outputs. It selects the source venue/skybox and numeric
skyline enum/table bindings, retaining all six instances and four shared images.
The export contains eleven source objects: RGBA16 sky/floor, IA8 clouds and four
CI4 images with their sixteen-entry RGBA5551 palettes. Raw source payloads total
14,592 bytes. Every decoded RGBA byte matches the pinned visual-oracle decoder.
The RGB1555 previews total 37,120 bytes, including palette previews; these are
not a runtime allocation or an admitted rendering format for the whole scene.

All sixteen cloud-alpha levels survive. Cloud conversion to binary-transparent
RGB1555 is deliberately rejected. The manifest distinguishes the zero racer-fog
override from its effective course-fog fallback, and retains source skyline tint.
Thirty-three generated outputs and all converter/source inputs are receipted.

Six synthetic tests pass on Windows and WSL: exhaustive IA8 values, CI4 indices
and row padding, source bindings/layout, deterministic export, bounds and receipts.
Wrong nibble order, palette endianness/pointer, binary alpha, changed bindings and
stale-output controls fail their checks. The existing source-art suite also passes
14 tests, with its optional retail-export test skipped in that ROM-free invocation.
The separate local retail export and all eleven oracle comparisons pass.

This card does not change the Saturn image. Source background geometry/camera,
the two cloud-scroll advances per source update, skyline tint/fade, sky/floor
render state and scene integration remain open. No EMULATOR/HARDWARE promotion.

## Source backdrop state (HOST)

`FZX_BACKDROP_STATE=1` in the reference harness compares a bounded presentation
packet against the original `Background_Update`, using independent vertex pools.
It retains the original 20 sky/floor/cloud vertices, signed UVs, colors and 96-byte
background state. It does not execute the asset loader, consume simulation RNG,
or introduce a Saturn rendering path. The selected venue is Mute City, one player;
cloud velocities, pool choice and pause are explicit inputs.

GCC/Clang at O2 match across T01–T10 with unchanged camera-only simulation hashes.
The local 9,907-input Mute City replay matches every source packet across both
compilers and a repeat; simulation trace stays identical and finishes in 121,814 ms.
Packet SHA-256: `51bf65f08ed7de2d795222ba0a1798c5a24820ddefc85578e37df939c2231898`.
The branch gate covers all 44 source speed/sign combinations, repeated pool choices,
pause, both scroll wrap signs, and the camera above/below/at cloud height. The speed
bound preserves the source `10 * 0.0005f`, which differs from literal `0.005f`.
Four compiled native mutations (pause, half-speed scroll, cloud-height branch and
sky UVs) are rejected. Three extraction/receipt tests reject changed source and
stale output. No runtime, visual, timing or HARDWARE acceptance follows.

Next: original skyline placement/tint/fade and initialization order, then native
scene submission and held-camera/source look checks within the memory/field budget.

## Source skyline state (HOST)

The backdrop probe also extracts the six original Mute City billboard instances.
Its 580-byte skyline packet preserves source IDs, positions, rounded 16.16 matrices,
primitive tint and the original quad vertices/UVs/vertical alpha fade. It follows
the source's camera-relative placement and inclusive +/-23,000 visibility bounds.
Original initialization/update run in separate pools for comparison; null texture
loaders in that HOST oracle do not qualify retail asset loading or native blending.

All ten synthetic scripts match under GCC/Clang, with simulation hashes unchanged.
All 9,907 packets of the local view-control race match across both compilers and a
repeat; the source race remains identical and finishes in 121,814 ms. Skyline trace
SHA-256: `309ed8513c6241b0910e04a02d1d2c146be0637af986d3bdbbbee2f70afb1743`.
Tests cover each visibility edge, repeated pools and a vertical-facing fallback.
Four new compiled faults (culling edge, facing sign, tint and missing alpha fade)
fail; the four prior backdrop faults still fail. Changed source bindings are rejected.
Native submission, blending, source-reference visuals and HARDWARE remain unrun.
Next: SH-2 state admission and native scene submission, retaining initialization/RNG
ordering as an explicit open gate before integration into the ordinary race lifecycle.

## SH-2 scene-state probe (EMULATOR)

`tools/scene_probe` checks 256 authored synthetic camera states against the HOST
source oracle. Both backdrop and skyline packets match under the existing exact
soft-float implementation. The control flips one backdrop hash bit and produces
256/256 differences; rebuilding the normal arm restores 256/256 matches. The
wrapper validates completion, case count, first difference and final packet hashes.
The stale-header control is rejected before native compilation. HOST preparation
also rejects changed build inputs instead of blessing an older oracle executable.

Host corpus SHA-256: `f3ea9a8386f44038d7c7299e12f4350aaa5a09d6b49d429772d2e767b5a57b0d`.
Restored probe ISO: `5ca35d82ee532d9fc621246d56d34ce5fbda178e550c2fe6dc6c87ea93b2a86b`.
Control ISO: `6f445a9cc87e1d6745a70b457d0dc4bdd9cb2bc94383393287179625943046f1`.
Emulator: Yaba 1.20.37 MCP, executable SHA prefix `da2ffb341532f108`.

| Standalone probe memory | Link-map allocation |
| --- | --- |
| Text / read-only data | 43,432 / 13,932 bytes |
| Data / BSS | 184 / 62,736 bytes |
| High work RAM end / stack top | 0x060216c4 / 0x06100000 |
| Source scene VRAM allocation | None; only the diagnostic text screen is drawn |

These are numeric probes, not a scene renderer or a game performance result.
No current game memory layout or recipe changed. Runtime submission, fractional
alpha, source-reference visuals, full-scene cost and HARDWARE remain open.

## Native source sky (EMULATOR; drawing cost MODELLED)

`FZX_SOURCE_SKY=1` draws the original sky triangles through the source camera.
The local converter retains all 64 original RGB1555 samples (128 bytes); it does
not rewrite the source asset. `FZX_SKY_SIMPLIFY=1` optionally selects gradient
boundaries offline with at most one RGB5 unit of texture-space error per channel.
The Mute City gradient fits its original endpoints. Native Gouraud interpolation
and span reduction are explicit Saturn presentation adaptations authorized by the
owner; geometry, camera relationship and original gradient colors remain intact.
Both flags default off. Floor, clouds and skyline are not drawn by this card.

HOST: GCC/Clang/repeat and undefined-behavior checks pass 25,728 synthetic pixel
checks. Exact-path hash `c1737b5b` is unchanged; the adapted path is `0223f577`.
Synthetic worst-case command count drops from 110 to 6. UV, clamp, triangle-order
and traversal-dependent shared-edge mutations fail; stale generated output fails.
The approximation has its own declared error allowance; exact-path tests retain
their original tolerance. Unequal-depth clipping includes shared-edge coverage.

An isolated staged-source build excludes the local hitch/clip/road experiments.
It rebuilt the original closure and native image with source camera/HUD/sky and
sky simplification enabled. All 19 held source-camera packets match, sky state is
stable with zero faults, and the 9,907-input replay finishes three laps in
121,814 ms with the same final pose as HOST. This is debugger replay, not pad-only
or console acceptance. Image SHA-256:
`4915c4f8ad483e9a66d4be98d41ffabefd0dcccb919d5076377d330f3cf5553a`.

| Native sky allocation / observation | Value |
| --- | --- |
| Original gradient samples | 128 bytes |
| Optional offline boundary table | 2 bytes for this source gradient |
| Double-buffered VDP1 shade banks | 3,072 bytes reserved |
| Native commands across held source views | At most 5 |
| Isolated game high work RAM end / stack top | 0x060f9a54 / 0x06100000 |
| Untouched painted stack guard after full replay | 20,856 bytes; allocator prefix preserved |

Sky-only cost across the 19 held views is about 1.34–1.88 ms (MODELLED). The
prior unsimplified sky ranged about 1.34–19.76 ms with identical source camera
and backdrop packets. This comparison isolates the sky timer; debug/HUD recipe
changes prevent treating it as a whole-frame A/B result. Whole-scene 60 fps,
complete source-reference appearance, all other memory pools and HARDWARE remain
open. Private evidence folders are `emu/source-sky-clean`,
`emu/source-sky-simplified` and `emu/source-sky-safe-stack` under FZX_EVIDENCE.

## Native original skyline (EMULATOR; drawing cost MODELLED)

`FZX_SOURCE_SKYLINE=1` adds the four original 64x64 CI4 skyline images at their six
source placements, alongside source sky/camera/HUD. `tools/skyline_assets.py`
prebakes the source tint into the original 16-entry palettes and converts the
vertical vertex-alpha fade to 4x4 ordered coverage. Image dimensions and surviving
palette indices are unchanged; source files remain intact. This is a documented
Saturn transparency adaptation, not replacement artwork. No texture conversion or
palette construction occurs during drawing. Static pixels/palettes upload once.

Source matrix and visibility state is reused, with repeated held-tick publication
cached. Native distorted sprites use source-corner ordering and a source scissor.
The clipping setup follows [Sega technical bulletin 15](https://antime.kapsi.fi/sega/files/Sattechs.pdf):
the enable bit is 10 and the inside/outside selector is 9. Near-plane or command
coordinate cases outside the bounded renderer fail visibly instead of dropping art.

HOST GCC/Clang/repeat and undefined-behavior checks cover projection, four texture
bindings, side/behind-camera rejection, command capacity and callback failure.
Translation/binding mutations fail; alpha/tint/invalid-palette checks pass, and a
stale pixel-header control is rejected. All twelve staged source files match the
isolated tree used for native compilation; older local performance WIP is excluded.

EMULATOR: all 19 held views match the original camera and skyline state (114 source
placements; inactive stale matrices are excluded). Drawing has no reported faults.
The 9,907-input replay finishes three laps in 121,814 ms, with the same final pose as
HOST. Native screenshots show the original skyline; the patterned fade is visible.
Image SHA-256: `80378d814d8f47fad108ec04cb9580a51db7d07922d0f3342e75442c8200fd7f`.
Private evidence: `emu/source-skyline-first` under FZX_EVIDENCE.

| Skyline / combined image allocation | Value |
| --- | --- |
| Four CI4 textures + four RGB1555 palettes | 8,192 + 128 bytes, main RAM and VDP1 |
| Native geometry submission | At most six sprites plus one user-clip command |
| High work RAM end / stack top | 0x060fd0d4 / 0x06100000 |
| Untouched painted stack guard after full replay | 6,908 bytes |
| Original low-work-RAM stand-ins end | 0x0026b0c0 |
| Held-view skyline draw cost | 0.82–0.99 ms; mean 0.94 ms, MODELLED |

This cost excludes state publication and other scene work. Whole-frame 60 fps,
complete source-reference visual acceptance and HARDWARE are still open. High work
RAM is now tight: further content needs explicit residency/loading work instead of
continuing to embed texture arrays there. Floor/clouds, source road/craft and the
entry/results/audio flow remain incomplete.

## Boot-loaded original skyline (EMULATOR)

`FZX_SKYLINE_CD=1` moves the same 8,320 converted texture/palette bytes from an
embedded array to `SKYLINE.PAK`. Offline conversion and native drawing are unchanged.
The pack and identity header have generation receipts. Boot requires its exact
name, size and FNV-1a identity, copies five sectors through bounded low work RAM,
and verifies every 32-bit VDP1 store. Nothing streams or converts during racing.
A load failure stops before race initialization. This flag defaults to zero.

The scratch area begins at the next 32-byte boundary after `sat_stub_data_end`;
it cannot overlap the original variables or exceed low work RAM. It contains the
sector buffer and 16 CDFS entries with guards on both sides. The loader reuses the
existing bounded CD PIO implementation. It never DMA-writes cartridge or VRAM.

HOST: GCC/Clang/repeat and undefined-behavior checks pass for transfer/tail bounds,
13 rejection cases and a failing hash-check mutation. A changed private pack copy
fails its receipt. In EMULATOR, a disc with its last asset byte flipped reaches
integrity failure status 8, while race timing remains uninitialized. Knob-off game
binary is byte-identical to the preceding skyline build (1,020,116 bytes;
SHA-256 `19399cc253fbeb9f86282be3f6bcaf876b9acbfa292aec6387054a3254a39570`).

The clean CD candidate finishes 9,907 inputs in 121,814 ms with identical final
pose and all 19 camera/backdrop/skyline packets matching the embedded baseline.
A second full replay with the corrected runner also passes: both low-RAM guards
remain intact, all 8,320 loaded bytes retain the expected identity, and stack
headroom is unchanged. The runner fix removed a redundant pause; native code and
image remained identical.

| Allocation | CD candidate |
| --- | --- |
| High work RAM end | 0x060fc934; net 1,952 bytes below embedded baseline |
| VDP1 skyline pixels/palettes | 8,320 bytes, unchanged |
| Low work RAM scratch | 0x0026b0c0..0x0026bac0; 2,560 bytes |
| Untouched painted stack guard after full race | 8,860 bytes |

Image SHA-256: `badefee70789530dddbc280743efa24f14276ee5bc4f1f4aac0509b5298f7bbd`.
Private evidence: `emu/texture-cd` under FZX_EVIDENCE. Native source was built from
an isolated staged tree; unrelated local performance WIP remains excluded.
The net high-RAM saving includes the newly linked loader/CD filesystem code.
Additional native packs can reuse that code. Next, move the 14,080-byte original
HUD atlas through this path, then integrate original floor/clouds and road/craft.
This is memory-residency evidence, not a whole-scene performance or HARDWARE result.


## Boot-loaded original HUD (EMULATOR)

`FZX_HUD_CD=1` places the existing seven-sheet native HUD atlas in `HUD.PAK`.
`tools/hud_cd_assets.py` checks the original asset receipt, sheet dimensions and
padding, then proves its generated embedded header equals the existing header.
The pack's 14,080 pixel bytes and sheet offsets are unchanged. A small identity
header retains atlas metadata; no pixel array remains in program RAM for this arm.
Both HUD and skyline use the already admitted boot CD loader and the same bounded
low-RAM scratch. There is no new image conversion or race-time streaming.

HOST GCC/Clang/repeat and undefined-behavior checks exercise both the 8,320-byte
and 14,080-byte transfers, including their different partial final sectors.
Padding/length mutations and a reversed hash-check control fail. A stale HUD pack
copy is rejected. `FZX_HUD_CD=0` gives the byte-identical preceding game binary:
1,018,164 bytes, SHA-256
`c345a2decd54d485581cc1197621446543a99cb5f9d59c25d67f6966107f00e4`.

EMULATOR: the isolated combined image loads seven HUD sectors, verifies all 14,080
bytes and retains both scratch guards through the full 9,907-input race. It finishes
in 121,814 ms with the same pose, all 19 camera/backdrop/skyline packets and drawing
statistics matching the embedded-HUD baseline. Local HUD appearance was reviewed.
Scaled emulator captures have a few skyline edge-pixel differences; this card does
not claim whole-frame pixel identity or final source-reference visual acceptance.
Flipping the last HUD pack byte on a private disc copy yields integrity status 8
and HUD load status 2 while race timing is still uninitialized.

| Combined allocation | HUD + skyline CD candidate |
| --- | --- |
| High work RAM end | 0x060f9274; another 14,016 bytes recovered |
| Native HUD atlas in VDP1 | 14,080 bytes, unchanged |
| Shared boot CD scratch in low work RAM | 0x0026b0c0..0x0026bac0; 2,560 bytes |
| Untouched stack guard after full race | 22,872 bytes |

Image SHA-256: `4f25e4dd3363ca08d2ff1962276ba03ab46bd374be79c7099faf0e03acbf3465`.
Private evidence: `emu/hud-cd` under FZX_EVIDENCE. This is a residency improvement;
whole-frame 60 fps and HARDWARE remain unproven. The recovered RAM makes room for
original floor/cloud rendering; source road/craft, entry/selection and complete
race presentation/audio still remain to be integrated.


## Native original floor/cloud experiment (EMULATOR; cost blocked)

`FZX_SOURCE_PLANES=1` draws the original Mute City floor and cloud textures on
VDP1 from the original backdrop vertices and camera. It requires the source sky,
skyline and both CD asset arms; it defaults off. `tools/plane_assets.py` verifies
the original backdrop receipt and bindings, then prebakes a 262,144-byte
`PLANES.PAK`. Both original 64x32 images are retained as the starting material:
eight fog/fade variants and four subtile levels are laid out offline. No pixels
are converted during racing. The pack loads directly into VDP1 RAM at boot.

The floor source combiner is `(primitive - texel) * shade + texel`; the primitive
is the original course fog color. Native fog uses eight prebaked levels and a
mean-color Gouraud correction. Clouds retain original IA8 intensity/alpha through
eight fade levels and 4x4 ordered coverage. Affine mapping, quantized fog/coverage
and minimum-cell guard clipping are Saturn adaptations. A clipped 8x4-texel cell
with more than four corners is decomposed into a convex fan; its source subtile
is remapped to each piece rather than dropping geometry. Final look acceptance,
particularly the magnified cloud/skyline coverage pattern, remains open.

Cloud wind is fixed at source-valid speeds +0.0015/-0.002 instead of consuming
simulation RNG. Original `Background_Update` advances it on each source input
tick (two cloud-half updates per tick). All 19 held scroll states match this
explicit policy. Original camera, skyline, floor and sky state remain unchanged.

HOST GCC/Clang/repeat and undefined-behavior checks pass. Synthetic controls for
material binding, capacity, callback failure, clipped five-corner cells and wide
cloud UV derivatives fail as intended. All 9,907 recorded source views pass the
sanitized geometry walk: maximum 298/640 commands and five adapted cells in a
view. The previous native failure at input 1,173 now passes. A stale pack copy is
rejected by its receipt. Flipping one byte of PLANES.PAK on a private disc copy
yields plane load status 2 and integrity status 8 before race initialization. The default-off game binary remains byte-identical to
the preceding HUD-CD candidate: 1,004,148 bytes, SHA-256
`d842e075ef8a508a4fbfdfbb93b3a9ddb7a9e23e8b686e7ac1e5da6ba66304f7`.

The isolated native candidate finishes 9,907 inputs and three laps in 121,814 ms
with the same final pose and all 19 camera/skyline packets matching the baseline.
All 19 native geometry hashes match HOST. Floor/sky vertices and unaffected
background state also match. Both low-RAM scratch guards remain intact; the
painted stack retains 12,612 untouched bytes. Local views at start and a banked
lap-two position show the original floor content; temporary road/craft and debug
text still prevent playable-port acceptance.

| Allocation / measurement | Combined floor/cloud candidate |
| --- | --- |
| High work RAM end | 0x060fb2f4; 8,320 bytes above preceding HUD-CD arm |
| VDP1 floor/cloud bank | 262,144 bytes |
| Reserved plane Gouraud tables | 10,240 bytes |
| Shared low-RAM boot scratch | 0x0026b0c0..0x0026bac0; 2,560 bytes |
| Untouched stack guard after full replay | 12,612 bytes |
| Plane drawing, 19 held views | 28.90-103.62 ms; mean 70.61 ms, MODELLED |

This approach fails the 60 fps budget. Restricting the tile walk to the visible
plane and using bounded native projection division reduced earlier costs, but
did not make it affordable. Do not enable this arm as a performance-qualified
recipe. Unblock it with a measured cheaper native plane path on the same content
and views, then verify the combined scene's full-route budget. Reuse projected
vertices or evaluate Saturn rotation backgrounds before further low-level work.
Meanwhile native shared-texture road and machine integration can proceed.

Image SHA-256: `5fc2593ccc5044c6f3df45de3c9e78bc0baeeb9cfada59332c11c2f6bd8971d2`.
Private evidence: `emu/planes-fan` under FZX_EVIDENCE, including source-file hashes,
full replay, geometry comparisons, memory guards and the knob-off identity.
HARDWARE, whole-frame 60 fps and complete scene visual acceptance remain open.


## Native original road tiles (EMULATOR; cost blocked)

`FZX_SOURCE_ROAD_TILES=1` replaces the temporary ribbon with the source road,
using the original camera, chunk traversal, outside-edge policy and materials.
It defaults off and requires the original simulation, source road/camera/skyline
and CD HUD. Both reuse switches also default off.

The converter verifies source receipts and pairs all 4,966 original triangles
into 2,483 quads across 300 chunks. It rejects unmatched winding, material,
chunk, outside-edge or UV topology. The original positions and source triangle
identities remain in the geometry pack. Twenty-eight shared native texture strips
occupy 108,384 bytes; metadata occupies 43,088 bytes. Boot-time CD transfers verify
the packs and upload textures directly to VDP1. There is no race-time pixel baking.

Saturn adaptations are explicit: affine textured quads, horizontal strip
resampling, a longitudinal mean row when repeated detail becomes too small,
three prebaked fog levels with Gouraud correction, and bounded guard-band
clipping. Clipped polygons with more than four corners become convex fans;
each fan piece remaps the source strip, so the boundary texture may distort.
Fog uses the actual clipped depths and the same corner indices as geometry.
Original geometry coverage is preserved; this does not imply pixel equivalence.

HOST GCC/Clang/repeat and all 9,907 sanitized source views pass, with a maximum
599 of 768 road draw commands. Synthetic capacity, malformed-pack, callback,
clipped-fog and reuse controls fail as intended. Two bounded reuse experiments
retain the route command hashes: projection-based conservative side culling and
reuse of adjacent strip endpoints. The culler can retain exceptionally close
outside geometry; universal command identity is not claimed.

The corrected native control and each single arm have 13 matched held views,
unchanged source-camera/background packets and geometry hashes matching HOST.
These road-only costs are MODELLED, measured on the same input/view set:

| Arm | Mean | Maximum |
| --- | --- | --- |
| Corrected control | 153.89 ms | 297.41 ms |
| Projection-based culling | 129.56 ms | 249.79 ms |
| Adjacent endpoint reuse | 150.23 ms | 291.94 ms |
| Both switches | 125.87 ms | 244.16 ms |

The combined image completes all 9,907 inputs and three laps in 121,814 ms,
with the same final pose as the preceding floor/cloud image. All 19 held native
geometry hashes match HOST; camera, road plan and background packets are unchanged.
The low-RAM guards remain intact and the painted stack retains 26,700 untouched
bytes. Across these 19 held views, road cost is 72.99-244.16 ms, mean 123.25 ms,
p95 244.16 ms, MODELLED. This is a held-view sample, not a full-frame cost census.
The corrected-control repeat reproduces the 153.89 ms mean; its largest held-view
difference is 0.031 ms, with identical geometry/scene packets and intact guards.
Flipping the final metadata byte in the combined image yields road load status 3
and integrity status 8 before race timing initializes. A stale pack copy is refused
by its receipt; the original four outputs remain current.
The earlier control timed out at input 9,883; its incomplete evidence is retained.

| Allocation | Combined native road image |
| --- | --- |
| High work RAM end | 0x060f7bd4 |
| Original road geometry in cartridge RAM | 146,120 bytes |
| Low work RAM road metadata | 43,088 bytes |
| Low work RAM input log | 49,152 bytes, with separate guards |
| VDP1 road textures / Gouraud tables | 108,384 / 6,144 bytes |
| Combined texture partition use | 412,384 of 442,336 bytes; 29,952 remain |

Both reuse switches off reproduce the corrected control binary byte for byte;
turning off road tiles reproduces the preceding floor/cloud candidate. Local
start, banked and second-lap views show the source road and markings. The craft
is still temporary, and cloud coverage and final source-reference look remain
open. Full scene performance, complete first-course flow and HARDWARE are unrun.

This path fails the 60 fps budget even after both bounded reuse experiments.
It remains an integration experiment. Unblock performance with a cheaper native
submission/projection path or source-derived geometry/texture adjustments, then
measure the same views and full combined route. Do not infer a combined scene
saving by adding standalone subsystem results. Original machine and entry/race
presentation can proceed without treating this renderer as performance-qualified.

Combined image SHA-256:
`8f53d3a6a58df4628dbffb2783beaf9cf3ecf629d3db28d9eb0b02e2df3401db`.
Private evidence: `emu/road-tiles-reuse11` plus matched `road-tiles-fog`,
`road-tiles-reuse10` and `road-tiles-reuse01` under FZX_EVIDENCE. The final source
manifest distinguishes the compiled C from later build-flag validation, forwarding
and host-test changes, which do not alter the candidate's executable.

## Original Blue Falcon in the race

`FZX_SOURCE_MACHINE=1` is default off and requires the source-camera/native-road
recipe. `tools/machine_pack.py` creates receipt-bound CD packs from the original
model and texture export. The native path replaces the temporary craft, using a
read-only source snapshot rather than calling the source drawing routine.
It retains actual model position/basis/scale, visibility, live body color,
ambient and tunnel lighting, packed light/reflection directions and source fog.
The outside fog record is extracted explicitly because this closure does not run
Background_Init. Snapshot tests establish that source simulation is not mutated.

The near model retains 261 source vertex records and all 132 triangles; 68 unique
positions and 133 normals are computed once per draw. Original LOD 4 retains 24 triangles
and LOD 5 retains 11. Source LODs 2/3 use the original near model to bound resident
chart storage. Source LOD 6 contains 9 vertices/3 triangles; it uses original LOD 5,
not an empty/invisible replacement. This is a documented LOD adaptation.

Original UV/material bindings produce point-sampled CI4 triangle charts capped
at 16 x 16 offline. Raw-texel palettes permit live body/environment changes. Native
Gouraud shading approximates multiplicative lighting; painter order, affine
mapping and fixed-point lighting introduce differences. The source normal basis
mapping is preserved; integer light normalization can increase outside lighting
about 0.42%. A clipped source triangle uses its shaded palette mean as a bounded
fallback. No route snapshot exercised that fallback; synthetic near-plane tests do.
The current path also submits back-facing triangles. Restoring the source back-face
cull setting is an open performance and appearance check, with source winding to verify.

HOST GCC/Clang outputs match; source snapshot nonmutation, bad geometry/metadata,
capacity/callback refusal and body/pose/alias mutation controls pass. All 9,907
actual source camera/model snapshots pass under UBSan with at most 132 draw commands.
The full EMULATOR replay completes three laps at source race time 121,814 ms with the same final
pose, 19 matching machine-state/draw checks and unchanged source-camera, road and
background packets. CD/low-RAM guards remain intact; stack retains 18,224 bytes.
Corrupt machine metadata produces machine status 3/integrity status 8 before race
initialization. Disabling the feature reproduces the prior combined-road binary.
Invalid flag/dependency profiles are rejected. An initial full replay stalled in
a debugger-only hold at input 624 and remains incomplete. A second run continued
after a CPU-core change/window restore; it remains diagnostic. The completed
replacement run audits one CPU core and restores only its own minimized window.

Four same-camera local comparisons against a software renderer of original
geometry/materials preserve recognizable silhouette, cockpit and side markings.
They show texture/filter/lighting differences and the higher-model LOD mapping.
The software reference retains the bottom viewport rows cropped by the native
single-player scissor. This is engineering review, not a running source-game
screenshot comparison or final owner acceptance.

| Allocation | Added machine / combined image |
| --- | --- |
| Low work RAM metadata / working cache | 15,164 / 7,044 bytes, separate boundary guards |
| VDP1 indexed pixels / CLUT+Gouraud | 16,064 / 5,280 bytes |
| Combined texture partition | 433,728 of 442,336 bytes; 8,608 remain |
| High work RAM end | 0x060f9cf4 |

Machine-only cost across 19 held views is 0.67-59.93 ms, mean 45.74 ms,
p95 59.93 ms, MODELLED. This fails the scene budget and does not establish a
combined frame rate. The minimum is an invisible-body view. Road and floor/cloud
remain much too expensive as well. These timings are unwrapped using video fields;
the existing 16-bit whole-frame HUD timer wraps on these long frames and is not
complete-scene timing evidence.
Next measure their phases and remove repeated work or move suitable planes to
Saturn background hardware. Retain source geometry/appearance through explicit
native adaptations. Entry/results/effects/audio, pad-only flow, final look and
HARDWARE remain open.

Image SHA-256: `ce52a6c5a6edff297aaa07139b7389059bd4efcb9602be553063681ce815636c`.
Private evidence: `emu/machine-clean` and `host/machine-state`; earlier partial
capture in `emu/machine-integration`. Only source code and counts are published.


## Exclusive road CPU diagnostic

The default-off road profiler changes no source content or conversion. It has
9,907-view HOST command identity, paired timer-wrap controls, 13-view native scene
identity and memory guards, A/B/A cost comparison and whole-binary OFF identity.
See [road measurements](ROAD_PERFORMANCE.md) for the exact image and cost table.
Profiling overhead is substantial and remains explicit; no phase is presented as
an uninstrumented saving. Source appearance, whole-scene 60 fps and hardware gates
remain unchanged.

## Shared road vertex cache

The default-off `FZX_ROAD_TILE_VERTEX_CACHE` preserves the original road assets
and all canonical commands across 9,907 HOST source views. It caches per-draw
view positions, screen projection and outcodes by source vertex ID. UV, material,
fog and subdivision remain live. Guarded low work RAM grows by 73,180 bytes;
the machine allocation follows the cache at 0x00294780..0x00299e40.
VDP1 textures remain 433,728/442,336 bytes. Native stack retains 17,880 bytes.

Thirteen held native views preserve all source scene packets and draw hashes,
with low-RAM/CD/cache guards intact. Uninstrumented A/B/A confirms an 11.02%
mean road CPU reduction (125.92 to 112.04 ms MODELLED), still far over budget.
Cache-off restores the exact admitted machine binary. No geometry or art was
changed; this short diagnostic is not a new full native race acceptance.
See [road measurements](ROAD_PERFORMANCE.md) for scope and private evidence.

## VDP2 original floor integration

The default-off RBG0 floor retains every original native pixel and source geometry/UVs.
Finite coverage, reciprocal perspective and eight fog bands replace repeated floor
tile submission. Full replay, HOST/native packets, synchronized table readback,
memory guards and corrupt-asset rejection pass. See [floor measurements](FLOOR_PERFORMANCE.md)
for quantified adaptations, A/B/A CPU cost and the new 152,544-byte VDP2 allocation.
The prior VDP1 floor atlas remains allocated; final source-reference appearance,
whole-scene 60 fps and physical hardware acceptance remain open.
