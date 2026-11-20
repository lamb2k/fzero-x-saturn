/* Course catalog: see engine_courses.h. Rows follow tools/course_catalog.py. */
#include "engine_courses.h"
#include <stddef.h>
#ifndef ENG_COURSE_INDEX
#define ENG_COURSE_INDEX 0
#endif
#if ENG_SCENES
#include "engine_scenes.h"
extern fzx_session_t fzx_session;
#endif

#if ENG_COURSE_SELECT
#define OPEN(i) 1u /* every catalog row is bundled on this disc (LOAD checks fzx_course_bundled) */
#else
#define OPEN(i) ((i) == ENG_COURSE_INDEX)
#endif
#define ROW(i, r, m, k) { (uint8_t)(i), 0u, (uint8_t)(r), (uint8_t)(m), (uint8_t)OPEN(i), { 0, 0, 0 }, k }
const fzx_course_row_t fzx_course_catalog[] = {
    ROW(0, 0, 0, "MUTE CITY"),
    ROW(1, 1, 1, "SILENCE"),
    ROW(3, 3, 5, "DEVIL'S FOREST"),
    ROW(5, 5, 3, "PORT TOWN"),
};
#undef ROW
#undef OPEN
const uint32_t fzx_course_catalog_count = sizeof(fzx_course_catalog) / sizeof(fzx_course_catalog[0]);

const fzx_course_row_t *fzx_course_row(uint32_t id) {
    for (uint32_t i = 0; i < fzx_course_catalog_count; ++i)
        if (fzx_course_catalog[i].id == id) return &fzx_course_catalog[i];
    return NULL;
}

#if ENG_COURSE_SELECT
/* The bundled courses' pack identities (tools/course_bundle.py) and the one LOAD selected. */
#include "course_select.h"
const fzx_course_id_t *fzx_cid = &fzx_course_ids[0];

const fzx_course_id_t *fzx_course_bundled(uint32_t id) {
    for (uint32_t i = 0; i < FZX_COURSE_IDS_COUNT; ++i)
        if (fzx_course_ids[i].id == id) return &fzx_course_ids[i];
    return NULL;
}

bool fzx_course_use(uint32_t id) {
    const fzx_course_id_t *c = fzx_course_bundled(id);
    if (!c) return false;
    fzx_cid = c;
    return true;
}
#endif

const fzx_course_row_t *fzx_course_selected(void) {
#if ENG_SCENES
    const fzx_course_row_t *r = fzx_course_row(fzx_session.course);
    if (r) return r;
#endif
    return fzx_course_row(ENG_COURSE_INDEX);
}
