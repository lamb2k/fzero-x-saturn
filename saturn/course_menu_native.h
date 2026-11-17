#ifndef FZX_COURSE_MENU_NATIVE_H
#define FZX_COURSE_MENU_NATIVE_H
#include <yaul.h>
#include "fzx_course_menu.h"
extern volatile uint32_t fzx_course_menu_status,fzx_course_menu_report[16];
extern volatile uint32_t fzx_course_menu_debug_buttons,fzx_course_menu_debug_limit,fzx_course_menu_debug_reset;
extern fzx_course_menu_snapshot_t fzx_course_menu_frame;
/* Time Attack course selection. Source main-menu requests return to caller;
 * machine requests remain held until that screen is connected. B cancels
 * the diagnostic destination hold. Uses source art and all stock cup meshes. */
bool fzx_course_menu_run(volatile const uint32_t *fields,vdp1_cmdt_t *commands,unsigned capacity,
                        void *scratch,uint32_t scratch_bytes);
#endif
