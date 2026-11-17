#include <yaul.h>
#include "race_workspace_native.h"
#include "road_native.h"
#include "ref.h"
#if FZX_SOURCE_ROAD_STREAM
#include "road_stream_native.h"
#endif

volatile uint32_t fzx_race_workspace_report[8];
static volatile uint32_t *before,*after;
static bool ready;
enum { GUARD=0x3e91c7a5u, GUARD_BYTES=32u };

bool fzx_race_workspace_guards(void) {
    if(!ready || !before || !after)return false;
    for(unsigned i=0;i<8;++i)if(before[i]!=GUARD || after[i]!=GUARD) {
        fzx_race_workspace_report[1]=4;return false;
    }
    return true;
}

bool fzx_race_workspace_load(void) {
    ready=false;before=NULL;after=NULL;
    fzx_race_workspace_report[0]=0x465a4750u;
    fzx_race_workspace_report[1]=2;
    const uintptr_t base=(uintptr_t)dram_cart_area_get();
    const uint32_t size=dram_cart_size_get(),bytes=ref_race_gfx_bytes();
    if(dram_cart_id_get()!=DRAM_CART_ID_4MIB || size!=FZX_ROAD_MAX_BYTES ||
       !base || (base&31u) || base!=(uintptr_t)fzx_native_road.data || fzx_road_load_status.status!=1 ||
       !bytes || bytes>size-GUARD_BYTES*2u || fzx_native_road.bytes>size)return false;
#if FZX_SOURCE_ROAD_STREAM
    if(fzx_road_stream_status!=1||!fzx_road_stream_guards()||
       fzx_road_stream_cart_end<base+fzx_native_road.bytes||
       fzx_road_stream_cart_end>base+size-GUARD_BYTES)return false;
    const uint32_t offset=(uint32_t)((fzx_road_stream_cart_end-base+95u)&~(uintptr_t)31u);
#else
    const uint32_t offset=(fzx_native_road.bytes+63u)&~31u;
#endif
    const uint32_t padded=(bytes+31u)&~31u;
    if(offset>size || padded>size-offset || GUARD_BYTES>size-offset-padded)return false;
    before=(volatile uint32_t *)(base+offset-GUARD_BYTES);
    volatile uint32_t *pool=(volatile uint32_t *)(base+offset);
    after=(volatile uint32_t *)(base+offset+padded);
    for(unsigned i=0;i<8;++i)before[i]=after[i]=GUARD;
    /* No DMA touches the cartridge. Use the same uncached CPU alias as ROAD. */
    for(uint32_t i=0;i<padded/4u;++i)pool[i]=0;
    for(uint32_t i=0;i<padded/4u;++i)if(pool[i]) {
        fzx_race_workspace_report[1]=3;return false;
    }
    if(ref_race_gfx_bind((void *)pool,bytes))return false;
    fzx_race_workspace_report[2]=(uint32_t)(uintptr_t)pool;
    fzx_race_workspace_report[3]=bytes;
    fzx_race_workspace_report[4]=(uint32_t)(uintptr_t)after;
    fzx_race_workspace_report[5]=(uint32_t)(uintptr_t)before;
    fzx_race_workspace_report[6]=(uint32_t)(uintptr_t)after;
    fzx_race_workspace_report[7]=(uint32_t)(base+fzx_native_road.bytes);
    ready=true;
    if(!fzx_race_workspace_guards()){ready=false;return false;}
    fzx_race_workspace_report[1]=1;return true;
}
