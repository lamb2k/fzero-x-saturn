# Status
Limit: 80 lines. History lives in git. Plan: [POC_PLAN.md](POC_PLAN.md).
Active goal: [first-course boot, selection and race completion](FIRST_COURSE_GOAL.md).

## Owner direction

Preserve F-Zero X on Saturn using original assets, prebaked into compatible formats.
Similar-quality rendering and behavior adaptations are authorized where exactness
is impractical. Preserve identity and playability; record meaningful differences.
Source assets stay private. Placeholders and missing content do not satisfy completion.

## Playable extent

The original Time Attack race code runs Mute City 1 on the master SH-2 with binary32
soft float. Full input replays finish three laps in 121,814 ms, matching HOST.
The image still has a temporary road/craft, debug overlay and incomplete presentation.
Source camera, partial original HUD and native sky are available behind explicit flags.
Boot/menu/selection/results/audio and the remaining source scene are not complete.
The old placeholder baseline missed 38 fields in 9,455 (MODELLED); it does not establish
performance for the content-bearing port. HARDWARE remains UNRUN.

## Gates and evidence

| Area | Current evidence / remaining work |
| --- | --- |
| Gate 0 | Repository hygiene, receipt rejection, evidence boundaries and visible simulation selector established |
| Gate 1 | Closure census, deterministic HOST reference and SH-2 emulator lane established; complete scene timing and capacity remain open |
| Simulation | Original race closure and 9,907-input source-camera replay finish at 121,814 ms; fixed-point conversion is not required to move presentation forward |
| Source models | Five Blue Falcon LODs and 22 textures exported; 72 pose/light checks pass. Native palette appearance and cost remain open |
| Source HUD | Timer, speed, lap, energy and lap flashes integrated under FZX_SOURCE_HUD=1; countdown/results/effects remain open |
| Source camera | 19 held SH-2 packets match; view/look-back replay finishes correctly |
| Source road | 300 chunks / 4,966 triangles, 146,120 cartridge bytes admitted. Materials/planner pass HOST gates; software rendering costs 132-165 fields/view and whole-patch baking exceeds VRAM |
| Source scene | Backdrop/skyline state pass 256 SH-2 probes. Native sky supports original geometry/colors and an offline-selected smooth gradient; original skyline drawing is admitted; floor/cloud drawing remains open |
| Gates 2-5 | Native scene, entry/results/audio, pad-only acceptance, full-route cost/memory and final soak remain incomplete |
| Gate 6 | Full-game content/modes/opponents/progression/persistence remain planned |

## Current card

Original HUD and skyline both load their identical native pixel bytes from CD
behind default-off flags. The shared loader verifies receipts, size/hash and VRAM
readback; corrupted packs stop before race initialization. HOST controls pass.
HUD knob-off binary is identical. The combined image finishes 9,907 inputs in
121,814 ms with matching pose and all 19 camera/backdrop/skyline packets.
High-RAM end is 0x060f9274 (another 14,016 bytes recovered); stack guard is 22,872.
The 2,560-byte low-RAM scratch follows original variables; both guards pass the
full race. Original HUD appearance was reviewed locally. See SOURCE_ART.md.

## Local experiments retained

Hitch diagnostics, clip-divider and software-road WIP remain local/unpromoted.
Use fixed-input replay for comparisons; live autopilot depends on render cadence.
No release, ROM-derived publication, SD write or physical acceptance occurred.

## Next

Implement original floor/cloud drawing using the recovered RAM and boot-loaded
texture packs. Use shared native road textures instead of per-frame software
conversion. Then integrate the original
machine, entry/selection, race presentation/audio and full-course acceptance.
