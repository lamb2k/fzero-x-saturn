#ifndef FZX_RESULTS_DRAW_H
#define FZX_RESULTS_DRAW_H
#include "fzx_results.h"

/* Offline images have eight-pixel row alignment and transparent row padding.
 * A source subrectangle is drawn from the full padded row with a destination
 * user clip. This preserves source texels without copying or repacking pixels
 * during results animation. Source and clip edges are half-open. */
typedef struct {
    uint32_t offset,bytes;
    uint16_t width,height,source_width;
    uint8_t clut,reserved;
    uint16_t palette[16];
} fzx_results_image_t;
typedef struct {
    int16_t x,y,clip[4];
    uint16_t width,height,image;
    uint32_t offset;
    uint8_t kind,clut,r,g,b,a;
} fzx_results_sprite_t;
typedef bool (*fzx_results_emit_t)(void *,const fzx_results_sprite_t *);

/* Returns false for malformed commands, source crops or image descriptors.
 * Native consumers must discard an unsuccessful list rather than submit a
 * partial frame. A fully clipped/transparent packet emits nothing. */
bool fzx_results_ui(const fzx_results_snapshot_t *,const fzx_results_image_t *,
                    unsigned images,const uint16_t (*font)[2],unsigned glyphs,
                    uint32_t texture_bytes,fzx_results_emit_t,void *,unsigned *count);
#endif
