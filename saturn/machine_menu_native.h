#ifndef FZX_MACHINE_MENU_NATIVE_H
#define FZX_MACHINE_MENU_NATIVE_H
#include <yaul.h>
#include "fzx_machine_menu.h"

typedef struct {
    uint8_t ready,course,character,skin;
    float engine;
} fzx_machine_menu_selection_t;
extern fzx_machine_menu_selection_t fzx_machine_menu_selection;
extern volatile uint32_t fzx_machine_menu_status,fzx_machine_menu_report[24];
extern volatile uint32_t fzx_machine_menu_debug_buttons,fzx_machine_menu_debug_limit;
extern volatile uint32_t fzx_machine_menu_debug_reset,fzx_machine_menu_debug_unlock;
/* report: 0 magic,1 ticks,2 screen,3 phase,4 character,5 request,6 course,
 * 7 common hash,8 common pixels,9 palettes,10 command count,11 connected,
 * 12 skin,13..16 VRAM guards,17 model pixels,18 model pixel capacity,
 * 19 source state,20 source snapshot,21 loaded UI bank,22 loaded model skin,
 * 23 source-screen resets. Source state/snapshot live in caller scratch.
 * status: 0 returned,1 active,2 destination,3 unsupported race held,4 CD error,
 * 5 guard error,6 source error,7 draw error,8 model load,9 model guard,
 * 10 loading,11 scratch error,12 texture capacity error.
 * debug_buttons bit16 enables injected source buttons. debug_unlock=1 applies
 * source everything-unlocked on entry/reset; normal boot uses fresh-save six.
 */
bool fzx_machine_menu_run(volatile const uint32_t *fields,vdp1_cmdt_t *commands,unsigned capacity,
                          void *scratch,uint32_t scratch_bytes,uint8_t course,bool *race_ready);

/* Model consumer owns its CPU/CD scratch, native geometry and texture guards.
 * Its memory starts after the protected source state. Pixel storage is the
 * remaining texture partition; selection releases the full-portrait bank.
 * Load only runs after both framebuffer pages have been blanked and drained.
 */
bool fzx_machine_menu_model_load(void *memory,uint32_t bytes,void *pixels,uint32_t pixel_bytes,
                                uint8_t screen,uint8_t character,uint8_t skin);
bool fzx_machine_menu_model_guards(void);
int fzx_machine_menu_model_draw(vdp1_cmdt_t *commands,unsigned first,unsigned capacity,
                                const fzx_machine_menu_snapshot_t *frame);
#endif
