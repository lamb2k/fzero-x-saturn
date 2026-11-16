#ifndef FZX_FLOOR_RBG_H
#define FZX_FLOOR_RBG_H
#include "fzx_view.h"
#include "backdrop_state.h"

/* VDP2 rotation parameter A, in hardware order. Values occupy Q16 bit positions;
 * coordinate/matrix/address fields retain the hardware's ten fractional bits. */
typedef struct {
    int32_t start[3],line[2],dot[2],matrix[6];
    int16_t viewpoint[3],pad0,center[3],pad1;
    int32_t move[2],scale[2];
    uint32_t coefficient_start;
    int32_t coefficient_line,coefficient_dot;
} fzx_floor_rp_t;
typedef struct {
    fzx_floor_rp_t rp;
    uint16_t window[224][2];
    uint32_t fault,visible_lines,index_min,index_max,commands,hash;
} fzx_floor_rbg_t;
typedef struct {int16_t x[4],y[4];uint32_t band;} fzx_floor_polygon_t;
typedef bool (*fzx_floor_emit_fn)(void *,const fzx_floor_polygon_t *);

/* Original finite floor, source UVs, a reciprocal coefficient table and eight
 * source-derived fog bands. Band zero clears the prior VDP1 backdrop so RBG0
 * can show through; bands 1..6 blend original fog, band 7 is opaque fog.
 * This is presentation only. It does not alter the source simulation. */
bool fzx_floor_rbg_prepare(const fzx_view_t *,const RefBackdrop *,
                           fzx_floor_rbg_t *,fzx_floor_emit_fn,void *);
/* Two-word coefficient entry at index 0..32767; invalid reciprocal ranges are
 * transparent. Pure integer construction also defines the offline LUT. */
uint32_t fzx_floor_rbg_coefficient(unsigned index);
#endif
