#ifndef FZX_MENU_NATIVE_H
#define FZX_MENU_NATIVE_H
#include <yaul.h>
#include "fzx_menu.h"
extern volatile uint32_t fzx_menu_status,fzx_menu_report[12];
extern volatile uint32_t fzx_menu_debug_buttons,fzx_menu_debug_limit,fzx_menu_debug_reset;
extern fzx_menu_snapshot_t fzx_menu_frame;
/* Diagnostic frontend phase. Source title requests return to the caller.
 * FZX_SOURCE_COURSE_MENU connects Time Attack to original course selection.
 * Other destination requests remain held for inspection; B returns to this
 * menu until the corresponding source screen is integrated. Never redirects
 * an unimplemented mode to a Time Attack race. */
bool fzx_menu_run(volatile const uint32_t *fields,vdp1_cmdt_t *commands,unsigned capacity,
                  void *scratch,uint32_t scratch_bytes);
#endif
