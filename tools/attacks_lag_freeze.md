# P1 outcome check: freeze record (lane claude/attacks-cost-3)

Frozen 2026-11-23, before the first lag-arm run of the scored round. Tier: HOST (tools/attacks_host fzx-atk, the engine's own field, attacks
and SH2ZAM sources). Ruling: coordinator, "P1 (one-tick lag of the player's contact share on uncleared second ticks) is approved to build, with the
outcome check exactly as attacks-cost-2 fixed it before measuring". One scoring round and one fix at most; after one fix that still fails: stop and report.

## The approximation (ENG_ATK_LAG, fzx-atk --order lag)
A second tick of a frame (the queue holds exactly one field tick and no contact) whose contact `fzx_atk_player_clear` cannot clear waits behind the
queued field tick like a cleared one. Its contact runs after the slave's kick, in the original order, with every rival's side exact. The player's share
of it (push, damage, regain, hit) is added to the player's contact of the next tick. Cleared second ticks, first ticks (the contact run ahead) and
ticks of 3 or more per frame are as before. The scripted hunter reads the field every tick, so its queue is always empty and it is never lagged
(hunter cells therefore compare exact against exact; the check reports it and verifies it).

## Judge (commit and hashes)
- Judge commit: 2c5e9ee (branch claude/attacks-cost-3; the judge is `tools/attacks_lag_outcome.py` with `tools/attacks_host/attacks_main.c`,
  `engine_attacks.c`, `engine_field.c` at that commit). Nothing in them changes after this record.
- sha256 (first 16 hex): attacks_lag_outcome.py `bf3656e4c319a6ae`, attacks_main.c `331a5392578b960b`.
- Binaries (WSL stage, built from that tree, SH2ZAM e83dae5, closest-point stop 128/8 as the engine): fzx-atk-mute-city-1 `964801a6961a1476`,
  fzx-atk-sand-ocean-1 `916fc9dde83bd408`, fzx-atk-big-blue-1 `d88a4679c45cce2f`.
- Seeds, ticks: `fzx-atk --seed S` replaces the field's and the attacks' seeds (grid pace and lane draws, the CPU attack decisions); 9,460 ticks (460 of
  nothing, 9,000 of race); the engine's default field (E2 pack, behaviour, surface, recharge, class rows off, level-of-detail bands), vis gate on, AI on.

## Cells (12)
course (Mute City 1, Sand Ocean 1, Big Blue 1) x driver (R01 closed loop, matrix gains 0.004,2.5; the scripted hunter, run-time driver 2) x
difficulty (1 STANDARD, 3 MASTER).

## Arms, 200 seeded races per cell each
| arm | order | seeds | role |
| --- | --- | --- | --- |
| A | canonical (exact) | 1..200 | reference |
| B | canonical (exact) | 1001..1200 | second seed set: the exact order's own variation |
| lag | `--order lag` | 1..200 | under test |
| l8 | `--order lag --lag 8` | 1..200 | control: the share reaches the player 8 ticks late |
| drop | canonical, `--lag-drop 1` | 1..200 | control: the player's contact share dropped on every tick |

## Metrics and rule
Per cell, means over the races: rival KOs (rivals spun out, `fzx_atk_report[4]`), rivals retired (`fzx_field_report[12]`), player KOs credited
(`fzx_atk_report[3]`), player damage (`fzx_atk_report[10]` over the player's max energy `config[3]`, a fraction of max energy). Per slot (30): the
distribution of its final place (the field's rank at the last tick, 1 = leader), two-sample Kolmogorov-Smirnov D.
Rule per cell and metric: `|lag - A| <= |B - A| + tolerance`; for the places `D(lag, A) <= D(B, A) + tolerance`.
Tolerances (C-168's: counts as its retirements per episode, probabilities for distributions): rival KOs, rivals retired, player KOs 0.02 a race;
player damage 0.005 of max energy; D 0.005. 12 cells x (4 + 30) = 408 cell-metrics per arm. The check passes when none breaks the rule.
Controls l8 and drop must each break the rule in at least one cell-metric; a control that does not is reported as the check being blind to it.

## Also required, outside the rule
Engine attack words equal HOST-lag's (`fzx-atk --order lag [--first 1]`) on the Jack courses and two others (gate `host` rows).

## Before this record (not scoring runs)
Smoke runs of fzx-atk --order lag on Mute City 1 (R01, default seed, one race each of canonical, saturn and lag) and a 3-seed A/B dry run of the
runner; no lag-arm scoring data existed. Result of the smoke: canonical = saturn bit for bit; lag finished the Mute City R01 race at 105,257 ms against
the exact 104,781 (the grid melee, 27 lagged shares applied).
