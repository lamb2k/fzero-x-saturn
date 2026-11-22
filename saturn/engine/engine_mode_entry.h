/* Race mode entry and outcome contract (ENG_MODES=1; plan P3 item 3; DRAFT for the front end, Codex C-137 / C-138).
 * Plain C, no hardware: the same file builds for the HOST fixture (tests/mode_entry_driver.c).
 *
 * Who owns what (C-137):
 *   front end  navigation, signs, course/machine/class choice, Back and confirm, display of the result;
 *              it writes fzx_session (and only after fzx_mode_entry_check returns FZX_ENTRY_OK);
 *   modes      which mode runs, whether ordinary entry is open, which course/machine/class combinations are legal,
 *              the normalised entry, race setup (field, laps, energy), termination and outcome, which record table a run
 *              may reach, and the cup progress call;
 *   records    the tables, their ranking and the one write (engine_records.c). Modes names the table; records writes it.
 * The entry check is pure: it reads the request and the environment, and writes only its output. A rejected or cancelled
 * entry therefore leaves fzx_session byte for byte as it was. */
#ifndef FZX_ENGINE_MODE_ENTRY_H
#define FZX_ENGINE_MODE_ENTRY_H
#include <stdbool.h>
#include <stdint.h>
#include "engine_modes.h"
#include "engine_scenes.h"

/* ---- capability query (what the front end may draw and offer) -------------------------------------------------- */
#define FZX_CAP_RUNS       0x01u /* the engine runs this mode (Versus, Options and unknown IDs: no bits at all) */
#define FZX_CAP_ENTRY_OPEN 0x02u /* ordinary entry is open; clear = show "unavailable" (Death Race until its win path qualifies) */
#define FZX_CAP_CLASS_PICK 0x04u /* the player chooses the class; clear = the class is forced (Time Attack none, Death Race Master) */
#define FZX_CAP_FIELD      0x08u /* the 30-car field races (clear: the player alone) */
#define FZX_CAP_ENDLESS    0x10u /* no finish at the lap count; ends by outcome or QUIT (Practice, Death Race) */
#define FZX_CAP_CUP        0x20u /* the cup manager runs the rounds; the course comes from the cup (Grand Prix) */
#define FZX_CAP_KEEPS_TIMES 0x40u /* a run may rank in a record table (Time Attack, Death Race) */
#define FZX_CAP_WINS_BY_KO 0x80u /* won when every rival is retired (Death Race) */
uint32_t fzx_mode_caps(uint32_t menu_mode);

/* ---- entry -------------------------------------------------------------------------------------------------------- */
typedef struct {            /* what the front end proposes (its cursor state) */
    uint8_t menu_mode;      /* ModeSelect ID: GP 0, TA 1, Death Race 2, Practice 5 (the session's IDs, unchanged) */
    uint8_t course;         /* course ID (engine_cup.h catalog) */
    uint8_t difficulty;     /* the chosen class 0 Novice .. 3 Master; ignored when the mode forces its class */
    uint16_t machine_id;
    uint32_t slider_bits;   /* binary32 balance, as SELECT keeps it */
} fzx_entry_req_t;

typedef struct {            /* the normalised entry: exactly what the front end copies into fzx_session */
    uint8_t menu_mode, course, difficulty, cup, skin;
    uint16_t machine_id;
    uint32_t slider_bits;
} fzx_entry_t;

typedef enum {
    FZX_ENTRY_OK = 0,
    FZX_ENTRY_NO_MODE,       /* Versus, Options or an unknown ID */
    FZX_ENTRY_CLOSED,        /* the mode runs but ordinary entry is not open (Death Race) */
    FZX_ENTRY_NO_COURSE,     /* the course is not in the catalog for this mode (avail bit) */
    FZX_ENTRY_NO_MACHINE,    /* the machine is not allowed */
    FZX_ENTRY_CLASS_LOCKED   /* the class is not open (Master before every cup is cleared on Expert) */
} fzx_entry_status_t;

typedef struct {            /* what the engine knows, passed in so the check stays pure */
    uint8_t (*course_avail)(uint8_t course); /* catalog avail bits (FZX_AVAIL_*), 0 = not in the catalog */
    bool (*machine_ok)(uint16_t machine_id);
    bool (*class_open)(uint8_t difficulty);  /* fzx_records_class_open */
    bool lab;                                /* harness only: lets a gated mode in (the script word fzx_mode_lab) */
} fzx_entry_env_t;

/* Validates REQ. On FZX_ENTRY_OK fills OUT; on any other status OUT is left untouched. Never writes fzx_session.
 * Practice and GP take the class as chosen; Time Attack and Death Race take their own (fzx_mode_class). Every status is
 * decided before the launch: the front end shows it and stays on SELECT. */
fzx_entry_status_t fzx_mode_entry_check(const fzx_entry_req_t *req, const fzx_entry_env_t *env, fzx_entry_t *out);

/* ---- outcome (what RESULTS shows and what is written) ---------------------------------------------------------------- */
enum { FZX_OUTCOME_NONE = 0, FZX_OUTCOME_FINISH, FZX_OUTCOME_WIN, FZX_OUTCOME_RETIRED, FZX_OUTCOME_QUIT };
typedef struct {
    uint8_t kind;           /* FZX_OUTCOME_*: FINISH (lap-count finish), WIN (Death Race: all rivals retired), RETIRED, QUIT */
    uint8_t record_table;   /* FZX_MODE_REC_TA / FZX_MODE_REC_DEATH, or FZX_MODE_NO_RECORD: the one table this run may reach */
    uint8_t record_eligible;/* 1: the front end calls fzx_records_commit once for this run (records still ranks it) */
    uint8_t gp_points;      /* 1: the cup manager scores this race (Grand Prix finish) */
} fzx_outcome_t;
/* Pure function of the run the engine snapshotted at the end of the race (fzx_scn_result). Practice never yields a record or
 * points; Death Race yields a record only for the WIN; Time Attack only for a finish. Every end still goes to RESULTS in the
 * engine today (RETIRE and QUIT show "no record"); where the front end sends the player next is its own routing. */
void fzx_mode_outcome(const fzx_run_result_t *run, fzx_outcome_t *out);

/* ---- race state the front end shows (BINDINGS, not yet a function) ------------------------------------------------------
 * The authoritative numbers are the engine's; the HUD reads them, it does not keep its own:
 *   laps completed  fzx_e1_report[1]      elapsed ms     fzx_e1_report[2]      finished  fzx_e1_report[3]
 *   lap times ms    fzx_e1_report[13..15] (the first three; the lap ring keeps 8)
 *   rivals retired  fzx_field_retired_now()   rivals total  FZX_FIELD_CARS - 1 (0 in Time Attack)
 * Practice shows the lap as ((laps completed) % 3) + 1 (the source wrap 3 -> 1, racer.c) and never a finish. */
#endif
