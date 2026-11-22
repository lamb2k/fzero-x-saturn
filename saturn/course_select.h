/* Run-time course choice (engine image, ENG_COURSE_SELECT=1): the identity of the selected course's packs
 * comes from fzx_cid, a row of tools/course_bundle.py's fzx_course_ids, instead of the one course's
 * compiled-in identity headers. CSEL(field, MACRO) is MACRO in every other build, so those build as
 * before. CNAME(file, "NAME") is the course's own disc name; CMAX(MAX, MACRO) sizes a buffer for
 * the largest bundled course. */
#ifndef FZX_COURSE_SELECT_H
#define FZX_COURSE_SELECT_H
#ifndef ENG_COURSE_SELECT
#define ENG_COURSE_SELECT 0
#endif
#if ENG_COURSE_SELECT
#include "course_ids.h"
extern const fzx_course_id_t *fzx_cid;
extern uint32_t fzx_cid_index; /* fzx_cid's row number in fzx_course_ids: index per-course tables with this, never with fzx_cid - fzx_course_ids */
#define CSEL(field, macro) (fzx_cid->field)
#define CNAME(file, name) ((const char *)fzx_cid->files[file])
#define CMAX(max, macro) (max)
#else
#define CSEL(field, macro) (macro)
#define CNAME(file, name) (name)
#define CMAX(max, macro) (macro)
#endif
#endif
