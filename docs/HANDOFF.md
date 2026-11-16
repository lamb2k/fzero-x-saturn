# Handoff
Limit: 50 lines. Replace this file each session; history lives in git.

## Owner direction

- Complete [Mute City 1 from boot/selection through race/results](FIRST_COURSE_GOAL.md).
- Original source assets, prebaked for Saturn; similar-quality native adaptations
  are authorized. Preserve identity/playability and record meaningful differences.
- No placeholders or isolated probes satisfy the goal. Source-only origin/gate1
  pushes are authorized after gates/hygiene; original content stays private.

## Current state

- Original race, camera, partial HUD and sky/skyline/floor/cloud are integrated.
- Native source-road tiles and two default-off reuse switches are now gated.
  All 300 chunks / 4,966 source triangles retained; metadata 43,088 bytes and
  textures 108,384 bytes. The craft remains temporary.
- HOST GCC/Clang/repeat, rejection controls and all 9,907 UBSan views pass;
  four reuse combinations retain route command hashes, maximum 599/768 commands.
- Combined native replay finishes 9,907 inputs at 121,814 ms, same final pose,
  all 19 held hashes and unchanged scene packets. Low-RAM guards pass; painted
  stack retains 26,700 bytes. ISO 8f53d3a6... in emu/road-tiles-reuse11.
- Road-only held-view mean 123.25 ms, max/p95 244.16 ms MODELLED; budget fails.
  Matched 13-view A/B/A mean 153.89/125.87/153.89 ms. Final corrupt metadata
  is rejected before race initialization; both reuse OFF and road OFF identities pass.
- All build/emulator sessions are terminal. Unrelated local road/hitch/clip WIP
  remains unstaged and must be preserved. No release, SD or hardware action.
- Plane cost also remains blocked. Machine, entry/results/effects/audio, combined
  frame qualification, physical testing and final look acceptance remain open.

## Next work

1. Read generated/CONTINUE-CODEX.md tail and generated/MACHINE-NEXT.md for details.
2. Integrate original Blue Falcon through actual race camera/model pose. Bound
   resident charts and caches; current VDP1 texture remainder is 29,952 bytes.
3. Read-only source snapshot must preserve live body colors and tunnel lighting;
   outside racer fog needs receipt-bound source data, not an uninitialized global.
4. Gate moving model, clipping, source-reference views, complete replay and memory;
   then original entry/selection and race audio/results. Full goal stays active.
5. Return to whole-scene cost with cheaper native projection/submission or reviewed
   source-derived asset adjustments; do not lower the 60 fps target.

## Working notes

- FZX_TEST_ROM is local; never print or publish its path.
- Use patched FZX_YABA_EXE with visible OpenGL; hidden runs stall.
- Fixed-input replay is the comparison baseline; live autopilot depends on cadence.
- Scripts preserve yabause.ini and stop only their own emulator build.
