/* Race modes: the rules table (engine_modes.h). */
#include "engine_modes.h"
#include "engine_cup.h"     /* FZX_AVAIL_* */

static const fzx_mode_rules_t rules[] = {
    /* mode              field endless pick forced cup record               avail           energy win */
    { FZX_MODE_GP,       1,    0,      1,   0,     1,  FZX_MODE_NO_RECORD,  FZX_AVAIL_GP,   256,   0 },
    { FZX_MODE_TA,       0,    0,      0,   0,     0,  FZX_MODE_REC_TA,      FZX_AVAIL_TA,   256,   0 },
    { FZX_MODE_DEATH,    1,    1,      0,   3,     0,  FZX_MODE_REC_DEATH,   FZX_AVAIL_GP,   128,   1 },
    { FZX_MODE_PRACTICE, 1,    1,      1,   0,     0,  FZX_MODE_NO_RECORD,  FZX_AVAIL_GP,   256,   0 },
};

const fzx_mode_rules_t *fzx_mode_rules(uint32_t menu_mode) {
    for (unsigned i = 0; i < sizeof(rules) / sizeof(rules[0]); ++i)
        if (rules[i].menu_mode == menu_mode) return &rules[i];
    return 0;
}

uint32_t fzx_mode_class(uint32_t menu_mode, uint32_t session_difficulty) {
    const fzx_mode_rules_t *r = fzx_mode_rules(menu_mode);
    if (!r) return session_difficulty & 3u;
    return r->class_pick ? (session_difficulty & 3u) : r->forced_class;
}

bool fzx_mode_keeps_times(uint32_t menu_mode) {
    const fzx_mode_rules_t *r = fzx_mode_rules(menu_mode);
    return r && r->record != FZX_MODE_NO_RECORD;
}
