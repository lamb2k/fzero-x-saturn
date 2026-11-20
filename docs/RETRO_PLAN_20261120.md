# Retrospective and plan from here (2026-11-20)

Written at the owner's request after the 11-19/20 push, with every lane stopped.
State: `engine` `e3559f5`, SH2ZAM main `accd505`. Every timing on this page is
Ymir, MODELLED. **Nothing has run on a Saturn.**

Goal (unchanged): the whole game playable on a real Saturn from the original
assets, 30 fps as the floor, plays right, Saturn exclusive, engine code in SH2ZAM.

## 1. Where we are

| Area | State |
| --- | --- |
| Loop | Boot, title, select, load (about 5.3 s), race, results, records, retry, soft reset. Codex's menus are in. |
| Race | Native physics, 3D player car, 29 native rivals, locked 30 fps, 0 intervals over two fields in races and menus. |
| Courses | All six Jack Cup courses in one image, exact finishes against HOST, pipes and cylinders included. |
| Sound | 21 CD-DA tracks and SCSP effects, on by default. |
| Margin | Critical-path p99 about 24.9 ms on Mute City's field, 23.1 to 26.6 across courses. Target 24.5 (33.3 ms / 1.36, the AvP console factor). |
| Not in | Grand Prix (gated on rivals), attacks and KOs (built, off), effects, 18 more courses, the full roster, Death Race, VS. |

## 2. Retrospective

### What worked (keep doing)

1. **A separate engine line with vertical slices.** Each milestone ran and was
   shown. One day took the engine from a flythrough to a six-course game loop.
2. **Fresh lanes with a tight brief and their own gates.** The best results came
   from new agents given one job, the files to read and the exact gates
   (pace pipeline, emit cuts, tubes, HOST equality).
3. **Profile first, exact cuts first.** The emit lane profiled per sub-step and
   took 5.3 to 2.9 ms with identical output. The exact prelude cuts needed no
   owner call.
4. **Contract before code with Codex.** `SCENES.md` went through six drafts in
   hours and both sides bound once. The cup landed without rework.
5. **Reference-seeded windows as the handling judge.** Whole-race closed-loop
   comparison could not work (the reference itself splits 16/17 boosts). Short
   windows from the reference state, with a failing mutation per event class,
   found real gaps and cleared false ones.
6. **Failing controls on every check.** They caught a scorer that added a quantum,
   a harness that dumped mid-pick, and a libc `memcpy` in the library.
7. **Owner look calls from side-by-side clips.** Cap 26, lock 30 and "no half-rate
   clouds" were each decided in one message.
8. **Freezing `engine` while a lane lands.** It ended the scenes lane's rebase loop.
9. **Stating the budget so it can be derived.** The balance lane showed two CPUs
   at 22 ms cannot hold 45 ms of work; the critical path is the right measure.

### What didn't work (stop doing)

1. **No hardware number yet.** Every margin decision rests on one factor borrowed
   from AvP. A second-emulator cross-check was offered and never run.
2. **Estimates given before measuring.** VDP2 prelude assumed 2 ms, measured 15.
   The DSP lane cost most of a lane-day for 0.3 ms, because the work it was
   aimed at is division and branching. Fog early-out: 2 ms estimated, 0.5 found.
3. **Reading flags and profiles without checking.** The "17-field wait", the
   wall "recoil" bit and the sky-line bisect were each a misread that cost a round.
4. **Lanes kept alive too long.** Five lanes ran past 800k tokens of context. They
   slowed, repeated runs, and one spent about 90 minutes on a one-field shift.
5. **Gate matrix gaps found by the next lane.** Load cancel was never tested with
   sound on; the legacy recipe broke through a shared tool; rivals loaded per
   course but started from round-1 grids. Each lane had its own harness copy.
6. **Qualifying on one course and on stand-ins.** Rival pace passed on Mute City by
   coincidence (its lap-2 target equals the constant pace). Stand-in rival art
   and a made-up grid hid the roster load cost.
7. **Rebase churn.** `engine` moved under gating lanes four times before the freeze.
8. **Making the owner the bottleneck.** Routine merges and next steps were listed as
   "waiting on you", and lanes were parked at natural stops.
9. **Too many progress pings.** "Waiting on its runs" messages added noise and no
   decision.
10. **Stage hygiene.** Stale generated receipts in most stages, CRLF commits, one
    extract into `/root`, C: down to 21 GB, and one lane killed with unpushed work.

### Start doing

1. **One shared gate script in the repo** (`tools/engine_gate.py`): every image
   (E0, drive, field, roster, scenes) by sound on/off, scenes on/off and each
   course, with cadence, critical path, HOST checkpoints, determinism, the
   controls, legacy identity and stacks. Lanes and the integrator run the same
   script. No private harness copies.
2. **Lane lifetime cap.** A lane hands over at about 400k tokens or one milestone,
   whichever comes first. It pushes at every report. Every lane ends with a
   handover file.
3. **A 30-minute measuring spike before any optimisation lane.** No lane starts
   from an estimate.
4. **Two courses and real data before "done".** Mute City plus one tube course
   for anything in the race; real packs, never stand-ins, for cost numbers.
5. **Merge windows.** `engine` takes merges in batches; it is frozen while a lane
   gates against it.
6. **Owner digest.** One message per milestone event or decision, with the clip.
   Routine merges, SH2ZAM trains and discs happen without asking.
7. **A disc with every milestone**, rebuilt automatically into the Desktop folder,
   plus a Mednafen run of the same disc as a second timing opinion.
8. **A shared generated-asset cache** keyed by tool hash, so stages stop carrying
   stale receipts.

## 3. Plan from here

Order is by risk removed per unit of work. Sizes are estimates in lane-sessions
(one fresh agent to its handover), not promises.

### P0. Hardware truth (owner, when home)
- Rebuild both discs from the current `engine` (six courses, real menus).
- Owner runs them: full loop with the counter, and the E0 soak.
- Meanwhile: the same disc under Mednafen.
- **Decides:** whether the margin work in P4 is needed at all, and E0/E1 sign-off.

### P1. Jack Cup Grand Prix playable (about 3 lane-sessions)
1. Land the shared gate script first (Start doing 1).
2. `claude/jack-gpfix`: grids from the previous finish order from round 2; finish
   the six-round check.
3. `claude/jack-wip` and SH2ZAM PR #57 (line-field-4): Port Town plates and ice,
   tube spans; fix the Sand Ocean skyline refusal (status 30); rivals drawn
   round tubes.
4. Codex opens the GP rows; full six-round cup with sound, each difficulty.
- **Exit:** rival bands 14/14 on six courses; a complete Jack Cup in Ymir with
  0 over two; Codex audit of E2/E3.

### P2. Combat and effects (about 4 lane-sessions)
1. `claude/attacks-3`: resolve the 106 event and 21 position failures under the
   C-092 judge, starting with the wall hit one tick early in the vehicle model;
   run the held-out set.
2. Rival drift, walls and falls (line-field card), so retirements reach the
   reference's 2 to 6 per race.
3. `fzero-x-saturn-fx` (unpushed work, recover first): spin-out, sparks,
   explosion, boost flame.
4. Turn `ENG_ATTACKS` and `ENG_FX` on; KOs feed the cup's spare machines.
- **Exit:** C-092 judge passes or Codex rules on the residue; owner accepts the
  KO clip.

### P3. The rest of the game (the largest block; about 10 to 14 lane-sessions)
1. **Courses:** batch-bake Queen, King and Joker (18 courses) through the
   per-course pipeline and the gate script. New features get a card each as they
   appear (half-pipes, jumps, other surfaces).
2. **Machines:** all 30 selectable with their stats and settings; 3D player packs
   already bake for all 30.
3. **Modes:** difficulty levels, Death Race, Practice, options; records per course
   and mode.
4. **Front end (Codex):** title and attract, machine select and settings, records,
   endings, loading art.
5. **Look:** course-look items per course; the Silence star pass on the slave.
- **Exit:** every stock cup completes on every difficulty in Ymir; Codex audit.

### P4. Margin and memory (sized after P0)
- If hardware is close to the model: keep the exact wins only
  (`ENG_LOOK_CLOSE=2`, HUD double-buffer).
- If hardware is near AvP's factor: the post-start pack frames are full on both
  CPUs, so the choice is less work there. Options go to the owner with clips.

### P5. Release hardening (about 3 lane-sessions)
- Hardware matrix (console models, loaders, 60 Hz; 50 Hz if wanted), a full
  play-through on hardware, save robustness, the build kit from the owner's ROM,
  known issues and docs.

## 4. Decisions for the owner (none blocks P1 or P2)

1. **X Cup** (generated courses): in scope, or stock cups only?
2. **VS mode** (split screen): in scope? It is unlikely to hold 30 fps as drawn today.
3. **Guests:** Robotnik and Metal Sonic stay locked until you approve Codex's art.
4. **50 Hz (PAL):** wanted, or 60 Hz only?
