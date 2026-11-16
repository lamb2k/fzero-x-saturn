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
- Latest card: default-off FZX_ROAD_TILE_PROFILE, nine exclusive CPU phases.
  9,907 HOST views unchanged with GCC/Clang UBSan; 13-view native diagnostic and
  paired field/FRT wrap controls pass. Profile-off entire binary matches machine baseline.
- Instrumented mean 197.67 ms; baseline 125.92 ms;
  repeat 125.92 ms, max A/A2 delta 0.018 ms. Instrumentation overhead
  is substantial; phase values include it. See ROAD_PERFORMANCE.md, not raw HUD.
- Profile high-RAM end 0x060fa0b4; stack guard 17,548 bytes, low-RAM guards pass.
  Asset allocations unchanged: VDP1 textures 433,728/442,336 bytes, remaining 8,608.
- Unrelated clip/hitch/software-road WIP is preserved unstaged. No release/SD/private upload.

## Next work

1. Read generated/CONTINUE-CODEX.md tail and generated/PERFORMANCE-NEXT.md.
2. One bounded road cache: reuse source-vertex transform/projection/outcodes per frame,
   using low work RAM. Keep exact command output; measure uninstrumented A/B/A.
3. Road still needs larger architectural gains; investigate offline/source distance
   meshes and prototype original floor/cloud on VDP2 with actual VRAM/API limits.
4. Measure machine phases and combined scene. Retain source content and document adaptations.
5. Finish source entry/selection/results/audio/effects, pad flow, retry, soak and final review.

## Evidence

- FZX_TEST_ROM is local; never print or publish its path.
- FZX_YABA_EXE uses visible OpenGL; replay audits one CPU core and restores its own window.
- Full correctness: emu/machine-clean. Profile: emu/road-profile-paired;
  emu/road-profile is the superseded timing-scope diagnostic. All under FZX_EVIDENCE.
- The native build isolate currently outputs PROFILE=0;
  qualified ON artifacts are preserved externally. Do not overwrite them with OFF.
