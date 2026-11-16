/* Bounded VDP1 submission for the native race HUD and its asset probe. */
#ifndef FZX_HUD_NATIVE_H
#define FZX_HUD_NATIVE_H
#include <yaul.h>
#include "fzx_hud.h"

bool fzx_hud_vdp1(vdp1_cmdt_t *commands, uint32_t capacity,
                  const fzx_hud_list_t *draws, const void *texture_base);
#endif
