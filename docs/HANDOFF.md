# Handoff
Limit: 50 lines. History lives in git.

## Goal and direction

- Follow the owner's local render plan C0-C8 and all [first-course gates](FIRST_COURSE_GOAL.md).
- Original boot/selection, Mute City 1, results/retry, audio/effects, pad flow,
  soak and 60 fps target remain required. Hardware and owner look review are open.
- Convert original assets offline for Saturn; document adaptations.
- Completed source-only origin/gate1 pushes authorized after gates and hygiene.

## C0 result

- Full 9,907-input native replay passes; source finish time 121,814 ms, same final pose.
- All final scene packets and whole-capture road/machine command counts match HOST.
- 1,982 pictures: 3.287 fps MODELLED, mean 304.215 ms, p95 433.333 ms,
  maximum 466.668 ms, 34,178 missed fields. No 30 or 60 fps acceptance.
- Mean CPU: road 121.720 ms, machine 52.858, clouds 32.863, floor 14.029;
  simulation 46.134 per catch-up loop and state publication 32.078 ms.
- Clock controls pass; profile-off binary equals admitted RBG0; 14 payloads unchanged.
- Low-RAM recorder 262,144 bytes; all guards pass. Stack guard 5,996 bytes,
  below C7's 16 KB target. Faster full captures will need more than 4,096 rows.
- Private evidence: emu/frame-profile-v2/race, with exact image/source receipts.
- The full C0 capture is finished; no replay needs restarting.

## C1/C2 passed; next: C3

- All 9,907 HOST views: p99 335, max 355 commands; forced-first control p99 818.
- GCC/Clang UBSan records match; native views match Python; old road unchanged.
- FZXRLD01: 151,128 bytes metadata; textures 143,976 including three fog variants.
- All original vertices/quads retained. 103 CLUT4 images stay within one RGB5
  step/channel; 37 RGB images retain exact filtered pixels. Windows/Linux agree.
- See ROAD_BAKE.md for format, receipts and controls. Far merges remain unqualified.
- C3 must retain 62 source fog colours across 415 non-base inputs; baked fog is base only.
- SH2ZAM view-edges-1 adopted in pointer-only 36626ea; synthetic/CI calibration pass.
  Old road 112.045 ->112.033 ms MODELLED; see SH2ZAM_EDGES_ADOPTION.md.
- C3 camera/pack/gather HOST gates pass: <=1 px, max552 vertices; ROAD_STREAM.md.
- Native stream is not integrated. Near-only fails command bounds; guard-band request in report.
- Require image-space controls and p99 road CPU <=8 ms MODELLED; >16 ms is no-go.
- Keep the old runtime road frozen. Do not edit SH2ZAM or invent general kernels.
- C8 countdown pack: 23 original quads, 2,352 B metadata / 70,400 B charts; HOST passes.
- Snapshot/view HOST+SH-2 compile pass; native draw/audio open; unlit source (COUNTDOWN.md).

## Preserved work and gaps

- Older clip/hitch/software-road WIP is still local and unstaged.
- The build isolate currently contains profile-OFF outputs. Qualified ON artifacts
  are preserved externally; do not overwrite them with OFF outputs.
- Fog trial withdrawn; other content and physical gates open. No release/SD write.
