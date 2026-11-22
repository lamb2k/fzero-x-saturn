# Attacks outcome judge (C-161): freeze record, DRAFT (not frozen; nothing native has been scored)

Status 2026-11-22: tooling built and exercised on the PILOT namespace with reference arms only. Not frozen: Codex has to confirm
the readings of C-161 (O-170) first. ENG_ATTACKS stays 0. After confirmation this file gets the final hashes and the dev and sealed
manifest commitments, and only then do native runs start. Hashes below are of the files as committed with this draft.

## What exists
- `tools/attacks_outcome.py`: generator (namespace, cell, index give every parameter), episode runner, per-episode outcome metrics.
  Arms: G original reference (closed-loop drivers, records pads, bakes the start), R converted-start reference replaying G's pads,
  S = R plus seeded 1 q start-velocity perturbation applied one tick after the load, C / T = R / S with the closed-loop drivers
  (no recorded pads), N native pair, `--mutate` for the control mutants.
- `tools/attacks_outcome_stats.py`: per-cell metrics, coverage, exact structural checks, paired cell-stratified max-t bootstrap
  (50,000 draws, seed 161161, 95 percent, one family over every applicable cell and metric), statuses pass / fail / inconclusive.
- `tools/attacks_outcome_stats_test.py`: 13 synthetic self-checks, each with an input that makes it fail.
- `tools/attacks_outcome_config.json`: every reading of C-161 as a named parameter (retire per episode, contact unit, damage racers,
  wall-KO window, applicability, zero-variance bound, bands, coverage, controls, fixtures).
- Reference driver extensions (`ref/duel_main.c`, `duel_probe.c`): episode parameters; defaults unchanged, and the default-path
  trace and pair log are byte-identical to the previous binary on 8 scenario and course pairs.

## Hashes (sha256, first 16 hex)
    attacks_outcome.py 9e69990ac401a8dd   attacks_outcome_stats.py bff96bcacd49cd45   attacks_outcome_config.json 410c84218371af2d
    attacks_outcome_stats_test.py 4d570a662ef5e3bd   ref/duel_main.c 9ffbba4bcb00b4a7   ref/duel_probe.c 89103567640c0cf0
    tools/attacks_host/duel_native.c e2c9fae5939917f4 (unchanged from claude/attacks-matched)
Provisional manifest commitments (change if the config changes): dev 3a32b261f8e24b7f..., sealed 15813bb83878835d...

## Readings submitted to Codex (O-170), each a config parameter
1 retire count per field = per 2-racer episode; 2 cells (tube = Sand Ocean, open = Mute City; weight band = tertiles by kg; attacker =
racer 0, the credited human); 3 damage and pre-contact energy for both racers, per contact episode = first contact event, first
energy drop >= 0.01 on a touching tick; 4 wall-KO = credited KO with victim non-contact damage >= 0.01 within 60 ticks before the
KO tick (the credit window); 5 coast-bias fails on the C-116 floor; 6 native spin-out side from its own draw; 7 pads recorded from
the original and replayed open-loop (see the noise-floor result: a closed-loop option is built); 8 driver/CPU attribution reported
as credited KOs against uncredited spin-outs. Added by the pilot: applicability of KO and wall-KO (all maneuvers, wall placement
for wall-KO), bump-maneuver victims start at 0.5 to 3 percent energy so bumps can KO, and the finite-sample bound for zero-variance
binary metrics (Bonferroni binomial; without it every probability that is 1.0 in both arms is inconclusive).

## Pilot results (reference arms only, PILOT namespace, 1,024 episodes per cell, 36,864 episodes)
- Coverage (R arm): every cell has >= 100 contact episodes with damage per racer and >= 50 KO events; wall-KO >= 50 in every
  wall-exposed cell (tube wall cells 5.7 to 8.7 percent).
- Structural checks exact in G, R, S arms after fixing two wrong checks (a KO may credit a contact from before the episode; victim energy
  can rise in a pit strip, about 3 episodes in 36,864: counted, not a check).
- Noise floor, R against S (1 q perturbation, open-loop pads), 306 intervals, critical value 4.0: damage, pre-contact energy, retire pass
  in all cells; hit passes through the zero-variance bound; KO: 13 pass, 1 fail, 22 inconclusive (mean difference -1.4 points, paired
  disagreement 3.4 percent, se 0.6 points); KO time 27 of 36; wall-KO 15 of 18.
- Same against closed-loop drivers (C against T): bias gone (KO mean -0.2 points, KO time +0.4 ticks) but KO still 13 pass, 23
  inconclusive (disagreement 3.5 percent, se 0.6 points): 4.0 x 0.6 = 2.4 points exceeds the 2 point band at 1,024 episodes.
  About 2,000 to 2,500 episodes per cell would be needed for the KO interval to fit at this noise level.
- G against R shifts KO by -2 to -6 points in side and spin cells (open-loop replay of pads timed for G): R, not G, is the reference arm.
