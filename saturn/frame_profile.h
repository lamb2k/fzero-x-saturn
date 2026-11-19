/* Bounded diagnostic recording; excluded entirely from ordinary builds. */
#ifndef FZX_FRAME_PROFILE_H
#define FZX_FRAME_PROFILE_H
#include <stdint.h>
#include <stdbool.h>
#ifndef FZX_SLAVE_ROAD
#define FZX_SLAVE_ROAD 0
#endif
#ifndef FZX_FRAME_PROFILE
#define FZX_FRAME_PROFILE 0
#endif
#if FZX_FRAME_PROFILE
bool fzx_fp_init(uint32_t low_end,uint32_t hz);
void fzx_fp_vblank(uint16_t ticks,uint32_t field);
void fzx_fp_begin(uint32_t input);
void fzx_fp_restart(void);
void fzx_fp_mark(unsigned phase);
void fzx_fp_submit(uint32_t input);
void fzx_fp_return(void);
void fzx_fp_present(void);
void fzx_fp_scene_begin(void);
void fzx_fp_scene_end(unsigned subsystem,uint32_t commands);
#if FZX_SLAVE_ROAD
/* A subsystem timed elsewhere (the slave CPU): its ticks in this CPU's units. */
void fzx_fp_scene_add(unsigned subsystem,uint32_t elapsed,uint32_t commands);
#endif
#endif
#endif
