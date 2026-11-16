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
- Extended clock and counters pass; 16-bit clock controls fail as required.
- Profile-off binary equals the admitted RBG0 build; 14 asset payloads unchanged.
- Low-RAM recorder 262,144 bytes; all guards pass. Stack guard 5,996 bytes,
  below C7's 16 KB target. Faster full captures will need more than 4,096 rows.
- Private evidence: emu/frame-profile-v2/race, with exact image/source receipts.
- First v1 capture timed out at 9,302 inputs. V2 kept the same emulator process
  through a documented controller handoff at 8,806, with no pause/reset/upload.
- All owner build/emulator processes are finished. No live capture to restart.

## C1 passed; next: C2

- All 9,907 HOST views: p99 335, max 355 commands; forced-first control p99 818.
- GCC/Clang UBSan records match; native views match Python; old road unchanged.
- Four texture tiers plus mean: 140,576 bytes RGB1555. Three fog copies would
  exceed budget; only 8/28 strips fit 16 colours losslessly. See ROAD_LOD_CENSUS.md.
- C2: FZXRLD01 receipt-bound pack, 4-byte repetition records, far chains and
  source-texture tiers. Bound packs/textures and qualify any palette adaptation.
- Keep the old runtime road frozen. Do not edit SH2ZAM or invent general kernels.
- The latest detailed checkpoint is at the TOP of generated/CONTINUE-CODEX.md.

## Preserved work and gaps

- Older clip/hitch/software-road WIP is still local and unstaged.
- The build isolate currently contains profile-OFF outputs. Qualified ON artifacts
  are preserved externally; do not overwrite them with OFF outputs.
- The screen-space fog trial remains withdrawn. Other source-art/content cards
  and all physical acceptance remain open. No release or SD write occurred.
