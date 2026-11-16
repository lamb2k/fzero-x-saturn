# Handoff
Limit: 50 lines. Replace this file each session; history lives in git.

## Owner direction

- Complete [Mute City 1 boot/selection/race/results](FIRST_COURSE_GOAL.md), targeting 60 fps.
- Use original assets converted for Saturn; similar-quality adaptations authorized.
- Source-only origin/gate1 pushes are authorized after gates/hygiene. Assets stay private.

## Current state

- Original race, camera, Blue Falcon, partial HUD, road and backgrounds are integrated.
- Complete native replay: 9,907 inputs, source time 121,814 ms, same final pose,
  19 matching scene/model packets and hashes, memory guards intact.
- Machine retains the near model's 132 triangles with live pose/body/lighting; chart cap 16
  and source LODs 1-3 -> 1, 4 -> 4, 5-6 -> 5 are adaptations. Final look remains open.
- Road 72.99-244.16 ms, machine 0.67-59.93 ms, planes 28.90-103.62 ms MODELLED subsystem
  samples fail the scene budget. The 16-bit whole-frame HUD timer wraps.
- Latest card: default-off FZX_ROAD_TILE_VERTEX_CACHE; transforms/projections/outcodes
  reused per source vertex within a draw. All 9,907 HOST commands unchanged under
  GCC/Clang UBSan; cache + profiler also passes. Authored stale/wrap/alias controls fail.
- 13-view native A/B/A road means 125.92 / 112.04 / 125.92 ms MODELLED, saving 11.02%.
  Max A/A2 delta 0.018 ms. Scene packets/hashes and all memory guards match.
  This short diagnostic does not replace the earlier complete native replay.
- Cache uses 73,180 bytes of guarded low RAM. High-RAM end 0x060f9f74;
  stack guard 17,880 bytes. Cache-off binary matches the admitted machine image.
  VDP1 textures remain 433,728/442,336 bytes, remaining 8,608.
- Unrelated clip/hitch/software-road WIP is preserved unstaged. No release/SD/private upload.

## Next work

1. Read generated/CONTINUE-CODEX.md tail and generated/PERFORMANCE-NEXT.md.
2. Prototype original floor on VDP2 with actual VRAM/coefficient addressing limits.
   Clouds can overlap the floor; one horizon switch does not cover both layers.
3. Road still needs larger architectural gains; investigate offline/source distance meshes.
4. Measure machine phases and combined scene. Retain source content and document adaptations.
5. Finish source entry/selection/results/audio/effects, pad flow, retry, soak and final review.

## Evidence

- FZX_TEST_ROM is local; never print or publish its path.
- FZX_YABA_EXE uses visible OpenGL; replay audits one CPU core and restores its own window.
- Full correctness: emu/machine-clean. Cache: emu/road-cache. Profile: emu/road-profile-paired;
  emu/road-profile is the superseded timing-scope diagnostic. All under FZX_EVIDENCE.
- The native build isolate currently outputs VERTEX_CACHE=0;
  qualified ON artifacts are preserved externally. Do not overwrite them with OFF.
