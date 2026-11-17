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
The [original title](TITLE.md) now accepts pad input; selection/results/audio and effects remain open.
Pad-only end-to-end acceptance, final look approval and HARDWARE remain unrun.

## Gates and evidence

| Area | Current evidence / remaining work |
| --- | --- |
| Gate 0 | Source-only hygiene, receipt rejection and private evidence boundaries established |
| Simulation | Full native replay preserves final pose/time and source state |
| Camera/HUD | 19 held camera packets match; timer/speed/lap/energy/countdown integrated; results open |
| Blue Falcon | Original near model retains 132 triangles, resident textures and live pose/body/lighting; full replay/guards pass; performance and final look remain open |
| Road | All 300 chunks / 4,966 triangles retained; shared cache reduces 13-view mean 125.92 to 112.04 ms MODELLED; budget blocked |
| Backdrop | C5 [dual layers](DUAL_RBG.md) unresolved; [compact clouds](PLANE_RESIDENCY.md) reclaim 128 KiB in native checks |
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

C1 passes over all 9,907 HOST views: p99 335 commands, maximum 355; forced
full repetition control p99 818 fails the 420 budget. GCC/Clang records match.
C2's [road bake](ROAD_BAKE.md) passes HOST gates: 151,128-byte metadata,
147,328-byte textures including three fog variants. All original rows/quads
remain; 140 tier images use bounded lookup conversion or exact RGB fallback.
Windows/Linux packs match; corruption controls fail. Retain all 415 fog-changing inputs.
[SH2ZAM view-edges-1](SH2ZAM_EDGES_ADOPTION.md) is adopted in pointer-only 36626ea.
Synthetic checks/calibration pass; old road 112.045 -> 112.033 ms MODELLED.
C3's [camera/pack/gather helpers](ROAD_STREAM.md) pass HOST and SH-2 compile:
1,291,264 visible vertex samples stay within 1 px; max gather is 552 records.
Native stream is blocked: two mapping arms fail the 1-texel p99 image gate
(9.33 texels); the shifted synthetic fixture also fails. See ROAD_STREAM.md.
The owner's local render plan ranks C1-C8; the full first-course goal remains.
C8 [countdown](COUNTDOWN.md) is native: 24 timeline views / 276 submitted commands
match HOST; three-lap final state/time and all guards pass. Sound remains open.
Ymir full race: 3.824 fps MODELLED, 29,118 missed fields; stack 49,068 B.
Source sign/texture residency and corrupt-texture rejection pass; owner look open.
[Unused core renderer bypass](NATIVE_CORE_MEMORY.md) saves 59,736 linked bytes;
13 native views retain exact scene commands/pixels; stack minimum 65,680 B.

C5 compact atlas passes 13 views and knob-off identity; 128 KiB reclaimed, no speed claim.
C8 [title](TITLE.md) retains source assets/input rules; 13 race handoff views match, stack 47,632 B.
C4 [vehicle rebake](MACHINE_REBAKE.md) is default-off and blocked on ordering/cost.
HOST p99 67 / max 112 commands; 13-view native mean 9.273 ms MODELLED vs 52.112 before.
The 1 ms/70-command targets and look gate fail; four order policies fail overlap.
Full race/state/guards pass: 3.837 fps MODELLED; stack guard 2,316 B fails C7.

## Local experiments retained
Unrelated hitch, clip-divider and software-road WIP remain local and unstaged.
Use fixed-input replay for comparisons; live autopilot depends on render cadence.
No release, ROM-derived publication, SD write or physical acceptance occurred.

## Next

View-guard-1 is [adopted](SH2ZAM_GUARD_ADOPTION.md); C3 needs revised mapping.
Next: original main menu, course/machine/settings selection; audio/results/effects, retry and soak.
SH2ZAM calibration, HARDWARE and owner appearance acceptance remain open.
