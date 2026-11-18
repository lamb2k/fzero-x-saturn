#ifndef FZX_CLOUDS_VDP2_NATIVE_H
#define FZX_CLOUDS_VDP2_NATIVE_H
#include <yaul.h>
#include "fzx_clouds_rbg.h"
/* FZX_CLOUDS_VDP2=1: clouds on RBG0 rotation parameter B (mode 3, switched by
 * the floor's W0 line window), the sky on the VDP2 back screen in line-colour
 * mode, cloud fade as VDP1 colour-calculation bands. See
 * generated/C5-CLOUDS-VDP2-PLAN.md. */
extern fzx_clouds_rbg_t fzx_clouds_rbg;
extern volatile uint32_t fzx_clouds_status,fzx_clouds_pending,fzx_clouds_commits;
extern volatile uint32_t fzx_clouds_degenerate,fzx_clouds_bands,fzx_clouds_sky_lines;
/* Once, after fzx_floor_load: level-0 cloud tile and map into VDP2, rotation
 * parameter B, the W1 line window and the back-screen line table. */
bool fzx_clouds_vdp2_load(const void *planes_vram);
/* Per frame: the 224 back-screen sky colours (sampled from the source sky bands
 * at the view's centre column). Needs a published sky. */
bool fzx_clouds_sky_lines(const fzx_view_t *view,const RefBackdrop *source);
/* Per frame, after fzx_clouds_sky_lines: parameter B, W1 and the fade bands
 * (a user-clip command first, then the bands). */
bool fzx_clouds_native(vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,
                       const RefBackdrop *source,uint32_t *count);
/* From fzx_floor_present at VBlank: tables, sky lines and band colours. */
void fzx_clouds_present(void);
#endif
