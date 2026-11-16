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
The original camera, Blue Falcon, road, sky/skyline/floor/cloud and partial HUD now
render together behind explicit local build flags. Three-lap input replays finish
at source race time 121,814 ms. This is an integration experiment, far over the 60 fps budget.
Boot/selection/results/audio and remaining effects are incomplete; debug UI remains.
Pad-only end-to-end acceptance, final look approval and HARDWARE remain unrun.

## Gates and evidence

| Area | Current evidence / remaining work |
| --- | --- |
| Gate 0 | Source-only publication hygiene, receipt rejection and private evidence boundaries established |
| Simulation | Original race closure; complete 9,907-input native replay preserves final pose/time and source state |
| Source camera/HUD | 19 held camera packets match; timer/speed/lap/energy integrated; countdown/results remain open |
| Source Blue Falcon | Original near model retains 132 triangles, with resident source-derived textures and live pose/body/lighting; full replay and guards pass; performance and final visual acceptance remain open |
| Source road | All 300 chunks / 4,966 triangles retained; 19-view cost 72.99-244.16 ms MODELLED after two reuse experiments |
| Source backdrop | Original sky/skyline/floor/cloud draw; plane-only cost 28.90-103.62 ms MODELLED remains blocked |
| Gates 2-5 | Entry/results/effects/audio, native rendering budget, pad-only flow and final soak incomplete |
| Gate 6 | Full-game content, modes, opponents, progression and persistence remain planned |

## Current card

`FZX_SOURCE_MACHINE=1` integrates the original Blue Falcon into the actual race.
The read-only source snapshot retains live body color, tunnel lights, reflection,
pose and visibility. Source LODs 1-3 use original LOD 1; LOD 4 stays 4; LODs 5-6 use 5.
The 16x16 chart cap, native palette/Gouraud lighting and LOD mapping are adaptations.
The temporary craft is disabled only when the original-machine path is enabled.

HOST: GCC/Clang, source nonmutation, authored failure controls and all 9,907
sanitized camera/model views pass. EMULATOR: full race, 19 source-state/draw hashes,
unchanged road/background packets and low-RAM guards pass. Stack retains 18,224 bytes.
Machine-only cost is 0.67-59.93 ms, mean 45.74 ms across 19 held views,
MODELLED. These are subsystem samples, not combined frame-rate acceptance.
Corrupt machine metadata fails before race initialization. Machine-off binary
matches the prior admitted road image. Local same-camera software source-model
comparisons preserve recognizable shape/markings; they are not source-game captures.
Earlier input 624 capture stall and a minimized-window/core-change diagnostic are
retained separately. The replacement run audits one CPU core and window visibility.

## Local experiments retained

Unrelated hitch, clip-divider and software-road WIP remain local and unstaged.
Use fixed-input replay for comparisons; live autopilot depends on render cadence.
No release, ROM-derived publication, SD write or physical acceptance occurred.

## Next

Prioritize rendering architecture: measure exclusive road phases on pinned views,
then eliminate repeated transforms/traversal and evaluate source-derived distance
meshes. Prototype the original floor/cloud content on Saturn background hardware.
Keep original vehicle geometry and resident textures; measure its expensive phases.
Then finish entry/selection, race audio/results/effects and pad-driven retry.
The complete content-bearing route must meet the unchanged 60 fps target.
