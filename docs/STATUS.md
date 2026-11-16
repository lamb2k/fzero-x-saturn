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

Default-off FZX_SOURCE_FLOOR_RBG preserves original floor pixels in a resident
VDP2 character map. Per-dot reciprocal perspective and eight fog bands are explicit
Saturn adaptations. Full EMULATOR replay passes; all 19 held floor/cloud packets
match HOST, other scene packets/hashes remain unchanged and memory guards pass.
Rotation/window uploads match the displayed input and stay inside VBlank.

Floor/cloud CPU A/B/A means are 74.12 / 56.62 / 74.12 ms MODELLED.
The mean saving is 17.50 ms; this does not establish whole-frame FPS.
Stack retains 8,012 bytes; new VDP2 data uses 152,544 bytes. VDP1 textures
remain unchanged. Floor-off binary identity and corrupt-asset rejection pass.
See [floor measurements](FLOOR_PERFORMANCE.md) and [road measurements](ROAD_PERFORMANCE.md).

Follow-up screen-space fog was not retained: the corrected trial saved only
0.78 ms floor CPU across 13 held views and reduced the stack margin to 5,588 bytes.
Both trials and source patches are preserved privately; the admitted image is unchanged.

## Local experiments retained

Unrelated hitch, clip-divider and software-road WIP remain local and unstaged.
Use fixed-input replay for comparisons; live autopilot depends on render cadence.
No release, ROM-derived publication, SD write or physical acceptance occurred.

## Next

Continue rendering architecture: address expensive clouds and evaluate
source-derived road distance meshes or offline preparation.
Measure vehicle phases while preserving its original geometry and textures.
Then finish entry/selection, race audio/results/effects and pad-driven retry.
The complete content-bearing route must meet the unchanged 60 fps target.
