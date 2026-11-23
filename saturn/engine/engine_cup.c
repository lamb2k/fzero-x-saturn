/* Course catalog, cup entrants, classification and the Grand Prix cup manager (SCENES.md
 * section 11, C-084/C-086). Plain C, no hardware: the HOST test (tests/test_cup.py) builds this
 * file as is, next to the original functions it is compared with. MIT; see ../../LICENSE. */
#include <string.h>
#include "engine_cup.h"

/* ---- Catalog ------------------------------------------------------------------------------
 * The courses lane's rows (claude/courses, claude/jack). One disc for every course
 * (ENG_COURSE_SELECT): the six Jack rows, Time Attack and GP where the bundle has the course's packs
 * (FZX_COURSE_BUNDLE_MASK, tools/course_bundle.py). Otherwise the image carries one course
 * (ENG_COURSE_INDEX): course 0 and 1 are listed and only the image's course runs Time Attack.
 * The HOST test builds the default: course 0 Time Attack, course 1 listed, unavailable. */
#ifndef ENG_COURSE_SELECT
#define ENG_COURSE_SELECT 0
#endif
#ifndef ENG_COURSE_INDEX
#define ENG_COURSE_INDEX 0
#endif
#if ENG_COURSE_SELECT
#include "course_ids.h"
#define AV(c) ((((FZX_COURSE_BUNDLE_MASK) >> (c)) & 1ull) ? (FZX_AVAIL_TA | FZX_AVAIL_GP) : 0u)
static const fzx_course_row_t catalog[] = {
    { 0u, 0u, AV(0), 0u }, { 1u, 1u, AV(1), 0u }, { 2u, 2u, AV(2), 0u },
    { 3u, 3u, AV(3), 0u }, { 4u, 4u, AV(4), 0u }, { 5u, 5u, AV(5), 0u },
    { 6u, 6u, AV(6), 0u }, { 7u, 7u, AV(7), 0u }, { 8u, 8u, AV(8), 0u },
    { 9u, 9u, AV(9), 0u }, { 10u, 10u, AV(10), 0u }, { 11u, 11u, AV(11), 0u },
    { 12u, 12u, AV(12), 0u }, { 13u, 13u, AV(13), 0u }, { 14u, 14u, AV(14), 0u },
    { 15u, 15u, AV(15), 0u }, { 16u, 16u, AV(16), 0u }, { 17u, 17u, AV(17), 0u },
    { 18u, 18u, AV(18), 0u }, { 19u, 19u, AV(19), 0u }, { 20u, 20u, AV(20), 0u },
    { 21u, 21u, AV(21), 0u }, { 22u, 22u, AV(22), 0u }, { 23u, 23u, AV(23), 0u },
};
#else
#define AV(c) ((c) == ENG_COURSE_INDEX ? FZX_AVAIL_TA : 0u)
static const fzx_course_row_t catalog[] = {
    { 0u, 0u, AV(0), 0u },
    { 1u, 1u, AV(1), 0u },
#if ENG_COURSE_INDEX > 1
    { (uint8_t)ENG_COURSE_INDEX, (uint8_t)ENG_COURSE_INDEX, FZX_AVAIL_TA, 0u },
#endif
};
#endif
#undef AV
#define CATALOG_COUNT (sizeof(catalog) / sizeof(catalog[0]))

const fzx_course_row_t *fzx_course_catalog(unsigned *count) {
    if (count) *count = (unsigned)CATALOG_COUNT;
    return catalog;
}
const fzx_course_row_t *fzx_course_find(uint8_t course) {
    for (unsigned i = 0; i < CATALOG_COUNT; ++i)
        if (catalog[i].course == course) return &catalog[i];
    return NULL;
}
uint8_t fzx_course_step(uint8_t course, int dir, uint8_t need) {
    unsigned at = CATALOG_COUNT;
    for (unsigned i = 0; i < CATALOG_COUNT; ++i) if (catalog[i].course == course) at = i;
    if (at == CATALOG_COUNT) {
        for (unsigned i = 0; i < CATALOG_COUNT; ++i)
            if ((catalog[i].avail & need) == need) return catalog[i].course;
        return FZX_COURSE_NONE;
    }
    for (unsigned k = 1; k <= CATALOG_COUNT; ++k) {
        const unsigned i = dir < 0 ? (at + CATALOG_COUNT - (k % CATALOG_COUNT)) % CATALOG_COUNT : (at + k) % CATALOG_COUNT;
        if ((catalog[i].avail & need) == need) return catalog[i].course;
    }
    return FZX_COURSE_NONE;
}

/* Cups in the original order (game.c: the next course is gCourseIndex + 1 within a cup). */
static const fzx_cup_def_t cups[] = {
    { 0u, 6u, { 0u, 1u, 2u, 3u, 4u, 5u } },       /* Jack */
    { 1u, 6u, { 6u, 7u, 8u, 9u, 10u, 11u } },     /* Queen */
    { 2u, 6u, { 12u, 13u, 14u, 15u, 16u, 17u } }, /* King */
    { 3u, 6u, { 18u, 19u, 20u, 21u, 22u, 23u } }, /* Joker */
};
const fzx_cup_def_t *fzx_cup_def(uint8_t cup) {
    return cup < sizeof(cups) / sizeof(cups[0]) ? &cups[cup] : NULL;
}
bool fzx_cup_gp_ready(uint8_t cup) {
    const fzx_cup_def_t *d = fzx_cup_def(cup);
    if (!d) return false;
    for (unsigned r = 0; r < d->rounds; ++r) {
        const fzx_course_row_t *row = fzx_course_find(d->course[r]);
        if (!row || !(row->avail & FZX_AVAIL_GP)) return false;
    }
    return true;
}

/* ---- Entrants ------------------------------------------------------------------------------ */
bool fzx_entrants_build(fzx_entrants_t *out, unsigned count, uint8_t player_machine, uint8_t player_skin,
                        const uint8_t *rival_machine) {
    memset(out, 0, sizeof(*out));
    if (count < 1u || count > FZX_ENTRANTS_MAX) return false;
    out->count = (uint8_t)count; out->player_slot = 0u;
    out->e[0] = (fzx_entrant_t){ 0u, player_machine, player_skin, FZX_ENT_PLAYER };
    for (unsigned i = 1; i < count; ++i) {
        if (!rival_machine || rival_machine[i] > 29u) { memset(out, 0, sizeof(*out)); return false; }
        out->e[i] = (fzx_entrant_t){ (uint8_t)i, rival_machine[i], 0u, 0u };
    }
    return true;
}
bool fzx_entrants_valid(const fzx_entrants_t *e) {
    if (!e || e->count < 1u || e->count > FZX_ENTRANTS_MAX || e->player_slot != 0u) return false;
    uint32_t seen = 0;
    for (unsigned i = 0; i < e->count; ++i) {
        const uint8_t id = e->e[i].entrant;
        if (id >= e->count || (seen & (1u << id))) return false;
        seen |= 1u << id;
        if (((e->e[i].flags & FZX_ENT_PLAYER) != 0) != (id == 0u)) return false;
    }
    return true;
}

/* ---- Classification ------------------------------------------------------------------------- */
#define FNV_BASIS 2166136261u
static uint32_t fnv(uint32_t h, const uint8_t *b, unsigned n) {
    for (unsigned i = 0; i < n; ++i) { h ^= b[i]; h *= 16777619u; }
    return h;
}
static uint32_t result_bytes_hash(uint32_t h, const fzx_classification_t *c) {
    const uint8_t head[7] = { c->cup, c->round, c->course, c->count, c->outcome, c->player_place, c->player_kos };
    h = fnv(h, head, sizeof(head));
    for (unsigned i = 0; i < c->count && i < FZX_ENTRANTS_MAX; ++i) {
        const fzx_class_row_t *r = &c->row[i];
        const uint8_t b[8] = { r->entrant, r->status, r->scoring, r->laps_done, (uint8_t)r->time_ms,
                               (uint8_t)(r->time_ms >> 8), (uint8_t)(r->time_ms >> 16), (uint8_t)(r->time_ms >> 24) };
        h = fnv(h, b, sizeof(b));
    }
    return h;
}
uint32_t fzx_classification_result_hash(const fzx_classification_t *c) { return result_bytes_hash(FNV_BASIS, c); }
uint32_t fzx_classification_attempt_hash(const fzx_classification_t *c) {
    const uint8_t id[4] = { (uint8_t)c->run_id, (uint8_t)(c->run_id >> 8), (uint8_t)(c->run_id >> 16), (uint8_t)(c->run_id >> 24) };
    return result_bytes_hash(fnv(FNV_BASIS, id, sizeof(id)), c);
}
bool fzx_classification_same_result(const fzx_classification_t *a, const fzx_classification_t *b) {
    if (a->cup != b->cup || a->round != b->round || a->course != b->course || a->count != b->count ||
        a->outcome != b->outcome || a->player_place != b->player_place || a->player_kos != b->player_kos ||
        a->valid != b->valid || a->result_hash != b->result_hash || a->count > FZX_ENTRANTS_MAX) return false;
    for (unsigned i = 0; i < a->count; ++i) {
        const fzx_class_row_t *x = &a->row[i], *y = &b->row[i];
        if (x->entrant != y->entrant || x->status != y->status || x->scoring != y->scoring ||
            x->laps_done != y->laps_done || x->time_ms != y->time_ms) return false;
    }
    return true;
}

/* Racer_RetireRacer: raceDistancePosition = (frame + ((total - position) / 10.0f)) - 600000.0f. */
static float retire_position_value(int32_t frame, unsigned total, unsigned position) {
    return ((float)frame + ((float)((int32_t)total - (int32_t)position) / 10.0f)) - 600000.0f;
}

bool fzx_classify(fzx_classification_t *out, const fzx_cl_key_t *key, const fzx_cl_in_t *in, unsigned n) {
    memset(out, 0, sizeof(*out));
    if (!key || !in || n < 1u || n > FZX_ENTRANTS_MAX || key->laps < 1u || key->outcome != FZX_OUT_FINISH) return false;
    /* Racer index = entrant: the original scans from the last racer down to racer 0. */
    int32_t time[FZX_ENTRANTS_MAX];
    float rdp[FZX_ENTRANTS_MAX];
    uint8_t finished[FZX_ENTRANTS_MAX], projected[FZX_ENTRANTS_MAX], laps[FZX_ENTRANTS_MAX];
    uint32_t seen = 0;
    for (unsigned i = 0; i < n; ++i) {
        const unsigned id = in[i].entrant;
        if (id >= n || (seen & (1u << id))) return false; /* entrants 0..n-1, each once */
        seen |= 1u << id;
    }
    for (unsigned i = 0; i < n; ++i) {
        const fzx_cl_in_t *e = &in[i];
        const unsigned r = e->entrant;
        laps[r] = e->laps_done; finished[r] = 0u; projected[r] = 0u; time[r] = e->race_ms; rdp[r] = 0.0f;
        switch (e->state) {
        case FZX_CLIN_FINISHED:
            finished[r] = 1u;
            break;
        case FZX_CLIN_RETIRED: /* crashed: raceTime = 0; raceDistancePosition from its retirement */
            time[r] = 0;
            rdp[r] = retire_position_value(e->retire_frame, n, e->retire_position);
            break;
        case FZX_CLIN_FALLING: /* falling off the track: retired at finalization */
            time[r] = 0;
            rdp[r] = retire_position_value(key->frame, n, e->position);
            break;
        default: { /* running: the reference projection, marked finished */
            int32_t d = 0;
            if (e->lap_ms[1] != 0) { d = e->lap_ms[0] - e->lap_ms[1]; if (d < 0) d = 0; }
            int32_t t = 0x7FFFFFFF; /* not left the line: the source divides by zero here */
            if (e->race_distance > 0.0f) {
                const float f = (float)((e->race_ms - d) * (int32_t)key->laps) * key->course_length / e->race_distance;
                t = f >= 2147483520.0f ? 0x7FFFFFFF - d : (int32_t)f;
                t += d;
            }
            time[r] = t; finished[r] = 1u; projected[r] = 1u;
            break;
        }
        }
    }
    /* Racer_UpdateRacePositions. */
    uint8_t placed[FZX_ENTRANTS_MAX] = { 0 }, order[FZX_ENTRANTS_MAX];
    int position = 1, last = (int)n;
    do {
        int fin = -1, act = -1;
        int32_t best = 0;
        float low = 0.0f;
        for (int r = (int)n - 1; r >= 0; --r) {
            if (placed[r]) continue;
            if (finished[r]) {
                if (fin < 0 || time[r] < best) { best = time[r]; fin = r; }
            } else if (act < 0 || rdp[r] < low) { low = rdp[r]; act = r; }
        }
        if (fin >= 0) { order[position - 1] = (uint8_t)fin; placed[fin] = 1u; ++position; }
        if (act >= 0) { order[last - 1] = (uint8_t)act; placed[act] = 1u; --last; }
    } while (last >= position);

    out->run_id = key->run_id; out->cup = key->cup; out->round = key->round; out->course = key->course;
    out->count = (uint8_t)n; out->outcome = key->outcome; out->player_kos = key->player_kos;
    int last_timed = -1;
    for (unsigned p = 0; p < n; ++p) {
        const unsigned r = order[p];
        fzx_class_row_t *row = &out->row[p];
        row->entrant = (uint8_t)r; row->laps_done = laps[r];
        row->status = finished[r] ? (projected[r] ? FZX_CL_PROJECTED : FZX_CL_FINISHED) : FZX_CL_RETIRED;
        row->time_ms = (uint32_t)time[r];
        if (r == 0u) out->player_place = (uint8_t)(p + 1u);
        if (time[r] != 0) last_timed = (int)p;
    }
    /* func_8008AA8C scores from the last row with a time up to place 1 (a finish only). */
    for (int p = 0; p <= last_timed; ++p) out->row[p].scoring = 1u;
    out->valid = 1u;
    out->result_hash = fzx_classification_result_hash(out);
    out->attempt_hash = fzx_classification_attempt_hash(out);
    return true;
}

/* ---- Cup manager --------------------------------------------------------------------------- */
static const int8_t start_spares[4] = { 5, 4, 3, 2 }; /* racer.c gTotalLives by difficulty */
static fzx_cup_t cup;
static fzx_cup_score_fn score_fn;
static uint32_t generation;

const fzx_cup_t *fzx_cup(void) { return &cup; }
const fzx_cup_live_t *fzx_cup_live(void) { return &cup.live; }
void fzx_cup_score_hook(fzx_cup_score_fn fn) { score_fn = fn; }
void fzx_cup_reset(void) {
    memset(&cup, 0, sizeof(cup));
    cup.generation = ++generation; /* results and intents of the old cup no longer match */
}
bool fzx_cup_begin(uint8_t c, uint8_t difficulty, const fzx_entrants_t *entrants) {
    fzx_cup_reset();
    if (!fzx_cup_def(c) || difficulty > 3u || !fzx_entrants_valid(entrants)) return false;
    cup.state = FZX_CUP_READY; cup.cup = c; cup.difficulty = difficulty; cup.round = 0u;
    cup.live.balance = start_spares[difficulty]; cup.live.menu_lives = cup.live.balance;
    cup.live.retry_locked = cup.live.menu_lives <= 0;
    cup.entrants = *entrants;
    for (unsigned i = 0; i < FZX_ENTRANTS_MAX; ++i) cup.grid[i] = (uint8_t)i;
    return true;
}
bool fzx_cup_race_start(uint32_t run_id) {
    if (cup.state != FZX_CUP_READY || !run_id || run_id == cup.accepted_run_id) return false;
    cup.state = FZX_CUP_RACING; cup.run_id = run_id;
    cup.live.menu_lives = cup.live.balance; cup.live.kos = 0u; /* sPlayer1Lives = gPlayerLives[0] */
    cup.live.retry_locked = cup.live.menu_lives <= 0;
    return true;
}
bool fzx_cup_race_ko(uint32_t run_id) {
    if (cup.state != FZX_CUP_RACING || run_id != cup.run_id || cup.live.kos == 0xFFu) return false;
    if (++cup.live.kos == FZX_KO_LIFE) { /* gPlayerLives[0]++ and sPlayer1Lives++, once */
        ++cup.live.balance; ++cup.live.menu_lives;
        cup.live.retry_locked = cup.live.menu_lives <= 0;
    }
    return true;
}
bool fzx_cup_accept(const fzx_classification_t *c) {
    if (cup.state != FZX_CUP_RACING || !c || !c->valid || c->run_id != cup.run_id) return false;
    const fzx_cup_def_t *d = fzx_cup_def(cup.cup);
    /* A finish carries every entrant; a retire, quit or pause retry is outcome-only (no rows). */
    const unsigned rows = c->outcome == FZX_OUT_FINISH ? cup.entrants.count : 0u;
    if (c->cup != cup.cup || c->round != cup.round || c->course != d->course[cup.round] ||
        c->count != rows || c->player_kos != cup.live.kos ||
        c->result_hash != fzx_classification_result_hash(c) || c->attempt_hash != fzx_classification_attempt_hash(c))
        return false;
    uint32_t seen = 0;
    for (unsigned p = 0; p < c->count; ++p) {
        const uint8_t id = c->row[p].entrant;
        if (id >= c->count || (seen & (1u << id))) return false;
        seen |= 1u << id;
    }
    switch (c->outcome) {
    case FZX_OUT_FINISH:
        if (score_fn) score_fn(c, cup.total); /* C8's points table, once */
        for (unsigned p = 0; p < c->count; ++p) cup.last_place[c->row[p].entrant] = (uint8_t)(p + 1u);
        cup.state = cup.round + 1u >= d->rounds ? FZX_CUP_FINAL : FZX_CUP_SCORED;
        break;
    case FZX_OUT_RETIRE: /* the pending -1, charged once; retry if sPlayer1Lives > 0 */
        --cup.live.balance;
        cup.state = cup.live.menu_lives > 0 ? FZX_CUP_RETIRED : FZX_CUP_OVER;
        break;
    case FZX_OUT_PAUSE_RETRY: /* Menus_PayGpRestartLife; locked at sPlayer1Lives <= 0 (no change) */
        if (cup.live.menu_lives <= 0) return false;
        --cup.live.balance;
        cup.state = FZX_CUP_READY;
        break;
    case FZX_OUT_QUIT:
        cup.state = FZX_CUP_OVER;
        break;
    default:
        return false;
    }
    cup.accepted_run_id = c->run_id;
    cup.last = *c;
    return true;
}
bool fzx_cup_intent(uint32_t intent, uint32_t run_id) {
    switch (intent) {
    case FZX_CUP_NEXT:
        if (cup.state != FZX_CUP_SCORED || run_id != cup.accepted_run_id) return false;
        /* The next grid keeps the finishing order (menus.c D_80115D50). */
        for (unsigned p = 0; p < cup.last.count; ++p) cup.grid[p] = cup.last.row[p].entrant;
        ++cup.round;
        cup.state = FZX_CUP_READY;
        return true;
    case FZX_CUP_RETRY:
        if (cup.state != FZX_CUP_RETIRED || run_id != cup.accepted_run_id) return false;
        cup.state = FZX_CUP_READY; /* the retire has paid */
        return true;
    case FZX_CUP_END: /* the accepted attempt's END only; a delayed one changes nothing */
        if ((cup.state != FZX_CUP_SCORED && cup.state != FZX_CUP_RETIRED && cup.state != FZX_CUP_FINAL &&
             cup.state != FZX_CUP_OVER) || run_id != cup.accepted_run_id) return false;
        fzx_cup_reset();
        return true;
    default:
        return false;
    }
}
bool fzx_cl_outcome_only(fzx_classification_t *out, const fzx_cl_key_t *key) {
    memset(out, 0, sizeof(*out));
    if (!key || key->outcome < FZX_OUT_RETIRE || key->outcome > FZX_OUT_PAUSE_RETRY) return false;
    out->run_id = key->run_id; out->cup = key->cup; out->round = key->round; out->course = key->course;
    out->count = 0u; out->outcome = key->outcome; out->player_place = 0u; out->player_kos = key->player_kos;
    out->valid = 1u;
    out->result_hash = fzx_classification_result_hash(out);
    out->attempt_hash = fzx_classification_attempt_hash(out);
    return true;
}
uint8_t fzx_cup_next_course(void) {
    if (cup.state != FZX_CUP_READY) return FZX_COURSE_NONE;
    return fzx_cup_def(cup.cup)->course[cup.round];
}
