# Status

Keep under 80 lines. History is in git. Every claim carries its evidence tier.

## Playable extent

Nothing of the original game is playable. The prototype driver (`FZX_SIM=proto`) follows the
sampled centreline of a fixture circuit in the web workbench (HOST). The Saturn image **boots
and renders the fixture circuit in Yabause** with the HUD naming `SIM PROTO` (EMULATOR,
2026-11-14). It cannot be driven there yet: the emulator presents no pad (see Blockers).
HARDWARE: UNRUN. The original racer code runs natively in float in the reference (`ref/`) on a
synthetic walled-road loop with authored (non-retail) tuning: it counts down, launches, drives
about one lap grinding walls, and retires near tick 700 (reference tier). With the ROM it also
runs real Mute City 1 with retail Blue Falcon tuning: launch at GO, cruise near speed 33, walls
at the first turn without steering; with bot steering it races three laps to the finish in
6,247 ticks from GO, about 104 s, no boosting (reference, 2026-11-14). The Saturn image boots Mute City 1's
layout with prototype handling (EMULATOR); the web workbench drives it locally (HOST).

## Plan position

Gate 0 done. Gate 1 (GO or NO-GO) in progress. Plan: `docs/POC_PLAN.md`.

| Card | State | Evidence |
| --- | --- | --- |
| G0.1 agent files, STATUS, HANDOFF, plan | done | files in tree |
| G0.2 hygiene scan + CI job | done | HOST: planted `.z64` stub and home path fail (`tests/test_hygiene.py`, CI `hygiene`) |
| G0.3 evidence folder, receipts | done | HOST: `tests/test_receipt.py`; Saturn build stops on a hand-edited header |
| G0.4 `FZX_SIM` on HUD and web | done | HOST: core + browser tests; EMULATOR: HUD shows `SIM PROTO (PROTOTYPE, NOT ORIGINAL)` |
| G1.5 emulator lane | done, pad input blocked | EMULATOR: `tools/emu/yabause_run.py`, CI `emulator` job; `docs/EMULATOR.md` |
| G1.4 first boot, render probe | first boot and 30 fps cause done; polygon/fill sweep open | EMULATOR/MODELLED: prototype `fzx_render` takes ~447k of 450k cycles per picture (52 polys); HUD, sync mode and VDP1 interval ruled out |
| G1.1 closure census | done | static: `docs/census/` (53 TICK, 66 OFFLINE, 188 OUT); control: PORT in racer.c changes every trace |
| G1.2 reference harness | done | reference: T01-T10 identical on 4 builds (CI `reference`); ROM lane M01-M05 identical on 4 builds; boost, dash, jump, wall, pit and a 3-lap finish covered |
| G1.3 cost model | model and emulator control done; cartridge cost open (no cart in emulator) | MODELLED: Mute City 1 mean 782 float ops/tick, 32k cycles = 14% of 8 ms (max 21%); EMULATOR control: one function, model 2.2k vs measured 1.9k cycles (`saturn/bench`, `docs/cost/`) |
| G1.6 importer on real ROM | import and real-ROM test pass; shape census done; reference comparison open | ROM: US rev0 fingerprint verified; Mute City 1 23 points, checksum verified; shapes road, walled road, tunnel, air |
| G1.7 fit table, cartridge detect | not started | |
| G1.8 gate report | not started | |

## Measurements

| What | Value | Tier |
| --- | --- | --- |
| Demo circuit: pictures per field, current main loop | 0.50: 30 fps; cause: prototype renderer CPU ~447k cycles/picture, ~8.6k per polygon | EMULATOR / MODELLED |
| Mute City 1 tick float work (original code) | 783 ops, 32k cycles = 14% of 8 ms master line (max 21%) | MODELLED (`docs/cost/`) |
| Demo circuit polygons at start pose | 52 | HOST, EMULATOR |
| Saturn image sha256 (this measurement) | `3c278d52…eaeebe` | build, local Yaul image |

## Owner decisions

- 2026-11-13: target 60 fps (NTSC); the 4 MB RAM cartridge is required.

## Blockers

- `BLOCKED_EMULATOR_PAD`: Yabause 0.9.15 reports no pad to the image (`PAD FFFF`). Unblock
  conditions in `docs/EMULATOR.md`. Pad-only lap proofs (G4.3) need this.
- ROM lane: the owner's US rev0 ROM is kept in the owner's private ROM store (outside this
  repo); sessions fetch it into scratch (docs/HANDOFF.md). Retail tuning comes from `ref --rom`.
