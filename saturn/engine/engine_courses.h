/* Course facts (ENG_COURSE_SELECT): one row per course this repository bakes, in the original
 * course order (course index = cup x 6 + round): cup, round, music and the display name. Which
 * courses a build can race is the scenes catalog's job (engine_cup.h fzx_course_catalog,
 * SCENES.md 11.1); its avail bits follow the bundle (FZX_COURSE_BUNDLE_MASK). LOAD and the race
 * take the used course's music. Plain data, no game assets. */
#ifndef FZX_ENGINE_COURSES_H
#define FZX_ENGINE_COURSES_H
#include <stdint.h>

typedef struct {
    uint8_t id;           /* original course index (session.course, records) */
    uint8_t cup, round;   /* 0 = Jack; round 0..5 */
    uint8_t music;        /* music id (race.c D_i2_80106F14), the CD-DA track's key */
    uint8_t pad[4];
    char key[16];         /* display key: the course name, upper case ASCII, NUL-padded */
} fzx_course_info_t;

extern const fzx_course_info_t fzx_course_info[];
extern const uint32_t fzx_course_info_count;
/* The row for an original course index, or NULL when it is not baked. */
const fzx_course_info_t *fzx_course_info_row(uint32_t id);
/* The course in use: the one LOAD selected (fzx_course_use), or the image's course before any. */
const fzx_course_info_t *fzx_course_selected(void);
/* ENG_COURSE_SELECT: one disc carries every catalog course's packs; LOAD selects the set. */
#include <stdbool.h>
bool fzx_course_use(uint32_t id);   /* false: not bundled */
#endif
