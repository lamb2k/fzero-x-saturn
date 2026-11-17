#ifndef FZX_COUNTDOWN_H
#define FZX_COUNTDOWN_H
#include "fzx_view16.h"
#include "countdown_state.h"

/* Symbolic atlas slots: the black panel is a solid polygon, never a made-up
 * texture. Hidden/invalid state is distinguished by the return value. */
typedef struct {bool visible,mouth_open;uint8_t panel;} fzx_countdown_phase_t;
bool fzx_countdown_phase(int timer,fzx_countdown_phase_t *out);

/* Once per sign, compose its source Q16.16 matrix with the source view.
 * The library owns all vertex transformation and projection. On failure out
 * is unchanged. Hidden state has no matrix and must not call this adapter. */
bool fzx_countdown_view(sh2z_view_t *out,const fzx_view_t *view,const RefCountdown *state);
#endif
