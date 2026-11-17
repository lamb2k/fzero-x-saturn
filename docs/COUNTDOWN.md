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
