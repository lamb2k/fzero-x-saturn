/* Engine records service (ENG_SCENES=1; SCENES.md section 5): the time-attack and
 * death-race tables in RAM, ranked by the source rule, kept in internal backup RAM
 * as two files (FZXREC_A, FZXREC_B) with a generation number and a CRC-32, so an
 * interrupted write always leaves the previous valid save. */
#ifndef FZX_ENGINE_RECORDS_H
#define FZX_ENGINE_RECORDS_H
#include <stdbool.h>
#include <stdint.h>
#include "engine_scenes.h"

#define FZX_REC_COURSES 24u
#define FZX_REC_TOP 5u
#define FZX_REC_MAX_TIMER 3599999u      /* empty time / best lap (Save_InitCourseRecord) */
#define FZX_REC_SLIDER_DEFAULT 0x3F000000u /* binary32 0.5 */
enum { FZX_REC_TA = 0, FZX_REC_DEATH_RACE = 1, FZX_REC_KINDS = 2 };

typedef struct {
    uint32_t ms;
    uint32_t slider_bits;
    uint16_t machine_id;
    uint8_t skin, pad;
    char name[4];                       /* ASCII, NUL padded, up to 3 visible; blank = all 0 */
} fzx_rec_time_t;                       /* 16 bytes */

typedef struct {
    fzx_rec_time_t top[FZX_REC_TOP];    /* race times, best first (time attack) */
    fzx_rec_time_t best_lap;
    uint32_t max_speed;                 /* km/h x 100 */
    uint16_t max_speed_machine_id;
    uint8_t best_lap_index, pad;
} fzx_rec_course_t;                     /* 104 bytes */

#if defined(ENG_MODES) && ENG_MODES
/* Cup progress (plan P3 item 3; common.c func_8007DEF0 and save.c Save_UpdateCupCompletion): which difficulties each cup
 * has been cleared on, and which machines won which cup at which difficulty. The unlock level (func_8007E2B4) is
 * computed from it: Master opens once every cup is cleared on Expert. */
#define FZX_REC_CUPS 4u /* Jack, Queen, King, Joker */
typedef struct {
    uint8_t cup_cleared[FZX_REC_CUPS]; /* gCupNumDifficultiesCleared: difficulties cleared, 0..4 */
    uint8_t credits_due, pad[3];       /* gForceCredits: every cup cleared on Expert, the ending not yet shown */
    uint16_t cup_done[4][10];          /* cupCompletion[difficulty][machine / 3]: bit (machine % 3) * 5 + cup */
} fzx_rec_progress_t;                  /* 88 bytes */
#endif

typedef struct {
    uint32_t magic, version, generation, boot_id, bytes, crc; /* crc over everything after it */
    fzx_rec_course_t ta[FZX_REC_COURSES];
    fzx_rec_time_t death_race[FZX_REC_COURSES]; /* one time per course (its own record) */
#if defined(ENG_MODES) && ENG_MODES
    fzx_rec_progress_t progress;        /* version 2; a version 1 file loads with this zeroed */
#endif
} fzx_records_t;

/* Results (SCENES.md section 5). */
typedef enum { FZX_REC_OK, FZX_REC_ABSENT, FZX_REC_FULL, FZX_REC_FAILED, FZX_REC_ALREADY } fzx_rec_result_t;

typedef struct {
    uint8_t place;        /* 1..5 in the top-5 race times, 0 = none (a name is asked only for 1..5) */
    uint8_t best_lap;     /* 1 = new best lap */
    uint8_t best_lap_index;
    uint8_t max_speed;    /* 1 = new fastest speed */
} fzx_rank_t;

/* Boot: reads both files, keeps the valid one with the higher generation, takes a new
 * boot_id (saved at once, so run ids never repeat across boots). [Diagnostics in
 * fzx_rec_report.] */
fzx_rec_result_t fzx_records_load(void);
/* Pure: what this run would change (no table change). QUIT and RETIRE rank nothing. */
void fzx_records_rank(const fzx_run_result_t *run, fzx_rank_t *out);
/* Once per run_id: insert into the RAM table, then write storage. ALREADY for a second
 * commit of the same run (nothing inserted). After FULL/FAILED the table stays dirty. */
fzx_rec_result_t fzx_records_commit(const fzx_run_result_t *run, const char name[4]);
/* Retry storage for a dirty table (no insertion). OK when clean. */
fzx_rec_result_t fzx_records_flush(void);
/* Captures: write the table again now (with fzx_rec_fault, the interrupted-write controls). */
fzx_rec_result_t fzx_records_rewrite(void);
#if defined(ENG_MODES) && ENG_MODES
/* A Grand Prix cup won (first overall, final round scored): marks the difficulty cleared for the cup, the machine's
 * trophy, and credits_due when this clears the last cup on Expert; then writes storage. `machine_id` >= 30 (guests)
 * keeps no trophy. Same results as fzx_records_commit. */
fzx_rec_result_t fzx_records_cup_clear(uint8_t difficulty, uint8_t cup, uint16_t machine_id);
/* func_8007E2B4: 0 start, 1 Jack, Queen and King each cleared twice, 2 every cup cleared on Expert (Master opens), 3 sixteen clears. */
uint32_t fzx_records_unlock_level(void);
/* Whether SELECT may offer a class: Novice, Standard and Expert always, Master from unlock level 2. */
bool fzx_records_class_open(uint32_t difficulty);
/* The ending is due (gForceCredits); the screens clear it with fzx_records_credits_seen(). */
bool fzx_records_credits_due(void);
fzx_rec_result_t fzx_records_credits_seen(void);
#endif
/* Const read API. */
const fzx_records_t *fzx_records_get(void);
const fzx_rec_course_t *fzx_records_course(uint32_t course);
uint32_t fzx_records_boot_id(void);

/* [0] last load result, [1] file loaded (1 A, 2 B, 0 none), [2] generation loaded,
 * [3] boot_id, [4] last commit/flush result, [5] file written (1 A, 2 B), [6] dirty,
 * [7] bup status of the last write, [8] A valid, [9] B valid, [10] commits, [11]
 * already, [12] free blocks, [13] block size. */
extern volatile uint32_t fzx_rec_report[16];
/* Captures: 1 = the next write stores a wrong CRC; 2 = the next write stores the file
 * cut in half (both stand for a write interrupted by a power cut). */
extern volatile uint32_t fzx_rec_fault;
/* engine_main.c: true holds the vertical blank's INTBACK and waits until none is in flight; false releases it.
 * Around every SMPC command issued from this thread (ENG_SMPC_OVERLAP_CONTROL=1: the control, no hold and the
 * command's SF window held open across a vertical blank). */
void engine_smpc_quiet(bool on);
#endif
