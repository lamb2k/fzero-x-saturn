# Handoff
Limit: 50 lines. Replace this file each session; do not append.
## Owner direction (2026-11-15)

- The target is the real game on Saturn, preserving source art. The old prohibition
  on the game's art/look is superseded. Original models, textures/materials,
  scenery, effects, HUD/fonts, menus and audio are required. Placeholders are tests.
- POC_PLAN section 4.7 defines the source-art gate; Gate 6 covers the complete game.
  A Mute City slice is an intermediate milestone, not completion.
- ROM-derived assets, packs, traces, stills and images stay local/ignored. Public
  source contains converters, runtime code and synthetic fixtures only.

## Current state, branch `gate1`

- Original racer code (`FZX_SIM=orig`, binary32 soft float) runs a 3-lap Mute City 1
  race on the master SH-2 with placeholder graphics, debug HUD and Start retry.
  Whole-race finish time matches a host input replay. HARDWARE is UNRUN.
- Verified retail export: five Blue Falcon LODs and 22 textures, all local.
  Native timer/speed/lap/energy and lap flashes run under FZX_SOURCE_HUD=1.
  Source-camera view/scissor integration is default-off; 19 held SH-2 packets match.
  A 9,907-input replay with view/look-back controls finishes in 121,814 ms, as on host.
- Local performance WIP adds bounded hitch logs and corrects MISS to elapsed-1.
  Rendering peaks at the same section each lap (up to 88% of a field before changes).
- FZX_CLIP_DIVU defaults off. HOST frame corpus hash 0027e206 is unchanged;
  65,776 SH-2 division cases match; the mutation control rejects all cases.
  First arm: 26 misses/9,871 loops and matching full-race replay. Reduced-plane
  arm: 32/9,759 and diagnostic autopilot retirement. NOT promoted; timings MODELLED.
- The race-log tool now rejects an empty/full log; replay requires an actual finish.
  Yaba OpenGL stalls at frame 0 when hidden here; test callers use a visible window.

## Next work

1. FZX_TEST_ROM is saved locally for Windows; forward it privately into WSL.
   Moving Blue Falcon now passes 72 host/SH-2 pose/light checks. Native palettes
   still differ visually and are slow. Exact reuse is gated default-off; source
   camera/view passes HOST and SH-2; source-scene performance remains over budget.
2. Road software draw is cost-blocked; full-detail native patch baking also exceeds
   VRAM (up to 2.13 MB/view). Shared tiles remain open; backdrop assets/state pass HOST; skyline bindings next.
   Measure stack high water (21,740-byte static gap) and source-scene cost. Use fixed inputs
   for comparisons; the live diagnostic autopilot depends on render/catch-up cadence.
3. Cut 18.5 s setup, load the CPU run-out racing line from ROM, rerun all 10 SH-2
   probes after closure changes, then prepare the owner console run.
4. Expand the faithful slice through Gate 6's content/mode coverage ledger.
## Working notes
- Soft-float versus fixed-point is open; CI default remains `proto`.
- Use FZX_YABA_EXE and FZX_TEST_ROM locally; never publish private paths. Emulator
  scripts restore the shared yabause.ini; stop this instance before rebuilding.
- `tools/emu/input_replay.ps1` uploads/verifies the existing log, restarts, then
  holds simulation at N via fzx_debug_replay_count. Use this for render comparisons.
