# Engine race modes (ENG_MODES, plan P3 item 3)

DRAFT 1 for Codex (the menus) and the engine integrator. Race side: Opus lane `claude/modes`. Nothing binds until Codex agrees.
`ENG_MODES=1` (scenes images; default 0 while the lane builds, the merge makes it the default and keeps `0` only as the
identity arm). Off, every BIN equals `origin/engine`.

## Session

`fzx_session.menu_mode` (US rev0 ModeSelect IDs, SCENES.md section 3) and `difficulty` (0 Novice .. 3 Master).

| menu_mode | Mode | Rivals | Laps | Class | Course | Keeps | Needs |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | Grand Prix | 29, the cup manager | 3, finish | SELECT picks | the cup's | cup clears, trophies | GP catalog bit |
| 1 | Time Attack | none (solo) | 3, finish | none | SELECT picks | top 5, best lap, top speed per course | TA catalog bit |
| 2 | Death Race | 29, half energy | endless | Master (forced) | SELECT picks | one time per course (win only) | GP catalog bit; attacks for the kills |
| 3 | VS | not run | | | | | owner decision |
| 5 | Practice | 29, own rows | endless, no finish | SELECT picks | SELECT picks | nothing | GP catalog bit |

`session_edit` writes `difficulty = fzx_mode_class(menu_mode, picked)`: Death Race is always 3. A mode the table does not run (VS,
Options, unknown) is refused. The table is `engine_modes.c`; `tests/test_modes.py` checks it against the decompiled source
(Time Attack `gTotalRacers = 1`, Death Race `energy *= 0.5` and its win rule, the lap wrap, `gTotalLives`, the ModeSelect IDs).

## Race rules

- **Solo (Time Attack):** `fzx_field_load` returns at once with no line and no rivals; the field tick does nothing and
  `fzx_field_cars` returns the player alone. The gate rows that measure the full 30-car field are Grand Prix rows.
- **Endless (Practice, Death Race):** the physics course's lap count is 99 (no lap-count finish; the lap-time ring keeps 8), the
  field's lap count is 99 (rivals never finish). Practice has no end but the pause menu's QUIT.
- **Death Race:** rivals start at half energy (`racer.c`), the race is won when every rival vehicle is retired
  (`fzx_field_retired_now`), the player's time at that tick is the record, and a player retirement is the loss. Without attacks
  (`ENG_ATTACKS`, not on this line) the kills do not happen: HOST with a passive player retires 1 rival of 29 in ten minutes.
- **Practice rows:** `generated/courses/select/course_practice.c` (tools/course_practice_bundle.py), from four reference runs of the
  original code as Practice per course (`fzx-gp-ref --mode practice`, difficulties 0..3, the shipped field seed). The Grand Prix
  class rows are not touched.

## Records and progress (engine_records.h)

- Only Time Attack and Death Race rank (`fzx_mode_keeps_times`); Grand Prix and Practice runs rank nothing (before this a GP
  finish ranked in the Time Attack table).
- Save version 2 adds `fzx_rec_progress_t`: `cup_cleared[4]` (difficulties cleared per cup, `gCupNumDifficultiesCleared`),
  `cup_done[difficulty][machine/3]` (trophies, `Save_UpdateCupCompletion` bits), `credits_due` (`gForceCredits`). Version 1 files
  still load (progress zero). `fzx_records_cup_clear` runs when a cup's final round is scored with the player first overall;
  `fzx_records_unlock_level` is `func_8007E2B4` without the staff ghost; `fzx_records_class_open(3)` is true from level 2 (every
  cup cleared on Expert). Codex's screens read these and call `fzx_records_credits_seen()` after the ending.

## Harness hooks (scenes images, ENG_MODES=1)

| Probe | Effect |
| --- | --- |
| `0x400 \| mode << 4 \| class` | SELECT: mode and class under the cursor (GP needs the catalog bit) |
| `0x800 \| cup << 4 \| class` | clear that cup on that class with the cursor's machine; `fzx_scn_report[14]` = `0x800 \| result \| unlock << 8 \| credits << 12 \| Master offered << 13` |

SELECT's placeholder walks Time Attack, Grand Prix, Practice, Death Race on C; START changes Practice's class (Grand Prix: left and
right); Master is skipped until it is open.
