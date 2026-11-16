# Status
Limit: 80 lines. History lives in git. Plan: [POC_PLAN.md](POC_PLAN.md).
Active goal: [first-course boot, selection and race completion](FIRST_COURSE_GOAL.md).

## Owner direction

Preserve F-Zero X on Saturn using original assets, prebaked into compatible formats.
Similar-quality rendering and behavior adaptations are authorized where exactness
is impractical. Preserve identity and playability; document meaningful differences.
Source assets stay private. Placeholders and missing content do not satisfy completion.

## Playable extent

Original Time Attack code runs Mute City 1 on the master SH-2 with binary32 soft float.
The original camera, Blue Falcon, road, sky/skyline/floor/cloud and partial HUD render
together behind explicit local flags. A full 9,907-input replay finishes all three
laps at source time 121,814 ms. This integration remains far over the 60 fps budget.
Boot/selection/results/audio and remaining effects are incomplete; debug UI remains.
Pad-only end-to-end acceptance, final look approval and HARDWARE remain unrun.

## Gates and evidence

| Area | Current evidence / remaining work |
| --- | --- |
| Gate 0 | Source-only hygiene, receipt rejection and private evidence boundaries established |
| Simulation | Full native replay preserves final pose/time and source state |
| Camera/HUD | 19 held camera packets match; timer/speed/lap/energy integrated; countdown/results open |
| Blue Falcon | Original near model retains 132 triangles, resident textures and live pose/body/lighting; full replay/guards pass; performance and final look remain open |
| Road | All 300 chunks / 4,966 triangles retained; shared cache reduces 13-view mean 125.92 to 112.04 ms MODELLED; budget blocked |
| Backdrop | Original floor now uses RBG0; floor/cloud mean 74.12 to 56.62 ms MODELLED over 13 views; clouds remain expensive |
| Gates 2-5 | Entry/results/effects/audio, native scene budget, pad flow and soak incomplete |
| Gate 6 | Full-game content, modes, opponents, progression and persistence planned |

## Current card

C0 frame profiling is gated: 9,907 inputs, all three laps, original final state
and scene packets retained. The 1,982-picture baseline is 3.287 fps MODELLED:
304.215 ms mean presentation interval, p95 433.333 ms, maximum 466.668 ms;
34,178 missed fields. These are complete-scene measurements, not physical FPS.
Road and machine command totals/ranges match HOST at the actual drawn inputs.
Clock controls, allocation guards and profile-off binary identity pass.
The diagnostic recorder leaves 5,996 bytes of stack guard, below C7's 16 KB target.
All 14 asset payloads match the admitted disc. See [frame measurements](FRAME_PERFORMANCE.md).

C1 is next: HOST road tier census over all 9,907 views, p99 at or below 420
commands and a forced-first-tier control above budget. No runtime road edits yet.
The owner's local render plan ranks C1-C8; the full first-course goal remains.

The last admitted rendering change remains the RBG0 floor: its 13-view
floor/cloud A/B/A means are 74.12 / 56.62 / 74.12 ms MODELLED. These cover
different views from the new whole-route baseline. The screen-space fog
follow-up was withdrawn for insufficient benefit and reduced memory margin.

## Local experiments retained

Unrelated hitch, clip-divider and software-road WIP remain local and unstaged.
Use fixed-input replay for comparisons; live autopilot depends on render cadence.
No release, ROM-derived publication, SD write or physical acceptance occurred.

## Next

Complete C1's road census and negative control, then road bake and stream,
vehicle rebake, bounded clouds, CPU split and combined measurement.
Finish original entry/selection, audio/results/effects, pad retry and soak.
SH2ZAM calibration, HARDWARE and owner appearance acceptance remain open.
