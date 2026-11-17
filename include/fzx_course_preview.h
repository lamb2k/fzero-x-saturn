#ifndef FZX_COURSE_PREVIEW_H
#define FZX_COURSE_PREVIEW_H
#include "sh2zam/view_experimental.h"

/* Private FZXCPV01 conversion. Storage stays resident and immutable after
 * admission; all scratch is supplied by the caller. No retail data here. */
#define FZX_COURSE_PREVIEW_VERTEX_LIMIT 768u
typedef struct {
    const uint8_t *data;
    uint32_t bytes,vertices,quads,xyz_offset,quad_offset,camera_offset;
    uint32_t max_vertices,max_quads;
} fzx_course_preview_pack_t;
typedef struct { int16_t x[3],y[3]; } fzx_course_preview_triangle_t;
typedef bool (*fzx_course_preview_emit_t)(void *,const fzx_course_preview_triangle_t *);
bool fzx_course_preview_open(fzx_course_preview_pack_t *out,const void *data,
                             uint32_t bytes,uint32_t expected_hash);
bool fzx_course_preview_view(const fzx_course_preview_pack_t *pack,unsigned yaw,
                             int32_t carousel_quarters,sh2z_view_t *out);
bool fzx_course_preview_gather(const fzx_course_preview_pack_t *pack,unsigned course,
                               sh2z_pos16_t *positions,unsigned capacity,unsigned *count);
/* Original double-sided triangle order and diagonal; side clipping is VDP1's
 * responsibility unless coordinates leave its signed 11-bit guard. No LOD. */
bool fzx_course_preview_render(const fzx_course_preview_pack_t *pack,unsigned course,
    unsigned yaw,int32_t carousel_quarters,sh2z_pos16_t *positions,
    sh2z_view_vertex_t *projected,unsigned capacity,fzx_course_preview_emit_t emit,
    void *context,unsigned *triangles);
#endif
