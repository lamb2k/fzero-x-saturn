/* The two source course combiners and their bounded RGBA16 tile views. */
#ifndef FZX_ROAD_MATERIAL_H
#define FZX_ROAD_MATERIAL_H
#include "fzx_road_pack.h"
#include "fzx_material.h"

typedef struct {
    fzx_material_texture_t texture;
    bool primitive; /* (P-S)*Pa+S; otherwise (T-S)*Ta+S */
} fzx_road_material_t;

/* Decode once at pack admission. TMEM selects a view into the immutable
 * source texture; in particular the tunnel's second tile is its second half.
 * Unsupported layouts/state are rejected, never replaced by a flat material. */
bool fzx_road_material_open(fzx_road_material_t *out,
                            const fzx_road_pack_t *pack, uint32_t index);
/* UVs in the road pack already include the source vertex-load scale and
 * ModifyVertex writes. Convert signed Q10.5 to Q24.8 by multiplying by eight;
 * do NOT apply gSPTexture scaling again. shade.a is the interpolated fog factor.
 * Road coverage is opaque: neither texture alpha nor fog cuts a hole. */
fzx_material_color_t fzx_road_material_shade(const fzx_road_material_t *material,
                                            fzx_material_color_t texel,
                                            fzx_material_color_t shade,
                                            const uint8_t primitive[4],
                                            const uint8_t fog[3]);
uint16_t fzx_road_material_pixel(const fzx_road_material_t *material,
                                 int32_t u_q8, int32_t v_q8,
                                 fzx_material_color_t shade,
                                 const uint8_t primitive[4], const uint8_t fog[3]);
#endif
