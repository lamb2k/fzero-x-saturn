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
| Source scene | Backdrop/skyline state pass 256 SH-2 probes. Native sky/skyline and original floor/cloud draw; floor/cloud full replay passes but its 28.90-103.62 ms MODELLED cost is blocked |
| Gates 2-5 | Native scene, entry/results/audio, pad-only acceptance, full-route cost/memory and final soak remain incomplete |
| Gate 6 | Full-game content/modes/opponents/progression/persistence remain planned |

## Current card

Original floor/cloud assets are prebaked and boot-loaded into VDP1 behind
FZX_SOURCE_PLANES=1 (default off). Full 9,907-input replay finishes at 121,814 ms;
19 geometry hashes match HOST, and camera/skyline/floor/sky state match baseline.
Cloud scroll follows original update rules with fixed source-valid wind, without
consuming simulation RNG. Both low-RAM guards pass; stack guard is 12,612 bytes.
High-RAM end: 0x060fb2f4. Pack: 262,144 VDP1 bytes plus 10,240 reserved shade bytes.
GCC/Clang/repeat, sanitized full-route geometry and failing controls pass.
Knob-off binary is identical. See SOURCE_ART.md for adaptations and evidence.

Cost blocker: plane drawing alone takes 28.90-103.62 ms in 19 held views (MODELLED).
Visible tile bounds and native projection division did not reach the budget.
Unblock with a cheaper native plane path preserving content, then combined-route
60 fps qualification. Final cloud/skyline fade quality also remains open.

## Local experiments retained

Hitch diagnostics, clip-divider and software-road WIP remain local/unpromoted.
Use fixed-input replay for comparisons; live autopilot depends on render cadence.
No release, ROM-derived publication, SD write or physical acceptance occurred.

## Next

Integrate source road with shared native textures and bounded subdivision;
software pixel conversion and whole-patch baking are already cost/capacity blocked.
Then integrate the original machine, entry/selection and race presentation/audio.
Return to plane cost with projected-vertex reuse or Saturn rotation backgrounds;
measure the complete content-bearing route before claiming 60 fps.
