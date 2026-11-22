/* HOST fixture (tests/test_modes.py): the mode entry and outcome contract (saturn/engine/engine_mode_entry.h). The front end side
 * is a model of SELECT: it holds a session, proposes an entry, and copies the normalised entry in only on FZX_ENTRY_OK.
 * Prints one PASS/FAIL line per case. */
#include <stdio.h>
#include <string.h>
#include "engine_mode_entry.h"
#include "engine_cup.h"

static uint8_t avail(uint8_t c) { return c == 0 ? (FZX_AVAIL_TA | FZX_AVAIL_GP) : c == 1 ? FZX_AVAIL_TA : 0; }
static bool machine_ok(uint16_t m) { return m < 30u; }
static bool class_locked(uint8_t d) { return d < 3u; }
static bool class_open(uint8_t d) { (void)d; return true; }
static int bad;
static void check(int ok, const char *what) { printf("%s %s\n", ok ? "PASS" : "FAIL", what); if (!ok) bad = 1; }

static fzx_session_t session;
/* The front end's confirm: check, and only on OK copy the entry in and mark it confirmed. Returns the status. */
static fzx_entry_status_t confirm(const fzx_entry_req_t *q, const fzx_entry_env_t *env) {
    fzx_entry_t e;
    const fzx_entry_status_t s = fzx_mode_entry_check(q, env, &e);
    if (s != FZX_ENTRY_OK) return s;
    session.menu_mode = e.menu_mode; session.course = e.course; session.difficulty = e.difficulty; session.cup = e.cup;
    session.machine_id = e.machine_id; session.skin = e.skin; session.slider_bits = e.slider_bits; session.confirmed = 1;
    return s;
}

int main(void) {
    const fzx_entry_env_t env = { avail, machine_ok, class_open, false };
    const fzx_entry_env_t env_locked = { avail, machine_ok, class_locked, false };
    const fzx_entry_env_t env_lab = { avail, machine_ok, class_open, true };
    fzx_entry_req_t q = { FZX_MODE_TA, 0, 2, 3, 0u };

    /* capabilities */
    check(fzx_mode_caps(FZX_MODE_VS) == 0 && fzx_mode_caps(4) == 0 && fzx_mode_caps(7) == 0, "versus, options and unknown IDs carry no capability");
    check(fzx_mode_caps(FZX_MODE_TA) == (FZX_CAP_RUNS | FZX_CAP_ENTRY_OPEN | FZX_CAP_KEEPS_TIMES), "time attack: alone, no class pick, keeps times");
    check(fzx_mode_caps(FZX_MODE_GP) == (FZX_CAP_RUNS | FZX_CAP_ENTRY_OPEN | FZX_CAP_CLASS_PICK | FZX_CAP_FIELD | FZX_CAP_CUP), "grand prix: field, class pick, cup, no times");
    check(fzx_mode_caps(FZX_MODE_PRACTICE) == (FZX_CAP_RUNS | FZX_CAP_ENTRY_OPEN | FZX_CAP_CLASS_PICK | FZX_CAP_FIELD | FZX_CAP_ENDLESS), "practice: field, endless, no times, no cup");
    check(fzx_mode_caps(FZX_MODE_DEATH) == (FZX_CAP_RUNS | FZX_CAP_FIELD | FZX_CAP_ENDLESS | FZX_CAP_KEEPS_TIMES | FZX_CAP_WINS_BY_KO), "death race: runs but ordinary entry closed");

    /* reject before launch: the session does not change */
    memset(&session, 0x5A, sizeof(session));
    const fzx_session_t before = session;
    const fzx_entry_req_t bad_reqs[] = {
        { FZX_MODE_VS, 0, 1, 3, 0u }, { 4, 0, 1, 3, 0u }, { FZX_MODE_DEATH, 0, 3, 3, 0u }, { FZX_MODE_GP, 1, 1, 3, 0u }, { FZX_MODE_TA, 5, 1, 3, 0u },
        { FZX_MODE_PRACTICE, 0, 1, 99, 0u },
    };
    const fzx_entry_status_t want[] = { FZX_ENTRY_NO_MODE, FZX_ENTRY_NO_MODE, FZX_ENTRY_CLOSED, FZX_ENTRY_NO_COURSE, FZX_ENTRY_NO_COURSE, FZX_ENTRY_NO_MACHINE };
    for (unsigned i = 0; i < sizeof(bad_reqs) / sizeof(bad_reqs[0]); ++i) {
        const int ok = confirm(&bad_reqs[i], &env) == want[i] && !memcmp(&session, &before, sizeof(session));
        printf("%s reject before launch, case %u (mode %u course %u machine %u): status %d, session untouched\n", ok ? "PASS" : "FAIL", i,
               bad_reqs[i].menu_mode, bad_reqs[i].course, bad_reqs[i].machine_id, (int)want[i]);
        if (!ok) bad = 1;
    }
    q = (fzx_entry_req_t){ FZX_MODE_PRACTICE, 0, 3, 3, 0u };
    check(confirm(&q, &env_locked) == FZX_ENTRY_CLASS_LOCKED && !memcmp(&session, &before, sizeof(session)), "master chosen while locked: refused, session untouched");
    q.menu_mode = FZX_MODE_GP;
    check(confirm(&q, &env_locked) == FZX_ENTRY_CLASS_LOCKED, "grand prix master while locked: refused");
    q.menu_mode = FZX_MODE_TA;
    check(confirm(&q, &env_locked) == FZX_ENTRY_OK, "time attack ignores the class, so a locked master does not matter");

    /* cancel without mutation: the check alone (a front end that never confirms) writes nothing */
    memset(&session, 0x5A, sizeof(session));
    fzx_entry_t e; memset(&e, 0xA5, sizeof(e));
    const fzx_entry_t e0 = e;
    q = (fzx_entry_req_t){ FZX_MODE_PRACTICE, 0, 2, 3, 0u };
    const fzx_session_t s0 = session;
    check(fzx_mode_entry_check(&q, &env, &e) == FZX_ENTRY_OK && !memcmp(&session, &s0, sizeof(session)), "cancel: a passing check leaves the session untouched");
    memset(&e, 0xA5, sizeof(e));
    q.menu_mode = FZX_MODE_VS;
    check(fzx_mode_entry_check(&q, &env, &e) == FZX_ENTRY_NO_MODE && !memcmp(&e, &e0, sizeof(e)), "a failing check leaves its output untouched");

    memset(&e, 0xA5, sizeof(e));
    q = (fzx_entry_req_t){ FZX_MODE_PRACTICE, 5, 1, 3, 0u };
    check(fzx_mode_entry_check(&q, &env, &e) == FZX_ENTRY_NO_COURSE && !memcmp(&e, &e0, sizeof(e)), "a course refusal leaves the output untouched");

    /* accepted entries and their normalisation */
    q = (fzx_entry_req_t){ FZX_MODE_TA, 0, 2, 3, 0x3F000000u };
    check(confirm(&q, &env) == FZX_ENTRY_OK && session.menu_mode == FZX_MODE_TA && session.difficulty == 0 && session.confirmed && session.skin == 1 && session.slider_bits == 0x3F000000u,
          "time attack accepted; class is the mode's own (0: no field, unused)");
    q = (fzx_entry_req_t){ FZX_MODE_PRACTICE, 0, 3, 3, 0u };
    check(confirm(&q, &env) == FZX_ENTRY_OK && session.difficulty == 3, "practice keeps the chosen class");
    q = (fzx_entry_req_t){ FZX_MODE_GP, 0, 2, 3, 0u };
    check(confirm(&q, &env) == FZX_ENTRY_OK && session.difficulty == 2 && session.menu_mode == FZX_MODE_GP, "grand prix keeps the chosen class");
    q = (fzx_entry_req_t){ FZX_MODE_DEATH, 0, 0, 3, 0u };
    check(confirm(&q, &env_lab) == FZX_ENTRY_OK && session.difficulty == 3 && session.menu_mode == FZX_MODE_DEATH, "death race only through the lab; class forced to Master");
    q = (fzx_entry_req_t){ FZX_MODE_TA, 0, 0, 3, 0u };
    q.menu_mode = FZX_MODE_TA;

    /* outcomes */
    fzx_outcome_t o;
    fzx_run_result_t r;
    struct { uint8_t mode, reason, kind, table, elig, pts; const char *what; } t[] = {
        { FZX_MODE_GP, FZX_END_FINISH, FZX_OUTCOME_FINISH, FZX_MODE_NO_RECORD, 0, 1, "grand prix finish: points, no record" },
        { FZX_MODE_TA, FZX_END_FINISH, FZX_OUTCOME_FINISH, FZX_MODE_REC_TA, 1, 0, "time attack finish: the time attack table" },
        { FZX_MODE_TA, FZX_END_RETIRE, FZX_OUTCOME_RETIRED, FZX_MODE_NO_RECORD, 0, 0, "time attack retire: nothing written" },
        { FZX_MODE_TA, FZX_END_QUIT, FZX_OUTCOME_QUIT, FZX_MODE_NO_RECORD, 0, 0, "time attack quit: nothing written" },
        { FZX_MODE_PRACTICE, FZX_END_FINISH, FZX_OUTCOME_NONE, FZX_MODE_NO_RECORD, 0, 0, "practice never finishes: no outcome" },
        { FZX_MODE_PRACTICE, FZX_END_QUIT, FZX_OUTCOME_QUIT, FZX_MODE_NO_RECORD, 0, 0, "practice exit: no record, no points" },
        { FZX_MODE_PRACTICE, FZX_END_RETIRE, FZX_OUTCOME_RETIRED, FZX_MODE_NO_RECORD, 0, 0, "practice retire: no record, no points" },
        { FZX_MODE_DEATH, FZX_END_FINISH, FZX_OUTCOME_WIN, FZX_MODE_REC_DEATH, 1, 0, "death race win: the death race table, no points" },
        { FZX_MODE_DEATH, FZX_END_RETIRE, FZX_OUTCOME_RETIRED, FZX_MODE_NO_RECORD, 0, 0, "death race player retired: no record" },
        { FZX_MODE_DEATH, FZX_END_QUIT, FZX_OUTCOME_QUIT, FZX_MODE_NO_RECORD, 0, 0, "death race quit: no record" },
        { FZX_MODE_VS, FZX_END_FINISH, FZX_OUTCOME_NONE, FZX_MODE_NO_RECORD, 0, 0, "versus: no outcome" },
    };
    for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); ++i) {
        memset(&r, 0, sizeof(r)); r.menu_mode = t[i].mode; r.reason = t[i].reason;
        fzx_mode_outcome(&r, &o);
        check(o.kind == t[i].kind && o.record_table == t[i].table && o.record_eligible == t[i].elig && o.gp_points == t[i].pts, t[i].what);
    }
    return bad;
}
