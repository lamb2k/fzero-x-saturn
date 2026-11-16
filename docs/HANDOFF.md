# Handoff
Limit: 50 lines. Replace this file each session; history lives in git.

## Owner direction

- Complete [Mute City 1 from boot/selection through race/results](FIRST_COURSE_GOAL.md).
- Similar-quality Saturn rendering/behavior adaptations are authorized when exactness
  is impractical. Assets remain original or are prebaked for Saturn compatibility.
- Preserve identity and playability; record differences. No replacement art.
- Source-only pushes to origin/gate1 are authorized after gates and hygiene.
  ROM-derived assets, traces, screenshots, audio and images remain private.
- This goal continues across cards. Do not stop at a probe or temporary graphics.

## Current state

- Original race code runs full Mute City 1 Time Attack on the master SH-2.
  Replay finishes in 121,814 ms; source camera and partial HUD are integrated.
- Source sky, skyline, floor and clouds now draw natively behind explicit flags.
  Original floor/cloud textures are prebaked into a 262,144-byte CD/VDP1 bank.
- Floor/cloud candidate passes 9,907 inputs, 19 HOST/native geometry hashes and
  matching camera/skyline/floor/sky state. Fixed valid cloud wind avoids RNG changes.
- Clipped polygons with five-plus corners and wide cloud UV derivatives are fixed;
  all 9,907 HOST views pass sanitizers, maximum 298 of 640 plane commands.
- Stack guard: 12,612 bytes; high-RAM end: 0x060fb2f4. CD scratch guards pass.
  Default-off binary matches the preceding HUD-CD image. Evidence: emu/planes-fan.
- Plane drawing costs 28.90-103.62 ms in 19 held views (MODELLED), so this approach
  is performance blocked. Visible bounds and native division were insufficient.
- Ordered cloud/skyline fade quality remains open. Placeholder road/craft,
  incomplete entry/results/effects/audio still prevent playable-port acceptance.
- HARDWARE remains UNRUN. No whole-frame 60 fps or final visual acceptance.

## Next work

1. Preserve older road/hitch/divider WIP and admitted native scene source.
2. Implement source road with shared native textures/subdivision. Do not return
   to per-frame software shading or oversized whole-patch texture banks.
3. Integrate original Blue Falcon appearance/pose, entry/selection and race audio.
4. Unblock plane cost using measured vertex reuse or Saturn rotation backgrounds;
   preserve content and verify source comparisons plus the full combined budget.
5. Qualify boot/select/race/results/retry, all memory pools, soak and console review.

## Working notes

- FZX_TEST_ROM is saved locally; never print or publish its path.
- Use patched FZX_YABA_EXE with visible OpenGL; hidden runs stall.
- input_replay.ps1 holds exact ticks, restores yabause.ini and stops its instance.
- Fixed-input replay is the comparison baseline; live autopilot depends on cadence.
- Public source only; all original content and emulator images remain local.
