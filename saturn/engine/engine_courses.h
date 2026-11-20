/* Course catalog (ENG_COURSE_SELECT, scenes contract C-084): one row per course this
 * repository bakes, in the original course order (course index = cup x 6 + round). The
 * scenes lane reads it to offer courses; LOAD and the race take the selected row's music.
 * Until per-course packs are selected at run time, an image carries one course's packs
 * (ENG_COURSE): only that row is open for Time Attack. Plain data, no game assets. */
#ifndef FZX_ENGINE_COURSES_H
#define FZX_ENGINE_COURSES_H
#include <stdint.h>

typedef struct {
    uint8_t id;           /* original course index (session.course, records) */
    uint8_t cup, round;   /* 0 = Jack; round 0..5 */
    uint8_t music;        /* music id (race.c D_i2_80106F14), the CD-DA track's key */
    uint8_t time_attack;  /* 1: this image can race it in Time Attack */
    uint8_t pad[3];
    char key[16];         /* display key: the course name, upper case ASCII, NUL-padded */
} fzx_course_row_t;

extern const fzx_course_row_t fzx_course_catalog[];
extern const uint32_t fzx_course_catalog_count;
/* The row for an original course index, or NULL when it is not baked. */
const fzx_course_row_t *fzx_course_row(uint32_t id);
/* The selected course's row (the session's course in the scenes image; the image's course otherwise). */
const fzx_course_row_t *fzx_course_selected(void);
#endif
