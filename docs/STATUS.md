# Status

Keep under 80 lines. History is in git. Every claim carries its evidence tier.

## Playable extent

Nothing of the original game is playable. The prototype driver (`FZX_SIM=proto`) follows the
sampled centreline of a fixture circuit in the web workbench (HOST). The Saturn image **boots
and renders the fixture circuit in Yabause** with the HUD naming `SIM PROTO` (EMULATOR,
2026-11-14). It cannot be driven there yet: the emulator presents no pad (see Blockers).
HARDWARE: UNRUN. The original racer code runs natively in float in the reference (`ref/`) on a
synthetic walled-road loop with authored (non-retail) tuning: it counts down, launches, drives
about one lap grinding walls, and retires near tick 700 (reference tier).

## Plan position

Gate 0 done. Gate 1 (GO or NO-GO) in progress. Plan: `docs/POC_PLAN.md`.

| Card | State | Evidence |
| --- | --- | --- |
| G0.1 agent files, STATUS, HANDOFF, plan | done | files in tree |
| G0.2 hygiene scan + CI job | done | HOST: planted `.z64` stub and home path fail (`tests/test_hygiene.py`, CI `hygiene`) |
| G0.3 evidence folder, receipts | done | HOST: `tests/test_receipt.py`; Saturn build stops on a hand-edited header |
| G0.4 `FZX_SIM` on HUD and web | done | HOST: core + browser tests; EMULATOR: HUD shows `SIM PROTO (PROTOTYPE, NOT ORIGINAL)` |
| G1.5 emulator lane | done, pad input blocked | EMULATOR: `tools/emu/yabause_run.py`, CI `emulator` job; `docs/EMULATOR.md` |
| G1.4 first boot, render probe | first boot done; budget sweep not started | EMULATOR: boots with and without the 4 MB cart |
| G1.1 closure census | done | static: `docs/census/` (53 TICK, 66 OFFLINE, 188 OUT); control: PORT in racer.c changes every trace |
| G1.2 reference harness | done; event coverage open | reference: T01-T10 identical on GCC/Clang x -O0/-O2, `ref/run_corpus.sh`, CI `reference`; tuning control fails as required |
| G1.3 cost model | not started | |
| G1.6 importer on real ROM | waiting for `FZX_TEST_ROM` | |
| G1.7 fit table, cartridge detect | not started | |
| G1.8 gate report | not started | |

## Measurements

| What | Value | Tier |
| --- | --- | --- |
| Demo circuit: pictures per field, current main loop | 0.50 (PIC 558 / FLD 1117): 30 fps, not 60 | EMULATOR / MODELLED |
| Demo circuit polygons at start pose | 52 | HOST, EMULATOR |
| Saturn image sha256 (this measurement) | `3c278d52…eaeebe` | build, local Yaul image |

## Owner decisions

- 2026-11-13: target 60 fps (NTSC); the 4 MB RAM cartridge is required.

## Blockers

- `BLOCKED_EMULATOR_PAD`: Yabause 0.9.15 reports no pad to the image (`PAD FFFF`). Unblock
  conditions in `docs/EMULATOR.md`. Pad-only lap proofs (G4.3) need this.
- `FZX_TEST_ROM` is not set in this environment; ROM-lane cards wait for it. Retail Blue Falcon
  tuning is ROM-lane too: `Racer_InitMachineStats` reads 38 words of the ROM's first 4 KB.
