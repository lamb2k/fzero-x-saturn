#ifndef FZX_TITLE_NATIVE_H
#define FZX_TITLE_NATIVE_H
#include <yaul.h>
#include "fzx_title.h"
extern volatile uint32_t fzx_title_status,fzx_title_report[8];
extern volatile uint32_t fzx_title_debug_buttons,fzx_title_debug_limit,fzx_title_debug_reset;
/* Boot phase: borrows the existing command buffer and temporary texture VRAM.
 * Returns only after the source requests the main menu and confirm is released.
 * Selection is a subsequent phase; this function does not initialize a race. */
bool fzx_title_run(volatile const uint32_t *fields,vdp1_cmdt_t *commands,unsigned capacity,
                   void *scratch,uint32_t scratch_bytes);
#endif
