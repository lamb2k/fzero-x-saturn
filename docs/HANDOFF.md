# Handoff
Limit: 50 lines. Replace this file each session; history lives in git.

## Owner direction

- Complete [Mute City 1 boot/selection/race/results](FIRST_COURSE_GOAL.md), targeting 60 fps.
- Original assets, prebaked for Saturn; similar-quality adaptations authorized.
  Preserve identity/playability. Source-only origin/gate1 pushes are authorized
  after gates/hygiene; original content and evidence stay private.

## Current state

- Original race, camera, partial HUD, road and sky/skyline/floor/cloud are integrated.
- `FZX_SOURCE_MACHINE=1` adds the real Blue Falcon to the actual race, with live
  pose, body color, tunnel lighting and visibility. Original near model: 132 triangles.
- Resident machine data: 15,164 bytes metadata, 16,064 texture bytes, 5,280 palette/shading
  bytes and 7,044 working bytes. Texture partition uses 433,728/442,336 bytes.
- Original LOD 1 serves source LODs 1-3; original LOD 4 stays 4; original LOD 5 serves 5-6.
  Source LOD 6 contains geometry; it is not dropped. Chart cap 16x16/Gouraud are adaptations.
- HOST compilers, controls, source nonmutation and all 9,907 sanitized views pass.
- Complete EMULATOR replay finishes 9,907 inputs at source time 121,814 ms with same final pose,
  19 matching machine hashes and unchanged source camera/road/background packets.
  All low-RAM guards pass; stack retains 18,224 bytes. High-RAM end 0x060f9cf4.
- Machine-only held-view range 0.67-59.93 ms, mean 45.74 ms MODELLED;
  road 72.99-244.16 ms and planes 28.90-103.62 ms separately. Full-scene 60 fps is unproven.
- Machine-off binary identity passes; corrupt metadata rejected before race starts.
  Earlier capture/window interruptions retained; the clean run audits one CPU core.
- Source software still comparisons preserve shape/markings with visible adaptations;
  original-game capture comparison, owner look acceptance and HARDWARE remain open.
- Unrelated clip/hitch/software-road work is preserved. No release/SD/private upload.

## Next work

1. Read generated/CONTINUE-CODEX.md tail and generated/PERFORMANCE-NEXT.md.
2. Prioritize road phase measurements, then shared transforms/offline bounds/LODs;
   existing projection-cull/endpoint-reuse experiments already failed budget.
3. Prototype original floor/cloud content on VDP2 after checking real VRAM/API limits.
4. Measure machine phases and the combined scene; retain original content and
   document meaningful adaptations. Do not lower the 60 fps target.
5. Finish source entry/selection/results/audio/effects, pad flow, retry and soak.

## Working notes

- FZX_TEST_ROM is local; never print or publish its path.
- Patched FZX_YABA_EXE uses visible OpenGL; hidden runs stall.
- Runtime evidence: emu/machine-clean, predecessor emu/machine-integration;
  source state and appearance oracle: host/machine-state under FZX_EVIDENCE.
- Test scripts restore yabause.ini and stop only their own emulator build.
