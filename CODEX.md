Read [AGENTS.md](AGENTS.md) first; it is the one agent file for this repository.
Then read docs/ENGINE_PLAN.md, docs/RETRO_PLAN_20261120.md (the current phases) and docs/HANDOFF.md.
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
  - front end on the engine: title, select, results, records, name entry,
    Grand Prix results and standings, pause and retry screens, bound through
    saturn/engine/SCENES.md;
  - cars: the roster's sprite sets and their look, including distant sets;
  - guest racers' art and its approval path with the owner;
  - the hardware sheet in docs/HARDWARE_TEST.md and the release disc.
- Does not review each engine merge, and does not hold `engine` for tooling
  or wording findings.
- Keeps the legacy recipe BIN frozen and byte-identical. No speed work on it.

## How Codex works with the engine line (from the 2026-11-20 retrospective)

These kept the two sides fast; keep them.

- **Contract before code.** Review an interface draft once, with numbered
  corrections, and bind only to an agreed draft. Six drafts of SCENES.md in
  one afternoon cost less than one rework.
- **Judges are fixed before measuring.** State the fixture, bands and
  controls prospectively; no retrospective promotion. Rule on root causes the
  engine side brings, not on requests to relax.
- **Lead each reply with the verdict** in one line (accepted, open, or
  blocked, and on what), then the numbered detail.

These cost time; change them.

- **Test the default image.** Run screens and audits with sound on and scenes
  on, as shipped. A sound-off pass missed a load fault that only showed with
  music playing.
- **Base on the current `engine` and say the base.** Merge `engine` into the
  screens branch before reporting it ready, run the gate matrix in AGENTS.md
  on that head, and push it. The integrator then merges it directly, with no
  separate integration lane.
- **Qualify on two courses,** one with tubes, and on real packs. Mute City
  alone has hidden per-course faults.
- **Archive as you go.** Keep evidence that an open gate cites; move the rest
  to the archive drive with a manifest when the system drive runs low.
- **Flag what you did not run** in the same reply: stack watermark, hardware,
  a mode not exercised.
