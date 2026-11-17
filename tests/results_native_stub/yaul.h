/* Synthetic API recorder; no SDK memory layout or emulated rendering claim. */
#include "../countdown_native_stub/yaul.h"
#define VDP1_VRAM(x) ((uintptr_t)0x25c00000u+(x))
enum {VDP1_CMDT_CM_CLUT_16=1,VDP1_CMDT_CC_HALF_TRANSPARENT=3};
static inline void vdp1_cmdt_normal_sprite_set(vdp1_cmdt_t *c){c->kind=0;}
