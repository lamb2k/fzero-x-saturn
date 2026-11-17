# C3 native stream: measured no-go

The local default-off `FZX_SOURCE_ROAD_STREAM=1` experiment is integrated and runs
the selected Mute City 1 replay, but fails the road CPU stop threshold.
It is not an accepted renderer and does not enable C6. The previous road
renderer remains the default. Hardware is unrun.

## Actual configuration

The experiment uses the pinned SH2ZAM `view-center-1` implementation at
`7de29ac`, the original road metadata and tier textures, one master SH-2,
600 gathered vertices and at most 512 road packets. Its working arrays are
in low work RAM; VDP1 command storage remains in high work RAM. This differs
from the proposal's high-RAM projection scratch. The result describes this
implementation, not a proof that every possible stream architecture fails.

The image links `fzx_road_stream_native` and its command adapter; neither
legacy tile drawing function is linked. The legacy tile cache and profiler
are disabled. Source machine rebake remains off, so the whole-scene result
does not measure a combined C3/C4 candidate. The selected configuration is
character 0, skin 1, engine 0.5078125; it is distinct from the frozen
9,907-input legacy-tuning baseline. Do not calculate a speedup between them.

Native1f binary SHA256:
`761b32109760cd1b78d16cbd3d26d14837e282204a3ca8a38d19f94215a58e1f`.
ISO SHA256:
`6b43d9b5f1f4756021f67bfdc128fd564457388f5d62ab201d4710221bb86c0f`.
Private source bindings retain the exact source files and library snapshot.

## Correctness and memory evidence

GCC/Clang native-wrapper, command-adapter, workspace and profiler tests pass
with corruption controls. All 13 selected native captures match HOST source
state, camera, road plan, C3 packets, VDP1 commands and referenced asset bytes.
Twenty allocation guards pass in each capture. Verifier controls reject
altered metadata, clip coordinates and commands. The capture includes aligned
metadata padding; only the exact file extent is asset content. Source scissor
Y is cropped by eight pixels when converting 240 lines to 224.

The original 576-vertex allocation fails the selected HOST route at input
1876. Its exact maximum is 600; the 600-capacity HOST run admits all 7,059
views with identical GCC/Clang outputs and unchanged held packets. Native
full-route profiling samples presented views, not all 7,059 views.

Native1d failed before usable captures and had only 1,468 bytes between the
linked program end and initial stack. Its failure and original log remain
preserved; the first corrupting instruction is not established. Native1f
omits the unused prototype mesh only when both the empty-core and stream
flags are enabled. Frame headers and steering hints remain identical across
1,024 HOST poses. The linked gap rises to 28,348 bytes, the minimum sampled
unused stack is 21,240 bytes over the full route, and all allocation guards
pass. The live texture partition leaves 1,888 bytes after the finish-menu
allocation. These are emulator/layout checks, not hardware acceptance.

The selected ideal-image gate is still failed: held input 684 has texture
error p99 1.0021976 pixels against the unchanged one-pixel threshold.
Forcing the first tier improves that diagnostic image but fails the command
budget; a tighter Q8 255 experiment worsens its p99 to 1.019438 pixels.
Neither is adopted. Exact native packet encoding does not clear this gate
or constitute final VDP1 appearance acceptance.

## Uninterrupted full-route result

The replay completes all three laps at input 7059 with exact source finish
state. The non-wrapping profile contains 1,411 presented passes, covering
inputs 5 through 7059 after the restart frame, with no intentional holds.

| Measurement | MODELLED result |
| --- | ---: |
| Road CPU mean | 73.736 ms |
| Road CPU p99 | 133.143 ms |
| Road CPU maximum | 137.928 ms |
| Road commands p99, including user clip | 417 |
| Road commands maximum in presented sample | 440 |
| Whole-scene delivery | 3.929 fps |
| Presentation interval mean / p99 | 254.515 / 350.000 ms |
| Missed fields | 20,122 |
| Minimum sampled unused stack | 21,240 B |

Two independent checks reject misleading explanations for the cost. Raw
capture words give 302,258,141 timer ticks over 21,532 presentation fields:
14,037.625 ticks per 60 Hz field. The road p99 is 112,141 ticks, approximately
eight fields, so a timer-unit error cannot explain the gap to 16 ms. The
actual scene-4 boundaries include eye conversion, stream draw, command
conversion, clip setup and guard/report work; they exclude simulation,
publication, other scene groups and VDP waits. Projection coefficient
conversion occurs just before this boundary, so it slightly undercounts
total road preparation. The raw road samples exactly match their aggregate.

Simulation averages about five source ticks per presented pass. The result
does not establish 60 Hz simulation or the 30 Hz drawing fallback. The
presentation callback is not an end-of-draw timing proof. This is an emulator
CPU/presentation measurement, not physical FPS or GPU-only time.

The 133.143 ms road p99 exceeds both the 8 ms target and explicit 16 ms stop
threshold. Stop this implementation's performance route; do not move it to
the slave as though it passed. Disabled-mode identity passes: all 1,023,652 executable bytes equal the
pre-C3 control rebuilt with the same library, finish-menu VRAM fix and two-source
non-LTO rule. A changed-byte control fails. Both disabled images have only
8,540 bytes of linked stack space and were not launched; this establishes build
identity, not runtime memory acceptance.
C4's independent ordering/cost blockers and dependency-safe C5/C8 work remain.

Private evidence: `host/road-stream-native/native-1f`,
`emu/road-stream/native-1f-selected13`, and
`emu/road-stream/native-1f-full`, including source bindings, raw profile rows,
qualification, verifier controls and memory checks. Retail captures and
converted assets remain outside Git.
