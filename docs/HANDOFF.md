# Handoff

Limit: 50 lines. Replace this file each session; do not append.

## Last session (2026-11-15)

- G1.2 committed; G1.3 host half (op counts, modelled master cost about 10 to
  16% of the 8 ms line on the fixture; see [cost/SUMMARY.md](cost/SUMMARY.md)).
- G1.5 done: `tools/emu/yaba_boot.ps1` boots the CI ISO on the owner's
  Yaba Sanshiro MCP build and passes. First boot recorded (G1.4 half).

## Next card

**G1.4 render probe.** Use the MCP tools (`memory_read`, `breakpoint_add`,
`framebuffer_burst`, `set_debug_option`) on a probe image:
1. per-field split (simulation, transform, command build, VDP1 completion):
   add field-stamped counters to `saturn/main.c` behind a knob and read them.
2. polygon count and coverage sweep: a probe mode that draws N quads of a set
   size; find the largest N that VDP1 finishes inside a field.
3. control: an oversized command list must trip the capacity assert.
Yaba is not cycle-exact: label every timing MODELLED. The ISO only comes
from CI (`gh run download <id> -n fzero-x-saturn-native`).

Then finish G1.3: microbenchmark SH2ZAM ops in HWRAM vs cartridge and convert
one function (`Math_OrthonormalizeAroundUp` is small) as the cross-check.

## Notes for the next agent

- Emulator: set `FZX_YABA_EXE` (owner's local Yaba MCP build; the path is in
  agent memory, not here). The script edits the shared `yabause.ini` and
  restores it; never leave it changed. Stop the emulator before rebuilding.
- The Vulkan video core (4) shows "Cannot initialize Video" on this host;
  the script uses OpenGL (1). The error dialog hides behind other windows:
  read it with UI Automation, not by guessing.
- Without `FZX_EVIDENCE`, evidence goes to `~/fzx-evidence` (WSL runs as
  root, so the WSL and Windows folders differ). valgrind is installed in WSL.
- From Git Bash, call WSL with `MSYS_NO_PATHCONV=1`.
- 3D assets: headless Blender is allowed for conversion and LOD, gated on
  parity with the source (counts and still pairs); augmentation behind a knob.
