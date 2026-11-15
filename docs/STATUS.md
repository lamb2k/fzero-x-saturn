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
| 1 GO / NO-GO | Not started. Next card: G1.1 closure census |
| 2 to 5 | Not started; wait for Gate 1 |

### Gate 0 cards

| Card | State | Evidence |
| --- | --- | --- |
| G0.1 agent file, STATUS, HANDOFF, plan, README pointer | Done | Files in tree |
| G0.2 hygiene scan and CI job | Done | `tools/hygiene_scan.py`; CI job `hygiene`. Controls in `tests/test_hygiene.py`: a planted `.z64` stub, ROM headers in three byte orders and four home-path forms each make the scan fail. HOST |
| G0.3 evidence folder, generation receipts | Done | `$FZX_EVIDENCE` convention in AGENTS.md. Importer writes `course.receipt`; `tools/check_receipt.sh` checks it; `saturn/Makefile` refuses a stale header. Control: a hand-edited header fails in `tests/test_import_course.py` (HOST) and stops `make -C saturn FZX_IMPORTED=1` in CI (Yaul container) |
| G0.4 `FZX_SIM` selector shown | Done | `fzx_sim_id()` in the core; SIM line on the Saturn HUD (built, unbooted, so not yet seen); web shows it, checked by `web/test-browser.py`. Only `proto` exists; any other value is a build error |

## Evidence tiers held

- HOST: core tests (GCC, Clang, sanitizers), importer and hygiene tests.
- EMULATOR: none. HARDWARE: UNRUN. MODELLED: none.

## Owner inputs outstanding

- `FZX_TEST_ROM` location, needed before G1.6 (plan section 7, item 1).

## Blockers

None.
