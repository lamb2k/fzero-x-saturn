# Original finish and initial results

`FZX_SOURCE_RESULTS=1` adds the original single-player Time Attack finish,
run-out and initial results panel to the selected Mute City 1 race. It requires
the original machine-selection, countdown and HUD paths. The switch defaults
off. Source state, native presentation, clock, diagnostic reset and corrupted-pack
checks pass. This does not certify the first-course milestone or 60 fps.

The state adapter extracts the original finish/results routines and drawing
layout from the pinned source. It observes the racer, camera and original
pressed-button state once per simulation tick. Previous-tick save processing
preserves the source update order. Race time, best lap, maximum speed, ranking,
new-record pulse and name-entry intent follow the source rules. Snapshots are
immutable, and sound/save requests are explicit events.

State code is compiled with the strict original-source closure. Yaul's default
presentation specs enable fast math; an initial native comparison caught its
reciprocal-multiplication change in the record pulse. A 32-phase regression
rejects that fast-math variant instead of allowing a wider state tolerance.

The results scene owns its tick counter. It advances after a successful source
update and guard check, independently of the diagnostic log's 12,288-entry
capacity. A restart returns this counter to zero while retaining session records.

A/Start slides the initial panel away and reaches `RECORDS_REQUEST`. Records
presentation, name entry, ordinary retry, persistent saving and audio playback
remain separate unfinished work. The adapter does not silently skip these
screens. Records are retained in memory across a diagnostic race restart.

## Native presentation

The offline converters preserve all 256 FINISH vertices and 222 triangles in
source order. The dedicated source finish camera and model matrix drive the
animation. Saturn draws the sides as solid polygons and the faces as CLUT4
triangle charts with opaque palette index zero. Geometry is back-face culled;
it is not depth-sorted or relit. The pack contains 9,280 B of metadata and
8,864 B of textures.

The results UI uses the original sheets and complete Font3 mapping: 54 images,
234 glyph slots and 22,016 B of texture data. Full texture rows plus user
clipping implement source crops without copying pixels each frame. Native
coordinates crop eight rows from the source's 320x240 presentation for 320x224.

Saturn adaptations are explicit: alpha is reduced to invisible, half-transparent
or opaque; the red record pulse uses sixteen offline RGB banks, with at most
one RGB5 step of channel error. Those banks occupy 151,552 B in the cartridge;
only the selected 9,472 B bank is copied to video memory when its level changes.

## Residency and limits

Metadata and bounded state/work storage live after the countdown allocation in
low RAM. UI, finish textures and tint banks are loaded once into guarded storage
after the graphics workspace in the required 4 MiB cartridge. Results reuse
46,496 B of the countdown's 70,400 B video-memory reservation only after the
countdown ends and the previous VDP1 draw completes.

Results append to the existing command capacity and reject insufficient space.
No CD reads occur during finish/results animation. A diagnostic restart restores
the original countdown texture before restarting simulation. Loading, reset,
tick and draw failures have distinct status codes; guards cover low RAM and
cartridge storage.

## Evidence

HOST source tests check 1,215 timeline ticks, 4,860 immutable snapshot reads and
12,288 source matrix poses under GCC/Clang with UBSan. Ten state and two matrix
mutations fail. The extended selected diagnostic race plus results produces
12,352 identical input/snapshot/matrix records under both host compilers.

Independent finish projection checks cover 12,288 poses and 1,285,009 emitted
triangles, with projected edges within one pixel. Reversed culling and shifted
Y controls fail. Offline finish charts preserve all 25,088 sampled chart pixels;
Windows and Linux outputs match. UI conversion checks cover every RGB5 color,
alpha, crops, palette fallback and all record-pulse shades.

The synthetic native API test checks clipping, texture offsets, palette slots,
transparency, command capacity and invalid/overlapping video-memory ranges.
Both compilers pass; five altered implementations fail. This is HOST API
recording, not emulated raster evidence.

The scene lifecycle test exercises production tick/reset/guard code against
synthetic state and CD services. GCC/Clang with UBSan check 12,384 ticks, 64 held
reads, log-cap independence, overflow, failure paths, retained records and
countdown restoration. Five altered implementations fail. These checks establish
the lifecycle contract, not source arithmetic or emulated display behavior.

EMULATOR: the selected three-lap diagnostic replay continues through finish,
the initial panel and its A/Start slide-out, for 12,288 inputs. All 19 held views,
the post-log-cap capture and three reset captures match the HOST source
inputs/results snapshots and 39 checked racer fields, globals, pose and camera.
All allocation guards pass; minimum sampled stack margin is 81,476 B.
Three altered racer-field controls fail the comparison.
This begins with ordinary pad-driven selection, then injects diagnostic race
inputs; it does not establish ordinary pad-only completion.

All 17 held views with active results textures match the GCC/Clang command
writers fed by original source snapshots and matrices. Cartridge bytes, active
VRAM textures, tint banks and referenced palettes match. A separate 700-view
HOST sweep requires at most 174 appended results commands; that count is not
a whole-scene timing or 60 fps result.

The input log remains unchanged at 12,288 entries while the source/results
frame reaches 12,322. Capture holds leave the results clock stable. Three
diagnostic restarts retain session records and return to source frame 600 with
matching state. The first restart restores borrowed countdown VRAM; all three
captures match the complete 70,400 B countdown texture. The later restarts occur
before results borrow it again, so this establishes one restoration and three
state/record resets, not three complete finish/restore cycles.

Four local stills show FINISH, initial results and slide-out. Existing diagnostic
text overlaps the upper results rows; presentation cleanup and final owner look
acceptance remain open. Alpha quantization and red-pulse banks remain the
explicit Saturn adaptations above.

The disabled Saturn program is byte-identical to the admitted machine-selection
program. The enabled program SHA256 is
`51337d2e59106a44776386b4b16a8c67d01fc3851cc2fdaad8fc682bbdd65f64`.
Four corrupted native packs (geometry, finish texture, UI and tint banks) are
rejected before the first race tick, with low-memory guards intact.
Audio, ordinary pad retry, soak,
physical hardware and whole-scene frame-rate acceptance remain open.

Private evidence stays in `host/results-native`, `host/finish` and `emu/results`
under the local evidence root. Generated source, converted packs, native images,
retail snapshots and screenshots are not publication artifacts.
