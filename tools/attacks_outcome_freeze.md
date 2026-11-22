# Attacks outcome judge (C-168, O-175): freeze record

Frozen 2026-11-22 by attacks-5 before any native run. Tier: HOST. ENG_ATTACKS stays 0. One scoring round: freeze, controls, native once.

## What is scored
- One closed-loop run of 2,400 paired starts per cell, 36 cells (maneuver side/spin/bump, environment open = Mute City / tube = Sand Ocean,
  placement wall/interior, attacker weight tertile) = 86,400 episodes, namespace `fzx-attacks-outcome/c168/v1`, manifest sha256
  `4dd79d46d5936eb52fa2c0f3219797b6996b816530cd89b4d6e3fdbd736026af`.
- Both arms use the same driver policy from identical converted starts. G (the original from tick 0) only bakes the start and the drivers' memory
  at T0 = 600. C = the original code from the converted start (reference arm). T = C plus the seeded 1 q start-velocity nudge one tick after the
  load. N = the native pair (SH2ZAM vehicle-dynamics-3 and body-contact-1) from the same packs, with the reference's scripted drivers ported line for
  line into `tools/attacks_host/duel_native.c --drive` (floats as in the original driver).
- Rule per cell and metric: pass when |N - C| <= |T - C| + tolerance. No intervals, no bands. Tolerances: 0.5 pp for hit, KO and wall-KO
  probabilities; 0.005 of max energy for damage and pre-contact energy; 2 ticks for time to KO; 0.02 retirements per episode.

## Readings taken (simplest reading, C-168 left them open)
1. Closed loop, so no pad replay; the reference arm is C. Native runs the ported driver on its own state.
2. Damage and pre-contact energy are already fractions of the machine's max energy, as a ratio of sums over damaging-contact episodes per cell.
3. Time to KO = mean ticks from T0 to the first credited KO over the episodes that have a KO in that arm (the KO share is its own metric).
4. Wall-KO = credited KO with non-contact (wall) damage on the victim in the 60 ticks up to the KO tick. Cells, retire counting, contact unit,
   applicability (config) are as in the draft readings; all are named parameters in the config.
5. Changes to the reference driver since the draft: the converted state is loaded before the driver's first decision (so C and N decide tick T0+1
   from the same state); `--drv-out` records the driver's tap memory at T0. Default (no load) runs are unchanged.
6. Controls: a mutant passes when, in some cell where its mechanism is exercised, one of its mapped metrics fails the rule against C (same T).
   `no-destroy` and `coast-bias` map to no metric (their fixture and C-116 floor belonged to the dropped judge); they run and are reported only.

## Hashes (sha256, first 16 hex), repo commit e36fbbf
    tools/attacks_outcome.py 52f9cd3f54c4c4ee   tools/attacks_outcome_stats.py 1915cb82ee137589   tools/attacks_outcome_config.json 84fea586dd098142
    tools/attacks_outcome_stats_test.py 2bc0b7c4a3250ae9 (15 self-checks, each with an input that fails)
    ref/duel_main.c 772f1aa20dc62a3a   ref/duel_probe.c 89103567640c0cf0   tools/attacks_host/duel_native.c d5a8d1e6daf1684b
    tools/attacks_host/duel_build.sh e5d8415d05df312a
    binaries (WSL stage): fzx-ref 21b866a26200ec29, fzx-duel-native 82c98514c06560bd, fzx-duel-native-tubes 03220a11f934679c
    reference episodes (C and T arms, 86,400): episodes-score-CT.jsonl sha256 5675e702b83d38a7 (local, ROM-derived)

## The reference's own variation, measured (C against T, 2,400 per cell, no native run)
| metric | cells | mean abs(T - C) | max abs(T - C) | tolerance |
| --- | --- | --- | --- | --- |
| hit | 36 | 0.0001 | 0.0012 | 0.005 |
| KO | 36 | 0.0020 | 0.0075 | 0.005 |
| time to KO (ticks) | 36 | 1.10 | 5.94 | 2.0 |
| retire per episode | 36 | 0.0024 | 0.0108 | 0.02 |
| damage, victim | 36 | 0.0004 | 0.0019 | 0.005 |
| damage, attacker | 36 | 0.0001 | 0.0005 | 0.005 |
| pre-contact energy, victim | 36 | 0.0010 | 0.0038 | 0.005 |
| pre-contact energy, attacker | 36 | 0.0019 | 0.0076 | 0.005 |
| wall-KO | 18 | 0.0005 | 0.0025 | 0.005 |
Per cell values: evidence `out/bars-C-T.txt` and `.json`. Coverage (info only): every cell has well over 1,000 damaging contact episodes per racer
(see `out/coverage-C.txt`).
