/* HOST driver for tests/test_cup.py (SCENES.md section 11): engine_cup.c against the original
 * classification functions (compiled from the pinned decompilation by the test), then a
 * scripted six-round cup. Prints "cup: ok" and exits 0 when every check holds. MIT. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine_cup.h"

typedef struct { int state, position, retire_frame, retire_position; int32_t race_ms, lap_ms[3]; float dist; } RefIn;
int ref_run(const RefIn *in, int n, int laps, float length, int frame, int *entrant, int32_t *time, int *scoring, int *fin);
int32_t ref_points(int place);

static int failures;
#define CHECK(c, ...) do { if (!(c)) { ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- Classification against the original -------------------------------------------------- */
static void to_cl(const RefIn *in, int n, fzx_cl_in_t *out) {
    for (int i = 0; i < n; ++i) {
        fzx_cl_in_t *c = &out[i];
        memset(c, 0, sizeof(*c));
        c->entrant = (uint8_t)i; c->state = (uint8_t)in[i].state; c->position = (uint8_t)in[i].position;
        c->laps_done = 3u; c->race_ms = in[i].race_ms; memcpy(c->lap_ms, in[i].lap_ms, sizeof(c->lap_ms));
        c->race_distance = in[i].dist; c->retire_frame = in[i].retire_frame; c->retire_position = (uint8_t)in[i].retire_position;
    }
}
static fzx_cl_key_t key_for(uint32_t run_id, uint8_t round, uint8_t course, int frame, float length) {
    fzx_cl_key_t k;
    memset(&k, 0, sizeof(k));
    k.run_id = run_id; k.cup = 0u; k.round = round; k.course = course; k.outcome = FZX_OUT_FINISH;
    k.laps = 3u; k.frame = frame; k.course_length = length;
    return k;
}
static int compare_case(const char *name, const RefIn *in, int n, int frame, float length) {
    int entrant[30], scoring[30], fin[30];
    int32_t time[30];
    const int before = failures;
    CHECK(ref_run(in, n, 3, length, frame, entrant, time, scoring, fin), "%s: reference incomplete", name);
    fzx_cl_in_t cl[30];
    to_cl(in, n, cl);
    fzx_classification_t c;
    const fzx_cl_key_t k = key_for(7u, 0u, 0u, frame, length);
    CHECK(fzx_classify(&c, &k, cl, (unsigned)n), "%s: classify refused", name);
    for (int p = 0; p < n && failures == before; ++p) {
        const fzx_class_row_t *r = &c.row[p];
        const int want_status = fin[p] ? (in[entrant[p]].state == FZX_CLIN_RUNNING ? FZX_CL_PROJECTED : FZX_CL_FINISHED)
                                       : FZX_CL_RETIRED;
        CHECK(r->entrant == entrant[p] && (int32_t)r->time_ms == time[p] && r->scoring == scoring[p] && r->status == want_status,
              "%s: place %d: got entrant %d time %ld scoring %d status %d, original %d %ld %d %d", name, p + 1, r->entrant,
              (long)(int32_t)r->time_ms, r->scoring, r->status, entrant[p], (long)time[p], scoring[p], want_status);
        if (entrant[p] == 0) CHECK(c.player_place == p + 1, "%s: player place", name);
    }
    return failures == before;
}

static RefIn fin_row(int32_t ms) { RefIn r = { FZX_CLIN_FINISHED, 1, 0, 0, ms, { 0, 0, 0 }, 0.0f }; return r; }
static RefIn run_row(int32_t ms, int32_t l1, int32_t l2, float dist, int pos) {
    RefIn r = { FZX_CLIN_RUNNING, pos, 0, 0, ms, { l1, l2, 0 }, dist }; return r;
}
static RefIn ret_row(int frame, int pos_then, int pos_now) {
    RefIn r = { FZX_CLIN_RETIRED, pos_now, frame, pos_then, 50000, { 0, 0, 0 }, 1000.0f }; return r;
}
static RefIn fall_row(int pos, float dist) { RefIn r = { FZX_CLIN_FALLING, pos, 0, 0, 90000, { 30000, 0, 0 }, dist }; return r; }

static void frozen_cases(void) {
    const float L = 9172.25f; /* any course length: the projection's units cancel */
    /* 1: positive d, d clamped to 0, no second lap, equal finished times, a falling racer,
     *    close retirements (same frame, positions apart; frames one apart). */
    RefIn a[30];
    int i = 0;
    a[i++] = fin_row(101233);                         /* the player */
    a[i++] = fin_row(99876); a[i++] = fin_row(101233); a[i++] = fin_row(101233); a[i++] = fin_row(100500);
    a[i++] = run_row(108000, 36000, 34000, 3.0f * L * 0.97f, 6);  /* d = 2000 */
    a[i++] = run_row(108000, 34000, 36000, 3.0f * L * 0.97f, 7);  /* d clamped to 0 */
    a[i++] = run_row(108000, 36000, 0, 3.0f * L * 0.60f, 20);     /* no second lap: d = 0 */
    a[i++] = run_row(108000, 35500, 35000, 3.0f * L * 0.95f, 8);
    a[i++] = run_row(108000, 35500, 35000, 3.0f * L * 0.95f, 9);  /* equal projected times */
    a[i++] = fall_row(12, 3.0f * L * 0.8f);
    a[i++] = ret_row(4000, 15, 28); a[i++] = ret_row(4000, 16, 29); a[i++] = ret_row(4001, 22, 27);
    a[i++] = ret_row(3999, 3, 30);
    while (i < 30) { a[i] = run_row(108000, 36100 + i * 13, 35000 + i * 7, 3.0f * L * (0.70f + i * 0.007f), 10 + i % 10); ++i; }
    compare_case("mixed", a, 30, 6480, L);
    /* 2: all rivals retired at one frame with equal positions (equal raceDistancePosition). */
    RefIn b[8];
    b[0] = fin_row(120000);
    for (int k = 1; k < 8; ++k) b[k] = ret_row(5000, 4, 9 - k);
    compare_case("equal-retired", b, 8, 7300, L);
    /* 3: every rival still running with equal times and distances. */
    RefIn c[6];
    c[0] = fin_row(110000);
    for (int k = 1; k < 6; ++k) c[k] = run_row(118000, 39000, 38000, 3.0f * L * 0.9f, k + 1);
    compare_case("equal-running", c, 6, 7000, L);
    /* 4: a falling racer and a retired one with nearby values. */
    RefIn d[5] = { fin_row(100000), fall_row(2, 1000.0f), ret_row(6000, 2, 4), ret_row(6001, 5, 5), run_row(106000, 0, 0, 2.0f * L, 3) };
    compare_case("fall-retire", d, 5, 6001, L);
}

static uint32_t rng = 12345u;
static uint32_t next_rand(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }
static void random_cases(unsigned count) {
    const float L = 7000.0f + (float)(next_rand() % 4000u);
    for (unsigned t = 0; t < count; ++t) {
        RefIn in[30];
        const int n = 2 + (int)(next_rand() % 29u);
        const int frame = 6000 + (int)(next_rand() % 50u);
        in[0] = fin_row(95000 + (int32_t)(next_rand() % 20u) * 1000);
        for (int k = 1; k < n; ++k) {
            const uint32_t pick = next_rand() % 10u;
            if (pick < 3) in[k] = fin_row(95000 + (int32_t)(next_rand() % 20u) * 1000); /* equal times likely */
            else if (pick < 6) {
                const int32_t l1 = 30000 + (int32_t)(next_rand() % 8u) * 500, l2 = (next_rand() % 4u) ? 30000 + (int32_t)(next_rand() % 8u) * 500 : 0;
                in[k] = run_row(110000, l1, l2, 3.0f * L * (0.5f + (float)(next_rand() % 50u) / 100.0f), 1 + (int)(next_rand() % (uint32_t)n));
            } else if (pick < 8) in[k] = ret_row(frame - (int)(next_rand() % 3u), 1 + (int)(next_rand() % (uint32_t)n), 1 + (int)(next_rand() % (uint32_t)n));
            else in[k] = fall_row(1 + (int)(next_rand() % (uint32_t)n), 3.0f * L * 0.5f);
        }
        char name[32];
        snprintf(name, sizeof(name), "random-%u", t);
        if (!compare_case(name, in, n, frame, L)) return;
    }
}

/* ---- The scripted cup ----------------------------------------------------------------------- */
static void points_hook(const fzx_classification_t *c, int32_t total[FZX_ENTRANTS_MAX]) {
    for (unsigned p = 0; p < c->count; ++p) if (c->row[p].scoring) total[c->row[p].entrant] += ref_points((int)p);
}
static fzx_entrants_t entrants30(void) {
    uint8_t m[30];
    for (unsigned i = 0; i < 30; ++i) m[i] = (uint8_t)i;
    fzx_entrants_t e;
    fzx_entrants_build(&e, 30u, 0u, 0u, m);
    return e;
}
/* A finish of round r: rivals finish with ties, two rivals retired, the player at `place_ms`. */
static fzx_classification_t finish(uint32_t run_id, uint8_t round, int32_t player_ms, uint8_t kos) {
    fzx_cl_in_t in[30];
    memset(in, 0, sizeof(in));
    for (unsigned i = 0; i < 30; ++i) {
        in[i].entrant = (uint8_t)i; in[i].state = FZX_CLIN_FINISHED; in[i].laps_done = 3u;
        in[i].race_ms = 100000 + (int32_t)((i * 7u + round * 3u) % 25u) * 500; /* repeats: equal times */
        in[i].position = (uint8_t)(i + 1u);
    }
    in[0].race_ms = player_ms;
    in[28].state = FZX_CLIN_RETIRED; in[28].retire_frame = 3000 + round; in[28].retire_position = 20u;
    in[29].state = FZX_CLIN_RETIRED; in[29].retire_frame = 3001 + round; in[29].retire_position = 25u;
    fzx_cl_key_t k = key_for(run_id, round, fzx_cup_def(0u)->course[round], 6000, 8000.0f);
    k.player_kos = kos;
    fzx_classification_t c;
    if (!fzx_classify(&c, &k, in, 30u)) { ++failures; printf("FAIL finish classify\n"); }
    return c;
}
static fzx_classification_t event(uint32_t run_id, uint8_t round, uint8_t outcome, uint8_t kos) {
    fzx_cl_key_t k = key_for(run_id, round, fzx_cup_def(0u)->course[round], 0, 8000.0f);
    k.outcome = outcome; k.player_kos = kos;
    fzx_classification_t c;
    if (!fzx_cl_outcome_only(&c, &k)) { ++failures; printf("FAIL event\n"); }
    return c;
}
static int32_t sum_totals(void) { int32_t s = 0; for (unsigned i = 0; i < 30; ++i) s += fzx_cup()->total[i]; return s; }

static void scripted_cup(void) {
    fzx_cup_score_hook(points_hook);
    fzx_entrants_t e = entrants30();
    CHECK(fzx_entrants_valid(&e), "entrants valid");
    fzx_entrants_t dup = e; dup.e[5].entrant = 4u;
    CHECK(!fzx_cup_begin(0u, 3u, &dup) && fzx_cup()->state == FZX_CUP_IDLE, "duplicate entrants refused");
    uint8_t guest[30]; for (unsigned i = 0; i < 30; ++i) guest[i] = (uint8_t)i; guest[3] = 33u;
    fzx_entrants_t g;
    CHECK(!fzx_entrants_build(&g, 30u, 0u, 0u, guest), "guest rival refused");

    CHECK(fzx_cup_begin(0u, 3u, &e), "begin Jack, 2 spares");
    CHECK(fzx_cup()->live.balance == 2 && fzx_cup_next_course() == 0u, "start balance and course");
    uint32_t run = 100;
    /* Round 1: a finish, then duplicates and delayed inputs. */
    CHECK(fzx_cup_race_start(++run), "race 1");
    fzx_classification_t c = finish(run, 0u, 99000, 0u);
    fzx_classification_t bad = c; bad.round = 1u;
    CHECK(!fzx_cup_accept(&bad), "wrong round refused");
    bad = c; bad.row[3].time_ms ^= 1u;
    CHECK(!fzx_cup_accept(&bad), "bad hash refused");
    bad = c; bad.player_kos = 1u; bad.result_hash = fzx_classification_result_hash(&bad); bad.attempt_hash = fzx_classification_attempt_hash(&bad);
    CHECK(!fzx_cup_accept(&bad), "KO count mismatch refused");
    CHECK(fzx_cup_accept(&c) && fzx_cup()->state == FZX_CUP_SCORED, "finish accepted");
    const int32_t after1 = sum_totals();
    CHECK(after1 > 0 && fzx_cup()->total[0] == ref_points(c.player_place - 1), "points through the hook");
    CHECK(!fzx_cup_accept(&c) && sum_totals() == after1, "duplicate result refused");
    CHECK(!fzx_cup_intent(FZX_CUP_RETRY, run), "a scored finish can't be retried");
    CHECK(!fzx_cup_intent(FZX_CUP_NEXT, run - 1), "NEXT with an old run_id refused");
    const uint32_t round1 = run;
    CHECK(fzx_cup_intent(FZX_CUP_NEXT, run) && fzx_cup()->round == 1u && fzx_cup_next_course() == 1u, "next course");
    CHECK(fzx_cup()->grid[0] == c.row[0].entrant, "next grid keeps the finishing order");
    CHECK(!fzx_cup_intent(FZX_CUP_NEXT, round1) && !fzx_cup_intent(FZX_CUP_END, round1), "delayed NEXT/END refused");
    /* Round 2: retire (charged once), retry free, delayed retry refused, totals and places kept. */
    uint8_t places[30]; memcpy(places, fzx_cup()->last_place, sizeof(places));
    CHECK(fzx_cup_race_start(++run), "race 2");
    fzx_classification_t ev = event(run, 1u, FZX_OUT_RETIRE, 0u);
    bad = ev; bad.count = 30u;
    CHECK(!fzx_cup_accept(&bad), "a retire with rows refused");
    CHECK(fzx_cup_accept(&ev) && fzx_cup()->state == FZX_CUP_RETIRED && fzx_cup()->live.balance == 1, "retire -1");
    CHECK(sum_totals() == after1 && !memcmp(places, fzx_cup()->last_place, sizeof(places)), "retire keeps totals and places");
    CHECK(!fzx_cup_accept(&ev), "duplicate retire refused");
    CHECK(fzx_cup_intent(FZX_CUP_RETRY, run) && fzx_cup()->live.balance == 1 && fzx_cup()->round == 1u, "retry is free");
    CHECK(!fzx_cup_intent(FZX_CUP_RETRY, run), "delayed retry refused");
    /* Pause retry: -1 once, back to READY; then the lock at 0 lives. */
    CHECK(fzx_cup_race_start(++run), "race 2b");
    ev = event(run, 1u, FZX_OUT_PAUSE_RETRY, 0u);
    CHECK(fzx_cup_accept(&ev) && fzx_cup()->live.balance == 0 && fzx_cup()->state == FZX_CUP_READY, "pause retry -1");
    CHECK(!fzx_cup_accept(&ev) && fzx_cup()->live.balance == 0, "duplicate pause retry refused");
    CHECK(fzx_cup_race_start(++run) && fzx_cup()->live.menu_lives == 0 && fzx_cup()->live.retry_locked, "race 2c at 0");
    for (int k = 0; k < 4; ++k) CHECK(fzx_cup_race_ko(run), "KO");
    ev = event(run, 1u, FZX_OUT_PAUSE_RETRY, 4u);
    CHECK(!fzx_cup_accept(&ev) && fzx_cup()->live.balance == 0 && fzx_cup()->state == FZX_CUP_RACING, "locked pause retry: no debit");
    CHECK(fzx_cup_race_ko(run) && fzx_cup()->live.balance == 1 && fzx_cup()->live.menu_lives == 1 && !fzx_cup()->live.retry_locked, "5th KO");
    CHECK(fzx_cup_race_ko(run) && fzx_cup()->live.balance == 1, "6th KO awards nothing");
    ev = event(run, 1u, FZX_OUT_PAUSE_RETRY, 6u);
    CHECK(fzx_cup_accept(&ev) && fzx_cup()->live.balance == 0 && fzx_cup()->state == FZX_CUP_READY, "eligible pause retry: one debit");
    /* Rounds 2..6: finishes. */
    for (uint8_t r = 1; r < 6; ++r) {
        CHECK(fzx_cup_race_start(++run), "race round %u", r + 1u);
        c = finish(run, r, 99000 + r * 700, 0u);
        CHECK(fzx_cup_accept(&c), "finish round %u", r + 1u);
        if (r < 5) CHECK(fzx_cup_intent(FZX_CUP_NEXT, run), "next after round %u", r + 1u);
    }
    CHECK(fzx_cup()->state == FZX_CUP_FINAL && fzx_cup()->last.round == 5u, "FINAL keeps round 6");
    const int32_t final_sum = sum_totals();
    CHECK(!fzx_cup_intent(FZX_CUP_NEXT, run) && !fzx_cup_intent(FZX_CUP_RETRY, run), "no NEXT or RETRY after the final");
    CHECK(!fzx_cup_intent(FZX_CUP_END, run - 1) && fzx_cup()->state == FZX_CUP_FINAL && sum_totals() == final_sum,
          "delayed END from round 5 changes nothing");
    CHECK(fzx_cup_intent(FZX_CUP_END, run) && fzx_cup()->state == FZX_CUP_IDLE && sum_totals() == 0, "END clears");

    /* Exhaustion at difficulty 3 (2 spares): retire, retry, retire, retry, retire -> OVER. */
    CHECK(fzx_cup_begin(0u, 3u, &e), "begin 2");
    int8_t want[3] = { 1, 0, -1 };
    for (int k = 0; k < 3; ++k) {
        CHECK(fzx_cup_race_start(++run), "exhaust race");
        ev = event(run, 0u, FZX_OUT_RETIRE, 0u);
        CHECK(fzx_cup_accept(&ev) && fzx_cup()->live.balance == want[k], "exhaust balance %d", want[k]);
        if (k < 2) CHECK(fzx_cup_intent(FZX_CUP_RETRY, run), "exhaust retry %d", k);
    }
    CHECK(fzx_cup()->state == FZX_CUP_OVER && !fzx_cup_intent(FZX_CUP_RETRY, run), "exhausted: no retry");
    CHECK(fzx_cup_intent(FZX_CUP_END, run), "END after exhaustion");
    /* A start at 0: 4 KOs then a retire ends the cup; 5 KOs allow the retry. */
    for (int kos = 4; kos <= 5; ++kos) {
        CHECK(fzx_cup_begin(0u, 3u, &e), "begin zero");
        for (int k = 0; k < 2; ++k) {
            CHECK(fzx_cup_race_start(++run), "zero race");
            ev = event(run, 0u, FZX_OUT_RETIRE, 0u);
            CHECK(fzx_cup_accept(&ev) && fzx_cup_intent(FZX_CUP_RETRY, run), "zero retire %d", k);
        }
        CHECK(fzx_cup()->live.balance == 0, "balance 0");
        CHECK(fzx_cup_race_start(++run), "race at 0");
        for (int k = 0; k < kos; ++k) fzx_cup_race_ko(run);
        ev = event(run, 0u, FZX_OUT_RETIRE, (uint8_t)kos);
        CHECK(fzx_cup_accept(&ev), "retire at 0 with %d KOs", kos);
        if (kos == 4) CHECK(fzx_cup()->state == FZX_CUP_OVER, "4 KOs: over");
        else CHECK(fzx_cup()->state == FZX_CUP_RETIRED && fzx_cup()->live.balance == 0 && fzx_cup_intent(FZX_CUP_RETRY, run), "5 KOs: retry");
        fzx_cup_reset();
    }
    /* Quit ends the cup with no charge. */
    CHECK(fzx_cup_begin(0u, 1u, &e) && fzx_cup_race_start(++run), "begin quit");
    ev = event(run, 0u, FZX_OUT_QUIT, 0u);
    CHECK(fzx_cup_accept(&ev) && fzx_cup()->state == FZX_CUP_OVER && fzx_cup()->live.balance == 4, "quit");
    /* Reset mid-cup: old results and intents refused, a new cup starts clean. */
    CHECK(fzx_cup_begin(0u, 1u, &e) && fzx_cup_race_start(++run), "begin reset");
    c = finish(run, 0u, 99000, 0u);
    const uint32_t old = run;
    fzx_cup_reset();
    CHECK(!fzx_cup_accept(&c) && !fzx_cup_intent(FZX_CUP_END, old) && fzx_cup()->state == FZX_CUP_IDLE, "pre-reset inputs refused");
    CHECK(fzx_cup_begin(0u, 1u, &e) && fzx_cup_race_start(++run), "new cup");
    CHECK(!fzx_cup_accept(&c) && sum_totals() == 0, "a pre-reset result refused by the new cup");

    /* Determinism: same inputs, same rows; the sporting result ignores run_id. */
    fzx_classification_t x = finish(500u, 2u, 101000, 0u), y = finish(500u, 2u, 101000, 0u), z = finish(501u, 2u, 101000, 0u);
    CHECK(fzx_classification_same_result(&x, &y) && x.attempt_hash == y.attempt_hash, "same inputs, same result");
    CHECK(fzx_classification_same_result(&x, &z) && x.result_hash == z.result_hash && x.attempt_hash != z.attempt_hash,
          "result hash excludes run_id; attempt hash includes it");
    z = finish(500u, 2u, 101500, 0u);
    CHECK(!fzx_classification_same_result(&x, &z), "a changed player time differs");
}

/* ---- Catalog ---------------------------------------------------------------------------------- */
static void catalog_checks(void) {
    CHECK(fzx_course_find(0u) && (fzx_course_find(0u)->avail & FZX_AVAIL_TA), "course 0 TA");
    CHECK(fzx_course_find(1u) && fzx_course_find(1u)->avail == 0u, "course 1 stub");
    CHECK(fzx_course_step(0u, 1, FZX_AVAIL_TA) == 0u && fzx_course_step(0u, -1, FZX_AVAIL_TA) == 0u, "TA list holds course 0 only");
    CHECK(!fzx_cup_gp_ready(FZX_CUP_JACK), "Jack GP not ready");
    const fzx_cup_def_t *j = fzx_cup_def(FZX_CUP_JACK);
    CHECK(j && j->rounds == 6u && j->course[5] == 5u, "Jack rounds 0..5");
}

int main(void) {
    frozen_cases();
    random_cases(400u);
    catalog_checks();
    scripted_cup();
    if (failures) { printf("cup: %d failures\n", failures); return 1; }
    printf("cup: ok\n");
    return 0;
}
