# Agent rules

This file is the one agent file for this repository. [CLAUDE.md](CLAUDE.md)
and [CODEX.md](CODEX.md) only add each agent's role. The plan for new work is
[docs/ENGINE_PLAN.md](docs/ENGINE_PLAN.md). [docs/POC_PLAN.md](docs/POC_PLAN.md)
is the record of the legacy line; its memory, evidence and source-art rules
still apply.

## Two lines

Owner decision, 2026-11-19: the game becomes a hybrid sprite and native
Saturn engine built from the original assets, Saturn exclusive, with 30
frames per second as the floor.

| | `engine` line | `gate1` legacy line |
| --- | --- | --- |
| What it is | its own Saturn image and main loop; links no original game code | the original race code with native presentation |
| Role | the game we ship; all new work | frozen fallback, HOST reference, front end until E3 |
| Physics gate | plays-right checks against the HOST reference | exact replay, finish at tick 7059 |
| Change gate | engine gates, ENGINE_PLAN section 5 | switch-off binary identity |

Do not add engine features as switches inside the legacy image, and do not
tune the legacy image for speed. If a task asks for either, stop and say so.

## Boot set

At the start of every session read, in this order, and nothing else first:

1. this file, then your role file (CLAUDE.md or CODEX.md);
2. [docs/ENGINE_PLAN.md](docs/ENGINE_PLAN.md): the plan, the milestones and the gates;
3. [docs/RETRO_PLAN_20261120.md](docs/RETRO_PLAN_20261120.md): where the engine
   line stands and the phases from here (P0 to P5);
4. [docs/STATUS.md](docs/STATUS.md): the legacy line's status;
5. [docs/HANDOFF.md](docs/HANDOFF.md): what the last session left.

Read source files when a card needs them. Old checkpoint prose reads like
instructions and pulls work off course.

## Work

- **Owner clarification, 2026-11-16: Saturn preservation takes priority over exact parity.**
  Approximate behavior or similar visual/audio quality is authorized when exact
  source behavior is impractical on Saturn. Preserve the game's identity and
  playable experience; document adaptations and compare them locally. Do not
  block useful native implementations on bit-exact rendering or request repeated
  permission for these adaptations. Source assets remain the starting material;
  placeholders and missing features do not become completed content.
  Asset clarification: prebake conversions offline; assets should remain original
  or be made Saturn-compatible. Approximation does not authorize replacement art.
- **Owner direction, 2026-11-15: the real game on Saturn, preserving source art.**
  Original machine models, track materials, scenery, effects, HUD/fonts, menus
  and audio are port content, extracted locally from the owner's ROM. Placeholder
  art is for synthetic tests only and cannot satisfy a playable-port milestone.
  Preserve source appearance and behavior through native conversion; do not
  replace them with newly designed assets. See POC_PLAN section 4.7.
- **Owner direction, 2026-11-19: plays right, Saturn exclusive, everything in SH2ZAM.**
  Native systems are judged on playing right against the HOST reference, not
  on bit-exact replays. Use the Saturn's hardware directly: VDP1 sprites, VDP2
  layers, both SH-2s, the SCU DSP, the 4 MB cartridge. All engine code is an
  SH2ZAM component with a generic name; this repository keeps glue, game data
  and the asset bakers.
- Work in vertical slices. A milestone in ENGINE_PLAN section 4 is done when
  it runs, its gates pass with each control shown failing, and the owner has
  seen stills, a clip and frame numbers. "Coded, unrun" is a valid status;
  write it.
- Judge every change on the whole presented frame against the floor: at most
  two 60 Hz fields between new frames, across a 30-racer race. Report mean,
  p99, worst interval and the count over budget.
- Measure before optimising. Prefer removing work, baking it offline or
  moving it to idle hardware over making slow code faster.
- A failing gate gets two distinct hypotheses tested. Then record the blocker
  and its exact unblock condition in STATUS and move to the next
  dependency-safe card. Never loosen a tolerance to pass.
- Divergence: find the first divergent tick and field, then the function
  that wrote it. Fix there.
- Engine line: the engine owns simulation, state and presentation. The
  original code is the HOST reference that the engine is tested against, and
  the source of data and tuning values. It does not run on the Saturn.
- Legacy line only: source owns simulation and state, the native layer owns
  presentation, and any change lands behind a knob that defaults off with
  knob-off identity. The legacy recipe BIN must keep building byte-identical.
- No emulation layer: no libultra layer, no display-list interpreter, no
  general float-to-fixed translator. Write the Saturn form directly.

## Workflow

- **Lanes.** One owner per lane, one branch per lane, isolated build stages,
  builds one after another inside a stage. A lane never edits another lane's
  code; it asks through the coordinator. A lane is a fresh agent with a short
  brief: the goal, a pushed base commit, the files to read, the gates and what
  is not its work. It pushes at every report. It hands over at its milestone
  or at about 400k tokens of context, whichever comes first, with a handover
  file that a new agent can start from.
- **Measure first.** An optimisation lane's first step is a profile of the
  thing it is asked to speed up, reported before any change. If the profile
  contradicts the brief, the lane stops for a new brief.
- **Gate matrix.** Run on the lane head merged with the current `engine`:
  - switch-off identity of every default image, and the legacy recipe BIN;
  - cadence on the locked 30 fps loop: every interval two fields, with master,
    slave and critical-path (master busy plus its wait for the track) mean,
    p99 and maximum;
  - plays right: HOST equals SH-2 at the checkpoints and the final state, and
    the course's finish against its HOST reference;
  - determinism over two runs;
  - the default image as shipped, and with sound off, and with scenes off;
  - the scenes loop: cold race and retry equal, menus at the floor, soft reset
    from every scene, a load cancelled at three points;
  - two courses for anything in the race, one of them with tubes, on real
    packs, never stand-ins;
  - memory floors and maps; every check shown failing on its control.
- **Merging.** The engine integrator merges a lane when its head contains the
  current `engine` and its report covers the matrix. No review round is needed
  per change. Merges go in batches, and `engine` is frozen while a lane gates
  on it. `gate1` and the release recipe stay with Codex.
- **Review.** One independent audit per milestone, by the agent that did not
  write the code, against ENGINE_PLAN section 5. Findings become cards fixed
  before the next milestone. A finding that breaks a gate blocks the
  milestone; a tooling or wording finding does not block a merge.
- **Judges.** Fixtures, bands and controls are agreed before measuring and are
  not changed after seeing results. Handling and contact are judged in short
  windows started from the reference's own state, with a failing mutation per
  event class; whole-race comparison is a guardrail only. An exception is the
  owner's to grant, by name.
- **Contracts.** An interface two lanes or two agents build against is written
  down and agreed as a numbered draft before either side binds to it.
- **Tools.** One gate script and one set of scorers live in the repository and
  every lane runs the same ones; a lane that needs more extends them. Tools
  need a failing control, not a review round, before use.
- **Hand-overs.** One message per milestone or per blocking question. Lead
  with the verdict in one line, then the commit and its base, the image
  hashes, the numbers with their tier, what was not run, and the evidence
  path. Number them. A message with no reply is not agreement.
- **Budgets.** Every hand-over states the lane's work RAM, VRAM, cartridge
  and stack use. The shared floors are in ENGINE_PLAN section 5; a lane that
  would cross one frees the space first.
- **SH2ZAM.** Components land on `claude/<topic>` or `codex/<topic>` branches
  with a pull request, an `*_experimental.h` contract tag that no open pull
  request already uses, host tests in CI, upstream SH4ZAM style and no libc
  memory calls in library objects. A pull request merges when it passes the
  integration runbook; the owner's standing approval (2026-11-19) covers
  these. The engine pins a commit on SH2ZAM main.
- **Demos.** Every milestone ships labelled emulator stills, a short clip and
  the frame table. Anything the owner judges by eye goes as a side-by-side
  clip of the same course and ticks. Say what looks wrong.
- **Hardware.** Each milestone's disc is rebuilt for the owner after the
  milestone merge, with the sheet in docs/HARDWARE_TEST.md, and run under a
  second emulator as a cross-check. No frame-rate claim without a hardware run.
- **Owner.** One digest per milestone event or decision, in plain words, with
  a recommendation for each question. Routine merges, SH2ZAM trains, discs and
  starting the next phase item need no approval and are not reported as
  waiting.

## Known traps

Each of these cost a round on 2026-11-19 and 20.

- A name is not a meaning: check a flag or counter in the reference source
  before reasoning from it, and check a profile against a frame trace.
- Generated assets go stale when a merge touches their generator. Check the
  receipts in a reused stage and regenerate with the tool.
- A change to a shared tool under `tools/` can break the legacy recipe.
  Rebuild it.
- An image can change through link-time layout alone when the SH2ZAM pin
  moves. Compare symbols before calling it a behaviour change.
- A system that passes on Mute City can pass by coincidence. Its rival pace
  target and its start grid are special cases.
- A cost measured with stand-in art or a made-up grid is not the cost.
- Sound moves the disc drive. Any disc read path needs a sound-on run.
- A per-CPU time cap is not a frame budget; the two CPUs' work can exceed two
  caps on the same frame. Budget the critical path.
- Stage scripts start with `set -euo pipefail`; commits use LF line endings.

## Evidence

- Tiers never promote each other: HOST, EMULATOR, HARDWARE, MODELLED.
  Emulator frame timings are MODELLED. HARDWARE is UNRUN until the owner runs it.
- Evidence lives outside git in `$FZX_EVIDENCE`, one subfolder per lane
  (`host/`, `emu/`, `rom/`, `cost/`). Commit hashes and counts, never
  retail-derived traces, stills or data.
- Generated files carry a receipt (`*.receipt`, written by the generator).
  Builds refuse output that does not match it. Regenerate; never hand-edit.
  Re-check any odd bug on a clean build first.
- The simulation in use is always visible (`FZX_SIM`, shown on the Saturn
  HUD and in the web workbench). The prototype driver is never the port.

## Repository safety

This repository is public: every push is a publication.

Owner authorization (2026-11-15): push completed, evidenced source-only changes to
`origin/gate1` regularly. This authorizes routine pushes after the gates and hygiene
scan; it does not authorize publishing ROM-derived content or releases.
The same rule covers `engine` and lane branches (2026-11-19).

- Run `python3 tools/hygiene_scan.py` before every push. CI runs it too.
- Never commit ROMs, extracted course, model or texture data, retail-course
  traces or stills of retail content. The ROM path is `FZX_TEST_ROM`, set
  locally; it never enters the repo, a CI log or a chat.
- Stage named paths only. No `git add -A`, no force-push, no history rewrites.

## Ask the owner only for

The three touchpoints in plan section 7, a missing ROM path when no ROM-free
card remains, anything outward-facing, look and feel acceptance at each milestone, a
hardware run, and an architecture choice ENGINE_PLAN does not already decide.

Do not ask for, or wait on, routine merges into `engine`, SH2ZAM pull requests
that pass the runbook, owner discs, housekeeping or the next phase item in
docs/RETRO_PLAN_20261120.md.

## Keep the state small

`docs/STATUS.md` stays under 80 lines and `docs/HANDOFF.md` under 50. History
lives in git. Trim in the same commit that would push either over its limit.
