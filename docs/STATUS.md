# Status

Keep under 80 lines. History is in git. Every claim carries its evidence tier.

## Playable extent

Nothing of the original game is playable. The prototype driver (`FZX_SIM=proto`) follows the
sampled centreline of a fixture circuit in the web workbench (HOST). The Saturn image builds in
CI and has **never been booted** (EMULATOR: UNRUN, HARDWARE: UNRUN).

## Plan position

Gate 0 (ground rules), in progress. Plan: `docs/POC_PLAN.md`.

| Card | State | Evidence |
| --- | --- | --- |
| G0.1 agent files, STATUS, HANDOFF, plan | done | files in tree |
| G0.2 hygiene scan + CI job | done | HOST: planted `.z64` stub and home path fail the scan (`tests/test_hygiene.py`, CI `hygiene` job) |
| G0.3 evidence folder, generation receipts | not started | |
| G0.4 `FZX_SIM` selector on HUD and web | not started | |

## Owner decisions

- 2026-11-13: target 60 fps (NTSC); the 4 MB RAM cartridge is required.

## Blockers

None yet. `FZX_TEST_ROM` is not set in this environment; ROM-lane cards wait for it.
