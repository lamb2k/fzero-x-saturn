#include "road_stream_native.h"
#include "road_stream_commands.h"
#include "road_lod_identity.h"
#include "texture_cd.h"
#include "road_native.h"
#include <string.h>

extern unsigned char sat_stub_data_end[];
extern volatile uint32_t fzx_input_log[];
const uint32_t fzx_road_renderer_id=0x43335253u; /* C3RS: bounded stream, not tiles. */
volatile uint32_t fzx_road_stream_status,fzx_road_stream_busy;
volatile uint32_t fzx_road_stream_low_start,fzx_road_stream_low_end;
volatile uint32_t fzx_road_stream_cart_start,fzx_road_stream_cart_end;
volatile uint32_t fzx_road_stream_fields,fzx_road_stream_sequence;
fzx_road_stream_draw_stats_t fzx_road_stream_stats;
volatile uint32_t fzx_road_stream_report[24];
static fzx_road_stream_pack_t pack;
static fzx_road_stream_draw_work_t *work;
static fzx_road_stream_command_t *packets;
enum {GUARD=0x51a7c36du,VERTICES=600};

static uintptr_t aligned(uintptr_t p){return (p+31u)&~(uintptr_t)31u;}
static void *reserve(uintptr_t *at,uint32_t count,uint32_t size){
    const uintptr_t p=aligned(*at);
    if(p<0x00200000u||p>0x002fffc0u||!size||count>(0x002fffc0u-p)/size)return NULL;
    *at=p+count*size;return (void *)p;
}
bool fzx_road_stream_guards(void){
    if(!work||fzx_road_stream_low_start<0x00200020u||fzx_road_stream_low_end>0x002fffe0u)return false;
    const volatile uint32_t *a=(const volatile uint32_t *)(uintptr_t)fzx_road_stream_low_start;
    const volatile uint32_t *b=(const volatile uint32_t *)(uintptr_t)fzx_road_stream_low_end;
    const volatile uint32_t *c=(const volatile uint32_t *)(uintptr_t)fzx_road_stream_cart_start;
    const volatile uint32_t *d=(const volatile uint32_t *)(uintptr_t)fzx_road_stream_cart_end;
    if(!c||!d)return false;
    for(unsigned i=0;i<8;++i)if(a[(int)i-8]!=GUARD||b[i]!=GUARD||c[(int)i-8]!=GUARD||d[i]!=GUARD||fzx_input_log[12288u+i]!=GUARD)return false;
    return true;
}
bool fzx_road_stream_load(void *vram){
    work=NULL;packets=NULL;fzx_road_stream_status=2;fzx_road_stream_busy=0;
    for(unsigned i=0;i<24;++i)fzx_road_stream_report[i]=0;
    fzx_road_stream_report[0]=fzx_road_renderer_id;fzx_road_stream_report[1]=1;
    const uintptr_t scratch=aligned((uintptr_t)sat_stub_data_end),log=(uintptr_t)fzx_input_log;
    uintptr_t at=aligned(log+12288u*4u+63u);
    const uintptr_t pixels=(uintptr_t)vram,base=(uintptr_t)VDP1_VRAM(0);
    if(scratch<0x00200000u||log!=((scratch+FZX_TEXTURE_CD_SCRATCH_RESERVE+63u)&~(uintptr_t)63u)||
       fzx_texture_cd_scratch_bytes()>log-32u-scratch||pixels<base||pixels>base+0x80000u||
       (pixels&7u)||FZX_ROAD_LOD_TEXTURE_BYTES>base+0x80000u-pixels)return false;
    const uintptr_t cart=(uintptr_t)dram_cart_area_get(),limit=cart+dram_cart_size_get();
    const uintptr_t meta=aligned((uintptr_t)fzx_native_road.data+fzx_native_road.bytes+63u);
    const uintptr_t mend=aligned(meta+FZX_ROAD_LOD_BYTES);
    if(dram_cart_id_get()!=DRAM_CART_ID_4MIB||fzx_road_load_status.status!=1||
       (uintptr_t)fzx_native_road.data!=cart||meta<cart||mend<meta||mend>limit-32u)return false;
    void *metadata=(void *)meta;
    fzx_road_stream_cart_start=(uint32_t)meta;fzx_road_stream_cart_end=(uint32_t)mend;
    for(unsigned i=0;i<8;++i){((volatile uint32_t *)meta)[(int)i-8]=GUARD;((volatile uint32_t *)mend)[i]=GUARD;}
    if(!fzx_texture_cd_load("ROADLOD.PAK",FZX_ROAD_LOD_BYTES,FZX_ROAD_LOD_FILE_HASH,
          metadata,(void *)scratch,log-32u-scratch)||
       !fzx_road_stream_open(&pack,metadata,FZX_ROAD_LOD_BYTES,FZX_ROAD_LOD_FILE_HASH,FZX_ROAD_LOD_TEXTURE_BYTES)){
        fzx_road_stream_status=3;return false;
    }
    work=reserve(&at,1,sizeof(*work));if(!work)return false;
    memset(work,0,sizeof(*work));
    work->remap=reserve(&at,pack.count[FZX_RLD_VERTICES],sizeof(*work->remap));
    work->positions=reserve(&at,VERTICES,sizeof(*work->positions));
    work->projected=reserve(&at,VERTICES,sizeof(*work->projected));
    work->images=reserve(&at,pack.count[FZX_RLD_IMAGES],sizeof(*work->images));
    packets=reserve(&at,FZX_ROAD_STREAM_MAX_DRAWS,sizeof(*packets));
    if(!work->remap||!work->positions||!work->projected||!work->images||!packets){work=NULL;return false;}
    work->remap_capacity=pack.count[FZX_RLD_VERTICES];work->vertex_capacity=VERTICES;
    work->image_capacity=pack.count[FZX_RLD_IMAGES];
    fzx_road_stream_low_start=(uint32_t)log;fzx_road_stream_low_end=(uint32_t)aligned(at);
    for(unsigned i=0;i<8;++i){
        ((volatile uint32_t *)log)[(int)i-8]=GUARD;
        ((volatile uint32_t *)(uintptr_t)fzx_road_stream_low_end)[i]=GUARD;
        fzx_input_log[12288u+i]=GUARD;
    }
    if(!fzx_texture_cd_load("ROADLTX.PAK",FZX_ROAD_LOD_TEXTURE_BYTES,FZX_ROAD_LOD_TEXTURE_FILE_HASH,
          vram,(void *)scratch,log-32u-scratch)||
       !fzx_road_stream_draw_prepare(work,&pack,vram,FZX_ROAD_LOD_TEXTURE_BYTES,FZX_ROAD_LOD_TEXTURE_FILE_HASH)){
        fzx_road_stream_status=3;return false;
    }
    if(!fzx_road_stream_guards()){fzx_road_stream_status=5;return false;}
    fzx_road_stream_report[2]=(uint32_t)(uintptr_t)work;
    fzx_road_stream_report[3]=(uint32_t)(uintptr_t)packets;
    fzx_road_stream_report[7]=(uint32_t)pixels;fzx_road_stream_report[8]=FZX_ROAD_LOD_TEXTURE_BYTES;
    fzx_road_stream_report[11]=sizeof(*work);fzx_road_stream_report[12]=sizeof(*packets);
    fzx_road_stream_report[13]=sizeof(fzx_road_stream_stats);
    fzx_road_stream_report[16]=(uint32_t)(uintptr_t)work->remap;
    fzx_road_stream_report[17]=work->remap_capacity;
    fzx_road_stream_report[18]=(uint32_t)(uintptr_t)work->positions;fzx_road_stream_report[19]=VERTICES;
    fzx_road_stream_report[20]=(uint32_t)(uintptr_t)work->projected;fzx_road_stream_report[21]=VERTICES;
    fzx_road_stream_report[22]=(uint32_t)(uintptr_t)work->images;fzx_road_stream_report[23]=work->image_capacity;
    fzx_road_stream_status=1;return true;
}
bool fzx_road_stream_native(vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,
    const int32_t eye[3],const RefRoadPlan *plan,int32_t pa,int32_t pb,void *pixels,void *shades,uint32_t *count){
    if(count)*count=0;
    fzx_road_stream_report[4]=0;fzx_road_stream_report[5]=0;fzx_road_stream_report[6]=0;
    if(!commands||capacity<2||!view||!eye||!plan||!count||fzx_road_stream_status!=1)return false;
    if(!fzx_road_stream_guards()){fzx_road_stream_status=5;return false;}
    fzx_road_stream_busy=1;
    uint32_t n=0,emitted=0;
    if(capacity-1>FZX_ROAD_STREAM_MAX_DRAWS)capacity=FZX_ROAD_STREAM_MAX_DRAWS+1;
    if(!fzx_road_stream_draw(work,view,eye,plan,pa,pb,0,packets,capacity-1,&n,&fzx_road_stream_stats)||
       !fzx_road_stream_commands(commands+1,capacity-1,packets,n,pixels,FZX_ROAD_LOD_TEXTURE_BYTES,
          shades,FZX_ROAD_STREAM_SHADE_BYTES,&emitted)||emitted!=n){
        fzx_road_stream_status=4;fzx_road_stream_busy=0;return false;
    }
    memset(commands,0,sizeof(*commands));vdp1_cmdt_user_clip_coord_set(commands);
    commands->cmd_xa=view->clip[0];commands->cmd_ya=view->clip[1];
    commands->cmd_xc=(int16_t)(view->clip[2]-1);commands->cmd_yc=(int16_t)(view->clip[3]-1);
    if(!fzx_road_stream_guards()){fzx_road_stream_status=5;fzx_road_stream_busy=0;return false;}
    *count=n+1;++fzx_road_stream_sequence;
    fzx_road_stream_report[4]=n;fzx_road_stream_report[5]=(uint32_t)(uintptr_t)(commands+1);
    fzx_road_stream_report[6]=emitted;fzx_road_stream_report[9]=(uint32_t)(uintptr_t)shades;
    fzx_road_stream_report[10]=FZX_ROAD_STREAM_SHADE_BYTES;fzx_road_stream_report[14]=fzx_road_stream_sequence;
    fzx_road_stream_busy=0;return true;
}
