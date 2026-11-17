#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "road_stream_under_test.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",__LINE__,#x);return 1;}}while(0)
unsigned fixture_cart_id=4,fixture_cart_bytes=0x400000;
fzx_road_pack_t fzx_native_road;
volatile fzx_road_load_status_t fzx_road_load_status;
static unsigned fault,loads,opens,prepares,draws,adapters;
static uint32_t vertex_count,image_count,scratch_bytes,draw_capacity,adapter_capacity;
enum {META=1,OPEN,TEXTURE,PREPARE,LOAD_GUARD,DRAW,ADAPTER,EMITTED,POST_GUARD};
uint32_t fzx_texture_cd_scratch_bytes(void){return scratch_bytes;}
bool fzx_texture_cd_load(const char *name,uint32_t bytes,uint32_t hash,void *out,void *scratch,uint32_t space){
    ++loads;if(scratch!=(void *)0x00200000u||space!=12256u)return false;
    if(!strcmp(name,"ROADLOD.PAK"))return fault!=META&&bytes==256&&hash==11&&out==(void *)0x04001040u;
    return !strcmp(name,"ROADLTX.PAK")&&fault!=TEXTURE&&bytes==512&&hash==22&&out==(void *)0x25c01000u;
}
bool fzx_road_stream_open(fzx_road_stream_pack_t *p,const void *data,uint32_t bytes,uint32_t hash,uint32_t tx){
    ++opens;if(fault==OPEN||bytes!=256||hash!=11||tx!=512)return false;
    memset(p,0,sizeof(*p));p->data=data;p->bytes=bytes;p->texture_bytes=tx;
    p->count[FZX_RLD_VERTICES]=vertex_count;p->count[FZX_RLD_IMAGES]=image_count;return true;
}
bool fzx_road_stream_draw_prepare(fzx_road_stream_draw_work_t *w,const fzx_road_stream_pack_t *p,
    const void *tx,uint32_t bytes,uint32_t hash){
    ++prepares;if(fault==PREPARE||tx!=(void *)0x25c01000u||bytes!=512||hash!=22||
        w->vertex_capacity!=600||w->remap_capacity!=vertex_count||w->image_capacity!=image_count||!p->data)return false;
    w->prepared=true;
    if(fault==LOAD_GUARD)*(uint32_t *)(uintptr_t)fzx_road_stream_cart_end=0;
    return true;
}
bool fzx_road_stream_draw(fzx_road_stream_draw_work_t *w,const fzx_view_t *v,const int32_t eye[3],
    const RefRoadPlan *p,int32_t pa,int32_t pb,unsigned flags,fzx_road_stream_command_t *out,
    uint32_t cap,uint32_t *n,fzx_road_stream_draw_stats_t *stats){
    (void)v;(void)p;(void)stats;++draws;draw_capacity=cap;
    if(fault==DRAW||!w->prepared||flags||eye[0]!=13||pa!=100||pb!=-99||out!=packets||!fzx_road_stream_busy)return false;
    *n=cap<3?cap:3;return true;
}
bool fzx_road_stream_commands(vdp1_cmdt_t *c,uint32_t cap,const fzx_road_stream_command_t *p,uint32_t n,
    const void *tx,uint32_t bytes,void *shades,uint32_t sbytes,uint32_t *count){
    ++adapters;adapter_capacity=cap;
    if(fault==ADAPTER||p!=packets||tx!=(void *)0x25c01000u||bytes!=512||
        shades!=(void *)0x25c20000u||sbytes!=FZX_ROAD_STREAM_SHADE_BYTES||cap!=draw_capacity)return false;
    for(uint32_t i=0;i<n;++i)c[i].kind=42;
    *count=n+(fault==EMITTED);
    if(fault==POST_GUARD)*(uint32_t *)(uintptr_t)fzx_road_stream_low_end=0;
    return true;
}
static void seed(void){
    fault=loads=opens=prepares=draws=adapters=0;scratch_bytes=12000;
    fixture_cart_id=4;fixture_cart_bytes=0x400000;vertex_count=32;image_count=5;
    fzx_native_road.data=(const uint8_t *)0x04000000u;fzx_native_road.bytes=4096;
    fzx_road_load_status.status=1;fzx_road_stream_sequence=0;
}
static bool render(unsigned capacity,uint32_t *count){
    static vdp1_cmdt_t commands[600];fzx_view_t view={0};RefRoadPlan plan={0};const int32_t eye[3]={13,0,0};
    view.clip[0]=7;view.clip[1]=9;view.clip[2]=301;view.clip[3]=219;
    bool ok=fzx_road_stream_native(commands,capacity,&view,eye,&plan,100,-99,
        (void *)0x25c01000u,(void *)0x25c20000u,count);
    if(ok&&(commands[0].kind!=8||commands[0].cmd_xa!=7||commands[0].cmd_ya!=9||
        commands[0].cmd_xc!=300||commands[0].cmd_yc!=218||commands[1].kind!=42))return false;
    return ok;
}
int main(void){
    CHECK(mmap((void *)0x00200000u,0x100000u,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)!=MAP_FAILED);
    CHECK(mmap((void *)0x04000000u,0x400000u,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)!=MAP_FAILED);
    CHECK(fzx_road_renderer_id==0x43335253u);
    seed();CHECK(fzx_road_stream_load((void *)0x25c01000u));
    CHECK(loads==2&&opens==1&&prepares==1&&fzx_road_stream_status==1&&fzx_road_stream_guards());
    CHECK(fzx_road_stream_report[0]==fzx_road_renderer_id&&fzx_road_stream_report[1]==1);
    CHECK(fzx_road_stream_report[2]==(uint32_t)(uintptr_t)work&&fzx_road_stream_report[3]==(uint32_t)(uintptr_t)packets);
    CHECK(fzx_road_stream_report[7]==0x25c01000u&&fzx_road_stream_report[8]==512);
    CHECK(fzx_road_stream_report[17]==32&&fzx_road_stream_report[19]==600&&fzx_road_stream_report[21]==600&&fzx_road_stream_report[23]==5);
    CHECK((uintptr_t)work>=0x00200000u&&(uintptr_t)work<fzx_road_stream_low_end);
    CHECK((uintptr_t)pack.data==fzx_road_stream_cart_start&&fzx_road_stream_cart_end==0x04001140u);
    uint32_t count=99;CHECK(render(600,&count)&&count==4&&draw_capacity==512&&adapter_capacity==512);
    CHECK(draws==1&&adapters==1&&!fzx_road_stream_busy&&fzx_road_stream_sequence==1);
    CHECK(fzx_road_stream_report[4]==3&&fzx_road_stream_report[5]&&fzx_road_stream_report[6]==3&&fzx_road_stream_report[14]==1);
    CHECK(render(2,&count)&&count==2&&draw_capacity==1&&fzx_road_stream_sequence==2);
    CHECK(!render(1,&count)&&!count&&draws==2);
    CHECK(!fzx_road_stream_report[4]&&!fzx_road_stream_report[5]&&!fzx_road_stream_report[6]);
    for(unsigned bad=0;bad<9;++bad){
        seed();void *pixel=(void *)0x25c01000u;
        switch(bad){case 0:fixture_cart_id=0;break;case 1:fixture_cart_bytes=4096;break;
        case 2:fzx_road_load_status.status=0;break;case 3:fzx_native_road.data++;break;
        case 4:scratch_bytes=12321;break;case 5:pixel=(void *)0x25c7fff8u;break;
        case 6:pixel=(void *)0x25c01001u;break;case 7:vertex_count=0xffffffffu;break;
        case 8:image_count=0xffffffffu;break;}
        CHECK(!fzx_road_stream_load(pixel)&&fzx_road_stream_status==2&&!fzx_road_stream_busy);
        CHECK(!render(10,&count)&&!count&&!draws);
    }
    for(unsigned f=META;f<=LOAD_GUARD;++f){
        seed();fault=f;CHECK(!fzx_road_stream_load((void *)0x25c01000u));
        CHECK(fzx_road_stream_status==(f==LOAD_GUARD?5u:3u)&&!fzx_road_stream_busy);
        CHECK(!render(10,&count)&&!count&&!draws);
    }
    for(unsigned bad=0;bad<5;++bad){
        seed();CHECK(fzx_road_stream_load((void *)0x25c01000u));
        uintptr_t address=bad==0?fzx_road_stream_low_start-32u:bad==1?fzx_road_stream_low_end:
            bad==2?fzx_road_stream_cart_start-32u:bad==3?fzx_road_stream_cart_end:(uintptr_t)(fzx_input_log+12288);
        *(uint32_t *)address=0;CHECK(!fzx_road_stream_guards());
        CHECK(!render(10,&count)&&!count&&fzx_road_stream_status==5&&!draws&&!fzx_road_stream_busy);
    }
    for(unsigned f=DRAW;f<=POST_GUARD;++f){
        seed();CHECK(fzx_road_stream_load((void *)0x25c01000u));fault=f;
        CHECK(!render(10,&count)&&!count&&!fzx_road_stream_busy&&!fzx_road_stream_sequence);
        CHECK(fzx_road_stream_status==(f==POST_GUARD?5u:4u));
        CHECK(adapters==(f==DRAW?0u:1u));
    }
    puts("ROAD_STREAM_NATIVE_HOST_PASS load admission allocation guards stream adapter capacity clip failures");return 0;
}
