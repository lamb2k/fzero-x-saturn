#ifndef FZX_MACHINE_DRAW_H
#define FZX_MACHINE_DRAW_H
#include "fzx_machine.h"
#include "fzx_view.h"
#include "ref.h"

enum {FZX_MACHINE_POSITION_MAX=128,FZX_MACHINE_NORMAL_MAX=256,FZX_MACHINE_TRIANGLE_MAX=256};
typedef struct {const uint8_t *data;uint32_t bytes,textures,hash;uint8_t fixture;} fzx_machine_pack_t;
typedef struct {
    int32_t positions[FZX_MACHINE_POSITION_MAX][3],screen[FZX_MACHINE_POSITION_MAX][2];
    uint32_t outcodes[FZX_MACHINE_POSITION_MAX],depth[FZX_MACHINE_TRIANGLE_MAX];
    fzx_material_color_t normals[FZX_MACHINE_NORMAL_MAX];
    uint16_t order[FZX_MACHINE_TRIANGLE_MAX];
    uint8_t position_ready[FZX_MACHINE_POSITION_MAX],normal_ready[FZX_MACHINE_NORMAL_MAX];
} fzx_machine_work_t;
typedef struct {
    int16_t x[4],y[4];uint16_t width,height,triangle,color;
    uint32_t offset;bool solid;fzx_machine_palette_t light;
} fzx_machine_quad_t;
typedef struct {uint32_t lod,triangles,draws,culled,clipped,saturated,fault,hash,positions,normals;} fzx_machine_stats_t;
typedef bool (*fzx_machine_emit_fn)(void *,const fzx_machine_quad_t *);
bool fzx_machine_pack_open(fzx_machine_pack_t *,const void *,uint32_t,uint32_t,uint32_t,bool);
const uint8_t *fzx_machine_pack_fog(const fzx_machine_pack_t *);
/* Original source pose through the live native camera. Charts retain original
 * UV/material bindings; per-triangle dynamic CLUT and Gouraud approximate source
 * multiplicative lighting. Near/overflow clipping uses source-mean flat color. */
bool fzx_machine_draw(const fzx_machine_pack_t *,const fzx_view_t *,const RefMachine *,
    fzx_machine_work_t *,unsigned,fzx_machine_emit_fn,void *,fzx_machine_stats_t *);
#endif
