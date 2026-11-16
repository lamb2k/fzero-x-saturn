/* Bounded native machine projection and VDP1 palette experiment. */
#ifndef FZX_MACHINE_H
#define FZX_MACHINE_H
#include "fzx_model.h"

typedef struct { int16_t position[3],uv[2]; int8_t normal[3]; } fzx_machine_vertex_t;
typedef struct {
    int16_t rotation[3][3]; /* Q1.14, model to diagnostic camera */
    int16_t light[3],reflection[3]; /* normalized Q1.14 directions */
    int16_t distance,pivot_y;
} fzx_machine_pose_t;

/* Diagnostic 128x128 viewport, focal length 160; no near-plane clipping.
 * False rejects the entire view. This is not the source game's camera. */
bool fzx_machine_project(const fzx_machine_vertex_t *vertices,unsigned count,
                         const fzx_machine_pose_t *pose,const uint8_t reflection_lut[256],
                         fzx_model_vertex_t *output);

typedef struct { uint16_t palette[16],gouraud[4]; unsigned saturated_channels; } fzx_machine_palette_t;
/* Base colors are source-combined texels before vertex lighting. The palette
 * uses average vertex shade; Gouraud is an additive residual at mean base color.
 * This cannot exactly reproduce multiplicative, varying source lighting. */
bool fzx_machine_light(const fzx_material_color_t *base,unsigned count,
                       const fzx_material_color_t shade[3],const uint8_t fog[3],
                       fzx_machine_palette_t *out);
#endif
