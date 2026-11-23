# P1 outcome check v2: freeze record (lane claude/attacks-cost-4, on claude/attacks-cost-3)

Frozen 2026-11-23, before any lag-arm, l8-arm or drop-arm run of this check. Tier: HOST (tools/attacks_host fzx-atk, the engine's own field,
attacks and SH2ZAM sources). Ruling: coordinator, "the P1 outcome check is underpowered ... the one allowed fix: fix the check's power, not its
tolerances, and freeze it BEFORE scoring lag again".

## Why v1 (tools/attacks_lag_freeze.md, 2c5e9ee) is replaced
With 200 races a cell the standard error of a mean count (0.03 to 0.05) was above the 0.02 tolerance, 360 place-KS rows each compared against a noisy
B-A, and an exact arm on a third seed set broke 161 of 408 rows. 1-tick and 8-tick lag were indistinguishable (98 vs 110 breaks). v1's result says nothing about P1.
Its lag/l8/drop data are not used again. No lag-arm race of v2 existed when this record was written (the only lag races ever run are v1's, seeds 1..200, other seed sets).

## Check (tools/attacks_lag_outcome2.py, sha256 first 16 hex `c845540e82d96043`)
- Cells (6): R01 closed loop (matrix gains 0.004,2.5) x Mute City 1, Sand Ocean 1, Big Blue 1 x difficulty 1 and 3. Hunter cells dropped (the hunter never lags).
- Races: 9,460 ticks (460 of nothing, 9,000 of race), default field and attacks at the seed (fzx-atk --seed S), the same binaries as v1
  (fzx-atk-mute-city-1 `964801a6961a1476`, fzx-atk-sand-ocean-1 `916fc9dde83bd408`, fzx-atk-big-blue-1 `d88a4679c45cce2f`, SH2ZAM e83dae5, closest-point stop 128/8).
- **N = 20,000 races per cell and arm** (blocks of 5,000, blocks 0 to 3).
- Arms, each on its own seed set (no two arms share a seed, so every comparison carries the same noise; v1's lag shared seeds with A, which favoured it):
  a exact seeds 1..N; b exact 1,000,001..; c exact 2,000,001.. and d exact 3,000,001.. (calibration only);
  lag `--order lag` 4,000,001..; l8 `--order lag --lag 8` (control) 5,000,001..; drop canonical `--lag-drop 1` (control) 6,000,001...
- Metrics per cell (5): rival KOs (spun out), rivals retired, player KOs credited, player damage (fraction of max energy), and ONE place statistic
  P = mean over the 30 slots of the two-sample KS D of the slot's final place (replaces 30 separate KS tests). 6 cells x 5 = 30 rows.
- Rule (unchanged): |X - A| <= |B - A| + tolerance, for P: P(X,A) <= P(B,A) + tolerance. Tolerances (unchanged, C-168's): counts 0.02 a race, player damage
  0.005 of max energy, P 0.005. Slack of a row = (|X-A| - |B-A|) / tolerance; the rule breaks above 1.0.
- Pass: no row breaks. Controls: l8 and drop must each break at least one row; if l8 does not, the check is still too weak: stop and report.

## Calibration (exact arms only; no lag-class arm looked at; evidence host\claude-acost3\oc2\calibration-n*.txt)
Rule applied to every ordered triple (X, A, B) of the exact arms a, b, c, d (24 triples x 30 rows = 720 tests). Required: 0 break and worst slack <= 0.75.
| N a cell and arm | rows breaking | worst slack | verdict |
| --- | --- | --- | --- |
| 5,000 | 0 | 0.710 | pass |
| 10,000 | 1 (Mute City 1 d3 rivals retired, 1.045) | 1.045 | FAIL |
| 15,000 | 0 | 0.527 | pass |
| 20,000 | 0 | 0.452 | pass |
N = 20,000 is the largest calibrated size, and 15,000 and 20,000 both pass; 5,000 passed by luck (10,000 fails). At 20,000 the worst slack is below half the break point.
The 24 permutations, not one X: an exact arm cannot be told apart from lag by one draw, so the calibration holds every ordering of the four.

## Run
`oc2run.sh oc2 run --arms lag,l8,drop --blocks 0-3` then `oc2run.sh oc2 score --blocks 0-3` (score.txt = score-n20000.txt). Scored once. Pass: P1 stays default on.
Fail: `ENG_ATK_LAG ?= 0`, the gate's HOST reference to `--order canonical`, report.
