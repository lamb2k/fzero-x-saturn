# Whole-scene frame measurements

`FZX_FRAME_PROFILE=1` records the admitted original race, road, machine, camera,
sky, skyline, RBG0 floor, clouds and partial HUD. It requires the native floor
and machine, defaults off, and changes no assets or gameplay. This is a diagnostic
for the still-incomplete first-course port, not a release or hardware result.

The master-SH2 free-running timer is extended at each VBlank-out interrupt.
Main-loop reads retry if the interrupt changes the epoch during a read. Field
gaps outside the expected /32 clock range fail the capture. Presentation-to-
presentation timer totals are also checked against the independent field count.
This avoids the 16-bit wrap in the old debug HUD's whole-loop counter.

Each record separates input handling, simulation, source-state publication,
waiting for the previous draw, scene/command construction, debug HUD, and
submission. The existing floor callback marks completed VDP1 frames immediately
before requesting the framebuffer change. Submission-to-callback latency includes
draw time and scheduling/display wait; it is not an isolated GPU raster timer.
Do not add that latency to the other phases: the work can overlap.

Seven whole-subsystem counter pairs cover sky, skyline, floor preparation,
clouds, road, machine and race HUD. They report total FRT ticks divided by
emitted VDP1 commands, plus commands per completed presentation and command
ranges. Counts include each subsystem's clip/setup commands. Floor counts cover
its VDP1 fog commands; they do not count VDP2 pixels as commands. Floor parameter
upload time remains inside the submission-to-presentation interval. These are
aggregate ratios across the captured frames, not an average of per-frame ratios.
No timer reads are added inside primitive loops. Counter totals and frame counts
must agree with the recorder, or decoding fails.

## Capture protocol

Use the original-scene recipe with `FZX_SOURCE_FLOOR_RBG=1`,
`FZX_ROAD_TILE_VERTEX_CACHE=1` and `FZX_FRAME_PROFILE=1`. Run
`tools/emu/input_replay.ps1 -FrameProfile` with its normal scene checks and
the full local input log and `-TimeoutSeconds 3600` for this slow baseline.
The wall-clock watchdog is separate from frame-time acceptance. Intermediate
`HoldTicks` are rejected because they
would contaminate cadence. The normal final hold occurs after presentation.

The recorder discards startup and the frame containing the source restart.
The restart loop can advance one to five source ticks; recording begins at
that actual next-loop input boundary. It then stores at most 4,096 64-byte rows in guarded low RAM, following the
original machine's allocation. Overflow fails; it never silently truncates.
`tools/frame_profile.py` checks every row, source-tick continuity, presentation
counts, both clocks and guards before reporting mean, nearest-rank p95, maximum,
missed fields and completed-presentation FPS. Raw records remain private.

The native scene is still far over budget. The 60 fps target requires a complete
content-bearing picture every 16.7 ms, as well as the unfinished entry, selection,
results, effects and audio. A replay's source race time is not wall-clock or
presentation time. HARDWARE remains UNRUN.

## Validation

GCC and Clang UBSan tests exercise repeated 16-bit rollover, 32-bit rollover,
an interrupt during a reader snapshot, restart, pending-presentation collision,
capacity overflow, clock-gap rejection and allocation guards. A deliberately
16-bit accumulator fails. Decoder controls reject incomplete, discontinuous,
paused and corrupted-clock records. Profile-off reproduces the admitted binary.

Private evidence: emu/frame-profile-v2/race; the earlier incomplete run remains in emu/frame-profile.

## Full-route baseline

The original-content scene completes all 9,907 fixed inputs and all three laps,
with the admitted final source state and source race time of 121,814 ms. The
final camera, road, machine, sky, skyline, clouds and floor packets match the
admitted image. Road and machine command totals and ranges also match the
independent GCC/Clang HOST records at every actually drawn input. This is a debugger replay; pad-only entry/results and physical
hardware acceptance remain open.

The captured window contains **1,982 completed pictures**, after the restart loop, from source input 1 through 9,907. The cadence is **3.287 fps MODELLED**, with **34,178 missed display fields**. This does not meet 30 or 60 fps.

| Measurement | Mean ms | p95 ms | Maximum ms |
| --- | ---: | ---: | ---: |
| Presentation interval | 304.215 | 433.333 | 466.668 |
| input | 0.038 | 0.038 | 0.076 |
| simulation | 46.134 | 49.506 | 63.339 |
| publication | 32.078 | 38.365 | 43.251 |
| wait previous | 0.007 | 0.013 | 0.019 |
| scene build | 225.494 | 346.369 | 382.078 |
| hud | 0.300 | 1.185 | 1.205 |
| submit return | 0.083 | 0.107 | 0.139 |
| submit to presentation | 11.613 | 19.176 | 20.025 |
| loop to presentation | 315.665 | 438.270 | 483.853 |

Submission-to-presentation latency overlaps CPU work and is not pure VDP1 raster time. Do not add it to the CPU phases. These are instrumented whole-route samples; the earlier 13-view subsystem means cover different views and are not a matched performance comparison.

| Subsystem | Mean CPU ms | Maximum CPU ms | Commands/picture | Maximum commands | FRT ticks/command |
| --- | ---: | ---: | ---: | ---: | ---: |
| sky | 1.821 | 1.914 | 3.85 | 5 | 397.16 |
| skyline | 0.887 | 1.141 | 2.37 | 4 | 314.26 |
| floor | 14.029 | 20.697 | 8.70 | 12 | 1354.01 |
| clouds | 32.863 | 70.552 | 85.01 | 195 | 324.66 |
| road | 121.720 | 270.691 | 277.81 | 595 | 367.95 |
| machine | 52.858 | 60.247 | 117.01 | 133 | 379.38 |
| race hud | 0.480 | 0.748 | 31.62 | 58 | 12.74 |

The worst presentation interval spans inputs 2116 to 2121; the largest scene-construction sample ends at input 4511. The simulation advances at most five source ticks per picture; its mean 4.998 steps/picture is catch-up behavior, not proof of a real-time 60 Hz simulation.

The recorder uses 262,144 bytes of low-RAM rows plus guards/alignment. Its row range is 0x00299e80 to 0x002d9e80. All allocation guards pass; measured untouched stack guard is **5,996 bytes**. This is below C7's 16 KB target. All 14 non-program ISO payloads are byte-identical to the admitted RBG0 disc; VDP1/VDP2 allocations are unchanged. Masking the recorded native clock to 16 bits is rejected by the decoder as required. The 4,096-row capacity is adequate for this slow baseline; a future faster full-route capture must extend or stream recording rather than truncate.

Image SHA-256: 2087e79946836d5093c641a2977302fac129ae91184c1fc7e9bcef2bf5fe712e. Source base: 904cb7841b3ffcb2b32066fd969e918e9de5f093 plus this card's source changes. The private receipt binds the exact indexed source, compiler output, recipe and unchanged SH2ZAM revision. The input log and all ROM-derived records remain private.

The first v1 run exhausted its 1,800-second harness allowance at input 9,302 and was not accepted as a full route. The v2 capture began with a 2,400-second allowance. As host throughput slowed, its controller was handed off at input 8,806 to a 3,600-second continuation allowance. The same emulator PID/image continued without pause, reset or input upload; the extra core audit and handoff receipt are retained. The frame-time acceptance target remains 16.7 ms. No SH2ZAM calibration image was available at this card boundary, so no calibration or hardware speed claim is made.
