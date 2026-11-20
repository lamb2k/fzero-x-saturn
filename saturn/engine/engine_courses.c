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

#define ROW(i, r, m, k) { (uint8_t)(i), 0u, (uint8_t)(r), (uint8_t)(m), (uint8_t)((i) == ENG_COURSE_INDEX), { 0, 0, 0 }, k }
const fzx_course_row_t fzx_course_catalog[] = {
    ROW(0, 0, 0, "MUTE CITY"),
    ROW(1, 1, 1, "SILENCE"),
    ROW(3, 3, 5, "DEVIL'S FOREST"),
    ROW(5, 5, 3, "PORT TOWN"),
};
#undef ROW
const uint32_t fzx_course_catalog_count = sizeof(fzx_course_catalog) / sizeof(fzx_course_catalog[0]);

const fzx_course_row_t *fzx_course_row(uint32_t id) {
    for (uint32_t i = 0; i < fzx_course_catalog_count; ++i)
        if (fzx_course_catalog[i].id == id) return &fzx_course_catalog[i];
    return NULL;
}

const fzx_course_row_t *fzx_course_selected(void) {
#if ENG_SCENES
    const fzx_course_row_t *r = fzx_course_row(fzx_session.course);
    if (r) return r;
#endif
    return fzx_course_row(ENG_COURSE_INDEX);
}
