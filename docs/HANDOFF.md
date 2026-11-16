# Handoff
Limit: 50 lines. Replace this file each session; history lives in git.

## Owner direction

- Complete [Mute City 1 from boot/selection through race/results](FIRST_COURSE_GOAL.md).
- Similar-quality Saturn rendering/behavior adaptations are authorized when exactness
  is impractical. Assets remain original or are prebaked for Saturn compatibility.
- Preserve source identity and playability; record differences. No replacement art.
- Source-only pushes to origin/gate1 are authorized after gates and hygiene.
  ROM-derived assets, traces, screenshots, audio and images remain private.
- This goal continues across cards. Do not stop at a probe or temporary graphics.

## Current state

- Original race code runs a full Mute City 1 Time Attack on the master SH-2.
  Replay finishes in 121,814 ms; source camera and partial HUD are integrated.
- Source Blue Falcon assets and pose/light state are extracted. Palette appearance
  and native model cost need work. Road drawing is not yet within budget.
- Source sky and skyline now draw natively. Original skyline images remain 64x64
  CI4; tint and ordered alpha coverage are prebaked. No per-frame conversion.
- Isolated native replay passes 19 views / 114 original skyline placements and
  finishes three laps in 121,814 ms. Stack guard is 6,908 bytes; high RAM is tight.
- Skyline drawing costs 0.82-0.99 ms (MODELLED); sky costs about 1.34-1.88 ms.
  Neither is whole-scene timing. HARDWARE and final visual acceptance remain open.
- Boot/selection, source floor/cloud, complete HUD/results/effects and audio
  remain incomplete. Placeholder road/craft still prevent playable-port acceptance.

## Next work

1. Native sky/skyline are gated. Preserve older road/hitch/divider WIP locally.
   Before more embedded assets, address high-RAM residency (end 0x060fd0d4).
2. Integrate original floor/cloud assets through Saturn-native rendering. Prebake
   conversions; avoid per-frame texture conversion or software shading.
3. Use shared native road textures/subdivision; whole-patch bake exceeded VRAM.
   Improve source machine palettes/rendering, then original entry/selection/audio.
4. Measure the combined content-bearing route, all memory pools and stack, then
   boot/select/race/results/retry, soak and prepare console acceptance.

## Working notes

- FZX_TEST_ROM is saved locally; never print or publish its path.
- Use the patched FZX_YABA_EXE with a visible OpenGL window; hidden runs stall.
- input_replay.ps1 holds exact ticks for camera/scene checks, restores yabause.ini
  and stops its emulator. Never launch a second instance of that build concurrently.
- Live autopilot is cadence-sensitive; fixed input is the comparison baseline.
- Source-only documentation and code may be public; all original content stays local.
