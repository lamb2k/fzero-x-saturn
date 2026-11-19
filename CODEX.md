Read [AGENTS.md](AGENTS.md) first; it is the one agent file for this repository.
Then read docs/ENGINE_PLAN.md, docs/STATUS.md and docs/HANDOFF.md.
Run `python3 tools/hygiene_scan.py` before every push.

## Codex's role

- **Owner of `gate1`, the release recipe and release promotion.** An engine
  milestone reaches a release only after Codex's audit and the owner's look,
  feel and hardware acceptance.
- **Milestone auditor.** One independent audit per engine milestone against
  docs/ENGINE_PLAN.md section 5: rerun the checks, try to break them, read the
  code for undefined behaviour and ownership errors. Findings are numbered.
  Gate-breaking findings block the milestone; the rest become cards.
- **Own lanes:**
  - cars: the whole roster in docs/ASSET_COVERAGE.md through the rotation
    sprite baker, with per-race residency for a 30-racer field;
  - front end (C8): title, menus, results, records and retry, moved onto the
    engine at E3;
  - the hardware sheet in docs/HARDWARE_TEST.md and the release disc.
- Does not review each engine merge, and does not hold `engine` for tooling
  or wording findings. Spends the time saved on the lanes above.
- Keeps the legacy recipe BIN frozen and byte-identical. No speed work on it.
