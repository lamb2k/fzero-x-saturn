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
| Source road | All 300 chunks / 4,966 triangles retained; shared vertex cache reduces 13-view mean 125.92 to 112.04 ms MODELLED; budget still blocked |
| Source backdrop | Original sky/skyline/floor/cloud draw; plane-only cost 28.90-103.62 ms MODELLED remains blocked |
| Gates 2-5 | Entry/results/effects/audio, native rendering budget, pad-only flow and final soak incomplete |
| Gate 6 | Full-game content, modes, opponents, progression and persistence remain planned |

## Current card

`FZX_ROAD_TILE_VERTEX_CACHE=1` reuses per-frame transforms/projections/outcodes,
default off. All 9,907 HOST source views retain identical commands under
GCC/Clang UBSan. Stale-generation, wrap and vertex-alias controls fail as intended.
The 13-view EMULATOR diagnostic preserves all scene packets and draw hashes;
CD/low-RAM/cache guards pass and stack retains 17,880 bytes.

Uninstrumented A/B/A road means are 125.92 / 112.04 / 125.92 ms, MODELLED.
The cache saves 11.02% on these views; maximum A/A2 variation is 0.018 ms.
Worst sampled road cost falls from 244.21 to 213.55 ms, still over budget.
It uses 73,180 bytes of low work RAM; texture assets and VRAM are unchanged.
Cache-off matches the admitted machine binary exactly. The prior complete
native race remains the full-route correctness checkpoint.
See [road measurements](ROAD_PERFORMANCE.md) for profiling and cache scope.

## Local experiments retained

Unrelated hitch, clip-divider and software-road WIP remain local and unstaged.
Use fixed-input replay for comparisons; live autopilot depends on render cadence.
No release, ROM-derived publication, SD write or physical acceptance occurred.

## Next

Prioritize rendering architecture: prototype the original floor on Saturn
background hardware, accounting for clouds that overlap it. Evaluate source-derived
road distance meshes after the exact shared-work cache.
Keep original vehicle geometry and resident textures; measure its expensive phases.
Then finish entry/selection, race audio/results/effects and pad-driven retry.
The complete content-bearing route must meet the unchanged 60 fps target.
