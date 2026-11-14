# Status

Keep under 80 lines. History is in git. Every claim carries its evidence tier.

## Playable extent

Nothing of the original game is playable. The prototype driver (`FZX_SIM=proto`) follows the
sampled centreline of a fixture circuit in the web workbench (HOST). The Saturn image builds in
CI and has **never been booted** (EMULATOR: UNRUN, HARDWARE: UNRUN).

## Plan position

Gate 0 (ground rules) done; Gate 1 (GO or NO-GO) next. Plan: `docs/POC_PLAN.md`.

| Card | State | Evidence |
| --- | --- | --- |
| G0.1 agent files, STATUS, HANDOFF, plan | done | files in tree |
| G0.2 hygiene scan + CI job | done | HOST: planted `.z64` stub and home path fail the scan (`tests/test_hygiene.py`, CI `hygiene` job) |
| G0.3 evidence folder, generation receipts | done | HOST: `tests/test_receipt.py`; Saturn build (local Yaul image) stops on a hand-edited `generated/course.h` |
| G0.4 `FZX_SIM` selector on HUD and web | done (web); Saturn coded, display UNRUN | HOST: core test + browser test show PROTO; Saturn: HUD string linked, seen on screen at G1.4 |

## Owner decisions

- 2026-11-13: target 60 fps (NTSC); the 4 MB RAM cartridge is required.

## Blockers

None yet. `FZX_TEST_ROM` is not set in this environment; ROM-lane cards wait for it.
