/* Race mode entry and outcome contract: see engine_mode_entry.h. */
#include "engine_mode_entry.h"

uint32_t fzx_mode_caps(uint32_t menu_mode) {
    const fzx_mode_rules_t *r = fzx_mode_rules(menu_mode);
    if (!r) return 0;
    uint32_t c = FZX_CAP_RUNS;
    if (!r->win_all_retired) c |= FZX_CAP_ENTRY_OPEN; /* Death Race stays closed until its win path qualifies (C-138) */
    if (r->class_pick) c |= FZX_CAP_CLASS_PICK;
    if (r->field) c |= FZX_CAP_FIELD;
    if (r->endless) c |= FZX_CAP_ENDLESS;
    if (r->cup) c |= FZX_CAP_CUP;
    if (r->record != FZX_MODE_NO_RECORD) c |= FZX_CAP_KEEPS_TIMES;
    if (r->win_all_retired) c |= FZX_CAP_WINS_BY_KO;
    return c;
}

fzx_entry_status_t fzx_mode_entry_check(const fzx_entry_req_t *req, const fzx_entry_env_t *env, fzx_entry_t *out) {
    const fzx_mode_rules_t *r = fzx_mode_rules(req->menu_mode);
    if (!r) return FZX_ENTRY_NO_MODE;
    if (!(fzx_mode_caps(req->menu_mode) & FZX_CAP_ENTRY_OPEN) && !env->lab) return FZX_ENTRY_CLOSED;
    if (!(env->course_avail(req->course) & r->avail)) return FZX_ENTRY_NO_COURSE;
    if (!env->machine_ok(req->machine_id)) return FZX_ENTRY_NO_MACHINE;
    const uint8_t cls = (uint8_t)fzx_mode_class(req->menu_mode, req->difficulty);
    if (r->class_pick && !env->class_open(cls)) return FZX_ENTRY_CLASS_LOCKED; /* a forced class is the mode's own */
    out->menu_mode = req->menu_mode;
    out->course = req->course;
    out->difficulty = cls;
    out->cup = 0; /* FZX_CUP_JACK: the only cup the catalog runs */
    out->machine_id = req->machine_id;
    out->skin = req->machine_id >= 33u ? 0u : 1u;
    out->slider_bits = req->slider_bits;
    return FZX_ENTRY_OK;
}

void fzx_mode_outcome(const fzx_run_result_t *run, fzx_outcome_t *out) {
    const fzx_mode_rules_t *r = fzx_mode_rules(run->menu_mode);
    out->kind = FZX_OUTCOME_NONE; out->record_table = FZX_MODE_NO_RECORD;
    out->record_eligible = 0; out->gp_points = 0;
    if (!r) return;
    switch (run->reason) {
    case FZX_END_FINISH:
        if (r->win_all_retired) out->kind = FZX_OUTCOME_WIN;
        else if (r->endless) return;           /* Practice never finishes: no outcome, nothing written */
        else out->kind = FZX_OUTCOME_FINISH;
        out->gp_points = r->cup;
        if (r->record != FZX_MODE_NO_RECORD) { out->record_table = r->record; out->record_eligible = 1; }
        break;
    case FZX_END_RETIRE: out->kind = FZX_OUTCOME_RETIRED; break;
    case FZX_END_QUIT:   out->kind = FZX_OUTCOME_QUIT; break;
    default: break;
    }
}
