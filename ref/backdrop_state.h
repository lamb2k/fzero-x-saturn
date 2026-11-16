/* Stock single-player Purple sky/floor/cloud geometry, without RCP storage.
 * Source initialization/RNG and skyline rendering remain separate gates. */
#ifndef FZX_REF_BACKDROP_STATE_H
#define FZX_REF_BACKDROP_STATE_H

typedef struct {
    short position[3];
    unsigned short flag;
    short uv[2];
    unsigned char color[4];
} RefBackdropVertex;
typedef struct {
    unsigned int faults, count;
    float state[24]; /* Original Background fields, in source declaration order. */
    RefBackdropVertex vertices[20]; /* floor 0..7, sky 8..11, clouds 12..19 */
} RefBackdrop;

/* Call after source camera initialization. Speeds are explicit source-state
 * inputs; this does not silently consume or replace the simulation RNG. */
int ref_backdrop_init(float cloud_x_speed, float cloud_z_speed);
/* Exactly one source Background_Update. Pool index is the caller's graphics
 * pool (0/1), independent of simulation tick; pause suppresses scroll updates.
 * The caller still owns source draw order, sampler/material and clipping. */
void ref_backdrop_tick(RefBackdrop *out, unsigned int pool, int paused);
#endif
