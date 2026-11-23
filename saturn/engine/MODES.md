# Engine race modes (ENG_MODES, plan P3 item 3)

DRAFT 1 for Codex (the menus) and the engine integrator. Race side: Opus lane `claude/modes`. Nothing binds until Codex agrees.
`ENG_MODES` and `ENG_GHOST` are on by default in every scenes image (`ENG_GHOST` follows `ENG_MODES`); `ENG_MODES=0` is the identity arm
(every BIN then equals the engine's from before the modes, checked: base b573443 f0672796 = this branch with the arm off).

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
  **Course 54 (HOST, original code, `fzx-gp-ref --mode death`, passive player):** the rivals do not lose energy. All 29 keep it
  (half of their machine's) until they retire, by falling: every rival leaves the road at the same place, distance about 19,530
  (the 15-node air run 312..326 of the baked line, 922 units long), flies 19 or 20 ticks (about 625 units at 33.7 a tick, short of the
  landing), falls, and `Racer_RetireRacer` (energy 0) follows 122..132 ticks later. Take-off to retirement is 141..151 ticks
  (mean 146.9), so the retire ticks 738..835 are the take-off ticks 590..686 plus that. The player's win is the last retirement
  (`gCpuRacersRetired + 1 == gTotalRacers`). The default CPU line (speed 69, lateral 0, `cpu.c` for course indexes 30 and up)
  never boosts over the gap. The native field has no rule for a flight that is shorter than its air run (its falls are lateral):
  card line-field-14 (hand-over). With the prototype rule the native field on HOST retires 29 of 29 in 6 seeds, sorted retire
  ticks within 14 ticks of the reference (band fixed before measuring: 40), and the controls (rule off, reach 40 ticks, fall time 60
  ticks, one global pace) fail. Per-rival pace comes from the reference's speed before take-off (`tools`-side field header).
- **Practice rows:** disc files `PRnn.BIN` (3,840 B a bundled course: [class][vehicle][8] big-endian), read at LOAD with the course's identity row
  into high work RAM (`fzx_practice_rows`, engine_courses.c) and checked against the FNV-1a the image keeps (`course_practice_index.c`); 24 courses
  of rows in the image would have been 92 KB. `course_practice.c` is the readable record of the same values (not linked; the gate reads it).
  tools/course_practice_bundle.py writes them, from four reference runs of the
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
| `0x4000 \| cup << 4 \| class` | clear that cup on that class with the cursor's machine; `fzx_scn_report[14]` = `0x4000 \| result \| unlock << 8 \| credits << 12 \| Master offered << 13` |

SELECT's placeholder walks Time Attack, Grand Prix, Practice, Death Race on C; START changes Practice's class (Grand Prix: left and
right); Master is skipped until it is open.

## Entry and outcome contract (DRAFT 2, answers Codex C-137 / C-138)

`engine_mode_entry.h` (HOST fixture `tests/mode_entry_driver.c`, run by `tests/test_modes.py`, six controls). Ownership:

| | Front end (Codex) | Modes | Records |
| --- | --- | --- | --- |
| Entry | navigation, signs, cursor, Back, confirm; writes `fzx_session` after `FZX_ENTRY_OK` | `fzx_mode_caps`, `fzx_mode_entry_check` (pure, never writes the session) | none |
| Race | HUD from the engine's numbers (bindings in the header) | field, laps, energy, class, termination | none |
| End | shows RESULTS from the immutable `fzx_scn_result` and `fzx_outcome_t` | `fzx_mode_outcome`: kind, record table, eligibility, GP points | `fzx_records_commit` once, `fzx_records_cup_clear` |

- Mode IDs are the session's (GP 0, TA 1, Death Race 2, Practice 5); Versus, Options and unknown IDs return capability 0 and `FZX_ENTRY_NO_MODE`.
- Capability bits: runs, entry open, class pick, field, endless, cup, keeps times, wins by KO. Death Race runs but its entry bit is clear
  (`FZX_ENTRY_CLOSED`) until its real win path qualifies; the harness opens it with `env.lab`.
- Entry statuses: NO_MODE, CLOSED, NO_COURSE (catalog avail bit), NO_MACHINE, CLASS_LOCKED (a chosen class that is not open: Master
  before every cup is cleared on Expert; a forced class is not checked). A rejected or cancelled entry changes nothing.
- Outcomes: FINISH (GP, TA), WIN (Death Race, every rival retired), RETIRED, QUIT, NONE (Practice has no finish). GP finish scores
  points and never a record; TA finish is eligible for the TA table; Death Race WIN for the Death Race table; Practice never reaches a
  table or points. Records still ranks the run and makes the one write.
- Not yet wired: the existing SELECT (`session_edit`) still validates by itself; once Codex's picker agrees, both call `fzx_mode_entry_check`.

## Time Attack ghost (ENG_GHOST, on with ENG_MODES)
- One ghost slot, as in the original: the best Time Attack run, kept as two backup-RAM files (FZXGHO_A / FZXGHO_B) written alternately with a generation, so a power loss or a bad write leaves the older ghost. Records stay in FZXREC_A / FZXREC_B.
- Format (engine_ghost.h): 44-byte head (magic, version, generation, course, race ms, samples, bytes, CRC, start position Q16) plus a nibble stream of second differences of the position, every second tick, one nibble per axis (0 = escape + 16 bits). Positions are whole units. 12,000 bytes of stream maximum (original racer.c: 16,200); Mute City's best run is 4,692 bytes (3,121 samples).
- Recording: engine_drive.c `step_once` calls `fzx_ghost_tick` after the physics step while the race clock runs and the player has not finished. The save runs at the Time Attack name-entry commit when the rank is 1.
- Load: a file is accepted only after magic, version, size, CRC and a full decode of the stream; anything else is refused and the older file is tried. A ghost for another course is not played.
- Playback: a position per tick (`fzx_ghost_pos`, Q16): even ticks are samples, odd ticks the midpoint. The translucent machine is the renderer's to draw.
- Counters: `fzx_ghost_report[16]`, `fzx_ghost_store[16]`, controls `fzx_ghost_fault` (1 wrong CRC, 2 half the file), `fzx_ghost_on` (0 = off).
- Ymir proof (EMULATOR): record then reload then play on Mute City; decoded recording equals the HOST (fzx-phys) positions at all 3,121 samples (9,363 axes, rounded to whole units); the half-file control is refused.

## Death Race: the field and what is open (claude/modes, 2026-11-22)

- **Course 54** is a catalog row with its own avail bit (`FZX_AVAIL_DEATH`): the mode's only course, picked by the cursor, never offered to TA, GP
  or Practice. `FZX_COURSE_BUNDLE_MASK` is 64-bit. The bundle takes it as the last course (`course_bundle.py ... death-race`).
- **Field rows** come from the HOST reference alone (`tools/modes_death_regen.sh`: `fzx-gp-ref --mode death`, seed 1, difficulty 1, and its per-tick
  diagnostic): grid, machines and chase from the report; each rival's pace the mean distance a tick over the 30 ticks before its first air tick, by
  id (`tools/death_field_header.py`, C-169 item 2); energy the full value (the mode halves it). The rule is SH2ZAM line-field-14 (pin 3188d32):
  `gap_reach_ticks 19`, `fall_ticks 127`, set in `engine_field.c` for the mode. The Death rows are in every class of the course row and in `PR54.BIN`.
- **No distance LOD** in the mode: with it (focus on the driving player) five of the 29 ran past the first air run and retired on lap 2 (EMULATOR,
  tick 1400 against the reference's 834); without it all 29 fall on the first lap.
- **No shift to the player's place** in the mode: the field builder moves every rival by the player's distance from his own grid slot (so places
  stay relative to him). Death Race's player is 30th on the grid but starts at the line, so the 29 stood 2,300 units too far on and fell 60 ticks
  early. Rivals keep their absolute slots now: retirement ticks (from the race start, as the reference's) are within -11..+31 of the reference
  by rank (EMULATOR vs HOST, seed 1; the qualification band is 40). Found by comparing against the HOST harness, which has no player shift.
