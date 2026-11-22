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
extern volatile uint32_t fzx_clouds_degenerate,fzx_clouds_bands,fzx_clouds_sky_line_count;
/* Once, after fzx_floor_load: level-0 cloud tile and map into VDP2, rotation
 * parameter B, the W1 line window and the back-screen line table. */
bool fzx_clouds_vdp2_load(const void *planes_vram);
/* Per frame: the 224 back-screen sky colours (sampled from the source sky bands
 * at the view's centre column). Needs a published sky. */
bool fzx_clouds_sky_lines(const fzx_view_t *view,const RefBackdrop *source);
/* FZX_VDP1_DBANK: bank 1 of the staged tables (cloud parameters and window, sky
 * lines) and the bank the next prepare uses. */
void fzx_clouds_dbank(void *clouds_bank1,void *sky_bank1);
void fzx_clouds_dbank_select(unsigned bank);
/* Per frame, after fzx_clouds_sky_lines: parameter B, W1 and the fade bands
 * (a user-clip command first, then the bands). */
bool fzx_clouds_native(vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,
                       const RefBackdrop *source,uint32_t *count);
#if ENG_BAL_CLOUDS
/* Engine balance (claude/balance): fzx_clouds_native split so the prepare can
 * run on either SH-2. Master: fzx_clouds_ready (the selected bank is free), then
 * after the prepare fzx_clouds_commit (the queue push, interrupts masked); after a
 * slave prepare, fzx_clouds_purge_remote first. Either CPU: fzx_clouds_prepare, with
 * the inputs (view, backdrop, sky lines, bank) in memory. */
bool fzx_clouds_ready(void);
bool fzx_clouds_prepare(vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,
                        const RefBackdrop *source,uint32_t *count);
void fzx_clouds_commit(void);
void fzx_clouds_purge_remote(void);
uint32_t fzx_clouds_stage_hash(uint32_t h);
#if ENG_BAL_SPLIT
/* ENG_BAL_SPLIT (claude/speed-2): the prepare in parts (src/fzx_clouds_rbg.h). Master with the job: setup, half0, then
 * either CPU: split_b (the second half into a stage), then the master: split_purge_remote (when the slave ran it),
 * split_finish (the stage behind the first half, the status words). split_count: commands so far. */
/* ready(arg): called by the setup once the fade bands may start (positions stored, outer clip passed), before the mapping. */
bool fzx_clouds_split_setup(vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,const RefBackdrop *source,
                            void (*ready)(void *),void *ready_arg);
bool fzx_clouds_split_half0(const fzx_view_t *view,const RefBackdrop *source);
uint32_t fzx_clouds_split_count(void);
void fzx_clouds_split_b(const fzx_view_t *view,const RefBackdrop *source);
void fzx_clouds_split_purge_remote(void);
bool fzx_clouds_split_finish(uint32_t *count);
#endif
#endif
/* From fzx_floor_present at VBlank: tables, sky lines and band colours.
 * Returns true when it wrote a frame's data. */
bool fzx_clouds_present(void);
/* Per presented frame, after the floor upload: FRT ticks from the first cloud
 * write to the end of the floor upload, and whether it began and ended in
 * VBlank. Log words: ticks (bits 0..13) | 0x4000 began in blank | 0x8000
 * ended in blank; the first FZX_CLOUDS_UPLOAD_LOG frames are kept. */
#define FZX_CLOUDS_UPLOAD_LOG 2048u
extern uint16_t fzx_clouds_upload_log[FZX_CLOUDS_UPLOAD_LOG];
extern volatile uint32_t fzx_clouds_upload_count,fzx_clouds_upload_max,fzx_clouds_upload_overruns;
void fzx_clouds_upload_record(uint16_t start,uint32_t began_in_blank);
#endif
