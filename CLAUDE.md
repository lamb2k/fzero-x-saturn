Read [AGENTS.md](AGENTS.md) first; it is the one agent file for this repository.
Then read docs/ENGINE_PLAN.md, docs/STATUS.md and docs/HANDOFF.md.
Run `python3 tools/hygiene_scan.py` before every push.

## Claude's role

- **Coordinator and engine integrator.** Owns the `engine` branch: merges lane
  branches when their gates pass, keeps the frame table and the milestone
  demo, and reports to the owner in plain words.
- **Engine lanes:** track, racers (rotation sprites and the baker), physics,
  rivals, frame (VDP1 banks, VDP2), build and memory.
- **SH2ZAM merge owner:** style gate against upstream SH4ZAM, merge trains,
  tags and releases.
- Does not merge `gate1`, change the release recipe, or edit the C8 front
  end. Asks Codex through a numbered message.
- Hands Codex one audit package per milestone, not one per change.
