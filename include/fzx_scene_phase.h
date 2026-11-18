#ifndef FZX_SCENE_PHASE_H
#define FZX_SCENE_PHASE_H
/* Default-off diagnostic (FZX_SCENE_PHASES=1): FRT ticks per section of the
 * floor and cloud RBG preparation, summed over the race, so a hold snapshot
 * gives the mean per call. Disabled, every macro expands to nothing.
 * Timing is MODELLED (emulator) and includes interrupts taken in a section. */
#ifndef FZX_SCENE_PHASES
#define FZX_SCENE_PHASES 0
#endif
enum {FZX_SCP_FLOOR,FZX_SCP_CLOUDS,FZX_SCP_LAYERS};
enum {
    FZX_SCP_SETUP,FZX_SCP_TRANSFORM,FZX_SCP_OUTER,FZX_SCP_WINDOW,FZX_SCP_MAPPING,
    FZX_SCP_BANDS,FZX_SCP_HASH,FZX_SCP_PHASES
};
#if FZX_SCENE_PHASES
#include <stdint.h>
extern volatile uint32_t fzx_scp_ticks[FZX_SCP_LAYERS][FZX_SCP_PHASES];
extern volatile uint32_t fzx_scp_calls[FZX_SCP_LAYERS];
typedef struct {uint16_t last;uint8_t layer,phase;} fzx_scp_t;
static inline uint16_t fzx_scp_now(void){
    const uint16_t high=*(volatile uint8_t *)0xfffffe12u;
    const uint16_t low=*(volatile uint8_t *)0xfffffe13u;
    return (uint16_t)(high<<8|low);
}
static inline void fzx_scp_switch(fzx_scp_t *s,unsigned phase){
    const uint16_t now=fzx_scp_now();
    fzx_scp_ticks[s->layer][s->phase]+=(uint16_t)(now-s->last);
    s->last=now;s->phase=(uint8_t)phase;
}
#define FZX_SCP_BEGIN(layer) fzx_scp_t fzx_scp_state={fzx_scp_now(),(uint8_t)(layer),FZX_SCP_SETUP};++fzx_scp_calls[layer]
#define FZX_SCP(p) fzx_scp_switch(&fzx_scp_state,FZX_SCP_##p)
#define FZX_SCP_END() fzx_scp_switch(&fzx_scp_state,FZX_SCP_SETUP)
#else
#define FZX_SCP_BEGIN(layer) ((void)0)
#define FZX_SCP(p) ((void)0)
#define FZX_SCP_END() ((void)0)
#endif
#endif
