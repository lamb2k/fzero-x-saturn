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
