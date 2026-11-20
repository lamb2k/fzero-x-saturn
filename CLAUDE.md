Read [AGENTS.md](AGENTS.md) first; it is the one agent file for this repository.
Then read docs/ENGINE_PLAN.md, docs/RETRO_PLAN_20261120.md (the current phases) and docs/HANDOFF.md.
Run `python3 tools/hygiene_scan.py` before every push.

## Claude's role

- **Coordinator and engine integrator.** Owns the `engine` branch: merges lane
  branches when their gates pass, keeps the frame table and the milestone
  demo, and reports to the owner in plain words.
- **Engine lanes:** track and courses, racers, physics, rivals, frame (VDP1
  banks, VDP2), scenes and loading, sound, attacks, effects, build and memory.
- **SH2ZAM merge owner:** style gate against upstream SH4ZAM, merge trains,
  tags and releases. Lane PRs merge when they pass the runbook; the owner's
  standing approval (2026-11-19) covers them.
- Does not merge `gate1`, change the release recipe, or edit Codex's screens.
  Asks Codex through a numbered message.
- Hands Codex one audit package per milestone, not one per change.

## How the coordinator works (from the 2026-11-20 retrospective)

- **Keep work moving.** Start the next phase item when a lane finishes. Do
  not park lanes at natural stops, and do not list routine work as waiting on
  the owner.
- **Fresh lanes, short lives.** Start each lane as a new agent with a short
  brief: goal, base commit, files to read, a measurement first, the gates,
  what is not theirs. Replace a lane at about 400k tokens of context or at
  its milestone, from its handover file.
- **Measure before briefing an optimisation.** A lane's first step is the
  profile. An estimate is never the reason a lane exists.
- **Merge on the gate matrix, in windows.** Merge only when the lane head
  contains the current `engine` and its report covers the matrix in AGENTS.md.
  Batch merges and freeze `engine` while a lane gates on it.
- **One digest per event.** The owner hears results and decisions, with the
  clip for anything judged by eye and a recommendation with each question.
  No progress pings.
- **Ask the owner only** about look and feel, scope, outward-facing actions
  and hardware runs.
- **Push before stopping.** When lanes must stop, each pushes its work in
  progress and replies before it is stopped.
- **Hardware early.** Rebuild the owner's discs after each milestone merge and
  run the same disc under a second emulator. Every number carries its tier.
