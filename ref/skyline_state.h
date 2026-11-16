/* Source Mute City skyline placement and authored billboard state. */
#ifndef FZX_REF_SKYLINE_STATE_H
#define FZX_REF_SKYLINE_STATE_H
#include "backdrop_state.h"
#define REF_SKYLINE_COUNT 6
typedef struct {
    unsigned int sprite_id, visible;
    float position[3];
    int matrix[16]; /* Source-rounded signed 16.16, row-major. */
} RefSkylineInstance;
typedef struct {
    unsigned int faults, count;
    unsigned char tint[4];
    RefBackdropVertex vertices[4];
    RefSkylineInstance instances[REF_SKYLINE_COUNT];
} RefSkyline;
int ref_skyline_init(void);
void ref_skyline_tick(RefSkyline* out, unsigned int pool);
#endif
