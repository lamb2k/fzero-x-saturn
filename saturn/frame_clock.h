/* One interrupt writer, one ordinary reader. Interrupts must sample the FRT
 * more often than its 16-bit wrap. Native recording checks every field gap. */
#ifndef FZX_FRAME_CLOCK_H
#define FZX_FRAME_CLOCK_H
#include <stdint.h>
typedef struct {
    volatile uint32_t sequence,epoch,anchor,field,max_gap,faults;
} fzx_frame_clock_t;
static inline void fzx_frame_clock_sample(fzx_frame_clock_t *c,uint16_t raw,uint32_t field) {
    uint32_t gap=(uint16_t)(raw-c->anchor);
    ++c->sequence;
    c->epoch+=gap;c->anchor=raw;c->field=field;
    if(gap>c->max_gap)c->max_gap=gap;
    /* A normal PAL/NTSC field is about 14k/17k ticks at /32. Detect missed
     * samples or an unexpected clock configuration, rather than accepting it. */
    if(gap<10000u || gap>20000u)++c->faults;
    ++c->sequence;
}
#endif
