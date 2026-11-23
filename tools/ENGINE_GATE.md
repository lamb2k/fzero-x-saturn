# Engine gate

`tools/engine_gate.py` runs the gate matrix in AGENTS.md (Workflow) for one engine commit and writes a pass/fail
table and a JSON receipt. Every lane and the integrator run this script and its scorers. Nobody keeps a private
harness copy. Tiers: HOST (`fzx-phys`) and EMULATOR (Ymir). Every timing is MODELLED, and hardware is never run here.

## Commands

```sh
python tools/engine_gate.py --quick                       # a lane, before each report: about 10 minutes
python tools/engine_gate.py --full                        # the integrator, per merge: the whole matrix
python tools/engine_gate.py --full --controls             # plus every check's failing control
python tools/engine_gate.py --quick --rev HEAD --base origin/engine --identity-vars "ENG_MYSWITCH=0"
python tools/engine_gate.py --full --only loop,cancel --courses sand-ocean-1
```

- **`--rev`** is the commit gated (default `HEAD`). Commit first: the gate builds a `git archive` of that commit,
  so uncommitted work is not in it. The scripts and the matrix come from the checkout you run from.
- **`--base`** is the identity base (default `origin/engine`; `""` skips identity). The base's BIN hashes come from
  the shared cache `fzx-evidence/host/gate-receipts/<sha>.bins.json`. When an image is missing there, the gate
  builds it in your stage. `--rebuild-base` always rebuilds it, for example when your stage's generated assets differ.
- **`--identity-vars`** gives the make variables for the identity builds, so a lane whose switch defaults to on
  can show that the switch's off position keeps the base's images.
- **`--base-vars`** adds make variables to the base's identity builds as well, for a switch that already exists on
  the base with another default (`--identity-vars "ENG_LOW_COLD=0" --base-vars "ENG_LOW_COLD=0"`: both sides with it
  off). Those base images are always built in your stage and never read from or written to the shared cache.
- **`--expect image=prefix,...`** checks BINs that were already reported for the same code.
- **`--rescore`** scores an earlier run's evidence again (same `--label`) without building or running anything.
- **`--control-names`** limits `--controls` to the named controls.
- **`--keep`** keeps the discs, dumps and stills. By default they are deleted after scoring and listed in
  `cleanup-manifest.txt`.
- **Exit status:** 0 when every check passes and, with `--controls`, every control fails. Otherwise 1.

Output goes to `fzx-evidence/host/claude-gate/<label>/` (`--evidence`, `--label`):
- `summary.md`: the table;
- `receipt.json`: every check, the BIN hashes, run times and the verdict;
- `builds/`, `runs/` (`result.txt`, `fields.txt`, harness log) and `host/`.

A copy of the receipt also goes to `gate-receipts/`. A lane's report names its receipt. The integrator compares it
with the base's receipt.

## What it checks

| Check | Pass | Quick | Full |
| --- | --- | --- | --- |
| `build` | the image builds | field, scenes (driver) | 9 images |
| `identity` | each default BIN equals the base's | field, scenes (driver) | E0, field, field sound off, field as shipped (pad), drive, roster, scenes as shipped, scenes (driver) with sound on and off |
| `legacy` | recipe BIN `4b4cacb2`, OFF BIN `bb5f0fc5` | `--legacy` | yes |
| `cadence` | 0 intervals over two fields, from tick 420 (every row on E0) | field race; loop races | every race image, loops, six courses |
| `timing` | INFO: master, slave (with the slave's cloud share) and critical path mean / p99 / max; VDP1 p99 | same | same |
| `margin` | unrounded critical-path p99 x 1.36 at most 33.3 ms (ENGINE_PLAN 5 gate 9; about 24.485 ms) | field race | race images, six courses |
| `host` | SH-2 checkpoints equal HOST's (`fzx-phys --checkpoints 256`, tubes build, the stage's packs); the finish ms equal, or a retirement where HOST retires; a whole race also matches HOST's final state hash | field race; loop's cold race | plus drive, roster, and each course |
| `determinism` | two runs give the same per-field ticks and pixel hashes, work RAM, VRAM and stills | field race | field race; loop |
| `loop` | cold and retry races give equal finish ms, checkpoints and tick-2000 stills | sound on | sound on and off |
| `reboot` | the saved name survives a reboot (the loop script waits for it, and for the saved Mute City record, `RECORD_MS` in `loop.txt`: the image's own Mute City HOST finish from `fzx-phys`; an image built with `ENG_ATTACKS` stops the gate, since this line has no `fzx-atk` reference) | yes | yes |
| `reset` | the soft reset returns to TITLE from title, select, records, a fade, load, race and results; the fire count rises each time | sound on | sound on and off |
| `cancel` | loads cancelled at 30, 100 and 180 fields return to TITLE (load state 3), and the next cold load races | sound on | sound on and off |
| `menus` | no menu frame over two fields: `fzx_scn_ctl[7]` is 0 and `[6]` is at most 2 at every mark | every scenes run | every scenes run |
| `memory` | master stack unused >= 20,480 B, slave stack >= 4,096 B, 0 guard trips | every race | every race |
| `load` | INFO: the cold LOAD fields | | each course |
| `disc` | every sound-on image runs from the shipping mixed-mode disc: data track 1 plus all 21 CD audio tracks (`tools/disc_cdda.py --ids all`), validated: the image's compiled track table equals the music manifest, the CUE lists 22 tracks (MODE1/2352, then AUDIO), each file has its TOC length | every sound-on run | every sound-on run |
| `audio` | INFO: audio peak, non-zero samples and hash of each sound-on run | yes | yes |
| `receipts` | INFO: stale generated receipts; the regenerated ROM-free outputs and whether they moved | yes | yes |

**Practice (claude/modes, ENG_MODES on by default).** The `practice-*` rows (kind `practice`, `course_pr.txt`, the six Jack Cup courses; run
names `practice-<slug>`, so they never clash with the engine's 24-course `course-<slug>` rows) are Practice / Standard rows: a Time Attack race is solo and
lighter than a field, so each course is entered through SELECT as Practice (probe `0x451`, then the course probe `0x2nn`, the
order matters: a mode probe after the course probe can reset the course) and raced against the full 29-opponent field. The window is
the tick-420 window to an explicit end mark (`results`, drive tick 3780: the Practice race in the harness ends by itself between drive
tick 6126 and 7176 on the five courses measured, so the window stays well inside it); the matrix's `practice` section holds that end
tick and the checkpoint period (256) and the gate refuses a script whose hold tick differs. The `exit` check quits through the pause
menu (START, DOWN, DOWN, A) and requires RESULTS and unchanged record file generations: zero race-record writes.

*The fixture (C-169).* The expectation is frozen from the HOST reference alone: `tools/practice_expected.py REPORTS_DIR OUT.json` reads
the four `fzx-gp-ref --mode practice` reports per course (`tools/modes_practice_regen.sh` keeps them) and writes, per course and class,
the vehicle count, the seed, and per vehicle id the machine, grid place, energy ceiling and chase target. The file is ROM-derived: it
lives in the evidence folder (`--practice-expected`, default `host/practice-expected/practice-expected.json`), never in git. When the
gate builds an image with the live field identity it archives `practice-identity.json` beside the BIN (the BIN's hash, the expected rows
and their hash, the stage bundle's hashes, and whether the bundle's rows equal the expected ones), so a rescore needs neither the stage
nor the tree. At RACE entry `practice_fixture` reads the image's own state and compares it with the archived rows: the session (mode 5,
class 1, course and row, machine 0, skin 1, slider bits 0); `fzx_field_ident` (the vehicle count 30, the seed it was initialised with, the
row, class and mode it was built for); `fzx_field_live` (per vehicle: the machine of its row, its energy ceiling, its chase target and its
start position); the grid source, which must be 1 (the course header) with `fzx_field_grid` equal to the HOST grid in order; and the
rivals' start positions, which must order the vehicles by the HOST grid places. An image with the sprite roster (`E2_ROSTER`) is also
compared on each rival's bound sprites. It fails closed: no archive, no expected row, or no mark fails the row. The `host` check asks
for exactly the checkpoints the matrix's period and end tick give (256, 512 ... 3584), every one present in HOST's file and equal; a
missing HOST reference, a HOST file lacking one, or an image that captured a subset or stopped early fails.
Controls, all failing `fixture` or `host` as they must: `ctl-pr-course`, `ctl-pr-solo` (real runs); on a copy of the observed state
(the frozen rows are never touched) `ctl-pr-roster`, `-entrants`, `-energy`, `-chase`, `-standing`, `-grid` (still places 1..30),
`-gridsrc` (the right grid from the wrong source), `-seed`, `-count`; `ctl-pr-bundle` (the archive says a linked row differs),
`ctl-pr-noarchive`; and for `host` `ctl-pr-nohost`, `-hostlack`, `-subset`, `-short`. The Practice bundle comes from
`tools/modes_practice_regen.sh` (run it in a clean stage after `tools/course_bundle.py`). Grand Prix cost is C-138's six live Jack
rounds; Time Attack cost is the standalone rows.

**24 courses (the `course-*` rows, kind `course`, `course.txt`, `courses: all`).** Every one of the 24 courses is raced from the shipped scenes
image (default modes on) against its own HOST reference, and the matrix still lists the six Practice rows under the distinct name `practice-*`.
The `memory` row's note prints `road arena peak`: the largest `fzx_road_stream_high_used` of the run, against the arena
`FZX_ROAD_STREAM_HIGH_BYTES` (114,688 B on the modes image; 131,072 B on the engine's: the 24-course image with the modes
needed 16,384 B of the gap, saturn/engine/MODES.md). A course whose peak passes the arena fails the run (TRACK LOAD).
A modes-versus-`ENG_MODES=0` comparison of the 24 rows is run with two matrix copies (the second adds `ENG_MODES=0` to the images'
`vars`, stage `claude-gate-modes7id`); the rule is that no course's critical p99 grows by more than 0.30 ms.

**Courses.** All six Jack Cup courses are raced from the shipped scenes image, chosen in SELECT with probe
`0x2nn`. Two are tube courses: Sand Ocean and Big Blue. Each course is judged against its own HOST reference, which
the gate makes from that course's physics pack in the stage. Port Town's R01 driver retires, as HOST's does.

**Sound and the disc.** Sound moves the disc drive: music plays from CD audio tracks, so a sound-on run on a
data-only CUE never seeks data after real CD-DA playback (C-093). The gate therefore assembles the shipping
mixed-mode disc for every sound-on image (the music tracks are written once per gate run and hard-linked into each
image's disc) and fails any sound-on run that used a data-only CUE. The music is the local `bgm_cdda.py` output
(`--music`, `FZX_GATE_MUSIC`; ROM-derived, never committed). The scenes and field images ship with sound on. The full matrix runs the loop, reset and cancel with sound
on and off, and the field race both ways.

## Controls (`--controls`)

Each check has an input that must make it fail. The control passes when its check fails. If the run dies first, the
table says so.

| Control | Check | Input |
| --- | --- | --- |
| ctl-identity | identity | field BIN built with `E0_FOG_EARLY=0` |
| ctl-legacy | legacy | the OFF BIN against the recipe hash |
| ctl-cadence | cadence | field with `ENG_PACE_LEAD=2` |
| ctl-margin | margin | field with the balance and the physics look-ahead off |
| ctl-host | host | the field race against HOST driven at heading gain 2.6 |
| ctl-determinism | determinism | the field race against the balance-off race |
| ctl-stack | memory | a word written into the painted master stack 4 KB above its floor at tick 600 |
| ctl-guard | memory | the slave stack's first guard word cleared at tick 600 |
| ctl-loop | loop | the retry race forced to retire |
| ctl-record | loop | the loop waits for a Mute City record 1 ms off the image's HOST finish (`RECORD_MS` + 1), so the wait times out; it counts only when the harness error is that record wait (`RECORD_MS` + 1) after mark `results1` and before `saved1` (`expect_error`, `expect_mark`, `expect_mark_not`) |
| ctl-smpc | loop | `ENG_SMPC_OVERLAP_CONTROL=1` (sound off): the backup library's SMPC commands without the quiet SMPC, each opened by a RESENAB whose SF stays set across a vertical blank before its command is written (the race's window held open: the handler's INTBACK issue waits on that SF); it counts only when the boot hangs before `title` with one quiet section open in the error dump (`fzx_smpc_quiet` begun = ended + 1, `expect_open`), from the control image's own symbols, and only when its pair `arm-smpc2` counted. The matrix check refuses `ctl-smpc` without `expect_error`, `expect_open`, `expect_mark_not` = `title` and `pair` |
| arm-smpc2 | loop | test arm paired with ctl-smpc, `ENG_SMPC_OVERLAP_CONTROL=2`: the same held window with the quiet SMPC kept; it counts when the loop passes and reaches `title` with `fzx_smpc_quiet` balanced and non-zero in the `title` dump (`expect_mark`, `expect_pass`) |
| ctl-reset | reset | the chord without B at SELECT |
| ctl-cancel | cancel | the chord without B during the load |
| ctl-menus | menus | `ENG_SCN_STALL_CONTROL=1`: one TITLE frame held for 3 fields |
| ctl-course | host | Silence judged against Mute City's reference |
| ctl-disc | disc | the sound-on cancel script on the data-only CUE: flagged non-shipping; its audio is compared with the mixed-disc run |

## The matrix file

`tools/engine_gate_matrix.json` holds:
- `limits`: the floors, agreed before measuring;
- `images`: the make variables of each image;
- `identity`: which images each mode builds for identity;
- `legacy`: the expected hashes;
- `courses`;
- `runs`: name, image, kind, script, course, modes, determinism;
- `controls`;
- `regenerate`: ROM-free generators the gate reruns when their receipt is stale.

Never change a limit after seeing a result. A change needs the integrator, and the owner for the floors.

## Adding a check

1. **A new run:** add a `runs` row. Kinds `race`, `loop`, `reset`, `cancel` and `course` already have scorers.
   A new scenes script goes in `tools/engine_gate/scripts/` (the harness's script language is in the harness source).
2. **A new kind or measure:** add a scorer method to `Gate` (`score_race` and `score_scenes` show the pattern). It
   calls `self.add(check, subject, ok, value, detail)`.
3. **Its control:** add a `controls` row with an input that must fail the check. Show the control failing in your
   report before using the check. A probe hook needed for a control goes behind a diagnostic switch that defaults to
   0, and the identity check must still pass.
4. **Push it** with your lane, and say so in your report. The integrator merges it like any tool change.

## Setup

- **Harness.** `ymir-gate.exe` links Ymir, which is GPL-3.0, while this repository is MIT. So its source stays
  outside git, in `<evidence>/host/tools/ymir-gate/` (`<evidence>` is `$FZX_EVIDENCE`, else `~/fzx-evidence`):
  `ymir-gate.cpp`, `build.cmd` and `compile.rsp`. Local paths (BIOS, music, SH2ZAM clone) come from the
  environment (`FZX_GATE_BIOS`, `FZX_GATE_MUSIC`, `FZX_GATE_SH2ZAM`, `FZX_GATE_HARNESS`) or from `local.json` in that
  folder; nothing machine-specific is in the repository. It builds against `Ymir-baseline-a9f0cdae` (`Full-x86-64-AVX2-MSVC-Release`), the same Ymir as
  every earlier lane harness. It is headless, with the software renderer unthreaded, the SH-2 cache on, the 4 MB
  DRAM cartridge and formatted internal backup RAM. It merges the race harness (`ymir-look`) and the scenes harness
  (`ymir-snd-scn`). The new parts are pokes by address or pointer (`*_sym+off`), per-field scene words and pixel
  hashes, and an `exit` line. Set its path with `FZX_GATE_HARNESS` and the BIOS with `FZX_GATE_BIOS`.
- **Stage.** WSL `yaul-image/claude-gate-*` in root's home (`FZX_YAUL_IMAGE`), driven by `tools/engine_gate/stage.sh`. Create one per lane
  with `--seed-stage <a stage with generated assets> --stage claude-gate-<lane>`.
  - **Never write through a hard link into another lane's stage.** The seed's `generated/` is copied (`cp -a`),
    never hard-linked, and every regeneration happens in your own stage. `upstream/` (read-only) is the only
    linked tree. Asset tools rewrite files in place, so a hard-linked `generated/` passes one lane's
    regeneration to every stage sharing the inode. On 2026-11-20 the uncommitted line-field-4 `course_bundle.py`
    rewrote the select bundle in three stages at once, and a clean `78e6a92` build there refused it as stale.
  - Each gated tree gets the SH2ZAM pin from `saturn/engine/sh2zam.pin` and the legacy submodule pin, both
    archived from `FZX_GATE_SH2ZAM`.
  - Builds in one stage run one after another. Harness runs run in parallel (`--jobs`).
- **ROM.** Nothing ROM-derived leaves the stage or the evidence folder. HOST references are made in the stage.
  Receipts hold hashes, counts and timings.

## Run time (measured 2026-11-20, 16-core host)

| Step | Time |
| --- | --- |
| One engine build | about 42 s |
| One legacy build | 30 to 40 s |
| Mute City race in the harness | about 2.7 min (9,465 fields; about 57 fields/s) |
| Loop (3 races, reboot) | about 7.5 min alone; up to 8 min with 12 parallel jobs |
| `--quick` (5 runs, base BINs cached) | 7.7 min (460 s) |
| `--full --controls` (9 images, legacy pair, 28 runs, 12 at a time) | about 22 min |

Ymir slows down with 12 or more parallel jobs: one Mute City race takes 2.7 min alone and about 5 min under load.
Lower `--jobs` when other lanes share the machine.

**Ghost storage (claude/modes C-169 item 3).** `ghost-f0`, `-f1`, `-f2` run `ghost_ta_f0/1/2.txt` (Time Attack, Mute City: race, new best, name entry,
soft reset, race again with the ghost read back): fault 0 none, 1 a wrong-CRC save, 2 a save stopped half way. The `ghost` check reads
`fzx_bup_log` (fields in and out, bytes and kind of each backup write), `fzx_ghost_store` and `fzx_rec_report` at `saved1`, and the reload at
`race2_start`: exactly one records commit and one ghost save begun; every ghost file at most one chunk (1 KB), consecutive ghost writes at least
two fields apart (one per manager frame), none holding the caller a whole field (FRT ticks); after the reset the whole new ghost (generation,
race ms equal the HOST finish) for fault 0, no ghost accepted and the records intact for faults 1 and 2; and `menus` 0 over two through the commit.
Controls `ctl-gh-frames`, `-commits`, `-readback`, `-ms`, `-big`, `-held`, `-nofault` alter the observed state of a run and must fail.

**Death Race natural end (claude/modes C-169 item 4).** `death-natural` runs `death_nat.txt` (mode probe `0x423`, no poke): the `death` check wants
29 retirements in the field's event log (`fzx_field_events`), end reason FINISH, one records commit, and the rivals' retirement ticks against the
original code's reference (`--death-ref`, `fzx-gp-ref --mode death`, seed 1; tools/modes_death_regen.sh) inside the qualification band of
tools/modes_dr_check.py (sorted ticks within 40, 24 of 29 by id within 60). Controls `ctl-dr-ticks`, `-short`, `-commits`, `-reason`, `-poke`,
`-noref`. Ticks are counted from the race start on both sides (the reference's sim ran 420 ticks of countdown first: its `ticks` 1255 = 420 + 834).

## Baseline

The baseline for `origin/engine` `78e6a92` is in `docs/ENGINE_GATE_BASELINE.md` and
`gate-receipts/78e6a92*`. It is the reference for every later merge.
