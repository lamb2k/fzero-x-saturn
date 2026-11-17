/* HOST lifecycle fixture only: no SDK layout or hardware behavior claim. */
#ifndef FZX_RESULTS_SCENE_TEST_YAUL_H
#define FZX_RESULTS_SCENE_TEST_YAUL_H
#include "../results_native_stub/yaul.h"
enum { DRAM_CART_ID_4MIB=4 };
static inline void *dram_cart_area_get(void){return (void *)(uintptr_t)0x04000000u;}
static inline unsigned dram_cart_size_get(void){return 0x400000u;}
static inline unsigned dram_cart_id_get(void){return DRAM_CART_ID_4MIB;}
#endif
