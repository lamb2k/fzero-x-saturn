# Handoff

Limit: 50 lines. Replace this file each session; do not append.

## Last session (2026-11-15)

- G1.2 committed (reference harness, `ref/check.sh` PASS).
- G1.3 host half: `ref/cost.sh` runs the reference under valgrind callgrind with
  one dump per `ref_tick`; `ref/cost_model.py` classes each SSE instruction and
  writes [cost/SUMMARY.md](cost/SUMMARY.md). Simulation looks small next to the
  8 ms master line (about 10 to 16% modelled on the fixture), but the fixture
  machine has no energy after a few wall hits, so most race ticks are atypical.

## Next card

**G1.5 emulator lane**, then finish G1.3 and do G1.4. The local Ymir tree
(`C:\Game Dev\Emulators\Ymir-baseline-a9f0cdae`, MSVC build in
`out/build/Full-x86-64-AVX2-MSVC-Release`) has a stub `ymir-headless`.
ymir-core has what a runner needs: `Saturn::LoadIPL`, `media::LoadDisc` +
`Saturn::LoadDisc`, `InsertCartridge<cart::DRAM32MbitCartridge>()`, and
`RendererCallbacks::VDP2DrawFinished`. Plan: a small out-of-tree runner
(boot ISO, run N frames, write a PNG and read a pass marker from WRAM).
Ymir is GPL-3: do not vendor it into this MIT repo.

The ISO comes from the CI artifact (no Yaul locally).

## Notes for the next agent

- WSL's default user is root; evidence lives in `/root/fzx-evidence` unless
  `FZX_EVIDENCE` is set. valgrind is installed in WSL.
- From Git Bash, call WSL with `MSYS_NO_PATHCONV=1`, or paths get mangled.
- The cost model's integer part (non-float x86 instructions at one cycle
  each) is a proxy. The cross-check on one converted function settles it.
- The Saturn HUD SIM line still has not been seen: check it at first boot.
