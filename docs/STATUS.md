# Status

Limit: 80 lines. History lives in git. Plan: [POC_PLAN.md](POC_PLAN.md).

## Playable extent

Nothing of the original game is playable. The workbench drives a synthetic
test circuit with the **prototype driver** (`FZX_SIM=proto`), which is not the
port. The Saturn image builds in CI and **has never been booted**, on an
emulator or a console. The importer has only run on synthetic records.

## Gates

| Gate | State |
| --- | --- |
| 0 Ground rules | Done in CI (see card table) |
| 1 GO / NO-GO | In progress. G1.1, G1.2 done; G1.3 host half done; next G1.5 emulator lane |
| 2 to 5 | Not started; wait for Gate 1 |

### Gate 0 cards

| Card | State | Evidence |
| --- | --- | --- |
| G0.1 agent file, STATUS, HANDOFF, plan, README pointer | Done | Files in tree |
| G0.2 hygiene scan and CI job | Done | `tools/hygiene_scan.py`; CI job `hygiene`. Controls in `tests/test_hygiene.py`: a planted `.z64` stub, ROM headers in three byte orders and four home-path forms each make the scan fail. HOST |
| G0.3 evidence folder, generation receipts | Done | `$FZX_EVIDENCE` convention in AGENTS.md. Importer writes `course.receipt`; `tools/check_receipt.sh` checks it; `saturn/Makefile` refuses a stale header. Control: a hand-edited header fails in `tests/test_import_course.py` (HOST) and stops `make -C saturn FZX_IMPORTED=1` in CI (Yaul container) |
| G0.4 `FZX_SIM` selector shown | Done | `fzx_sim_id()` in the core; SIM line on the Saturn HUD (built, unbooted, so not yet seen); web shows it, checked by `web/test-browser.py`. Only `proto` exists; any other value is a build error |

### Gate 1 cards

| Card | State | Evidence |
| --- | --- | --- |
| G1.1 closure census | Done | [census/SUMMARY.md](census/SUMMARY.md): 40 TICK functions (4,450 lines), 95 OFFLINE, 8 OUT. Mode `GAMEMODE_TIME_ATTACK`, tick order from `Race_Update`. Control: `--define PORT` finds 91 fork-hook calls in the closure; the stock build finds 0. HOST |
| G1.2 reference harness | Done | [ref/README.md](../ref/README.md): the original stock race code built natively in binary32 SSE float, driven through the original `Controller_UpdateInputs` and `Racer_Update`. T01 to T10 on the synthetic oval give the same trace hash with gcc and clang at -O0 and -O2, and match `ref/expected/fixture.hashes`. Control: one bit of one machine constant moves the T02 hash. Found on the way: clang needs `-fno-unsafe-math-optimizations` beyond `-fno-fast-math` (1 ulp start-position drift). CI job `reference`. HOST |
| G1.3 tick cost model | In progress | [cost/SUMMARY.md](cost/SUMMARY.md): `ref/cost.sh` counts every binary32 op per tick by kind and function (callgrind, gcc -O0 attribution, -O2 cross-check). Fixture: 539 float ops / 3,570 x86 instrs per live tick; MODELLED with SH2ZAM per-call costs, mean 9.6%, p95 13.5%, max 15.6% of the 8 ms master line (worst tick incl. no-energy ticks 63k float cycles, 28%). `Racer_UpdateFromControls` is 59% of it. HOST counts, MODELLED cycles. Not done: HWRAM vs cartridge microbenchmark and the convert-one-function cross-check, both need the G1.5 emulator lane. Fixture machine has energy on only 775 of 9,987 race ticks; rerun on Mute City 1 at G1.6 |

## Evidence tiers held

- HOST: core tests (GCC, Clang, sanitizers), importer and hygiene tests, reference harness determinism (ref/check.sh), per-tick op counts (ref/cost.sh).
- MODELLED: G1.3 fixed-point cycles per tick (SH2ZAM model costs; no cache or bus).
- EMULATOR: none. HARDWARE: UNRUN.

## Owner inputs outstanding

- `FZX_TEST_ROM` location, needed before G1.6 (plan section 7, item 1).

## Blockers

None.
