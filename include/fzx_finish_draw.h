#ifndef FZX_FINISH_DRAW_H
#define FZX_FINISH_DRAW_H
#include "fzx_finish_pack.h"

typedef struct {
    sh2z_pos16_t positions[FZX_FINISH_VERTICES];
    sh2z_view_vertex_t projected[FZX_FINISH_VERTICES];
} fzx_finish_work_t;
typedef struct {int16_t x[4],y[4];uint16_t triangle,material,image;} fzx_finish_quad_t;
typedef bool (*fzx_finish_emit_t)(void *,const fzx_finish_quad_t *);

/* Source combined model/view row-vector matrix Q16.16 plus lens XX/YY.
 * Source viewport320x240 is cropped by8 vertical pixels for Saturn224.
 * The finish sign has its own source camera, not the racing camera. */
bool fzx_finish_view(const int32_t matrix[18],sh2z_view_t *);
bool fzx_finish_prepare(const fzx_finish_pack_t *,fzx_finish_work_t *);
/* Source order, source back-face culling, no added lighting or depth sort.
 * Cross-near-plane geometry is rejected explicitly; never submit a partial
 * list on false. Position preparation is load-time only. */
bool fzx_finish_draw(const fzx_finish_pack_t *,const sh2z_view_t *,fzx_finish_work_t *,
                     fzx_finish_emit_t,void *,unsigned *count);
#endif
