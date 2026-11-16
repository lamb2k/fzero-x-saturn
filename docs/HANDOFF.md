# Handoff
Limit: 50 lines. Replace this file each session; history lives in git.

## Owner direction

- Complete [Mute City 1 boot/selection/race/results](FIRST_COURSE_GOAL.md), targeting 60 fps.
- Original assets converted for Saturn; similar-quality adaptations authorized.
- Source-only origin/gate1 pushes authorized after gates/hygiene. Assets stay private.

## Current state

- Original race, camera, Blue Falcon, partial HUD, road and backdrops are integrated.
- Latest full native replay: 9,907 inputs, source time 121,814 ms, same final pose.
- Default-off FZX_SOURCE_FLOOR_RBG moves original floor pixels to VDP2 RBG0.
  Reciprocal perspective, finite window and eight fog bands are explicit adaptations.
  Clouds retain the previous native sprite path. Final appearance acceptance is open.
- All 19 floor packets/cloud hashes match HOST; camera/road/machine/other backdrop
  packets and hashes remain unchanged. CD, low-RAM/cache and stack guards pass.
- Floor table uploads match displayed inputs; all captured uploads remain in VBlank.
- Thirteen-view floor/cloud CPU A/B/A: 74.12/56.62/74.12 ms MODELLED,
  saving 17.50 ms mean. This is not whole-frame FPS; the budget is still blocked.
- New VDP2 data 152,544 bytes; original VDP1 floor atlas remains allocated.
  VDP1 textures 433,728/442,336 bytes; high-RAM end 0x060fc4f4; stack guard 8,012 bytes.
- Floor-off binary equals admitted cache image. Corrupt coefficient boot and stale
  receipt controls reject as intended. Full GCC/Clang UBSan projection checks pass.
- Road cache mean 112.04 ms; vehicle remains expensive. Whole-frame HUD timer wraps.
- Entry/selection/results/audio/effects, pad flow, soak and HARDWARE remain open.
- Unrelated clip/hitch/software-road WIP preserved unstaged. No release/SD/private upload.

## Next work

1. Read generated/CONTINUE-CODEX.md tail and generated/PERFORMANCE-NEXT.md.
2. Continue architecture: repeated floor fog clipping and expensive clouds;
   source-derived road LOD/offline preparation; measured vehicle phases.
3. Keep original content. Measure combined scene and worst views after each change.
4. Finish source entry/selection/results/audio/effects, pad flow, retry and final soak.

## Evidence

- FZX_TEST_ROM stays local; never print or publish its path.
- Latest full race and A/B/A: emu/floor-rbg; HOST projection: host/floor-rbg.
- Per-held byte register reads were unsupported; runtime-registers.json uses actual
  word reads. Rotation/window VRAM readbacks match each held frame directly.
- Native isolate outputs FLOOR_RBG=0; qualified ON artifacts are preserved externally.
  Do not overwrite ON artifacts with OFF. HARDWARE remains UNRUN.
