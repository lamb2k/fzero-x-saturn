#ifndef FZX_COURSE_PREVIEW_NATIVE_H
#define FZX_COURSE_PREVIEW_NATIVE_H
#include <yaul.h>
#include "fzx_course_menu.h"
bool fzx_course_preview_load(void *memory,uint32_t bytes);
bool fzx_course_preview_guards(void);
/* Append the source carousel meshes at the source object's draw position.
 * Returns the next free command index, or -1 for invalid state/capacity. */
int fzx_course_preview_draw(vdp1_cmdt_t *commands,unsigned first,unsigned capacity,
                           const fzx_course_menu_snapshot_t *frame);
extern volatile uint32_t fzx_course_preview_report[8];
#endif
