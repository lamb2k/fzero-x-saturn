# Engine scenes: the boot-to-reset loop (DRAFT 6, 2026-11-19)

Interface between the scene manager and race scene (Opus, branch `claude/e3-scenes`) and the menu screens (Codex,
branch `codex/e3-c8-screens`).
- Draft 1 (e4ac275) answered C-064; draft 2 (16c00a9) carries C-065; draft 3 makes the guest gate per identity (C-066).
- Draft 4 adds section 11, course selection and the Grand Prix cup (C-084, branch `claude/e3-cup`).
- Draft 5 revises 11.3 to 11.7 per C-086: reference trigger and projection, outcomes and spare-machine transactions, manager transitions, canonical hashes.
- Draft 6 (C-087): END keyed to the accepted run_id, the pause-retry lock before any change, outcome-only records for RETIRE, QUIT and PAUSE_RETRY, and the trigger as menu updates.
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
    identity n). The P3 guest build enables both bits.
    - An enabled guest is selectable through select, race, results and records. A disabled guest is hidden in all
      four.
    - Owner lineup choice (C-066): Robotnik (33) is one of the three selectable racers in the C8 machine-select
      lineup. Metal Sonic (34) is tracked separately and has its own bit.
  - A build with a guest's bit clear keeps that guest's rows in a save, showing a generic machine name and preview.
    Its identity is never dropped or renumbered.
  - P3 SELECT includes the 30 stock machines and the enabled guests. Up/down changes machine,
    L/R adjusts balance in steps of 1/128, X chooses minimum and Z chooses maximum.
    The two initial guest handling presets are Black Bull for Dr. Robotnik's Eggmobile and
    Sonic Phantom for Metal Sonic. Both keep their guest IDs through LOAD, RESULTS and RECORDS.
  - RECORDS opens from TITLE with B. Left/right wraps across the six Jack courses,
    C switches between Time Attack and Death Race, and B or Start returns to TITLE.
    Empty entries display dashes. Browsing never writes a record.

  To reproduce the P3 scenes disc, stage the 30 player meshes in `generated/mach3d`.
  Run these from the repository root on the host with the private source assets available:

  ```sh
  python3 tools/machine_tuning.py --rom "$FZX_TEST_ROM"
  python3 tools/guest_machine_packs.py "$GUEST_KIT/player-3d"
  python3 tools/engine_select_art.py "$PLAYER_SPRITE_SETS" generated/select_art --guest-ui "$GUEST_KIT/ui"
  python3 tools/engine_load_cards.py generated/course_menu generated/c8_cards/engine_load_cards.h
  ```

  `GUEST_KIT` is the private hybrid-guests directory. `PLAYER_SPRITE_SETS` contains the
  checked player-profile folders `c00s1` through `c29s1`. The loading-card generator
  also reads the sibling `generated/course_menu_preview` pack. Stage the contents of
  `$GUEST_KIT/title-cover/native-title` in `generated/title_guest`. The build reads the
  pack and its identity header from that same directory. Use `ENG_TITLE_ART=title` for the
  original title or `ENG_P3_UI=0` for the pre-P3 scenes build.
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

## 11. Course catalog and Grand Prix cup (DRAFT 6, C-084, C-086, C-087)

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

- The courses lanes supply the rows and per-course packs (`claude/jack`, COURSES.md section 9). The one-disc scenes
  image (`ENG_COURSE_SELECT=1`, the scenes default) lists the six Jack rows, each with `FZX_AVAIL_TA` when the bundle
  carries its packs (`FZX_COURSE_BUNDLE_MASK`); GP stays closed until the GP field and results path are signed off. A
  single-course image lists courses 0 and 1 and opens only its own. The HOST default is course 0 (TA) plus course 1
  with `avail = 0`.
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
    uint8_t outcome, player_place, player_kos, valid;   /* outcome: FZX_OUT_* (11.4) */
    uint32_t result_hash, attempt_hash;             /* section 11.6 */
    fzx_class_row_t row[FZX_ENTRANTS_MAX];          /* official order, place 1 first; rows [count..] zero */
} fzx_classification_t;
const fzx_classification_t *fzx_cup_classification(void);   /* const for RESULTS */
```

**Trigger (FINISH only).** In GP, RACE finalizes the classification on the 442nd menu update that observes the
player's `RACER_STATE_FINISHED`.
- That is the official trigger: each menu update that observes FINISHED adds one to `sPlayerFinishTimer`, so the
  first observing update sets it to 1.
- `menus.c` calls `func_8008AA8C` once `sPlayerFinishState` is `PLAYER_FINISH_SUCCESS_COMPLETE`, which means
  `sPlayerFinishTimer > sFinishedSuccessTime`. That value is the sum of the five `gFinishedSuccessScript` times:
  88 + 23 + 23 + 23 + 284 = 441.
- RACE runs one menu update per 60 Hz simulation tick, starting with the tick that sets the player's finished flag.
- The emulator probe records three values to anchor that offset: the tick that set the flag, the timer at
  classification, and the classification frame.
- The loser camera (`CAMERA_MODE_FINISHED_LOSER`, timer > 300) occurs only in Time Attack and Death Race, so it never
  triggers a GP classification.
- Rivals race on until the trigger. Time Attack produces no classification, as in the original.

**RETIRE, QUIT and PAUSE_RETRY are outcome-only.** These can happen before any finish timer starts. RACE writes an
immediate, unscored record at the event (`fzx_cl_outcome_only`):
- `count = 0`, no rows, `player_place = 0`, the outcome and `player_kos`, and both hashes over that header;
- it never waits for the trigger and never projects times for an aborted attempt;
- the scored places and totals of earlier rounds are untouched, and `fzx_cup_accept` checks `count` against the
  outcome (all entrants for FINISH, 0 otherwise).

**Inputs.** Each entrant's state at the trigger, in the original's terms (`Racer` fields):

```c
enum { FZX_CLIN_RUNNING = 0, FZX_CLIN_FINISHED, FZX_CLIN_FALLING, FZX_CLIN_RETIRED };
typedef struct {
    uint8_t entrant, state, position, laps_done;    /* position: Racer.position before the trigger (1-based) */
    int32_t race_ms;                                /* Racer.raceTime: finish time, or the race clock if running */
    int32_t lap_ms[3];                              /* Racer.lapTimes (0 = not completed) */
    float race_distance;                            /* Racer.raceDistance, the course's units */
    int32_t retire_frame;                           /* RETIRED: sRaceFrameCount when it retired */
    uint8_t retire_position;                        /* RETIRED: Racer.position when it retired */
} fzx_cl_in_t;
```

**Finalization.** It follows `func_8008AA8C`, `Racer_RetireRacer` and `Racer_UpdateRacePositions` in the original's
arithmetic: `s32` times and `f32` distances, with `FLT_EVAL_METHOD` 0 on both HOST and the SH-2.
- **Finished:** the row keeps its time.
- **Retired:** a retired machine is crashed in the original's terms. Its time is 0, and it keeps the
  `raceDistancePosition` it got when it retired: `(retire_frame + ((count - retire_position) / 10.0f)) - 600000.0f`.
- **Falling:** a machine falling off the track is retired at finalization, with that formula at the trigger frame
  and its current position.
- **Running:** the row gets the reference projection and status `FZX_CL_PROJECTED`:
  - `d = lap_ms[1] != 0 ? max(lap_ms[0] - lap_ms[1], 0) : 0`
  - `time = (s32)(((race_ms - d) * laps) * course_length / race_distance) + d`
  - The multiplication and division are `f32`, truncated toward zero, as in the source.
- **Order** (`Racer_UpdateRacePositions`):
  - Classified rows fill from place 1 by ascending time. On equal times the higher entrant goes ahead, because the
    original scans from the last racer with a strict `<`.
  - Retired rows fill from the last place forward by ascending `raceDistancePosition`. On equal values the higher
    entrant goes further back.
- **Scoring:** only when the player finished (outcome `FZX_OUT_FINISH`). Every place from 1 through the last row
  with a nonzero time has `scoring = 1`. Every other row has 0.
- **Times:** the race clock is ticks × 50 / 3 ms for everyone. The player's row is the section 4 `race_ms`.

### 11.4 Attempt outcomes, spare machines and retry

```c
enum { FZX_OUT_FINISH = 1, FZX_OUT_RETIRE, FZX_OUT_QUIT, FZX_OUT_PAUSE_RETRY };
typedef struct { int8_t balance, menu_lives; uint8_t kos, retry_locked; } fzx_cup_live_t;
const fzx_cup_live_t *fzx_cup_live(void);   /* read-only, published during RACE */
```

| Event | Original | Applied by the manager |
| --- | --- | --- |
| Cup start | `gTotalLives[difficulty]`: 5, 4, 3, 2 | `balance` is set |
| Race start | `sPlayer1Lives = gPlayerLives[0]` (`menus.c` init) | `menu_lives = balance`, `kos = 0` |
| The 5th KO in a race | `gPlayerLives[0]++` and `sPlayer1Lives++` | `fzx_cup_race_ko(run_id)`: on the 5th, both +1, once per `run_id` |
| A player retire | pending −1 (`D_800F80C4`), applied once at the menu change | at `fzx_cup_accept` of the RETIRE outcome |
| A pause-menu retry | `Menus_PayGpRestartLife`, locked when `sPlayer1Lives <= 0` | the PAUSE_RETRY outcome: −1 once, then the same round restarts |
| Quit | ends the cup | the QUIT outcome: no charge; the cup ends |

- **Retry after a retire:** allowed when `menu_lives > 0`. A start at 0 plus the 5th KO allows a retry; 4 KOs don't.
- **No second charge:** the retire has already paid, so RESULTS' RETRY costs nothing.
- **Exhaustion:** with `menu_lives <= 0`, a retire ends the cup (game over).
- **Pause retry:** RACE offers it only when `retry_locked` is 0.
- **One owner:** RACE reports events (KOs, the attempt's outcome); only the manager changes the balance.

### 11.5 Cup manager (outside every arena)

```c
typedef struct {
    uint8_t state, cup, difficulty, round;           /* FZX_CUP_IDLE .. FZX_CUP_OVER */
    uint32_t generation, run_id, accepted_run_id;    /* the attempt racing; the attempt applied */
    fzx_cup_live_t live;                             /* 11.4 */
    fzx_entrants_t entrants;
    int32_t total[FZX_ENTRANTS_MAX];                 /* written only through C8's scoring hook */
    uint8_t last_place[FZX_ENTRANTS_MAX];            /* latest scored race position (C8's tie-break) */
    uint8_t grid[FZX_ENTRANTS_MAX];                  /* next race's grid order (D_80115D50) */
    fzx_classification_t last;                       /* the applied classification, kept through FINAL */
} fzx_cup_t;
const fzx_cup_t *fzx_cup(void);
typedef void (*fzx_cup_score_fn)(const fzx_classification_t *c, int32_t total[FZX_ENTRANTS_MAX]);
void fzx_cup_score_hook(fzx_cup_score_fn fn);        /* C8's points table */
uint8_t fzx_cup_next_course(void);                    /* LOAD's course in GP; FZX_COURSE_NONE outside READY */
```

One static in low work RAM, next to the session: scene arenas never hold cup state. Section 4's `reason` gains
`FZX_END_PAUSE_RETRY` (4), so the finish snapshot and the classification name the same four outcomes.

**States:**

| State | Meaning |
| --- | --- |
| `IDLE` | no cup |
| `READY` | round r loads next |
| `RACING` | attempt `run_id` is in progress |
| `SCORED` | a finish has been applied |
| `RETIRED` | a retire has been applied, and retry is allowed |
| `FINAL` | after round 6's scored finish |
| `OVER` | exhaustion or quit |

**Transitions.** Each one checks its preconditions first, and changes nothing when they don't hold:

| Call | From | Check | To |
| --- | --- | --- | --- |
| `fzx_cup_begin(cup, difficulty, entrants)` | any | the cup is defined; complete, unique entrants | `READY`, round 0 (clears everything first) |
| `fzx_cup_race_start(run_id)` | `READY` | a new `run_id` | `RACING` |
| `fzx_cup_race_ko(run_id)` | `RACING` | `run_id` is current | `RACING` |
| `fzx_cup_accept(c)` | `RACING` | `run_id` is current; cup, round and course match; `count` is all entrants (FINISH) or 0 (other outcomes); the rows name a complete, unique entrant set; `player_kos` matches the live count; hashes valid. PAUSE_RETRY also needs `menu_lives > 0`, checked before any change | by outcome: FINISH → `SCORED`, or `FINAL` after the last round; RETIRE → `RETIRED`, or `OVER` on exhaustion; PAUSE_RETRY → `READY` (same round); QUIT → `OVER` |
| `fzx_cup_intent(NEXT, run_id)` | `SCORED` | `run_id` is the accepted one | `READY`, round + 1 |
| `fzx_cup_intent(RETRY, run_id)` | `RETIRED` | `run_id` is the accepted one | `READY`, same round, no charge |
| `fzx_cup_intent(END, run_id)` | `FINAL`, `OVER`, `SCORED`, `RETIRED` | `run_id` is the accepted one | `IDLE` (cleared) |
| `fzx_cup_reset()` | any | none | `IDLE`; the generation counter moves, so old results and intents are refused |

- **Consumed once:** each result and each navigation is consumed once.
  - Refused: a second accept of the same attempt, and any NEXT, RETRY or END whose `run_id` is not the accepted one.
    That includes a delayed input from an earlier round or from a cup before a reset; it changes no state, totals
    or rows.
  - `fzx_cup_reset()` is the one unconditional path.
- **Pause-retry lock:** `fzx_cup_accept` enforces it before any change. A current, correctly hashed PAUSE_RETRY with
  `menu_lives <= 0` fails with no debit and no transition.
- **Points and tie-breaks:**
  - Only FINISH calls C8's scoring hook. It does so once, and also records `last_place` (C8's tie-break).
  - RETIRE, PAUSE_RETRY and QUIT leave the totals and the places as they were.
  - A scored finish can't be retried. The original's GP finish menu offers only the next course (`MENU_STATE_GP_NEXT_COURSE`); retry comes from the pause menu (PAUSE_RETRY) or the retire screen (RETIRED). So no points are banked twice.
- **FINAL:** keeps the round-6 classification and the totals until END, for C8's final standings.
- **Next grid:** NEXT keeps each entrant's finishing position for the next race's grid (`D_80115D50`, `menus.c`).
- **RESULTS** writes only intents. **LOAD** takes the GP course from `fzx_cup_next_course()`. A soft reset calls
  `fzx_cup_reset()`.

### 11.6 Hashes and comparisons

- **FNV-1a:** 32-bit, offset basis 2166136261, prime 16777619, over this byte sequence. Integers are little-endian
  at their stated width.
- **Result bytes:**
  - Header: `cup` u8, `round` u8, `course` u8, `count` u8, `outcome` u8, `player_place` u8, `player_kos` u8.
  - Then, for each of the `count` active rows in place order: `entrant` u8, `status` u8, `scoring` u8, `laps_done`
    u8, `time_ms` u32.
- **`result_hash`:** FNV-1a over the result bytes. It doesn't include `run_id`. It is the sporting comparison between
  a cold race and a retry (same initial state and inputs).
- **`attempt_hash`:** FNV-1a over `run_id` (u32) followed by the result bytes. It is the attempt's identity.
- **Comparisons:** a determinism check compares `result_hash` and also every field of the header and the active rows
  directly.
- **HOST reference:** the HOST cup test compiles `func_8008AA8C`, `Racer_RetireRacer` and
  `Racer_UpdateRacePositions` from the pinned decompilation. For each frozen case it compares the ordered rows,
  times and scoring places against `fzx_classify` exactly.
- **Frozen cases:** a positive `d`, a falling racer, equal times, close retirements, and a machine that hasn't left
  the line.

### 11.7 Checks (Opus)

| Check | Tier |
| --- | --- |
| Frozen classification cases against the original functions (11.6) | HOST |
| A scripted 6-round cup: ties, retired rivals, duplicate and delayed inputs (an END from an earlier round and from before a reset), outcome-only retire, quit and pause retry, the pause-retry lock (a start at 0 with 4 KOs refused with no debit; the 5th KO allows exactly one debit), retry and exhaustion, a finish that can't be retried, FINAL kept until END, and reset | HOST |
| Classification determinism: cold race and retry give equal `result_hash` and equal rows | HOST, EMULATOR (GP trigger probe on course 0) |
| The loop on course 0 with TA course select: cold and retry equal, 0 over two fields, soft reset | EMULATOR |
| `ENG_SCENES=0` identity | EMULATOR (build) |

### 11.8 Receipts (claude/e3-cup, 2026-11-19)

**HOST** (`tests/test_cup.py`, WSL GCC with UBSan): 2 tests OK.
- Frozen cases plus 400 random fields match the original functions row for row.
- The scripted 6-round cup passes.
- All 8 negative controls fail, as they should.

**EMULATOR** (Ymir, `dd852f8`, `ENG_SCENES=1`, course 0, Time Attack, harness `claude-cup`):
- **GP trigger probe:** cold race and retry give `result_hash` 0x9DA78BC3, equal, with attempt hashes that differ.
  - The player is 13th of 30, with 7 projected rows.
  - The finish is at drive tick 6663; timer 442 at field frame 6684.
- **Loop:** cold and retry both finish at 104,040 ms; checkpoints and tick-2000 stills are equal; memory is equal
  over 3 races.
- **Pace:** races and menus have 0 intervals over two fields (worst 2).
- **Reset:** 8 marks and 7 fires.
- **TA course select:** course 1 is refused and course 0 confirmed.
- **Identity:** `ENG_SCENES=0` matches `origin/engine` 5dcec2c byte for byte (E1 pad, E1 driver, E0).
- **Open:** load cancels at 100 and 180 fields fail with `ENG_SND=1`, on plain `origin/engine` 5dcec2c too. With
  `ENG_SND=0`, all three cancels pass.

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
