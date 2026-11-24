# Original race countdown

The original countdown now loads and draws in the native scene behind
`FZX_SOURCE_COUNTDOWN=1`. Its source timer/matrix, resident assets, submitted
commands and guarded allocations pass the native checks below. The full
three-lap replay retains the original final state. Sound, final owner appearance
acceptance and hardware remain open.
This is dependency-safe C8 work while the C3 road image gate is blocked.

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
output bytes. This intermediate feeds the receipt-bound native pack below.

`tools/countdown_pack.py` consumes that receipt and bakes `FZXCDN01` metadata
(2,352 bytes) and 28 RGB1555 quad charts (70,400 bytes). All 92 vertex records
and 46 authored triangles remain represented by the 23 original quad pairs.
The original quads are planar and convex. Each chart point maps back to its
original triangle's UVs; naive bilinear UV interpolation would distort some
of these faces. Mirror-wrap is baked offline, including negative coordinates.
The two mouth states and four digit states remain independently selectable.

The source setup explicitly clears lighting and selects direct texture color
and alpha (`0xfcffffff / 0xfffcf279`). No countdown lighting pass is required;
the earlier lighting requirement was mistaken. The native adaptation uses
nearest source texels instead of the original three-point filter, retaining
RGB5 channels and binary transparency. This still needs native scene review.

```sh
python3 tools/countdown_pack.py
bash tools/check_receipt.sh generated/countdown_pack/countdown_pack.receipt
python3 tests/test_countdown_pack.py
```

An independent 3D triangle-coordinate check matches all 35,200 baked pixels;
Windows/Linux packs are identical. Synthetic controls reject changed UV
orientation, non-planar/non-convex geometry, material changes, missing triangles
and invalid texture spans. The actual receipt checker rejects changed metadata
or texture bytes. Private evidence: `host/countdown/pack-qualification.json`,
`pack-receipt-controls.json` and `native-charts.png`. Native residency is covered by the integration checks below.

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

`ref/countdown_bridge.c` copies the existing packed matrix without advancing or
mutating source state. `src/fzx_countdown.c` selects the mouth/panel from the
source timer and composes the model matrix with the current camera once per
sign. SH2ZAM owns the vertex transform and projection. The native scene publishes
this snapshot after the completed source tick.

The full 9,907-input HOST replay preserves the admitted race and camera traces
byte for byte; repeated snapshot reads leave source pose/state unchanged. The
459 nonzero timer states produce 20,384 visible vertex samples within one pixel
of independently projected source matrices. GCC and Clang agree on the complete
projected records. Synthetic tests cover all 461 timer values, 800 rotated model
samples, invalid states and failing timer/matrix/translation/mutation controls.
The adapter compiles for SH-2 (660 bytes text, no data/BSS); this is compile
evidence only. Run `python3 tests/test_countdown.py` under Linux.

The [adopted guard-band contract](SH2ZAM_GUARD_ADOPTION.md) now bounds the
command builder. Across all 9,907 source states, 459 have a nonzero timer and
227 emit commands: 3,506 total, maximum 16 per sign. GCC/Clang UBSan records
match; no source-facing faces, chart selections or phase changes are missing.
Visible vertices and whole-sign polygon coverage stay within one pixel.
One offscreen vertex differs by two pixels without exceeding that visible
coverage bound. Fifty front-facing guard checks reject wholly offscreen faces.
There are no visible clipped textured faces in the admitted replay.

The builder checks source view-space winding before pixel rounding so thin
front faces remain represented. Visible textured clipping fails explicitly;
only the original black panel can emit a clipped solid fan. Synthetic missing
face, wrong chart, unsafe coordinate and thin-face controls pass. Evidence:
`host/countdown/draw/qualification.json` and `geometry-coverage.json`.

`src/fzx_countdown_pack.c` now admits the receipt-bound native pack at boot,
checks complete-file identities and every vertex/quad/image span, and selects
the original mouth and digit charts without allocating memory. The actual
pack passes GCC/Clang UBSan with 92 positions and all 230 panel/mouth/quad
selections. Synthetic valid-checksum corruption, truncation, wrong identity,
insufficient output capacity and wrong-panel controls fail as intended. The
SH-2 object is 1,477 text bytes with zero static data/BSS; this is compile
evidence only at that foundation step; native integration follows below.
Evidence: `host/countdown/native-pack/qualification.json` and `sh2-compile.txt`.
Run `python3 tests/test_countdown_native_pack.py` under Linux.

`saturn/countdown_draw_native.c` converts prepared commands without loading or
allocating storage. The caller supplies the admitted pack, verified VRAM span,
source snapshot/phase and command buffers. Reserve 30 native command slots,
including user clip. Charts retain RGB1555 transparency and disabled end codes;
the original black panel remains an opaque polygon. A rejected call writes no
native commands and returns count zero.

Synthetic API-recorder checks pass with GCC/Clang UBSan over all 461 timer
values, 10,340 chart commands and 240 black-panel commands. Wrong transparency,
address, solid kind and capacity controls fail. This recorder checks API use,
not SDK binary command layout. Strict SH-2 compilation passes: builder 1,604
text bytes, native adapter 960 text bytes, both with zero static data/BSS.
Evidence: `host/countdown/native-adapter/qualification.json` and
`host/countdown/draw-native-compile/sh2-compile.txt`.

```sh
python3 tests/test_countdown_draw.py
python3 tests/test_countdown_native.py
```

## Native scene integration

The default-off native path requires the original race/camera/machine, compact
cloud pack and `FZX_NATIVE_CORE_EMPTY=1`. The [core bypass](NATIVE_CORE_MEMORY.md)
removes unused placeholder-rendering work and storage; without it, the first
countdown link exceeded high work RAM by 3,328 bytes. The wrapper loads after
the machine/HUD, publishes after camera state, and draws after the machine and
before HUD. Its 30 reserved command slots include the user clip.

The two new disc files match their admitted packs exactly. All 14 existing
non-program payloads remain unchanged. CD directory capacity rises from 16 to
32 entries only with countdown enabled; shared CD scratch is 3,008 bytes and
fits below the input-log guard. The 70,400-byte texture bank follows machine
shades in VDP1 VRAM. Metadata and native work occupy 6,464 bytes of low RAM,
with guards on both sides; profiling rows begin after that allocation.

Ymir passes 24 held views through the same 685-input prefix, covering every
3/2/1/GO and mouth-state boundary, sign entry/exit and maximum command count.
All source countdown/camera packets, metadata and resident texture bytes match
HOST. All 276 staged and submitted native commands match independent conversion
of the qualified HOST command records, including modes, texture addresses,
coordinates and user clip. The maximum is 17 commands including the clip.
Local still review shows the original sign, digits and departure; the adaptation
retains nearest RGB5 texels and binary transparency. This is not an N64 raster
identity claim or owner appearance approval.

The complete 9,907-input Ymir replay reaches all three laps with the original
final pose, scene packets and 121,814 ms source finish time. The countdown is
hidden at the end. Guards pass, including the profiling allocation; minimum
untouched stack is 49,068 bytes. Road and machine command aggregates match HOST
at every recorded input. A one-byte mutation in the countdown texture file is
rejected by the actual native loader before race startup (countdown status 5,
texture checksum status 8). Synthetic wrapper span/publication/guard controls
also fail as intended; all 14 countdown tests pass with GCC/Clang UBSan.

The recorded window contains 1,983 pictures: 3.824 fps MODELLED, 261.520 ms mean
presentation interval, p95 366.667 ms, maximum 416.667 ms and 29,118 missed
fields. This remains far below 60 fps. These are Ymir results; the prior full
baseline used Yaba, so they do not establish a matched speedup. The initial
input-1 hold precedes recording (zero recorded rows at that hold); the final
hold follows the last presentation. The wrapped-clock negative control fails.

Private evidence under `$FZX_EVIDENCE`: `host/countdown/native-on-fit`,
`emu/countdown/native-first/command-comparison.json`,
`emu/countdown/full-race-first/summary.json` and
`emu/countdown/texture-control-first/negative-control.json`. Runtime evidence
is EMULATOR; timings are MODELLED; HARDWARE remains UNRUN. Sound and complete
entry/race/results/retry acceptance remain unfinished.

## Engine line: the sign on the engine's race images (ENG_COUNTDOWN)

The engine keeps the original's 460-tick start delay (`body.intro` counts 460 down to 0 as `gRaceIntroTimer` does) and, with
`ENG_COUNTDOWN=1` (the default on the drive images that carry the 3D player, `FZX_E1_DRIVE=1` and `E1_CLOSEUP=1`), draws the
original's sign over it: `saturn/engine/engine_countdown.c`. Nothing here is the C8 scene: the engine reads its own body and camera,
and it never writes either.

- **Art.** The same 23 planar quads and RGB5 charts as above, baked by `tools/countdown_engine_pack.py` into
  `generated/countdown_engine` (ROM-derived, local, with a receipt). The 20 frame/grate quads' charts are baked at half the source
  step in both directions (the face is small on screen); the two Mr. Zero panels and the digit panel keep the source step. The pack is
  36,864 bytes of VDP1 VRAM (the full-step pack is 70,400). It sits directly below the 3D player's region, so it needs no
  slot-pool room. The worst course (Space Plant) leaves 66,048 bytes there; a course with less than the pack's size loads without
  the sign (status 7) and the HUD's text digits stay.
- **Pose.** The original's: rows 0.2 * (Z, Y, X') with Y the racer's up axis, X the camera's backward axis, Z = Y x X, X' = Z x Y;
  origin = position + (v - height) * up + 50 * forward - v * camera forward, with v = 33 + 1500 s^2 and s = (timer - 220) / 240 above
  220, v = 33 for 31..220, s = (30 - timer) / 30 for 30 down to 1. Integer maths (Q30 axes, Q16.16 distances).
- **Panels.** Black above timer 220, then 3 (220..161), 2 (160..101), 1 (100..41), GO (40..1), hidden at 0. The mouth is open for
  210..220, 150..160, 90..100 and 40 down to 1. The placeholder HUD digits are drawn only when the sign did not load.
- **Where the cost goes.** The per-frame build runs in the master's idle time, counted in `phys_ticks`, for the 460 intro ticks only.
  Work (about 7 KB) and the 2,352-byte metadata are in low work RAM ahead of the frame rows. The slave's `fzx_bal_rows` (12 KB) moved
  to low work RAM to give back the code's high work RAM.
- **Camera.** The engine's chase camera is rigid; the original's swoops in from far behind for the first 280 ticks. The sign is
  placed from the camera exactly as the original places it, so it matches the original once the camera has settled (timer 220
  and below); during the swoop the sign's path is the engine camera's, not the original's. The settled sign is the original's size
  and centred like it; it sits about 10 pixels lower on a 224-line picture because the engine's settled camera pitch differs.
- **Sounds (not done here).** The original's countdown sounds (racer.c) are system sound effects: NA_SE_47 when the timer is 220
  (the 3), NA_SE_9 at 160 (the 2), NA_SE_10 at 100 (the 1) and NA_SE_11 at 40 (GO); the demo fanfare (Audio_StartDemo) at timer 460
  and the course music at timer 1. None are in the engine's 9-event race bank, so this lane draws the sign only; the menu-audio
  lane needs these triggers (all on the player-0 intro timer, not while paused).
- **Cost (EMULATOR, timing MODELLED; Mute City field race, sound off, whole race from tick 1).** Critical-path p99 19.871 to 20.049 ms
  (+0.178), max 21.638 to 21.900 ms, every presentation interval 2 fields in both arms, final picture and all 36 HOST checkpoints
  identical. The cost is in the 230 intro rows only: +0.37 ms while the sign is off screen, about +2.9 ms while it is drawn
  (115 frames; the projection of its 92 vertices and the per-quad work of `fzx_countdown_draw`). Their own p99 is 21.7 ms against the
  24.485 ms limit.
- **Reports.** `fzx_cdn_report` (`engine_countdown.h`): status, frames drawn, most commands, refusals (expected 0), commands dropped
  (expected 0), FRT ticks of a build, of the pose and of the drawing. The gate's `countdown` check reads it; `ctl-countdown` is an
  image built with `ENG_COUNTDOWN=0`.
