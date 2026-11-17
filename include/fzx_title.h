#ifndef FZX_TITLE_H
#define FZX_TITLE_H
#include <stdbool.h>
#include <stdint.h>

/* First cartridge title screen only. Source Title_Update owns the counter and
 * transition request. The bridge maps pad edges and records audio events.
 * A cue is an event, not evidence of sound playback. No 64DD state is emulated. */
typedef struct {
    int32_t timer;
    uint32_t ticks,unused_random_calls;
    uint16_t held,pressed,cue;
    uint8_t connected,transition,advance,stop_music;
} fzx_title_state_t;
void fzx_title_reset(fzx_title_state_t *state);
bool fzx_title_step(fzx_title_state_t *state,uint16_t buttons,bool connected,bool transition);
bool fzx_title_push_start_visible(const fzx_title_state_t *state);
#endif
