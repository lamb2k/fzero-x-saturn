#ifndef FZX_RACER_SPRITE_NATIVE_H
#define FZX_RACER_SPRITE_NATIVE_H
/* FZX_RACER_SPRITES: extra racers drawn with the player's rotation sprite set
 * (until per-machine sets exist). Each body uses the source draw-state
 * convention of RefMachine: model_pos and model_basis (rows), the player's
 * model scale. Bodies are posed, culled, sorted far to near and emitted as one
 * VDP1 distorted sprite each. Returns the number of commands written. */
#include <stdint.h>
#include <yaul.h>
#include "fzx_view.h"

#define FZX_RACER_SPRITE_MAX_EXTRA 31u

typedef struct {
    float pos[3];
    float basis[3][3];
} fzx_racer_sprite_body_t;

unsigned fzx_racer_sprites_draw(vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,
                                const fzx_racer_sprite_body_t *bodies,unsigned n);
#endif
