# Status
Limit: 80 lines. History lives in git. Plan: [POC_PLAN.md](POC_PLAN.md).
Active goal: [first-course boot, selection and race completion](FIRST_COURSE_GOAL.md).
Current C0-C8 decisions and dependencies: [render programme](RENDER_PLAN_CURRENT.md).
## Owner direction
Preserve F-Zero X with original assets prebaked for Saturn; placeholders/missing features are not completion.
Saturn-native rendering: source packet/hash/arithmetic and C4 stat identity gates lifted; tags optional.
Judge pixels, colour and visible defects against the original image; no numeric visual threshold.
Keep 16 KiB stack, guards, no crashes, road420/vehicle70, legacy default, hygiene/ROM rules and evidence tiers.

## Playable extent

Original Time Attack code runs Mute City 1 on the master SH-2 with binary32 soft float.
Original pad-driven title/main/course/machine/settings screens now reach the race.
Selected skin 1 / engine 0.5078125 survives the handoff. A separate 7,059-input
native diagnostic replay completes three laps at source time 110,639 ms.
The original camera, Blue Falcon, road, sky/skyline/floor/cloud and partial HUD render
behind explicit local flags, including finish/results and records/name entry. Retry,
audio/effects, ordinary pad-only completion, soak, final look and HARDWARE remain open. No 30 or 60 fps acceptance.

## Gates and evidence

| Area | Current evidence / remaining work |
| --- | --- |
| Gate 0 | Source-only hygiene, receipt rejection and private evidence boundaries established |
| Simulation | Selected native handoff and full replay match HOST state/pose/camera; legacy disabled arm remains unchanged |
| Camera/HUD | Source camera, timer/speed/lap/energy/countdown and initial results integrated; final appearance open |
| Entry | [Machine/settings](MACHINE_MENU.md): 383 native views, all 30 models/settings screens, source state/commands/assets/guards, three corrupt packs and off identity pass |
| Road | All 300 chunks / 4,966 triangles retained; shared cache reduces 13-view mean 125.92 to 112.04 ms MODELLED; budget blocked |
| Backdrop | C5 [dual layers](DUAL_RBG.md) unresolved; [compact clouds](PLANE_RESIDENCY.md) reclaim 128 KiB in native checks |
| Gates 2-5 | Records/retry, effects/audio, native scene budget, ordinary pad race flow and soak incomplete |
| Gate 6 | Full-game content, modes, opponents, progression and persistence planned |

## Current cards

C0's frozen legacy-tuning baseline completes 9,907 inputs / three laps at 3.287 fps MODELLED:
304.215 ms mean presentation interval, p95 433.333 ms, maximum 466.668 ms;
34,178 missed fields. This is not physical FPS or a measurement of the new selected replay.
Clock controls, allocations and profile-off identity pass; baseline stack 5,996 B fails C7.

C1 passes all 9,907 HOST views: p99 335 commands, max 355; forced-full control p99 818 fails 420.
C2's [road bake](ROAD_BAKE.md) retains original rows/quads: 151,128 B metadata / 147,328 B textures.
Windows/Linux outputs and corruption controls pass; retain all 415 fog-changing inputs.
[SH2ZAM view-edges-1](SH2ZAM_EDGES_ADOPTION.md) and [view-guard-1](SH2ZAM_GUARD_ADOPTION.md) are adopted.
C3 [camera/pack/gather](ROAD_STREAM.md): 1,291,264 samples within 1 px, max gather 552; HOST/SH-2 pass.
Historical texture metric: 1 px merge census p99 413 commands / 0.650 px texture; HOST only.
[view-merge-1](SH2ZAM_MERGE_ADOPTION.md) adopted after synthetic gates; tagged census unchanged.
[view-round-1](SH2ZAM_ROUND_ADOPTION.md): integral adapter and unchanged fixtures pass HOST.
[view-center-1](SH2ZAM_CENTER_ADOPTION.md): all9,907 views, zero exclusions, p99 413; 13 images pass.
C3 [native stream](ROAD_STREAM_NATIVE.md) completes selected 7,059 inputs with exact finish/guards;
road p99 133.143 ms and 3.929 fps MODELLED fail the 16 ms stop gate. Historical ideal-image check failed.

C4 [vehicle rebake](MACHINE_REBAKE.md): 9.273 ms MODELLED, default-off; [shade pin](SH2ZAM_SHADE_ADOPTION.md) HOST-qualified.
HOST p99 67 / max 112; cost/command gates fail; [fixed orders](C4_OCCLUSION_FEASIBILITY.md) fail held-out views.
Its full race matches source at 3.837 fps MODELLED; 2,316 B stack fails C7.
C5 compact atlas reclaims 128 KiB; [fixed grid](CLOUD_GRID.md) fits both routes but fails cloud appearance.
C8 [countdown](COUNTDOWN.md): 24 views / 276 commands and full source race pass; sound/look open.
[Core bypass](NATIVE_CORE_MEMORY.md) saves 59,736 linked B with 13 native command/pixel checks.

C8 machine/settings: source portraits, colors, tuning, all 30 LOD1 models and navigation.
The original racer tuning handoff is corrected only when enabled; the old replay is a distinct baseline.
A guarded 182,000 B graphics pool uses the required 4 MiB cartridge; CD scratch reserves 12 KiB.
Final selected replay: four held views / 39 racer fields, guards and mutation controls pass;
minimum sampled stack 107,568 B. This is diagnostic replay, not pad-only or speed acceptance.

C8 [initial results](RESULTS.md): 12,288 inputs/post-cap clock, 23 captures, four bad packs pass.
[Records/name entry](RECORDS.md): 7,924 inputs, 29 source-exact captures, 20 native command/residency checks.
Three diagnostic resets retain AAA; stack minimum 23,064 B; 19 HOST tests and two bad packs pass.
OFF equals rebuilt pre-records code with the same library; historical library delta is +832 linked B.
[Finish menu](FINISH_MENU.md): 73 source holds, 47 palette boundaries, 64 backdrops and OFF identity pass.
Final appearance, pad retry and audio remain open; diagnostic text/HUD obscure the records view.

## Preserved work and next

Older hitch, clip-divider and software-road WIP remain local and unstaged.
Use fixed-input replay for comparisons; live autopilot depends on render cadence.
Finish-menu gates pass. Opus: claude/c3-c4-perf and claude/c4-perf; Codex: visual C4 draw order/other work;
ordinary retry, audio/effects, pad-only race completion and soak remain.
C3/C4/C5 performance blockers and full combined 60 fps qualification remain.
No release, retail-derived publication, SD write or physical acceptance occurred.
