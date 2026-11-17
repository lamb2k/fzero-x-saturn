#ifndef FZX_TEST_ROAD_STREAM_NATIVE_YAUL
#define FZX_TEST_ROAD_STREAM_NATIVE_YAUL
#include "../results_native_stub/yaul.h"
enum {DRAM_CART_ID_4MIB=4};
extern unsigned fixture_cart_id,fixture_cart_bytes;
static inline void *dram_cart_area_get(void){return (void *)(uintptr_t)0x04000000u;}
static inline unsigned dram_cart_size_get(void){return fixture_cart_bytes;}
static inline unsigned dram_cart_id_get(void){return fixture_cart_id;}
#endif
