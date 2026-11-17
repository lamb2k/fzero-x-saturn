#ifndef FZX_RECORDS_DRAW_H
#define FZX_RECORDS_DRAW_H
#include "fzx_records.h"
#include "fzx_results_draw.h"

#define FZX_RECORDS_DRAW_MAX 256u
typedef struct {
    int16_t x,y;
    uint8_t slot,character,r,g,b;
} fzx_records_machine_draw_t;
typedef struct {
    int16_t x,y,width,height;
    uint16_t image;
    uint8_t visible;
} fzx_records_spinner_draw_t;
typedef struct {
    fzx_results_draw_t draws[FZX_RECORDS_DRAW_MAX];
    fzx_records_machine_draw_t machines[6];
    fzx_records_spinner_draw_t spinner;
    uint32_t tick,frame;
    uint16_t count;
    uint8_t machine_count;
} fzx_records_draw_snapshot_t;

/* Pure capture of the original draw branch. IMAGE/GLYPH image is already the
 * records bank image ID. Ordered UI packets precede the spinner or machines.
 * Model slots are source instance identities, never character IDs. Source
 * positions are 320x240; native presentation crops eight rows at each edge.
 * Failure leaves output unchanged. Generated source is linked privately. */
bool fzx_records_draw_capture(const fzx_records_snapshot_t *,fzx_records_draw_snapshot_t *);
/* Emit only the ordered 2D packets. Geometry has its own explicit consumer. */
bool fzx_records_ui(const fzx_records_draw_snapshot_t *,const fzx_results_image_t *,
                    unsigned images,uint32_t texture_bytes,fzx_results_emit_t,
                    void *,unsigned *count);
/* Source-math bridges, Q16 model*view[16] and projection XX/YY[2]. */
bool fzx_records_machine_matrix(int32_t out[18]);
bool fzx_records_spinner_matrix(const fzx_records_draw_snapshot_t *,int32_t out[18]);
#endif
