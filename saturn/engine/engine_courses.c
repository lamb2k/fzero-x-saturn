/* Course catalog: see engine_courses.h. Rows follow tools/course_catalog.py. */
#include "engine_courses.h"
#include <stddef.h>
#ifndef ENG_COURSE_INDEX
#define ENG_COURSE_INDEX 0
#endif
#define ROW(i, r, m, k) { (uint8_t)(i), (uint8_t)((i) / 6u), (uint8_t)(r), (uint8_t)(m), { 0, 0, 0, 0 }, k }
const fzx_course_info_t fzx_course_info[] = {
    ROW(0, 0, 0, "MUTE CITY"),
    ROW(1, 1, 1, "SILENCE"),
    ROW(2, 2, 2, "SAND OCEAN"),
    ROW(3, 3, 5, "DEVIL'S FOREST"),
    ROW(4, 4, 4, "BIG BLUE"),
    ROW(5, 5, 3, "PORT TOWN"),
    /* Queen Cup (cup 1): the music is the BGM id of race.c D_i2_80106F14 */
    ROW(6, 0, 7, "SECTOR ALPHA"),
    ROW(7, 1, 6, "RED CANYON"),
    ROW(8, 2, 5, "DEVIL'S FOREST 2"),
    ROW(9, 3, 0, "MUTE CITY 2"),
    ROW(10, 4, 4, "BIG BLUE 2"),
    ROW(11, 5, 8, "WHITE LAND"),
    /* King Cup (cup 2) and Joker Cup (cup 3): the music is the retail table's (Rainbow Road plays Sector's) */
    ROW(12, 0, 2, "FIRE FIELD"),
    ROW(13, 1, 1, "SILENCE 2"),
    ROW(14, 2, 7, "SECTOR BETA"),
    ROW(15, 3, 6, "RED CANYON 2"),
    ROW(16, 4, 8, "WHITE LAND 2"),
    ROW(17, 5, 0, "MUTE CITY 3"),
    ROW(18, 0, 7, "RAINBOW ROAD"),
    ROW(19, 1, 5, "DEVIL'S FOREST 3"),
    ROW(20, 2, 6, "SPACE PLANT"),
    ROW(21, 3, 2, "SAND OCEAN 2"),
    ROW(22, 4, 3, "PORT TOWN 2"),
    ROW(23, 5, 8, "BIG HAND"),
#if ENG_MODES
    /* Death Race (course id 54, ROM record 24): sector venue, Sector's music */
    ROW(54, 0, 7, "DEATH RACE"),
#endif
};
#undef ROW
const uint32_t fzx_course_info_count = sizeof(fzx_course_info) / sizeof(fzx_course_info[0]);

const fzx_course_info_t *fzx_course_info_row(uint32_t id) {
    for (uint32_t i = 0; i < fzx_course_info_count; ++i)
        if (fzx_course_info[i].id == id) return &fzx_course_info[i];
    return NULL;
}

#if ENG_COURSE_SELECT
/* The bundled courses' identity: the image keeps only an index (id, size, FNV; tools/course_bundle.py). The selected
 * course's row is a disc file (CIDnn.BIN), read at LOAD into high work RAM and checked as it arrives; the race reads it
 * through fzx_cid. Rows are never in the image: twelve courses would be 50 KB of cold data in low work RAM, which is
 * as large as the host buffers that carry its load image (engine_low.x). */
#include "course_select.h"
#include "texture_cd.h"
extern unsigned char sat_stub_data_end[];
static fzx_course_id_t cid_row __attribute__((aligned(8)));
const fzx_course_id_t *fzx_cid = &cid_row;
static int32_t cid_loaded = -1;      /* the course id cid_row holds, or -1 */
/* The selected row's position in the course index (the row's own `row` field, checked at load): the per-course tables
 * (course_class.h, course_drops.h) are indexed by it. */
uint32_t fzx_cid_index = 0u;

static const fzx_course_index_t *course_index_row(uint32_t id) {
    for (uint32_t i = 0; i < FZX_COURSE_IDS_COUNT; ++i)
        if (fzx_course_index[i].id == id) return &fzx_course_index[i];
    return NULL;
}

#if ENG_MODES
#include "course_practice.h"
/* Practice's rival rows of the course in use (PRnn.BIN, big-endian int32 [class][vehicle][8]): disc data read at LOAD with the row,
 * 3,840 B in high work RAM instead of 3,840 B a bundled course in the image (tools/course_practice_bundle.py). */
#if ENG_LOW_COLD
/* Read at the race's field load only (engine_field.c), never in a race frame: low work RAM (engine_low.x). */
__attribute__((section(".bss.lowcold.practice_rows")))
#endif
static int32_t practice_rows[4][30][8] __attribute__((aligned(8)));
const int32_t (*const fzx_practice_rows)[30][8] = practice_rows;
static int32_t practice_loaded = -1;
volatile uint32_t fzx_practice_load[4];   /* [0] loads, [1] last course id, [2] last result (1 ok), [3] cd retries seen (gate) */
#endif

#if !ENG_MODES
bool fzx_course_use(uint32_t id) {
    const fzx_course_index_t *x = course_index_row(id);
    if (!x) return false;
    if (cid_loaded == (int32_t)id) return true;
    cid_loaded = -1;
    if (x->bytes != FZX_COURSE_ROW_BYTES || id > 99u) return false;
    char name[12] = { 'C', 'I', 'D', (char)('0' + id / 10u), (char)('0' + id % 10u), '.', 'B', 'I', 'N', 0, 0, 0 };
    const uintptr_t scratch = ((uintptr_t)sat_stub_data_end + 31u) & ~(uintptr_t)31u;
    if (!fzx_texture_cd_load(name, FZX_COURSE_ROW_BYTES, x->hash, &cid_row, (void *)scratch, 4096u)) return false;
    if (cid_row.id != id || cid_row.row != (uint32_t)(x - fzx_course_index)) return false; /* the file is not this course's row */
    cid_loaded = (int32_t)id;
    fzx_cid_index = cid_row.row;
    return true;
}
#else
bool fzx_course_use(uint32_t id) {
    const fzx_course_index_t *x = course_index_row(id);
    if (!x) return false;
    if (id > 99u) return false;
    const uintptr_t scratch = ((uintptr_t)sat_stub_data_end + 31u) & ~(uintptr_t)31u;
    if (cid_loaded != (int32_t)id) {
        cid_loaded = -1;
        if (x->bytes != FZX_COURSE_ROW_BYTES) return false;
        char name[12] = { 'C', 'I', 'D', (char)('0' + id / 10u), (char)('0' + id % 10u), '.', 'B', 'I', 'N', 0, 0, 0 };
        if (!fzx_texture_cd_load(name, FZX_COURSE_ROW_BYTES, x->hash, &cid_row, (void *)scratch, 4096u)) return false;
        if (cid_row.id != id || cid_row.row != (uint32_t)(x - fzx_course_index)) return false; /* the file is not this course's row */
        cid_loaded = (int32_t)id;
        fzx_cid_index = cid_row.row;
    }
    if (practice_loaded != (int32_t)id) {
        practice_loaded = -1;
        char name[12] = { 'P', 'R', (char)('0' + id / 10u), (char)('0' + id % 10u), '.', 'B', 'I', 'N', 0, 0, 0, 0 };
        const bool ok = fzx_texture_cd_load(name, FZX_COURSE_PRACTICE_BYTES, fzx_course_practice_hash[x - fzx_course_index], practice_rows, (void *)scratch, 4096u);
        ++fzx_practice_load[0]; fzx_practice_load[1] = id; fzx_practice_load[2] = ok;
        if (!ok) return false;
        practice_loaded = (int32_t)id;
    }
    return true;
}
#endif
#endif

const fzx_course_info_t *fzx_course_selected(void) {
#if ENG_COURSE_SELECT
    return fzx_course_info_row(fzx_cid->id);
#else
    return fzx_course_info_row(ENG_COURSE_INDEX);
#endif
}
