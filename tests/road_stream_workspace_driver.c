#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "yaul.h"
#include "road_native.h"
#include "race_workspace_native.h"
#include "road_stream_native.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",__LINE__,#x);return 1;}}while(0)
fzx_road_pack_t fzx_native_road;
volatile fzx_road_load_status_t fzx_road_load_status;
volatile uint32_t fzx_road_stream_status,fzx_road_stream_cart_end;
static unsigned binds;
static bool stream_ok;
static void *bound;
void *dram_cart_area_get(void){return (void *)0x04000000u;}
uint32_t dram_cart_size_get(void){return FZX_ROAD_MAX_BYTES;}
uint32_t dram_cart_id_get(void){return DRAM_CART_ID_4MIB;}
unsigned int ref_race_gfx_bytes(void){return 182000;}
int ref_race_gfx_bind(void *p,unsigned int n){++binds;bound=p;return n!=182000;}
bool fzx_road_stream_guards(void){return stream_ok;}
static void seed(void){
    memset((void *)0x04000000u,0xa5,FZX_ROAD_MAX_BYTES);binds=0;bound=NULL;stream_ok=true;
    fzx_native_road.data=(const uint8_t *)0x04000000u;fzx_native_road.bytes=4096;
    fzx_road_load_status.status=1;fzx_road_stream_status=1;fzx_road_stream_cart_end=0x04003000u;
    for(unsigned i=0;i<8;++i)((uint32_t *)(uintptr_t)fzx_road_stream_cart_end)[i]=0x51a7c36du;
}
static uint32_t hash(void){
    const unsigned char *p=(const unsigned char *)0x04000000u;uint32_t h=2166136261u;
    for(unsigned i=0;i<FZX_ROAD_MAX_BYTES;++i)h=(h^p[i])*16777619u;
    return h;
}
int main(void){
    CHECK(mmap((void *)0x04000000u,FZX_ROAD_MAX_BYTES,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)!=MAP_FAILED);
    seed();CHECK(fzx_race_workspace_load()&&binds==1&&fzx_race_workspace_guards());
    CHECK(bound==(void *)0x04003040u&&fzx_race_workspace_report[5]==0x04003020u);
    CHECK(fzx_race_workspace_report[5]>=fzx_road_stream_cart_end+32u);
    for(unsigned i=0;i<8;++i){
        CHECK(((uint32_t *)(uintptr_t)fzx_road_stream_cart_end)[i]==0x51a7c36du);
        CHECK(((uint32_t *)(uintptr_t)fzx_race_workspace_report[5])[i]==0x3e91c7a5u);
    }
    for(unsigned i=0;i<0x3000;++i)CHECK(((unsigned char *)0x04000000u)[i]==0xa5);
    for(unsigned i=0;i<((182000u+31u)&~31u);++i)CHECK(!((unsigned char *)bound)[i]);
    CHECK(fzx_race_workspace_report[7]==0x04001000u);
    unsigned rejected_failures=0;
    static const char *const cases[]={"failed stream","failed stream guard","below cart base",
        "at cart limit","insufficient tail space","below original road end","beyond cart limit","UINT32_MAX"};
    for(unsigned bad=0;bad<8;++bad){
        seed();switch(bad){case 0:fzx_road_stream_status=3;break;case 1:stream_ok=false;break;
        case 2:fzx_road_stream_cart_end=0x03ffffe0u;break;
        case 3:fzx_road_stream_cart_end=0x04400000u;break;
        case 4:fzx_road_stream_cart_end=0x043ffff0u;break;
        case 5:fzx_road_stream_cart_end=0x04000020u;break;
        case 6:fzx_road_stream_cart_end=0x04400020u;break;
        case 7:fzx_road_stream_cart_end=UINT32_MAX;break;}
        const uint32_t before=hash();const bool loaded=fzx_race_workspace_load();
        const bool unchanged=hash()==before;
        const bool rejected=!loaded&&!binds&&!fzx_race_workspace_guards()&&unchanged&&fzx_race_workspace_report[1]==2;
        printf("boundary %s: %s unchanged=%u\n",cases[bad],rejected?"rejected":"FAILED",(unsigned)unchanged);
        if(!rejected)++rejected_failures;
    }
    CHECK(!rejected_failures);
    seed();CHECK(fzx_race_workspace_load());
    ((uint32_t *)(uintptr_t)fzx_race_workspace_report[5])[0]^=1;
    CHECK(!fzx_race_workspace_guards());
    CHECK(*(uint32_t *)(uintptr_t)fzx_road_stream_cart_end==0x51a7c36du);
    puts("ROAD_STREAM_WORKSPACE_HOST_PASS distinct guards original metadata preserved failed stream range bounds");return 0;
}
