#include "hud_native.h"
#include <string.h>

bool fzx_hud_vdp1(vdp1_cmdt_t *commands, uint32_t capacity,
                  const fzx_hud_list_t *draws, const void *texture_base)
{
    if (!commands || !draws || draws->count > capacity ||
        draws->count > FZX_HUD_MAX_DRAWS) return false;
    for (uint32_t i = 0; i < draws->count; ++i) {
        const fzx_hud_draw_t *d = &draws->draws[i];
        vdp1_cmdt_t *c = &commands[i];
        memset(c, 0, sizeof(*c));
        c->cmd_xa = d->x; c->cmd_ya = d->y;
        if (d->color) {
            vdp1_cmdt_polygon_set(c);
            vdp1_cmdt_color_set(c, (rgb1555_t){.raw=d->color});
            c->cmd_xb = (int16_t)(d->x+d->width-1); c->cmd_yb = d->y;
            c->cmd_xc = c->cmd_xb; c->cmd_yc = (int16_t)(d->y+d->height-1);
            c->cmd_xd = d->x; c->cmd_yd = c->cmd_yc;
        } else {
            vdp1_cmdt_normal_sprite_set(c);
            const vdp1_cmdt_draw_mode_t mode = {.end_code_disable=1,
                .color_mode=VDP1_CMDT_CM_RGB_32768, .cc_mode=VDP1_CMDT_CC_REPLACE};
            vdp1_cmdt_draw_mode_set(c, mode);
            vdp1_cmdt_char_base_set(c, (vdp1_vram_t)((uintptr_t)texture_base+d->offset));
            vdp1_cmdt_char_size_set(c, d->width, d->height);
        }
    }
    return true;
}
