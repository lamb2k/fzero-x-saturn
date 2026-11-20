# Engine scenes: the boot-to-reset loop (DRAFT 4, 2026-11-19)

Interface between the scene manager and race scene (Opus, branch `claude/e3-scenes`) and the menu screens (Codex,
branch `codex/e3-c8-screens`).
- Draft 1 (e4ac275) answered C-064; draft 2 (16c00a9) carries C-065; draft 3 makes the guest gate per identity (C-066).
- Draft 4 adds section 11, course selection and the Grand Prix cup (C-084, branch `claude/e3-cup`).
- Nothing binds until Codex agrees.

Owner (2026-11-19): "Yes boot to reset loop should be in progress with loading screens".

Source references (`records_entry.c`, `save.c`, `racer.c`, `menus.c`) are the C8 baseline as cited in C-065.

## 1. The loop and who owns what

```
BOOT -> TITLE -> SELECT -> LOAD -> RACE -> RESULTS -> (retry: LOAD -> RACE) | TITLE
TITLE -> RECORDS -> TITLE
any scene + soft reset chord -> TITLE (session cleared)
```

| Scene | Owner | Role |
| --- | --- | --- |
| BOOT | Opus | hardware init, RAM-cartridge check, records load, then TITLE |
| TITLE | Codex | original title art; to SELECT, or a RECORDS navigation request |
| SELECT | Codex | mode, course, machine and skin, settings (slider); writes the session (section 3) |
| LOAD | Opus | animated loading screen; reads, decompresses and uploads the race set (section 7) |
| RACE | Opus | the engine race (E1 drive now, E2 rivals later); writes the finish snapshot (section 4) |
| RESULTS | Codex | post-finish results, name entry, the records table, and the retry/title intent; calls the records service (section 5) |
| RECORDS | Codex | read-only records browser (section 5 read API) |

Opus also owns:
- the scene manager, transitions and fades;
- reset execution and precedence, and load cancellation;
- the records storage backend and the sound-event queue;
- the full race-after-retry integration check (C-064 item 5).

Retry is not a scene: it's an intent the manager carries out (section 6).

## 2. Scene entry points (SH2ZAM scene-1, generic; the game keeps the table)

Each scene registers six functions. All run on the master.

```c
typedef struct {
    bool (*enter)(fzx_scene_ctx_t *ctx);      /* arenas empty; session readable; false = fault; bounded (section 7) */
    bool (*preload_step)(fzx_scene_ctx_t *ctx); /* optional: one bounded chunk of art upload; true = done */
    void (*update)(fzx_scene_ctx_t *ctx);     /* one 60 Hz step (section 6); may be called 0..4 times per frame */
    unsigned (*draw)(fzx_scene_ctx_t *ctx, vdp1_cmdt_t *list, unsigned capacity); /* once per presented frame */
    void (*leave)(fzx_scene_ctx_t *ctx);      /* the transition away has begun; once; stop starting work */
    void (*release)(fzx_scene_ctx_t *ctx);    /* all leases retired (section 8); give everything back; restore VDP2 */
} fzx_scene_ops_t;

typedef struct {
    const fzx_session_t *session;    /* read-only except in SELECT (fzx_session_edit) */
    const fzx_run_result_t *result;  /* RESULTS only: the immutable finish snapshot; NULL elsewhere */
    const fzx_records_t *records;    /* const read API (section 5) for TITLE, RECORDS and RESULTS */
    sh2z_arena_t *arena_hi, *arena_lo, *arena_cart; /* scene arenas, reset after release */
    const fzx_input_t *input;        /* this step's input (section 6) */
    uint32_t step, epoch;            /* step: 60 Hz counter; epoch: increments per scene entered */
    uint32_t build_bank;             /* VDP1 bank this frame's draw writes (section 8) */
} fzx_scene_ctx_t;
```

- **`draw`:** writes into the build bank it is given, within its command lease, and never submits. The manager owns
  the VDP1 command banks (vdp1-banks-1) and the submit.
- **Scene memory:** data a scene keeps lives in its arena, never in file-scope state that outlives the scene. The
  exception is constant tables.
- **Preloading:** while the manager shows the previous scene's fade or a loading frame, it calls `preload_step` once
  per frame until it returns true, then `enter`. This keeps `enter` within its bound for art-heavy screens such as
  SELECT and RESULTS.

## 3. Session selection (outside every arena)

```c
typedef struct {
    uint16_t version;      /* FZX_SESSION_VERSION */
    uint8_t menu_mode;     /* US rev0 ModeSelect ID: GP 0, TA 1, Death Race 2, VS 3, Practice 5 (Options 6 is UI only) */
    uint8_t course;        /* original course index */
    uint16_t machine_id;   /* stable machine identity, table below */
    uint8_t skin;          /* colour/skin index for that machine */
    uint8_t players;       /* VS only: player count */
    uint8_t difficulty, cup; /* GP only, once admitted */
    uint8_t player_render; /* FZX_RENDER_3D or FZX_RENDER_SPRITE: renderer choice, not identity */
    uint8_t confirmed;     /* 1 after SELECT confirms; LOAD refuses to start on 0 */
    uint32_t slider_bits;  /* the slider as IEEE-754 binary32 bits (exact: D-pad steps of 1/128 and finer analogue values) */
} fzx_session_t;
```

- **Storage and lifetime:** one static `fzx_session` in low work RAM. It survives every scene teardown and retry.
  A soft reset clears it to defaults; going from RESULTS to TITLE keeps it.
- **Who writes it:** only SELECT, through `fzx_session_edit()`, which sets `confirmed`.
  - LOAD applies it once: it translates `menu_mode` to the runtime GameMode (`fzx_mode_runtime()`, explicit table;
    for example Time Attack 1 → 0x0e) and recomputes the machine constants from `machine_id` and `slider_bits` on
    every confirmed selection.
  - Later edits never reach a running race.
- **First loop:** SELECT advertises Time Attack on the one supported course. Every other mode and course launch
  stays disabled until its race exists.
- **`slider_bits` round trip:** 0, 0.5, 0.5078125 and 1, exactly (a host test in section 10).
- **`player_render`:** defaults to 3D (owner 2026-11-19, "I would say 3d"). The rivals' renderer is undecided and
  isn't part of the session.
- **Machine identity** is one shared namespace:

  | IDs | Meaning |
  | --- | --- |
  | 0..29 | the original roster, in original order |
  | 30, 31, 32 | Super Falcon, Super Stingray and Super Cat (already used by the racer-art keys) |
  | 33 | Robotnik (guest) |
  | 34 | Metal Sonic (guest) |
  | 35..255 | reserved |

  - Each guest is gated on its own: `FZX_GUEST_MASK` has bit 33 for Robotnik and bit 34 for Metal Sonic (bit n =
    identity n; default 0).
    - An enabled guest is selectable through select, race, results and records. A disabled guest is hidden in all
      four.
    - Owner lineup choice (C-066): Robotnik (33) is one of the three selectable racers in the C8 machine-select
      lineup. Metal Sonic (34) is tracked separately and has its own bit.
  - A build with a guest's bit clear keeps that guest's rows in a save, showing a generic machine name and preview.
    Its identity is never dropped or renumbered.
  - Our side is plumbing only: the identity, the gate, save stability, and loading the art pack the session names.
    The artwork itself is Codex's lane.

## 4. Finish snapshot (immutable)

RACE writes it once, at its end, before requesting the transition. RESULTS gets a `const` pointer, and nothing
writes it after that.

```c
typedef struct {
    uint32_t run_id;       /* (boot_id << 16) | attempt: boot_id is persistent (section 5), so IDs never repeat across boots */
    uint8_t reason;        /* FZX_END_FINISH, FZX_END_RETIRE, FZX_END_QUIT (pause menu) */
    uint8_t menu_mode, course, skin;
    uint16_t machine_id;
    uint8_t laps_done, best_lap_index;  /* best_lap_index: which lap (0-based) was fastest */
    uint32_t slider_bits;
    uint32_t race_ms;      /* valid for FINISH */
    uint32_t lap_ms[FZX_LAPS_MAX];
    uint32_t best_lap_ms;
    uint32_t max_speed;    /* km/h x 100 as shown on the HUD */
} fzx_run_result_t;
```

- **Retire:** the energy reaches zero while taking damage (the spin-out). Running out of boost energy alone never
  retires.
- **Soft reset:** produces no snapshot; the attempt is dropped. QUIT and RETIRE snapshots are shown but never ranked.

## 5. Records service (Opus storage, Codex screens)

**Table**, per course and per record kind. Kinds: Time Attack, and Death Race (its own single-time record). GP and VS
don't use the Time Attack table.

```c
typedef struct { uint32_t ms; uint16_t machine_id; uint8_t skin; uint8_t pad; uint32_t slider_bits; char name[4]; } fzx_rec_time_t;
typedef struct {
    fzx_rec_time_t top[5];                        /* race times, best first */
    fzx_rec_time_t best_lap; uint8_t best_lap_index;
    uint32_t max_speed; uint16_t max_speed_machine_id;
} fzx_rec_course_t;
```

- **Name:** `char name[4]`, ASCII with NUL padding, up to 3 visible characters.
  - The alphabet is A–Z, apostrophe, `!`, period, comma, 0–9, hyphen and a literal space (`records_entry.c`).
  - A shorter or blank name is valid. Blank is `{0,0,0,0}`; a space is 0x20 and is stored as such.
- **Empty defaults** (`Save_InitCourseRecord`, `save.c`):
  - times and best lap are `MAX_TIMER` = 3,599,999 ms;
  - slider 0.5 (`0x3F000000`), name all zero, max speed 0;
  - shown as eight dashes. There is no prefilled stock table.
- **Ranking** (`racer.c`, strict improvement):

  | Outcome | Rule |
  | --- | --- |
  | Race time | inserted before the first slower time; equal older rows stay ahead; identity, name and slider move with it |
  | Best lap and its lap index | tracked on their own |
  | Fastest speed and its machine | tracked on their own |

  - `fzx_records_rank(const fzx_run_result_t *, fzx_rank_t *out)` is pure. It returns all three outcomes: top-5 place
    (0 = none), new best lap (with its index), and new max speed.
  - RESULTS opens name entry only for a top-5 place. A best-lap-only or speed-only result updates without a prompt.
- **Read API:** `const fzx_records_t *fzx_records_get(void)` and
  `const fzx_rec_course_t *fzx_records_course(kind, course)`. TITLE, RECORDS and RESULTS read through it; nothing
  but the service writes the table.
- **Commit:** `fzx_records_commit(const fzx_run_result_t *, const char name[4])`, once per `run_id`.
  1. It inserts into the RAM table, then writes storage.
  2. A second commit with the same `run_id` returns `ALREADY` and inserts nothing.
  3. After `FULL` or `FAILED`, the RAM table stays updated and dirty. `fzx_records_flush()` retries storage without
     inserting again. RESULTS may offer it, and BOOT and TITLE retry silently.
- **`run_id` uniqueness:** `boot_id` is a 16-bit counter kept in the save header and incremented and written at boot.
  If storage is absent or full, `boot_id` comes from the RTC seconds instead, so dedup is at least scoped to the boot.
- **Storage:** internal backup RAM, through the BIOS backup library.
  - Two files, `FZXREC_A` and `FZXREC_B`, each with a header {magic, version, generation, boot_id, size, CRC-32}.
  - A write goes to the older generation's file. If it is interrupted, the other file is still the previous valid
    save.
  - The on-disk empty code is `MAX_TIMER` itself, so no adapter is needed.
- **Results:**
  - load returns `OK`, `ABSENT` (defaults), `FULL` (records live in RAM this session, with a one-line notice) or
    `FAILED` (both files bad: defaults, with a notice);
  - commit and flush return `OK`, `ALREADY`, `FULL` or `FAILED`;
  - a version mismatch is upgraded or reported `FAILED`, never misread.

## 6. Steps, requests, retry, reset and input

- **60 Hz steps:** menu and race logic advance in 60 Hz steps, independent of presented frames. Each presented frame,
  the manager computes the elapsed steps from the field counter (at most 4) and calls `update` that many times, then
  `draw` once. Menus therefore run at the same speed whether a frame takes one field or two.
- **Requests:** `fzx_scene_request(target, rank)`. At most one is pending, and a higher rank replaces a lower one.
  Ranks, highest first:

  | Rank | Requests |
  | --- | --- |
  | Soft reset | the reset chord |
  | END_RACE | the race has ended |
  | RETRY / TITLE / GOTO | everything else |

  Once the fade out has begun, only the soft reset can still redirect it.
- **Retry:** one confirmed intent, from RESULTS or the pause menu. The manager:
  1. settles the outgoing attempt once (the snapshot is final; RESULTS has committed or chosen not to);
  2. keeps the session and the committed records;
  3. resets the race-local arena;
  4. keeps the resident race set (section 7);
  5. runs LOAD on the fast path, then a fresh RACE with the next `run_id`.
- **Title:** RESULTS to TITLE keeps the session, and the menu cursors restore from it.
- **Soft reset:** A+B+C+Start held together, from any scene, including mid-load and mid-fade. The manager:
  - cancels any outstanding load;
  - drops the attempt and clears the session;
  - releases the resident race set;
  - goes to TITLE.

  Committed records stay. The chord is latched until all four buttons are released, so it fires once and doesn't
  carry into TITLE.
- **Input (`fzx_input_t`, per step):**
  - `connected` (port 1 has a pad);
  - `held`;
  - `edges`: presses not yet consumed, delivered on one step only even when a frame runs several steps;
  - `repeat`: directional auto-repeat, delivered every 6 steps after a 20-step delay, on the step it falls due.
- **Consumed edges:** when a request is accepted, the buttons held at that moment are latched. They report no edge
  until they have been released, across fades and loads, so a held A doesn't confirm the next screen.
- **Who sees the chord:** the manager detects the soft reset chord before any scene sees the input.

## 7. Loading and residency

- **Loading screen:** LOAD shows the course name card from the original art if the ROM has one, else a text card in
  the original font (the report says which). It runs the load in chunks between presented frames, each under about
  10 ms, so the animation never freezes.
  - Chunks are: CD read, cell4z expansion into the cartridge, the 3D player model, the track packs and the HUD.
  - A CD read in flight finishes its sector batch before a cancel takes effect, and its data is discarded.
- **Music:** LOAD fades the CD music out before its first disc read (the `ENG_CDDA` hook from `claude/e4-sound`).
  RACE starts the course track.
- **Resident race set:**
  - **Key:** (runtime mode, course, field and entrant set, machine_id, skin, slider_bits, player_render). Any change
    invalidates it.
  - **Contents:** track packs, expanded racer art in the cartridge, the 3D player model, the physics pack and the
    derived machine constants.
  - **Lifetime:** it survives retry, so the retry load skips the reads. It is released on TITLE, a soft reset or a
    key change.
  - **Memory:** a fixed retained race region, separate from the scene arenas. Menus never allocate from it.
  - **During the race,** VDP1 slots receive cells from it (sprite-cache-1); the slot pool is part of RACE's VDP1
    lease.
- **Menu art:** loaded through `preload_step` (section 2), from its own arena or cartridge lease.

## 8. Video leases and the hand-over

**The manager owns, always:**
- the VDP1 command banks (vdp1-banks-1);
- VDP2 colour offset A and its enable bits (every fade; unused by the E0 map in `VDP2_MAP.md`);
- NBG3 and its font palette, CRAM 1008–1023;
- the back-screen colour.

A scene that needs a plain background asks `fzx_scene_back_colour(rgb)`.

**Scene leases.** Each lease is named and bounded. Exact byte sizes come from each scene's art receipt, are checked at
`enter`, and are recorded in the clean-reset table.

| Scene | VDP1 | VDP2 / CRAM | Command lease |
| --- | --- | --- | --- |
| TITLE | RGB1555 title sprites; normal clip and local-coordinate commands | none private | its list share |
| SELECT | original RGB1555/CLUT4 menu, course and machine art, model textures, writable Gouraud tables (settings, reflections) | none private | its list share |
| RESULTS | results, name and table art, machine previews, finish-menu pixels and CLUTs | mode-0 CRAM words 256–287, 512–543 and 768–799 (finish menu, with the mirrored upper window) | its list share |
| RECORDS | original fonts, CLUTs and textures, machine preview commands | none private; no finish-menu palette | its list share |
| LOAD | loading card art | none private | small |
| RACE | everything else of VDP1 VRAM (track textures and shades, racer CLUTs, shadow, slot pool, 3D player textures and Gouraud) | RBG0 floor and clouds, coefficient table, line windows, back-screen table and CRAM 1–65 (`VDP2_MAP.md`) | the race list |

**Race backdrop under RESULTS:** RESULTS is drawn over the race's last image.
- At `END_RACE`, RACE hands its VDP2 backdrop (RBG0 floor and clouds, back screen) to RESULTS as a retained lease.
  RESULTS doesn't modify it.
- RACE's VDP1 commands stop. The manager replays RACE's final list once, behind the RESULTS overlay, until RESULTS
  draws its own.
- When RESULTS leaves, the retained backdrop is released with it.

**Writable tables in flight:** a writable Gouraud table or CLUT that a list in flight may read is changed only
- through a double-banked copy (vdp1-banks-1 section E), or
- after the manager confirms that the list has retired.

**Retirement** (C-065 point 4; it replaces draft 1's two-presentation rule):
- A lease is reclaimed only on confirmed replacement of the final list that references it. That is the scene's last
  `draw` list, or the last fade list the manager replayed with the scene's commands.
- Confirmation means that VDP1 has displayed a later list containing none of the scene's commands: the manager's
  own empty or fade-only list.
  - The manager confirms it from the vdp1-banks-1 state: the bank holding that list was submitted, drawn and then
    swapped out (its end-of-draw stamp is followed by a VBlank-out flip to a list built after `leave`).
  - Elapsed presentations alone are never proof.
- Only then does it call `release`.
- In `release`, the scene returns every VDP2 register, VRAM range and CRAM range in its lease to the baseline (screens
  off, the boot map). The next scene's `enter` sets up its own and assumes nothing.

## 9. Sound events

- `fzx_snd_event(id)` posts to a manager-owned queue, stamped with the poster's `epoch`.
- The manager delivers each event exactly once, at the next frame boundary, and the transition does not drop events
  posted before `leave`. A confirm sound posted on the press that starts a transition plays once, during the fade.
- Events posted by a scene after its `leave` are rejected (counted, not played), so a race's boost sound never plays
  on the results screen.
- Music commands (`play track`, `fade out`) are separate calls, owned by the manager and LOAD.

## 10. Checks

| Check | Owner |
| --- | --- |
| Menu screens against their existing C8 fixtures (select/settings, results/name entry/table, records browser, retry UI) | Codex |
| Knob-off identity: with `ENG_SCENES=0` the E1 drive image is byte-identical | Opus |
| The race inside the loop finishes at the same time and final state as E1 standalone | Opus |
| Race after retry is bit-identical to race 1 from a cold boot (full engine race, C-064 item 5) | Opus |
| Clean reset: after N retries and title round trips, the arena high-water marks, stack low-water, lease map, VDP1/VDP2 maps, cartridge map and live sentinels all match pass 1 | Opus |
| Soft reset from each scene, including mid-load and mid-fade; the chord fires once | Opus |
| No interval over two fields in TITLE, SELECT, LOAD, RESULTS and RECORDS | both (each for its scenes) |
| Records: ranking against the `racer.c` rule (ties keep older rows ahead); the three outcomes; an interrupted commit keeps the previous save; FULL/FAILED stay dirty, and flush inserts once; a second commit for one `run_id` refused; `slider_bits` round trip | Opus (service), Codex (screens) |
| Retirement: a reclaim before confirmed replacement is caught by a deliberate early-release control | Opus |
| Sound: a pre-leave confirm plays once across the transition; post-leave posts rejected | Opus |

## 11. Course catalog and Grand Prix cup (DRAFT 4, C-084)

Opus supplies the catalog, the entrants, one finalized classification per attempt, the spare-machine balance
and the cup manager. Codex (C8) owns the points table, the total ranking and the standings screens.
Rules below cite the US rev0 decompilation (`racer.c`, `menus.c`, `game.c`).

### 11.1 Course catalog

```c
#define FZX_AVAIL_TA 0x01u            /* the build has the course's packs and Time Attack race path */
#define FZX_AVAIL_GP 0x02u            /* ... and its GP field and results path */
typedef struct {
    uint8_t course;                   /* stable original course ID (cup c, round r = 6c + r) */
    uint8_t display_key;              /* index into C8's menu-name table */
    uint8_t avail;                    /* FZX_AVAIL_* */
    uint8_t reserved;
} fzx_course_row_t;
const fzx_course_row_t *fzx_course_catalog(unsigned *count);   /* rows in original course order */
const fzx_course_row_t *fzx_course_find(uint8_t course);       /* NULL: not in this build */

typedef struct { uint8_t cup, rounds, course[6]; } fzx_cup_def_t;  /* cup IDs as the original: Jack 0 .. X 4 */
const fzx_cup_def_t *fzx_cup_def(uint8_t cup);
bool fzx_cup_gp_ready(uint8_t cup);   /* every round's row has FZX_AVAIL_GP */
```

- The courses lane (`claude/courses`) supplies the rows and per-course packs. Until it lands, the catalog is course 0
  (TA) plus a stub row for course 1 with `avail = 0`.
- Jack lists six rounds, courses 0 to 5, in order (`game.c`: the next course is `gCourseIndex + 1` within the cup).
  GP is selectable for a cup only when `fzx_cup_gp_ready` is true.
- **Time Attack:** SELECT lists the rows with `FZX_AVAIL_TA`, shows `display_key`, and confirms the chosen `course`
  into the session (section 3). LOAD, retry and the records table (`fzx_records_course(course)`) all use that ID.
  LOAD refuses a course whose row lacks the mode's bit, back to SELECT.

### 11.2 Entrants

```c
#define FZX_ENTRANTS_MAX 30u
typedef struct { uint8_t entrant, machine_id, skin, flags; } fzx_entrant_t;  /* flags: FZX_ENT_PLAYER */
typedef struct { uint8_t count, player_slot; fzx_entrant_t e[FZX_ENTRANTS_MAX]; } fzx_entrants_t;
const fzx_entrants_t *fzx_cup_entrants(void);
```

- `count` is the actual field, 30 for Jack including the player. Entrant IDs are stable for the whole cup:
  0 is the player, 1 to 29 are the rivals in the original racer order.
- `machine_id` uses the section 3 namespace. Rivals are never guests. With `FZX_GUEST_MASK = 0` the player can't be
  a guest either.

### 11.3 Classification (one per attempt, immutable)

```c
enum { FZX_CL_FINISHED = 1, FZX_CL_PROJECTED, FZX_CL_RETIRED };
typedef struct {
    uint8_t entrant, status, scoring, laps_done;   /* scoring: 1 = this place earns points */
    uint32_t time_ms;                               /* official time; 0 for RETIRED */
} fzx_class_row_t;
typedef struct {
    uint32_t run_id;                                /* section 4 */
    uint8_t cup, round, course, count;              /* key: run_id + cup + round + course */
    uint8_t player_place, player_reason, player_kos, valid;
    uint32_t hash;                                  /* FNV-1a over the key and rows (determinism check) */
    fzx_class_row_t row[FZX_ENTRANTS_MAX];          /* official order, place 1 first */
} fzx_classification_t;
const fzx_classification_t *fzx_cup_classification(void);   /* const for RESULTS; RACE writes it once */
```

RACE finalizes it once, at its end, after the player's finish wait. It follows `func_8008AA8C` and
`Racer_UpdateRacePositions`:

- **Rivals keep racing until the trigger.** Rivals don't stop at the player's finish. In the HOST reference (original
  code, player CPU-driven) rivals finish up to about 1,600 ticks after the player. At the results trigger, the
  original then classifies every rival still running.
- **A finished entrant** keeps its time.
- **A rival still running** is classified with a projected time: elapsed × race distance ÷ distance raced.
  Status is `FZX_CL_PROJECTED`, which ranks and scores like a finish.
- **A retired entrant** has no time. Retired entrants go behind every classified row. A later retire ranks ahead of
  an earlier one.
- **Classified rows** sort by time, ascending. On equal times the higher entrant ID goes first, as in the original's
  scan.
- **Scoring:** only when the player finished. Every row from place 1 through the last row with a time has
  `scoring = 1`; trailing retired rows have 0. A player retire or quit scores nobody.
- Times come from the race tick (60 Hz), converted to ms one way for everyone. The player's row is the section 4
  `race_ms`.

### 11.4 Spare machines and retry

- **Cup start balance:** 5, 4, 3 or 2 by difficulty (`gTotalLives`).
- **KO award:** the 5th KO in a race adds one. RACE counts the KOs in `player_kos`; the manager applies the award.
- **Retire debit:** a player retire costs one. A pause-menu retry also costs one (`Menus_PayGpRestartLife`).
- **Retry eligibility:** decided on the balance at race start, KO award included (`sPlayer1Lives > 0`). With 0 at
  race start, a retire ends the cup (game over).
- **One owner:** the manager applies every debit and award, once per `run_id`, in `fzx_cup_accept`. RACE and
  RESULTS only read the balance.

### 11.5 Cup manager (outside every arena)

```c
typedef struct {
    uint8_t active, cup, difficulty, round;          /* round 0..rounds-1 */
    int8_t spares, spares_at_start;                  /* balance now; at the current race's start */
    uint8_t retry_ok, ended;
    uint32_t last_run_id;                            /* the last applied attempt */
    fzx_entrants_t entrants;
    int32_t total[FZX_ENTRANTS_MAX];                 /* written only through the C8 scoring hook */
    uint8_t last_place[FZX_ENTRANTS_MAX];            /* latest race position (C8's tie-break) */
} fzx_cup_t;
const fzx_cup_t *fzx_cup(void);
void fzx_cup_begin(uint8_t cup, uint8_t difficulty);  /* LOAD, on a confirmed GP session with no cup active */
bool fzx_cup_accept(const fzx_classification_t *c);   /* applies once per run_id; false for a duplicate */
enum { FZX_CUP_NEXT = 1, FZX_CUP_RETRY, FZX_CUP_END };
bool fzx_cup_intent(uint32_t intent);                 /* RESULTS' only write */
uint8_t fzx_cup_next_course(void);                    /* LOAD's course in GP */
void fzx_cup_reset(void);                             /* soft reset, or a new cup */
typedef void (*fzx_cup_score_fn)(const fzx_classification_t *c, int32_t total[FZX_ENTRANTS_MAX]);
void fzx_cup_score_hook(fzx_cup_score_fn fn);        /* C8's points table */
```

- **Storage:** one static in low work RAM, next to the session. Scene arenas never hold cup state.
- **Accepting a result:** `fzx_cup_accept` applies the spares, calls C8's hook once if the player finished, and
  records `last_place`. A duplicate `run_id` changes nothing.
- **Intents:**
  - NEXT moves to the next round. After the last round the cup ends, and C8 shows the final standings before
    leaving.
  - RETRY replays the same course, only when `retry_ok`. Otherwise it is refused and the cup ends.
  - END clears the cup and goes to TITLE.
- **LOAD** takes the GP course from `fzx_cup_next_course()`. RESULTS never writes SELECT's session.
- **Clearing:** a soft reset or a new cup clears the cup.
- A player retire leaves the round and the prior totals intact.

### 11.6 Checks (Opus)

| Check | Tier |
| --- | --- |
| A scripted 6-round cup: ties, retired rivals, duplicate results, retry and exhaustion, and reset | HOST |
| Classification determinism: same inputs, same rows and hash; cold race and retry give equal classifications | HOST, EMULATOR |
| The loop on course 0 with TA course select: cold and retry equal, 0 over two fields, soft reset | EMULATOR |
| `ENG_SCENES=0` identity | EMULATOR (build) |

## Implementation status (claude/e3-scenes, 2026-11-19; EMULATOR, Ymir, modelled)

- **What runs:** `ENG_SCENES=1` (with `FZX_E1_DRIVE=1 E1_CLOSEUP=1 E1_CLOSEUP_EARLY=1`) runs the whole loop around the E1 drive with the 3D player: BOOT, TITLE, SELECT, LOAD, RACE, RESULTS, retry, RECORDS and soft reset, sequenced by SH2ZAM scene-1 (PR #43).
  - TITLE, SELECT, RESULTS and RECORDS are text placeholders until the C8 screens bind.
  - `ENG_SCENES=0` builds the E0, E1 pad and E1 driver images byte for byte as before.
- **Loading (2026-11-19, `4b45f54`):**
  - A cold load takes 352 fields (5.9 s), down from 1,643: the reads now stream sequential sectors from one play command (`FZX_CD_STREAM`), and the track pack isn't hashed a second time after the CD loader checked it (`FZX_LOAD_VERIFIED`).
  - The loading card changes at most every 2 fields: the VBlank interrupt draws the spinner and progress bar.
  - The loading screen takes a turn inside every CD read and in the track preparation loops, where the soft reset chord cancels the load and returns to TITLE. Cancels at 30, 150 and 300 fields return cleanly, and the next cold load races.
  - The longest gap between turns is 21 fields, in the path-pack open and the physics parse. Both files are pinned by their asset receipts, so those passes are the next items to bake offline.
- **VDP1 (2026-11-19, `c37e86b`):** the manager sends its own VDP1 list on every frame outside RACE (prelude, a black full-screen polygon, END), so menu scenes can add commands.
  - The earlier hang was a list put while Yaul's previous VDP1 sync was still pending. The manager now waits for the previous frame first, as the race's frames do.
  - Retirement is confirmed at the second manager frame after the fade out.
- **Still open:**
  - RESULTS doesn't keep the race backdrop.
  - The records service and the sound queue's driver aren't linked.
  - TITLE, SELECT, RESULTS and RECORDS are placeholders until the C8 screens bind.
