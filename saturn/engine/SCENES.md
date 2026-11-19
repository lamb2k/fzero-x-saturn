# Engine scenes: the boot-to-reset loop (DRAFT 1, 2026-11-19)

Interface between the scene manager and race scene (Opus, branch `claude/e3-scenes`) and the menu screens (Codex,
branch `codex/e3-c8-screens`). Answers C-064. Nothing binds until both sides accept this draft.

Owner (2026-11-19): "Yes boot to reset loop should be in progress with loading screens".

## 1. The loop and who owns what

```
BOOT -> TITLE -> SELECT -> LOAD -> RACE -> RESULTS -> (RETRY -> LOAD -> RACE) | TITLE
                                                RECORDS (from TITLE) -> TITLE
any scene + A+B+C+Start (soft reset) -> TITLE (session cleared)
```

| Scene | Owner | Notes |
| --- | --- | --- |
| BOOT | Opus | hardware init, RAM-cartridge check, records load, then TITLE |
| TITLE | Codex | original title art and attract; to SELECT or RECORDS |
| SELECT | Codex | course, mode, machine and skin, settings (slider); writes the session (section 3) |
| LOAD | Opus | animated loading screen; reads, decompresses and uploads the race set (section 7) |
| RACE | Opus | the engine race (E1 drive now, E2 rivals later); writes the finish snapshot (section 4) |
| RESULTS | Codex | results, name entry, records table, retry/title choice; calls the records service (section 5) |
| RECORDS | Codex | records view from the title |

Opus also owns: the scene manager, transitions and fades, reset execution and precedence, load cancellation, the
records storage backend, the sound-event queue, and the full race-after-retry integration check (C-064 item 5).
Retry is not a scene: it's an intent the manager carries out (section 6).

## 2. Scene entry points (SH2ZAM scene-1, generic; the game keeps the table)

Each scene is a table entry of five functions. All run on the master, once per presented frame unless noted.

```c
typedef struct {
    bool (*enter)(fzx_scene_ctx_t *ctx);          /* arena is empty; session readable; may return false = fault */
    uint32_t (*tick)(fzx_scene_ctx_t *ctx);       /* one presented frame; returns 0 or a request (section 6) */
    unsigned (*draw)(fzx_scene_ctx_t *ctx, vdp1_cmdt_t *list, unsigned capacity); /* into the build bank */
    void (*leave)(fzx_scene_ctx_t *ctx);          /* stop starting work; called once, when the transition begins */
    void (*release)(fzx_scene_ctx_t *ctx);        /* resources are free (section 8); restore VDP2; arena resets after */
} fzx_scene_ops_t;

typedef struct {
    const fzx_session_t *session;   /* read-only except in SELECT (fzx_session_edit) */
    const fzx_run_result_t *result; /* RESULTS only: the immutable finish snapshot; NULL elsewhere */
    sh2z_arena_t *arena_hi;         /* high work RAM, reset after release */
    sh2z_arena_t *arena_lo;         /* low work RAM, reset after release */
    sh2z_arena_t *arena_cart;       /* RAM cartridge (menus only; the race uses the resident set, section 7) */
    fzx_input_t input;              /* this frame's pad, edges already filtered (section 6) */
    uint32_t frame, epoch;          /* epoch: increments at every scene switch */
} fzx_scene_ctx_t;
```

- `enter` may not block for more than one frame. Big loads belong to LOAD.
- `draw` writes into the bank the manager gives it and never submits. The manager owns the VDP1 command banks
  (vdp1-banks-1) and the submit.
- Static data a scene keeps between frames lives in its arena, never in file-scope `static` that outlives the scene.
  The exception is constant tables.

## 3. Session selection (outside every arena)

```c
typedef struct {
    uint16_t version;      /* FZX_SESSION_VERSION */
    uint8_t course;        /* 0..N-1, original course order */
    uint8_t mode;          /* FZX_MODE_* (time attack, GP, practice, ...) as the C8 fixtures define */
    uint16_t machine_id;   /* stable machine identity, table below */
    uint8_t skin;          /* colour/skin index for that machine */
    uint8_t slider;        /* exact accel/top-speed slider setting, original units */
    uint8_t player_render; /* FZX_RENDER_3D or FZX_RENDER_SPRITE: renderer choice, not identity */
    uint8_t confirmed;     /* 1 after SELECT confirms; RACE refuses to start on 0 */
} fzx_session_t;
```

- A single static `fzx_session` in low work RAM. It survives every scene teardown and retry. Soft reset clears it to
  defaults; title-from-results keeps it.
- Only SELECT writes it, through `fzx_session_edit()`, and sets `confirmed`. LOAD copies it into race-local state
  once at its start ("apply once"). Later edits never reach a race in progress.
- `player_render` defaults to 3D (owner 2026-11-19, "I would say 3d"). Rival rendering is undecided and isn't part
  of the session.
- **Machine identity:** `machine_id` 0..29 are the original roster in its original order. 30 and 31 are reserved
  now for the two guest racers, so saves never renumber when they are enabled.
  - Guests are hidden in select, race and records until the owner approves their look. That is one build flag,
    `FZX_GUESTS`, default 0.
  - A save containing guest IDs loads in a guest-off build: those rows display as their stored ID with a generic
    name and are never dropped.
  - IDs 32..255 are reserved.

## 4. Finish snapshot (immutable)

RACE writes it once at its end, before requesting the transition. The manager hands RESULTS a `const` pointer;
nothing writes it after that.

```c
typedef struct {
    uint32_t run_id;       /* increments per race start (boot = 0); identifies the attempt */
    uint8_t reason;        /* FZX_END_FINISH, FZX_END_RETIRE (energy 0), FZX_END_QUIT (pause menu) */
    uint8_t course, mode, skin;
    uint16_t machine_id;
    uint8_t slider, laps_done;
    uint32_t race_ms;      /* valid for FINISH */
    uint32_t lap_ms[FZX_LAPS_MAX];
    uint32_t best_lap_ms;
    uint32_t max_speed;    /* km/h x 100 as shown on the HUD */
} fzx_run_result_t;
```

A soft reset produces no snapshot: the attempt is dropped. QUIT and RETIRE snapshots are shown but never ranked.

## 5. Records service (Opus storage, Codex screens)

- **Who ranks:** the records service. `fzx_records_rank(const fzx_run_result_t *)` is a pure function. It returns the
  race-time place (0 = none) and whether the best lap is a new record, for that course and mode. RESULTS calls it
  and shows name entry only when there is a place or a new best lap.
- **Commit:** `fzx_records_commit(const fzx_run_result_t *, const char name[FZX_NAME_BYTES])`, once per `run_id`.
  A second commit with the same `run_id` returns `FZX_REC_ALREADY` and changes nothing.
- **Schema:** per course and mode, the top 5 race times plus one best lap. Each row holds:
  - `{race_ms or lap_ms, machine_id, skin, slider, name[8]}`.
  - Name: the alphabet and visible length follow the legacy C8 name-entry fixture (Codex states them). It is stored
    in 8 bytes as glyph indices, padded with 0xFF.
- **Empty defaults:**
  - the original game's default records table, if the C8 baseline has one (Codex says which);
  - otherwise empty rows, with time 0xFFFFFFFF and name all 0xFF, which display as "--".
- **Storage:** internal backup RAM, through the BIOS backup library.
  - Two files, `FZXREC_A` and `FZXREC_B`, each with a header {magic, version, generation, size, CRC-32}.
  - A commit writes the older generation's file. If a write is interrupted, the other file is still the previous
    valid save.
- **Load result:** `fzx_records_load()` returns one of:
  - `OK`;
  - `ABSENT` (no file, defaults in RAM);
  - `FULL` (no room to create the files; records work in RAM for the session, and RESULTS shows a one-line notice);
  - `FAILED` (both files unreadable or corrupt: defaults in RAM, and a notice).

  `version` mismatches are upgraded on load or treated as `FAILED`, never silently misread.
- **Commit result:** `OK`, `ALREADY`, `FULL` or `FAILED`. The RAM table is updated on any result except `ALREADY`.

## 6. Requests, retry, reset and input

- `tick` returns one of:
  - `FZX_REQ_NONE`;
  - `FZX_REQ_GOTO(scene)`;
  - `FZX_REQ_RETRY`;
  - `FZX_REQ_TITLE`;
  - `FZX_REQ_END_RACE` (RACE only, after writing the snapshot).

  The manager holds at most one pending request.
- **Precedence**, highest first: soft reset > END_RACE > RETRY / TITLE / GOTO. A higher request replaces a pending
  lower one until the fade-out completes. After that, only soft reset can still pre-empt.
- **Retry:** one confirmed intent, from RESULTS (or the pause menu). The manager:
  1. settles the outgoing attempt once (the snapshot is final; RESULTS has already committed or chosen not to);
  2. keeps the session and the committed records;
  3. resets the race-local arena;
  4. keeps the resident race set (section 7);
  5. runs LOAD on the fast path, then a fresh RACE with `run_id + 1`.
- **Title:** RESULTS to TITLE keeps the session, and the menu cursors restore from it.
- **Soft reset (A+B+C+Start, all held):** from any scene, including mid-load and mid-fade. It cancels any outstanding
  load, drops the attempt, clears the session to defaults and releases the resident race set, then goes to TITLE.
  Records already committed stay.
- **Input:**
  - The manager reads the pad once per presented frame and gives scenes level and press-edge bits.
  - The buttons whose edge triggered a transition are latched as consumed. They report no edge until they have been
    released, across fades and loads, so a held A doesn't confirm on the next screen.
  - Soft reset is detected by the manager before scenes see the input.

## 7. Loading and the resident race set

- **LOAD** shows an animated loading screen: the course name card from the original art if the ROM has one, else a
  text card in the original font (the report says which). It runs the load as chunks between presented frames, each
  chunk under about 10 ms, so the animation never freezes.
  - Chunks are: CD read, cell4z decompression into the slot cache, the 3D player model, the track packs and the HUD.
  - A CD read in flight finishes its sector batch before a cancel takes effect, and the data is discarded.
- **Music:** LOAD fades the CD music out before its first disc read (the `ENG_CDDA` hook from `claude/e4-sound`) and
  RACE starts the course track.
- **Resident race set:** keyed by (course, machine_id, skin, player_render). It holds the track packs, racer art in
  the cartridge, the 3D player model and the physics pack.
  - It survives retry, so the retry load skips it.
  - It is released on TITLE, a soft reset or a key change.
  - Its memory is a fixed region, not a scene arena.

## 8. Video resources and the hand-over

- **The manager owns:**
  - the VDP1 command banks (vdp1-banks-1);
  - VDP2 colour offset A and its enable bits, used for every fade (unused by the E0 map, checked in
    `VDP2_MAP.md`);
  - the NBG3 text layer (dbgio), and the back-screen colour during transitions.
- **A scene owns, between `enter` and `release`:**
  - all of VDP1 VRAM outside the command banks (textures, CLUTs, Gouraud tables);
  - all VDP2 VRAM and CRAM except NBG3 and the colour offset;
  - its arenas.

  Each scene declares its VDP2 layers and CRAM ranges in a comment block at the top of its file, and the manager's
  reset check covers them.
- **Retirement rule:**
  1. After `leave`, the manager draws the fade frames itself (the scene's last list plus the colour offset, or an
     empty list once fully faded).
  2. It calls `release` only after the scene's last submitted list has been displayed and then replaced, so neither
     bank holds the scene's commands. With double banks that is the second presented frame after the scene's last
     `draw`.
  3. Only then may the scene's VDP1 VRAM be overwritten.
- **VDP2 restore:** in `release`, the scene returns every VDP2 register, VRAM range and CRAM range it changed to the
  baseline (screens off, the boot map). The next scene's `enter` sets up its own and assumes nothing.

## 9. Sound events

- `fzx_snd_event(id)` posts to a queue owned by the manager. Each event carries the poster's `epoch`.
- The manager delivers each event once, at the next frame boundary, and drops events posted by a scene after its
  `leave`. That way a confirm sound from a menu plays once, and a race's boost sound never plays on the results
  screen.
- Music commands (`play track`, `fade out`) are separate calls, owned by the manager and LOAD.

## 10. Checks

| Check | Owner |
| --- | --- |
| Menu screens against their existing C8 fixtures (select/settings, results/name entry/table, retry UI) | Codex |
| Knob-off identity: with `ENG_SCENES=0` the E1 drive image is byte-identical | Opus |
| The race inside the loop finishes at the same time and final state as E1 standalone | Opus |
| Race after retry is bit-identical to race 1 from a cold boot (full engine race, C-064 item 5) | Opus |
| Clean reset: after N retries and title round trips, the arena high-water marks, stack low-water, VDP1/VDP2 maps, cartridge map and live sentinels all match pass 1 | Opus |
| Soft reset from each scene, including mid-load and mid-fade | Opus |
| No interval over two fields in TITLE, SELECT, LOAD and RESULTS | both (each for its scenes) |
| Records: interrupted commit keeps the previous save; FULL and FAILED paths; a second commit for one run_id refused | Opus (backend), Codex (screens) |

Open for Codex to fill in:
- the mode list;
- the name alphabet and length;
- whether the original has a default records table;
- each menu scene's VDP1/VDP2 claim.
