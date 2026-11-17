/* Synthetic cartridge admission and ownership checks. No retail data or SDK. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "yaul.h"
#include "race_workspace_native.h"
#include "road_native.h"

static _Alignas(32) uint8_t allocation[FZX_ROAD_MAX_BYTES+64u];
static uint8_t *cart;
static uint32_t cart_size,cart_id,pool_bytes,bind_calls;
static int bind_result,bind_corruption;
static uint32_t *bound_pool;
static uint32_t road_hash;
fzx_road_pack_t fzx_native_road;
volatile fzx_road_load_status_t fzx_road_load_status;

void *dram_cart_area_get(void){return cart;}
uint32_t dram_cart_size_get(void){return cart_size;}
uint32_t dram_cart_id_get(void){return cart_id;}
unsigned int ref_race_gfx_bytes(void){return pool_bytes;}
static uint32_t hash(const void *data,uint32_t bytes){const uint8_t*p=data;uint32_t h=2166136261u;while(bytes--)h=(h^*p++)*16777619u;return h;}
int ref_race_gfx_bind(void *memory,unsigned int bytes){
    ++bind_calls;bound_pool=memory;
    assert(bytes==pool_bytes && ((uintptr_t)memory&31u)==0);
    assert((uint8_t*)memory>=cart+fzx_native_road.bytes+32u);
    const uint32_t padded=(bytes+31u)&~31u;
    assert((uint8_t*)memory+padded+32u<=cart+cart_size);
    for(uint32_t i=0;i<padded/4u;++i)assert(bound_pool[i]==0);
    if(bind_corruption)bound_pool[-1]^=1;
    return bind_result;
}
static void prepare(uint32_t road_bytes,uint32_t bytes){
    memset(allocation,0xa5,sizeof(allocation));cart=allocation+32;
    cart_size=FZX_ROAD_MAX_BYTES;cart_id=DRAM_CART_ID_4MIB;pool_bytes=bytes;
    bind_calls=0;bind_result=bind_corruption=0;bound_pool=NULL;
    memset(&fzx_native_road,0,sizeof(fzx_native_road));fzx_native_road.data=cart;fzx_native_road.bytes=road_bytes;
    fzx_road_load_status.status=1;
    for(uint32_t i=0;i<road_bytes && i<FZX_ROAD_MAX_BYTES;++i)cart[i]=(uint8_t)((i*37u+(i>>8))&255u);
    road_hash=hash(cart,road_bytes<=FZX_ROAD_MAX_BYTES?road_bytes:FZX_ROAD_MAX_BYTES);
}
static void road_unchanged(uint32_t bytes){
    assert(hash(allocation+32,bytes)==road_hash);
    for(unsigned i=0;i<32;++i)assert(allocation[i]==0xa5 && allocation[FZX_ROAD_MAX_BYTES+32u+i]==0xa5);
}
static void success(uint32_t road_bytes,uint32_t bytes){
    prepare(road_bytes,bytes);assert(fzx_race_workspace_load());assert(fzx_race_workspace_guards());assert(bind_calls==1);
    const uintptr_t start=(uintptr_t)bound_pool;const uint32_t padded=(bytes+31u)&~31u;
    assert((start&31u)==0);assert(start>=(uintptr_t)cart+road_bytes+32u);
    assert(start+padded+32u<=(uintptr_t)cart+cart_size);
    for(uint32_t i=0;i<padded/4u;++i)assert(bound_pool[i]==0);
    for(unsigned i=0;i<8;++i)assert(bound_pool[(int)i-8]==0x3e91c7a5u && bound_pool[padded/4u+i]==0x3e91c7a5u);
    for(const uint8_t*p=cart+road_bytes;p<(const uint8_t*)bound_pool-32;++p)assert(*p==0xa5);
    for(const uint8_t*p=(const uint8_t*)bound_pool+padded+32;p<cart+cart_size;++p)assert(*p==0xa5);
    assert(fzx_race_workspace_report[0]==0x465a4750u && fzx_race_workspace_report[1]==1);
    assert(fzx_race_workspace_report[2]==(uint32_t)start && fzx_race_workspace_report[3]==bytes);
    assert(fzx_race_workspace_report[4]==(uint32_t)(start+padded));
    assert(fzx_race_workspace_report[5]==(uint32_t)(start-32));
    assert(fzx_race_workspace_report[6]==(uint32_t)(start+padded));
    assert(fzx_race_workspace_report[7]==(uint32_t)((uintptr_t)cart+road_bytes));road_unchanged(road_bytes);
}
static void rejected_without_write(void){
    const uint32_t before_hash=hash(allocation,sizeof(allocation));
    assert(!fzx_race_workspace_load());assert(!fzx_race_workspace_guards());assert(bind_calls==0);
    assert(fzx_race_workspace_report[1]==2);assert(hash(allocation,sizeof(allocation))==before_hash);
}
static void failures(void){
    for(unsigned test=0;test<10;++test){
        success(16385,182000);prepare(16385,182000);
        switch(test){
        case 0:cart_id=0;break;
        case 1:cart_size=1024*1024;break;
        case 2:cart=NULL;break;
        case 3:fzx_native_road.data=cart+32;break;
        case 4:fzx_road_load_status.status=0;break;
        case 5:fzx_native_road.bytes=FZX_ROAD_MAX_BYTES+1u;break;
        case 6:pool_bytes=0;break;
        case 7:pool_bytes=UINT32_MAX;break;
        case 8:fzx_native_road.bytes=FZX_ROAD_MAX_BYTES;break;
        case 9:cart_size=FZX_ROAD_MAX_BYTES+1u;break;
        }
        rejected_without_write();
    }
    for(unsigned alignment=1;alignment<32;++alignment){
        prepare(16385,182000);cart+=alignment;fzx_native_road.data=cart;rejected_without_write();
    }
    const uint32_t padded=(182000u+31u)&~31u;
    success(FZX_ROAD_MAX_BYTES-padded-64u,182000);
    prepare(FZX_ROAD_MAX_BYTES-padded-63u,182000);rejected_without_write();
    prepare(16385,182000);bind_result=7;assert(!fzx_race_workspace_load());assert(bind_calls==1);assert(!fzx_race_workspace_guards());road_unchanged(16385);
    prepare(16385,182000);bind_corruption=1;assert(!fzx_race_workspace_load());assert(bind_calls==1);
    assert(fzx_race_workspace_report[1]==4);bound_pool[-1]^=1;
    assert(!fzx_race_workspace_guards());road_unchanged(16385);
}
static void guard_words(void){
    for(unsigned side=0;side<2;++side)for(unsigned word=0;word<8;++word){
        success(16385,182000);uint32_t*guard=side?bound_pool+((182000u+31u)&~31u)/4u:bound_pool-8;
        guard[word]^=0x80000001u;assert(!fzx_race_workspace_guards());assert(fzx_race_workspace_report[1]==4);road_unchanged(16385);
    }
}
int main(int argc,char**argv){
    if(argc==2 && !strcmp(argv[1],"write-fault")){
        prepare(16385,182000);assert(!fzx_race_workspace_load());assert(!fzx_race_workspace_guards());
        assert(fzx_race_workspace_report[1]==3 && !bind_calls);road_unchanged(16385);puts("readback write-fault rejected");return 0;
    }
    assert(argc==1);assert(!fzx_race_workspace_guards());
    for(unsigned remainder=0;remainder<32;++remainder)success(16384u+remainder,182000u+remainder);
    success(128,1);guard_words();failures();
    puts("workspace:32alignment-residues,182000Bpool,padding,road-preservation,exact-fit,16guardwords,42prewrite-controls,bind-failure,failed-finalguard-notready PASS");return 0;
}
