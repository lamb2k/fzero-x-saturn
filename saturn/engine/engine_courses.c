/* Course catalog: see engine_courses.h. Rows follow tools/course_catalog.py. */
#include "engine_courses.h"
#include <stddef.h>
#ifndef ENG_COURSE_INDEX
#define ENG_COURSE_INDEX 0
#endif
#define ROW(i, r, m, k) { (uint8_t)(i), 0u, (uint8_t)(r), (uint8_t)(m), { 0, 0, 0, 0 }, k }
const fzx_course_info_t fzx_course_info[] = {
    ROW(0, 0, 0, "MUTE CITY"),
    ROW(1, 1, 1, "SILENCE"),
    ROW(2, 2, 2, "SAND OCEAN"),
    ROW(3, 3, 5, "DEVIL'S FOREST"),
    ROW(4, 4, 4, "BIG BLUE"),
    ROW(5, 5, 3, "PORT TOWN"),
};
#undef ROW
const uint32_t fzx_course_info_count = sizeof(fzx_course_info) / sizeof(fzx_course_info[0]);

const fzx_course_info_t *fzx_course_info_row(uint32_t id) {
    for (uint32_t i = 0; i < fzx_course_info_count; ++i)
        if (fzx_course_info[i].id == id) return &fzx_course_info[i];
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

#if ENG_LOW_COLD
/* The bundle's table is cold and sits in low work RAM (engine_low_rodata.ld); LOAD copies the selected row here,
 * so everything the race reads through fzx_cid (the skyline ring, the sky colours) stays in high work RAM. */
static fzx_course_id_t cid_row;
#endif
bool fzx_course_use(uint32_t id) {
    const fzx_course_id_t *c = fzx_course_bundled(id);
    if (!c) return false;
#if ENG_LOW_COLD
    cid_row = *c;
    c = &cid_row;
#endif
    fzx_cid = c;
    return true;
}
#endif

const fzx_course_info_t *fzx_course_selected(void) {
#if ENG_COURSE_SELECT
    return fzx_course_info_row(fzx_cid->id);
#else
    return fzx_course_info_row(ENG_COURSE_INDEX);
#endif
}
