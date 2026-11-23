/* Course catalog, cup entrants, the finalized race classification and the Grand Prix cup
 * manager (SCENES.md section 11, C-084/C-086). Plain C: the same file builds for HOST tests.
 * MIT; see ../../LICENSE. */
#ifndef FZX_ENGINE_CUP_H
#define FZX_ENGINE_CUP_H
#include <stdbool.h>
#include <stdint.h>

/* ---- 11.1 Course catalog ---------------------------------------------------------------- */
#define FZX_AVAIL_TA 0x01u /* the build has the course's packs and Time Attack race path */
#define FZX_AVAIL_GP 0x02u /* ... and its GP field and results path */
#define FZX_AVAIL_DEATH 0x04u /* the Death Race course (id 54): the only course Death Race runs on (racer.c: the mode's own course) */
typedef struct {
    uint8_t course;        /* stable original course ID (cup c, round r = 6c + r) */
    uint8_t display_key;   /* index into C8's menu-name table */
    uint8_t avail;         /* FZX_AVAIL_* */
    uint8_t reserved;
} fzx_course_row_t;
const fzx_course_row_t *fzx_course_catalog(unsigned *count);
const fzx_course_row_t *fzx_course_find(uint8_t course);
#define FZX_COURSE_NONE 0xFFu
/* The next row after `course` (dir +1 / -1, wrapping) whose avail has all of `need`; the
 * first such row when `course` is not listed; FZX_COURSE_NONE when no row qualifies. */
uint8_t fzx_course_step(uint8_t course, int dir, uint8_t need);

#define FZX_CUP_ROUNDS_MAX 6u
#define FZX_CUP_JACK 0u
typedef struct { uint8_t cup, rounds, course[FZX_CUP_ROUNDS_MAX]; } fzx_cup_def_t;
const fzx_cup_def_t *fzx_cup_def(uint8_t cup);
bool fzx_cup_gp_ready(uint8_t cup);

/* ---- 11.2 Entrants ------------------------------------------------------------------------ */
#define FZX_ENTRANTS_MAX 30u
#define FZX_ENT_PLAYER 0x01u
typedef struct { uint8_t entrant, machine_id, skin, flags; } fzx_entrant_t;
typedef struct { uint8_t count, player_slot; fzx_entrant_t e[FZX_ENTRANTS_MAX]; } fzx_entrants_t;
/* Entrant 0 is the player; 1..count-1 take rival_machine[i] (original roster 0..29 only). */
bool fzx_entrants_build(fzx_entrants_t *out, unsigned count, uint8_t player_machine, uint8_t player_skin,
                        const uint8_t *rival_machine);
/* Complete and unique: entrants 0..count-1 each exactly once, entrant 0 the player. */
bool fzx_entrants_valid(const fzx_entrants_t *e);

/* ---- 11.3 Classification -------------------------------------------------------------------- */
enum { FZX_CL_FINISHED = 1, FZX_CL_PROJECTED, FZX_CL_RETIRED };
enum { FZX_OUT_FINISH = 1, FZX_OUT_RETIRE, FZX_OUT_QUIT, FZX_OUT_PAUSE_RETRY };
typedef struct {
    uint8_t entrant, status, scoring, laps_done;
    uint32_t time_ms;      /* official time; 0 for RETIRED */
} fzx_class_row_t;
typedef struct {
    uint32_t run_id;
    uint8_t cup, round, course, count;
    uint8_t outcome, player_place, player_kos, valid;
    uint32_t result_hash, attempt_hash;
    fzx_class_row_t row[FZX_ENTRANTS_MAX];
} fzx_classification_t;

enum { FZX_CLIN_RUNNING = 0, FZX_CLIN_FINISHED, FZX_CLIN_FALLING, FZX_CLIN_RETIRED };
typedef struct {
    uint8_t entrant, state, position, laps_done;
    int32_t race_ms;        /* Racer.raceTime: the finish time, or the race clock if running */
    int32_t lap_ms[3];      /* Racer.lapTimes (0 = not completed) */
    float race_distance;    /* Racer.raceDistance */
    int32_t retire_frame;   /* RETIRED: sRaceFrameCount at retirement */
    uint8_t retire_position;
} fzx_cl_in_t;
typedef struct {
    uint32_t run_id;
    uint8_t cup, round, course, outcome, player_kos, laps;
    int32_t frame;          /* sRaceFrameCount at the trigger (falling machines retire here) */
    float course_length;    /* gCurrentCourseInfo->length, the same units as race_distance */
} fzx_cl_key_t;
/* SCENES.md 11.3, FINISH only: func_8008AA8C + Racer_RetireRacer + Racer_UpdateRacePositions.
 * Inputs must name entrants 0..n-1 exactly once. */
bool fzx_classify(fzx_classification_t *out, const fzx_cl_key_t *key, const fzx_cl_in_t *in, unsigned n);
/* A retire, quit or pause retry: the outcome-only record (count 0, no rows, place 0), made at
 * the event; no trigger wait, no projected times. */
bool fzx_cl_outcome_only(fzx_classification_t *out, const fzx_cl_key_t *key);
/* The current attempt's record (RESULTS reads it; engine_scenes.c). */
const fzx_classification_t *fzx_cup_classification(void);
/* SCENES.md 11.6. */
uint32_t fzx_classification_result_hash(const fzx_classification_t *c);
uint32_t fzx_classification_attempt_hash(const fzx_classification_t *c);
/* Sporting equality (cold against retry): every header field but run_id and every active row. */
bool fzx_classification_same_result(const fzx_classification_t *a, const fzx_classification_t *b);

/* ---- 11.4 / 11.5 Spare machines and the cup manager ------------------------------------------ */
typedef struct { int8_t balance, menu_lives; uint8_t kos, retry_locked; } fzx_cup_live_t;
enum { FZX_CUP_IDLE = 0, FZX_CUP_READY, FZX_CUP_RACING, FZX_CUP_SCORED, FZX_CUP_RETIRED, FZX_CUP_FINAL, FZX_CUP_OVER };
typedef struct {
    uint8_t state, cup, difficulty, round;
    uint32_t generation, run_id, accepted_run_id;
    fzx_cup_live_t live;
    fzx_entrants_t entrants;
    int32_t total[FZX_ENTRANTS_MAX];
    uint8_t last_place[FZX_ENTRANTS_MAX];
    uint8_t grid[FZX_ENTRANTS_MAX];
    fzx_classification_t last;
} fzx_cup_t;
const fzx_cup_t *fzx_cup(void);
const fzx_cup_live_t *fzx_cup_live(void);
bool fzx_cup_begin(uint8_t cup, uint8_t difficulty, const fzx_entrants_t *entrants);
bool fzx_cup_race_start(uint32_t run_id);
bool fzx_cup_race_ko(uint32_t run_id);
bool fzx_cup_accept(const fzx_classification_t *c);
enum { FZX_CUP_NEXT = 1, FZX_CUP_RETRY, FZX_CUP_END };
bool fzx_cup_intent(uint32_t intent, uint32_t run_id);
uint8_t fzx_cup_next_course(void);
void fzx_cup_reset(void);
typedef void (*fzx_cup_score_fn)(const fzx_classification_t *c, int32_t total[FZX_ENTRANTS_MAX]);
void fzx_cup_score_hook(fzx_cup_score_fn fn);
#define FZX_KO_LIFE 5u     /* racer.c: gRacersKOd == 5 */
#define FZX_FINISH_TRIGGER_TICKS 442u /* menus.c: sPlayerFinishTimer > 88+23+23+23+284 */

#endif
