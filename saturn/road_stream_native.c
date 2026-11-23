#include "road_stream_native.h"
#include "course_select.h"
#include "road_stream_commands.h"
#include "road_lod_identity.h"
#include "texture_cd.h"
#include "road_native.h"
#include "fzx_road_stream_phase.h"
#include <string.h>
#if FZX_ROAD_STREAM_CLUSTERS
#include "road_cluster_identity.h"
/* Baked cluster bounds (ROADCL.PAK), admitted in place in low work RAM. */
static fzx_road_stream_clusters_t clusters;
#endif

extern unsigned char sat_stub_data_end[];
extern volatile uint32_t fzx_input_log[];
const uint32_t fzx_road_renderer_id=0x43335253u; /* C3RS: bounded stream, not tiles. */
volatile uint32_t fzx_road_stream_status,fzx_road_stream_busy;
volatile uint32_t fzx_road_stream_low_start,fzx_road_stream_low_end;
volatile uint32_t fzx_road_stream_cart_start,fzx_road_stream_cart_end;
volatile uint32_t fzx_road_stream_fields,fzx_road_stream_sequence;
fzx_road_stream_draw_stats_t fzx_road_stream_stats;
volatile uint32_t fzx_road_stream_report[24];
#if FZX_ROAD_STREAM_BUDGET
#if !FZX_ROAD_STREAM_EMIT
#error FZX_ROAD_STREAM_BUDGET requires FZX_ROAD_STREAM_EMIT
#endif
/* Guard counters, read by captures: frames drawn coarse, frames that dropped
 * commands, commands dropped, the last frame's and the peak full draw size. */
volatile uint32_t fzx_road_stream_budget_coarse_frames,fzx_road_stream_budget_drop_frames;
volatile uint32_t fzx_road_stream_budget_dropped,fzx_road_stream_budget_last,fzx_road_stream_budget_peak;
#endif
static fzx_road_stream_pack_t pack;
static fzx_road_stream_draw_work_t *work;
static fzx_road_stream_command_t *packets;
#if FZX_ROAD_STREAM_SPLIT
#if FZX_ROAD_STREAM_PHASES
#error FZX_ROAD_STREAM_SPLIT does not run with the phase diagnostic (its totals are the slave's alone)
#endif
#if !FZX_ROAD_LOD||!FZX_ROAD_STREAM_BUDGET||!FZX_ROAD_STREAM_EMIT||!FZX_ROAD_STREAM_NATIVE_LAYOUT
#error FZX_ROAD_STREAM_SPLIT is built for the engine's draw (native layout, direct emit, budget guard, simplified far road)
#endif
/* Linked at 0x2xxxxxxx (libyaul .uncached): both CPUs read and write it. */
static fzx_road_split_t split_rec __uncached;
static fzx_road_thief_t thief;
volatile uint32_t fzx_road_steal_report[8];
/* The thief's scratch (a part of the road arena): 288 projected vertices, 160 commands with their Gouraud words. */
enum {THIEF_VERTICES=288,THIEF_COMMANDS=160};
#endif
#ifndef FZX_ROAD_STREAM_EMIT
#define FZX_ROAD_STREAM_EMIT 0
#endif
#ifndef FZX_ROAD_STREAM_HOT_HIGH
#define FZX_ROAD_STREAM_HOT_HIGH 0
#endif
#if FZX_ROAD_STREAM_HOT_HIGH
/* Stepping/clipping scratch (3.5 KiB) in high work RAM (.bss), the rest of
 * the work in the guarded low-RAM range. */
static fzx_road_stream_hot_t hot_high;
#endif
#ifndef FZX_ROAD_STREAM_CAMERA30
#define FZX_ROAD_STREAM_CAMERA30 0
#endif
#if FZX_ROAD_STREAM_CAMERA30
/* view-camera-1: the C3 view from the source camera (main.c). */
extern RefCamera fzx_source_camera;
#endif
#ifndef FZX_ROAD_STREAM_NATIVE_LAYOUT
#define FZX_ROAD_STREAM_NATIVE_LAYOUT 0
#endif
/* The native layout projects whole visible chunk runs: HOST max 757 over the
 * selected route and 686 over the census, against 600 gathered positions. */
enum {GUARD=0x51a7c36du,VERTICES=FZX_ROAD_STREAM_NATIVE_LAYOUT?800:600,NATIVE_COPIES=256};

static uintptr_t aligned(uintptr_t p){return (p+31u)&~(uintptr_t)31u;}
static void *reserve(uintptr_t *at,uint32_t count,uint32_t size){
    const uintptr_t p=aligned(*at);
    if(p<0x00200000u||p>0x002fffc0u||!size||count>(0x002fffc0u-p)/size)return NULL;
    *at=p+count*size;return (void *)p;
}
#ifndef FZX_ROAD_STREAM_WORK_HIGH
#define FZX_ROAD_STREAM_WORK_HIGH 0
#endif
#if FZX_ROAD_STREAM_WORK_HIGH
/* FZX_ROAD_STREAM_WORK_HIGH (default 0): the work record and the tables the
 * per-frame build reads for every quad (projected vertices, native positions,
 * quad records, spans, chunks) in high work RAM (SDRAM, .bss) instead of low
 * work RAM (DRAM). Guard words bracket the arena. */
#ifndef FZX_ROAD_STREAM_HIGH_BYTES
#define FZX_ROAD_STREAM_HIGH_BYTES 98304u
#endif
enum {HIGH_BYTES=FZX_ROAD_STREAM_HIGH_BYTES};
static uint32_t road_high[(HIGH_BYTES+64u)/4u] __attribute__((aligned(32)));
volatile uint32_t fzx_road_stream_high_used;
static uintptr_t high_at;
static void *reserve_high(uint32_t count,uint32_t size){
    const uintptr_t base=(uintptr_t)road_high+32u,end=base+HIGH_BYTES,p=aligned(high_at?high_at:base);
    if(!size||p>end||count>(end-p)/size)return NULL;
    high_at=p+count*size;fzx_road_stream_high_used=(uint32_t)(high_at-base);return (void *)p;
}
static bool high_guards(void){
    for(unsigned i=0;i<8;++i)if(road_high[i]!=GUARD||road_high[(HIGH_BYTES+32u)/4u+i]!=GUARD)return false;
    return true;
}
#define RESERVE_HOT(at,count,size) reserve_high(count,size)
#else
#define RESERVE_HOT(at,count,size) reserve(at,count,size)
#define high_guards() true
#endif
bool fzx_road_stream_guards(void){
    if(!work||fzx_road_stream_low_start<0x00200020u||fzx_road_stream_low_end>0x002fffe0u||!high_guards())return false;
    const volatile uint32_t *a=(const volatile uint32_t *)(uintptr_t)fzx_road_stream_low_start;
    const volatile uint32_t *b=(const volatile uint32_t *)(uintptr_t)fzx_road_stream_low_end;
    const volatile uint32_t *c=(const volatile uint32_t *)(uintptr_t)fzx_road_stream_cart_start;
    const volatile uint32_t *d=(const volatile uint32_t *)(uintptr_t)fzx_road_stream_cart_end;
    if(!c||!d)return false;
    for(unsigned i=0;i<8;++i)if(a[(int)i-8]!=GUARD||b[i]!=GUARD||c[(int)i-8]!=GUARD||d[i]!=GUARD||fzx_input_log[12288u+i]!=GUARD)return false;
    return true;
}
#if FZX_ROAD_PREP_BAKED
/* Engine scenes image: the native layout prepared offline (tools/road_prep.py), each table
 * read straight into its work array and checked against its FNV as it arrives. */
#include "road_prep_identity.h"
volatile uint32_t fzx_road_prep_report[4]; /* [0] 1 adopted, 2+n table n refused, 9 counts, 10 alignment */
static bool prep_table(const char *what,uint32_t at,uint32_t bytes,uint32_t hash,void *dest,uintptr_t scratch,uint32_t scratch_bytes,uint32_t code){
    (void)what;
    if(((uintptr_t)dest&3u)){fzx_road_prep_report[0]=10;return false;}
    if(!fzx_texture_cd_load_range(CNAME(FZX_CF_RPREP,"ROADPREP.PAK"),CSEL(prep_bytes,FZX_ROAD_PREP_BYTES),at,bytes,hash,dest,(void *)scratch,scratch_bytes)){
        fzx_road_prep_report[0]=code;return false;}
    return true;
}
static bool road_prepare(fzx_road_stream_draw_work_t *w,void *vram,uintptr_t scratch,uint32_t scratch_bytes){
    static uint32_t nunit[(CMAX(FZX_COURSE_MAX_PREP_NUNIT_BYTES,FZX_ROAD_PREP_NUNIT_BYTES)+3u)/4u];
    if(CSEL(prep_lod_hash,FZX_ROAD_PREP_LOD_HASH)!=CSEL(lod_file_hash,FZX_ROAD_LOD_FILE_HASH)||CSEL(prep_ltx_hash,FZX_ROAD_PREP_LTX_HASH)!=CSEL(lod_tex_file_hash,FZX_ROAD_LOD_TEXTURE_FILE_HASH)||
       CSEL(prep_quads,FZX_ROAD_PREP_QUADS)!=pack.count[FZX_RLD_QUADS]||CSEL(prep_chunks,FZX_ROAD_PREP_CHUNKS)!=pack.count[FZX_RLD_CHUNKS]||
       CSEL(prep_images,FZX_ROAD_PREP_IMAGES)!=pack.count[FZX_RLD_IMAGES]||CSEL(prep_strips,FZX_ROAD_PREP_STRIPS)>256u||
       CSEL(prep[1].bytes,FZX_ROAD_PREP_NPOS_BYTES)>w->npos_capacity*sizeof(*w->npos)){fzx_road_prep_report[0]=9;return false;}
    if(!prep_table("nunit",CSEL(prep[0].at,FZX_ROAD_PREP_NUNIT_AT),CSEL(prep[0].bytes,FZX_ROAD_PREP_NUNIT_BYTES),CSEL(prep[0].hash,FZX_ROAD_PREP_NUNIT_HASH),nunit,scratch,scratch_bytes,2)||
       !prep_table("npos",CSEL(prep[1].at,FZX_ROAD_PREP_NPOS_AT),CSEL(prep[1].bytes,FZX_ROAD_PREP_NPOS_BYTES),CSEL(prep[1].hash,FZX_ROAD_PREP_NPOS_HASH),w->npos,scratch,scratch_bytes,3)||
       !prep_table("nquads",CSEL(prep[2].at,FZX_ROAD_PREP_NQUADS_AT),CSEL(prep[2].bytes,FZX_ROAD_PREP_NQUADS_BYTES),CSEL(prep[2].hash,FZX_ROAD_PREP_NQUADS_HASH),w->nquads,scratch,scratch_bytes,4)||
       !prep_table("nspans",CSEL(prep[3].at,FZX_ROAD_PREP_NSPANS_AT),CSEL(prep[3].bytes,FZX_ROAD_PREP_NSPANS_BYTES),CSEL(prep[3].hash,FZX_ROAD_PREP_NSPANS_HASH),w->nspans,scratch,scratch_bytes,5)||
       !prep_table("nchunks",CSEL(prep[4].at,FZX_ROAD_PREP_NCHUNKS_AT),CSEL(prep[4].bytes,FZX_ROAD_PREP_NCHUNKS_BYTES),CSEL(prep[4].hash,FZX_ROAD_PREP_NCHUNKS_HASH),w->nchunks,scratch,scratch_bytes,6)||
       !prep_table("images",CSEL(prep[5].at,FZX_ROAD_PREP_IMAGES_AT),CSEL(prep[5].bytes,FZX_ROAD_PREP_IMAGES_BYTES),CSEL(prep[5].hash,FZX_ROAD_PREP_IMAGES_HASH),w->images,scratch,scratch_bytes,7))
        return false;
    const uint16_t *u=(const uint16_t *)(const void *)nunit;
    for(unsigned s=0;s<CSEL(prep_strips,FZX_ROAD_PREP_STRIPS);++s)w->nunit[s]=u[s];
    if(!fzx_road_stream_draw_adopt(w,&pack,vram,CSEL(lod_tex_bytes,FZX_ROAD_LOD_TEXTURE_BYTES),CSEL(prep_npos_count,FZX_ROAD_PREP_NPOS_COUNT))){fzx_road_prep_report[0]=8;return false;}
    fzx_road_prep_report[0]=1;return true;
}
#endif
bool fzx_road_stream_load(void *vram){
    work=NULL;packets=NULL;fzx_road_stream_status=2;fzx_road_stream_busy=0;
    for(unsigned i=0;i<24;++i)fzx_road_stream_report[i]=0;
    fzx_road_stream_report[0]=fzx_road_renderer_id;fzx_road_stream_report[1]=1;
    const uintptr_t scratch=aligned((uintptr_t)sat_stub_data_end),log=(uintptr_t)fzx_input_log;
    uintptr_t at=aligned(log+12288u*4u+63u);
    const uintptr_t pixels=(uintptr_t)vram,base=(uintptr_t)VDP1_VRAM(0);
    if(scratch<0x00200000u||log!=((scratch+FZX_TEXTURE_CD_SCRATCH_RESERVE+63u)&~(uintptr_t)63u)||
       fzx_texture_cd_scratch_bytes()>log-32u-scratch||pixels<base||pixels>base+0x80000u||
       (pixels&7u)||CSEL(lod_tex_bytes,FZX_ROAD_LOD_TEXTURE_BYTES)>base+0x80000u-pixels)return false;
    const uintptr_t cart=(uintptr_t)dram_cart_area_get(),limit=cart+dram_cart_size_get();
    const uintptr_t meta=aligned((uintptr_t)fzx_native_road.data+fzx_native_road.bytes+63u);
    const uintptr_t mend=aligned(meta+CSEL(lod_bytes,FZX_ROAD_LOD_BYTES));
    if(dram_cart_id_get()!=DRAM_CART_ID_4MIB||fzx_road_load_status.status!=1||
       (uintptr_t)fzx_native_road.data!=cart||meta<cart||mend<meta||mend>limit-32u)return false;
    void *metadata=(void *)meta;
    fzx_road_stream_cart_start=(uint32_t)meta;fzx_road_stream_cart_end=(uint32_t)mend;
    for(unsigned i=0;i<8;++i){((volatile uint32_t *)meta)[(int)i-8]=GUARD;((volatile uint32_t *)mend)[i]=GUARD;}
    if(!fzx_texture_cd_load(CNAME(FZX_CF_RLOD,"ROADLOD.PAK"),CSEL(lod_bytes,FZX_ROAD_LOD_BYTES),CSEL(lod_file_hash,FZX_ROAD_LOD_FILE_HASH),
          metadata,(void *)scratch,log-32u-scratch)||
       !fzx_road_stream_open(&pack,metadata,CSEL(lod_bytes,FZX_ROAD_LOD_BYTES),CSEL(lod_file_hash,FZX_ROAD_LOD_FILE_HASH),CSEL(lod_tex_bytes,FZX_ROAD_LOD_TEXTURE_BYTES))){
        fzx_road_stream_status=3;return false;
    }
#if FZX_ROAD_STREAM_WORK_HIGH
    high_at=0;
    for(unsigned i=0;i<8;++i){road_high[i]=GUARD;road_high[(HIGH_BYTES+32u)/4u+i]=GUARD;}
#endif
    work=RESERVE_HOT(&at,1,sizeof(*work));if(!work)return false;
    memset(work,0,sizeof(*work));
#if FZX_ROAD_STREAM_HOT_HIGH
    work->hot=&hot_high;
#endif
    work->remap=reserve(&at,pack.count[FZX_RLD_VERTICES],sizeof(*work->remap));
    work->positions=reserve(&at,VERTICES,sizeof(*work->positions));
    work->projected=RESERVE_HOT(&at,VERTICES,sizeof(*work->projected));
    work->images=reserve(&at,pack.count[FZX_RLD_IMAGES],sizeof(*work->images));
    packets=reserve(&at,FZX_ROAD_STREAM_MAX_DRAWS,sizeof(*packets));
#if FZX_ROAD_STREAM_NATIVE_LAYOUT
    /* Native layout: renumbered positions (wrap copies included), 8-byte quad
     * records and texture spans, and chunk runs; remap is its boot scratch. */
    work->npos_capacity=pack.count[FZX_RLD_VERTICES]+NATIVE_COPIES;
    work->npos=RESERVE_HOT(&at,work->npos_capacity,sizeof(*work->npos));
    work->nquads=RESERVE_HOT(&at,pack.count[FZX_RLD_QUADS],sizeof(*work->nquads));
    work->nspans=RESERVE_HOT(&at,pack.count[FZX_RLD_QUADS],sizeof(*work->nspans));
    work->nchunks=RESERVE_HOT(&at,pack.count[FZX_RLD_CHUNKS],sizeof(*work->nchunks));
    if(!work->npos||!work->nquads||!work->nspans||!work->nchunks){work=NULL;return false;}
#endif
    if(!work->remap||!work->positions||!work->projected||!work->images||!packets){work=NULL;return false;}
#if FZX_ROAD_STREAM_SPLIT
    /* The thief's scratch from the same arena; any missing piece leaves it disabled (no steal). */
    memset(&thief,0,sizeof(thief));thief.seen_gen=0xffffffffu;
    {   fzx_road_stream_hot_t *h=RESERVE_HOT(&at,1,sizeof(*h));
        sh2z_view_vertex_t *pj=RESERVE_HOT(&at,THIEF_VERTICES,sizeof(*pj));
        uint8_t *cm=RESERVE_HOT(&at,THIEF_COMMANDS,32u);
        uint16_t *sh=RESERVE_HOT(&at,THIEF_COMMANDS,8u);
        if(h&&pj&&cm&&sh){thief.hot=h;thief.proj=pj;thief.vcap=THIEF_VERTICES;thief.cmds=cm;thief.shades=sh;thief.ccap=THIEF_COMMANDS;}
    }
    work->split=NULL;split_rec.tag=0;split_rec.ready=0;split_rec.gen=0;split_rec.abort=0;split_rec.front=0;
#endif
#if FZX_ROAD_STREAM_CLUSTERS
    {   void *cl=reserve(&at,FZX_ROAD_CLUSTER_BYTES,1);
        if(!cl||!fzx_texture_cd_load("ROADCL.PAK",FZX_ROAD_CLUSTER_BYTES,FZX_ROAD_CLUSTER_FILE_HASH,cl,(void *)scratch,log-32u-scratch)||
           !fzx_road_stream_clusters_open(&clusters,cl,FZX_ROAD_CLUSTER_BYTES,FZX_ROAD_CLUSTER_FILE_HASH,&pack,CSEL(lod_file_hash,FZX_ROAD_LOD_FILE_HASH))){
            fzx_road_stream_status=6;work=NULL;return false;}
        work->clusters=&clusters;
    }
#endif
    work->remap_capacity=pack.count[FZX_RLD_VERTICES];work->vertex_capacity=VERTICES;
    work->image_capacity=pack.count[FZX_RLD_IMAGES];
    fzx_road_stream_low_start=(uint32_t)log;fzx_road_stream_low_end=(uint32_t)aligned(at);
    for(unsigned i=0;i<8;++i){
        ((volatile uint32_t *)log)[(int)i-8]=GUARD;
        ((volatile uint32_t *)(uintptr_t)fzx_road_stream_low_end)[i]=GUARD;
        fzx_input_log[12288u+i]=GUARD;
    }
    if(!fzx_texture_cd_load(CNAME(FZX_CF_RLTX,"ROADLTX.PAK"),CSEL(lod_tex_bytes,FZX_ROAD_LOD_TEXTURE_BYTES),CSEL(lod_tex_file_hash,FZX_ROAD_LOD_TEXTURE_FILE_HASH),
          vram,(void *)scratch,log-32u-scratch)||
#if FZX_ROAD_PREP_BAKED
       !road_prepare(work,vram,scratch,log-32u-scratch)){
#else
       !fzx_road_stream_draw_prepare(work,&pack,vram,CSEL(lod_tex_bytes,FZX_ROAD_LOD_TEXTURE_BYTES),CSEL(lod_tex_file_hash,FZX_ROAD_LOD_TEXTURE_FILE_HASH))){
#endif
        fzx_road_stream_status=3;return false;
    }
    if(!fzx_road_stream_guards()){fzx_road_stream_status=5;return false;}
    fzx_road_stream_report[2]=(uint32_t)(uintptr_t)work;
    fzx_road_stream_report[3]=(uint32_t)(uintptr_t)packets;
    fzx_road_stream_report[7]=(uint32_t)pixels;fzx_road_stream_report[8]=CSEL(lod_tex_bytes,FZX_ROAD_LOD_TEXTURE_BYTES);
    fzx_road_stream_report[11]=sizeof(*work);fzx_road_stream_report[12]=sizeof(*packets);
    fzx_road_stream_report[13]=sizeof(fzx_road_stream_stats);
    fzx_road_stream_report[16]=(uint32_t)(uintptr_t)work->remap;
    fzx_road_stream_report[17]=work->remap_capacity;
    fzx_road_stream_report[18]=(uint32_t)(uintptr_t)work->positions;fzx_road_stream_report[19]=VERTICES;
    fzx_road_stream_report[20]=(uint32_t)(uintptr_t)work->projected;fzx_road_stream_report[21]=VERTICES;
    fzx_road_stream_report[22]=(uint32_t)(uintptr_t)work->images;fzx_road_stream_report[23]=work->image_capacity;
    fzx_road_stream_status=1;return true;
}
#if FZX_ROAD_STREAM_CLUT_ALIGN
volatile uint32_t fzx_road_stream_clut_report[3];
static uintptr_t clut_area;
/* Detector control only: rewrite the copied tables (same values) while VDP1 may be reading them. */
void fzx_road_stream_clut_touch(void){
    volatile uint16_t *d=(volatile uint16_t *)clut_area;
    if(!d)return;
    for(uint32_t i=0;i<fzx_road_stream_clut_report[0]*16u;++i)d[i]=d[i];
}
/* While the tables move, a moved material keeps its old offset in the low 20 bits and its slot + 1
 * above them (no work-RAM list; the offsets are below the 512 KB of VDP1 VRAM). */
#define CLUT_SLOT_SHIFT 20u
#define CLUT_SLOTS 256u
bool fzx_road_stream_clut_align(void *area,uint32_t area_bytes,uint32_t *used){
    const uintptr_t pixels=(uintptr_t)fzx_road_stream_report[7],dest=(uintptr_t)area;
    const uintptr_t base=(uintptr_t)VDP1_VRAM(0),end=base+0x80000u;
    fzx_road_stream_clut_report[0]=0;fzx_road_stream_clut_report[1]=0;fzx_road_stream_clut_report[2]=2;
    /* dest inside VDP1 VRAM before the capacity subtraction, so end-dest cannot wrap (C-127 finding 4). */
    if(!used||fzx_road_stream_status!=1||!work||!work->images||(dest&31u)||dest<base||dest>=end||
       area_bytes>end-dest||dest<pixels+CSEL(lod_tex_bytes,FZX_ROAD_LOD_TEXTURE_BYTES))return false;
    *used=0;
    const uint32_t low=(1u<<CLUT_SLOT_SHIFT)-1u,count=work->image_capacity*3u;
    uint32_t n=0,lookups=0;
    for(uint32_t q=0;q<count;++q){
        fzx_road_stream_material_t *m=&work->images[q/3u].variant[q%3u];
        if(!m->mode)continue;
        ++lookups;
        if(!((pixels+m->palette)&31u))continue;
        uint32_t k=n;
        for(uint32_t p=0;p<q&&k==n;++p){
            const fzx_road_stream_material_t *o=&work->images[p/3u].variant[p%3u];
            if(o->mode&&(o->palette>>CLUT_SLOT_SHIFT)&&(o->palette&low)==m->palette)k=(o->palette>>CLUT_SLOT_SHIFT)-1u;
        }
        if(k==n){
            if(n==CLUT_SLOTS||(n+1u)*32u>area_bytes||m->palette>low){
                for(uint32_t p=0;p<q;++p){fzx_road_stream_material_t *o=&work->images[p/3u].variant[p%3u];
                    if(o->mode)o->palette&=low;} /* the old offsets back, then refuse */
                fzx_road_stream_clut_report[2]=3;return false;
            }
            const volatile uint16_t *s=(const volatile uint16_t *)(pixels+m->palette);
            volatile uint16_t *d=(volatile uint16_t *)(dest+n*32u);
            for(unsigned j=0;j<16u;++j)d[j]=s[j];
            ++n;
        }
        m->palette|=(k+1u)<<CLUT_SLOT_SHIFT;
    }
    for(uint32_t q=0;q<count;++q){
        fzx_road_stream_material_t *m=&work->images[q/3u].variant[q%3u];
        if(m->mode&&(m->palette>>CLUT_SLOT_SHIFT))m->palette=(uint32_t)(dest+((m->palette>>CLUT_SLOT_SHIFT)-1u)*32u-pixels);
    }
    *used=n*32u;clut_area=dest;
    fzx_road_stream_clut_report[0]=n;fzx_road_stream_clut_report[1]=lookups;fzx_road_stream_clut_report[2]=1;
    return true;
}
#endif
#ifndef FZX_ROAD_STREAM_LINE_START
#define FZX_ROAD_STREAM_LINE_START 0
#endif
#if FZX_ROAD_STREAM_LINE_START
/* FZX_ROAD_STREAM_LINE_START: VDP1 draws a distorted sprite or polygon as lines from its
 * A-D edge to its B-C edge and ends a line once it leaves the clip, but it walks every
 * pixel before a line enters it. A road command whose B-C corners lie nearer the view
 * than its A-D corners is mirrored after the build: corners B,A,D,C, the horizontal flip
 * toggled and the Gouraud words swapped the same way, so the same texels and shades land
 * on the same screen corners and the lines start on the visible side. */
volatile uint32_t fzx_road_stream_line_flips;
static int32_t line_out(int32_t x,int32_t y,const int16_t clip[4]){
    int32_t o=0;
    if(x<clip[0])o+=clip[0]-x;else if(x>=clip[2])o+=x-clip[2]+1;
    if(y<clip[1])o+=clip[1]-y;else if(y>=clip[3])o+=y-clip[3]+1;
    return o;
}
static void line_start(vdp1_cmdt_t *commands,uint32_t n,const int16_t clip[4]){
    for(uint32_t i=0;i<n;++i){
        fzx_road_stream_vdp1_t *c=(fzx_road_stream_vdp1_t *)(void *)(commands+i);
        const unsigned kind=c->ctrl&0xfu;
        if(kind!=2u&&kind!=3u&&kind!=4u)continue;
        if(line_out(c->xb,c->yb,clip)+line_out(c->xc,c->yc,clip)>=line_out(c->xa,c->ya,clip)+line_out(c->xd,c->yd,clip))continue;
        int16_t t;
        t=c->xa;c->xa=c->xb;c->xb=t;t=c->ya;c->ya=c->yb;c->yb=t;
        t=c->xd;c->xd=c->xc;c->xc=t;t=c->yd;c->yd=c->yc;c->yc=t;
        if(kind!=4u)c->ctrl^=0x0010u;
        if(c->pmod&4u){
            /* Two 32-bit reads; a word pair is rewritten only when its halves differ (VDP1 VRAM is slow). */
            volatile uint32_t *g=(volatile uint32_t *)((uintptr_t)VDP1_VRAM(0)+((uintptr_t)c->grda<<3));
            const uint32_t ab=g[0],cd=g[1];
            if((ab>>16)!=(ab&0xffffu))g[0]=(ab<<16)|(ab>>16);
            if((cd>>16)!=(cd&0xffffu))g[1]=(cd<<16)|(cd>>16);
        }
        ++fzx_road_stream_line_flips;
    }
}
#endif
static bool native_run(bool split,vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,
    const int32_t eye[3],const RefRoadPlan *plan,int32_t pa,int32_t pb,void *pixels,void *shades,uint32_t *count){
    if(count)*count=0;
#if FZX_ROAD_STREAM_SPLIT
    if(work)work->split=split&&split_rec.tag?&split_rec:NULL;
#else
    (void)split;
#endif
    FZX_RSP(REPORT);
    fzx_road_stream_report[4]=0;fzx_road_stream_report[5]=0;fzx_road_stream_report[6]=0;
    if(!commands||capacity<2||!view||!eye||!plan||!count||fzx_road_stream_status!=1)return false;
    if(!fzx_road_stream_guards()){fzx_road_stream_status=5;return false;}
    fzx_road_stream_busy=1;
    uint32_t n=0,emitted=0;
    if(capacity-1>FZX_ROAD_STREAM_MAX_DRAWS)capacity=FZX_ROAD_STREAM_MAX_DRAWS+1;
#if FZX_ROAD_STREAM_EMIT && FZX_ROAD_STREAM_BUDGET
    /* Road command budget guard: never more than FZX_ROAD_STREAM_BUDGET_COMMANDS
     * road commands, the user clip included. Frames within it are the
     * default draw, unchanged; see fzx_road_stream_draw_vdp1_guarded. */
    fzx_road_stream_emit_t emit;sh2z_view_center_t center;fzx_road_stream_budget_t info;
    const uint32_t budget=capacity-1<FZX_ROAD_STREAM_BUDGET_COMMANDS-1u?capacity-1:FZX_ROAD_STREAM_BUDGET_COMMANDS-1u;
    FZX_RSP(EYE);
    if(!fzx_road_stream_emit_setup(&emit,commands+1,capacity-1,pixels,CSEL(lod_tex_bytes,FZX_ROAD_LOD_TEXTURE_BYTES),
          shades,FZX_ROAD_STREAM_SHADE_BYTES)||
#if FZX_ROAD_STREAM_CAMERA30
       !fzx_road_stream_camera_view(&center,&fzx_source_camera,view)||
#else
       !fzx_view16_from_source_center(&center,view,eye)||
#endif
       !fzx_road_stream_draw_vdp1_guarded(work,&center,plan,pa,pb,0,&emit,budget,&n,&info,&fzx_road_stream_stats)){
        fzx_road_stream_status=4;fzx_road_stream_busy=0;return false;
    }
    emitted=n;
    fzx_road_stream_budget_last=info.full;
    if(info.coarse)++fzx_road_stream_budget_coarse_frames;
    if(info.dropped){++fzx_road_stream_budget_drop_frames;fzx_road_stream_budget_dropped+=info.dropped;}
    if(info.full>fzx_road_stream_budget_peak)fzx_road_stream_budget_peak=info.full;
#elif FZX_ROAD_STREAM_EMIT
    /* Direct emit: final VDP1 commands and Gouraud words, no packets. */
    fzx_road_stream_emit_t emit;
#if FZX_ROAD_STREAM_CAMERA30
    sh2z_view_center_t center;
    FZX_RSP(EYE);
#endif
    if(!fzx_road_stream_emit_setup(&emit,commands+1,capacity-1,pixels,CSEL(lod_tex_bytes,FZX_ROAD_LOD_TEXTURE_BYTES),
          shades,FZX_ROAD_STREAM_SHADE_BYTES)||
#if FZX_ROAD_STREAM_CAMERA30
       !fzx_road_stream_camera_view(&center,&fzx_source_camera,view)||
       !fzx_road_stream_draw_vdp1_center(work,&center,plan,pa,pb,0,&emit,capacity-1,&n,&fzx_road_stream_stats)){
#else
       !fzx_road_stream_draw_vdp1(work,view,eye,plan,pa,pb,0,&emit,capacity-1,&n,&fzx_road_stream_stats)){
#endif
        fzx_road_stream_status=4;fzx_road_stream_busy=0;return false;
    }
    emitted=n;
#else
    if(!fzx_road_stream_draw(work,view,eye,plan,pa,pb,0,packets,capacity-1,&n,&fzx_road_stream_stats)||
       !fzx_road_stream_commands(commands+1,capacity-1,packets,n,pixels,CSEL(lod_tex_bytes,FZX_ROAD_LOD_TEXTURE_BYTES),
          shades,FZX_ROAD_STREAM_SHADE_BYTES,&emitted)||emitted!=n){
        fzx_road_stream_status=4;fzx_road_stream_busy=0;return false;
    }
#endif
#if FZX_ROAD_STREAM_LINE_START
    line_start(commands+1,n,view->clip);
#endif
    FZX_RSP(REPORT);
    memset(commands,0,sizeof(*commands));vdp1_cmdt_user_clip_coord_set(commands);
    commands->cmd_xa=view->clip[0];commands->cmd_ya=view->clip[1];
    commands->cmd_xc=(int16_t)(view->clip[2]-1);commands->cmd_yc=(int16_t)(view->clip[3]-1);
    if(!fzx_road_stream_guards()){fzx_road_stream_status=5;fzx_road_stream_busy=0;return false;}
    *count=n+1;++fzx_road_stream_sequence;
    fzx_road_stream_report[4]=n;fzx_road_stream_report[5]=(uint32_t)(uintptr_t)(commands+1);
    fzx_road_stream_report[6]=emitted;fzx_road_stream_report[9]=(uint32_t)(uintptr_t)shades;
    fzx_road_stream_report[10]=FZX_ROAD_STREAM_SHADE_BYTES;fzx_road_stream_report[14]=fzx_road_stream_sequence;
#if FZX_SCENE_PHASES && FZX_ROAD_STREAM_PHASES
    {   /* Diagnostic: close this frame's phases and add them to the race totals. */
        extern volatile uint32_t fzx_scp_road[FZX_RSP_PHASES],fzx_scp_road_calls;
        FZX_RSP(OTHER);
        for(unsigned i=0;i<FZX_RSP_PHASES;++i)fzx_scp_road[i]+=fzx_rsp.ticks[i];
        ++fzx_scp_road_calls;
    }
#endif
    fzx_road_stream_busy=0;return true;
}
bool fzx_road_stream_native(vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,
    const int32_t eye[3],const RefRoadPlan *plan,int32_t pa,int32_t pb,void *pixels,void *shades,uint32_t *count){
    return native_run(true,commands,capacity,view,eye,plan,pa,pb,pixels,shades,count);
}
#if FZX_ROAD_STREAM_SPLIT
/* The same build on the calling CPU with no claims: the slave must be idle (the frame is redone whole). The caller's
 * cache may hold older copies of the work's tables: purged first. */
bool fzx_road_stream_native_whole(vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,
    const int32_t eye[3],const RefRoadPlan *plan,int32_t pa,int32_t pb,void *pixels,void *shades,uint32_t *count){
    if(work)cpu_cache_area_purge(work,sizeof(*work));
    return native_run(false,commands,capacity,view,eye,plan,pa,pb,pixels,shades,count);
}
void fzx_road_stream_split_arm(uint32_t tag){
    split_rec.abort=0;split_rec.tag=tag;
}
void fzx_road_stream_steal_run(const fzx_view_t *view,const int32_t eye[3],const RefRoadPlan *plan,void *pixels,void *shades,
    vdp1_cmdt_t *commands,unsigned capacity,bool (*slave_done)(void)){
    const uint32_t tag=split_rec.tag;
    thief.first=REF_ROAD_GROUPS;thief.ngroups=0;thief.commands=0;thief.chunks=0;thief.fault=0;
    if(!work||!thief.hot||!tag)return;
    ++fzx_road_steal_report[0];
    const uint16_t t0=cpu_frt_count_get();
    /* The slave's setup (plan, fog, ramps) is published at split_rec.ready; a slave that ends first has nothing left to claim. */
    while(split_rec.ready!=tag){if(slave_done()||(uint16_t)(cpu_frt_count_get()-t0)>50000u)return;} /* bounded: the finish has its own timeout */
    /* Stale lines: the plan (written by the slave's pick), the colour key, and the tables when it rebuilt them. */
    cpu_cache_area_purge((void *)plan,sizeof(*plan));
    if(thief.seen_gen!=split_rec.gen){
        thief.seen_gen=split_rec.gen;
        cpu_cache_area_purge(&work->fog,(size_t)((uintptr_t)(&work->mix_valid+1)-(uintptr_t)&work->fog));
    } else cpu_cache_area_purge(work->ramp_key,sizeof(work->ramp_key));
    if(capacity-1>FZX_ROAD_STREAM_MAX_DRAWS)capacity=FZX_ROAD_STREAM_MAX_DRAWS+1; /* as the slave's build does */
    sh2z_view_center_t center;fzx_road_stream_emit_t emit;
    if(!fzx_view16_from_source_center(&center,view,eye)||
       !fzx_road_stream_emit_setup(&emit,commands+1,capacity-1,pixels,CSEL(lod_tex_bytes,FZX_ROAD_LOD_TEXTURE_BYTES),
          shades,FZX_ROAD_STREAM_SHADE_BYTES)){thief.fault=1;++fzx_road_steal_report[4];return;}
    thief.shade_offset=emit.shade_offset;
    emit.commands=(fzx_road_stream_vdp1_t *)(void *)thief.cmds;emit.shades=thief.shades;
    if(!fzx_road_stream_steal(work,&center,plan,&emit,FZX_RS_COARSE,tag,&thief)){++fzx_road_steal_report[4];return;}
    if(thief.ngroups){++fzx_road_steal_report[1];fzx_road_steal_report[2]+=thief.ngroups;fzx_road_steal_report[3]+=thief.commands;}
    fzx_road_steal_report[5]+=(uint16_t)(cpu_frt_count_get()-t0);
}
bool fzx_road_stream_steal_merge_job(const RefRoadPlan *plan,vdp1_cmdt_t *commands,void *shades,uint32_t *count){
    const uint16_t t0=cpu_frt_count_get();
    if(split_rec.abort){++fzx_road_steal_report[7];return true;} /* the slave drew the whole frame again: the thief's groups are dropped */
    if(!count||!*count)return false;
    /* The slave's stats are purged by its finish; the plan by the steal. */
    uint32_t added=0;
    const bool ok=fzx_road_stream_steal_merge(&thief,plan,fzx_road_stream_stats.chunks,*count-1u,
        FZX_ROAD_STREAM_BUDGET_COMMANDS-1u,(fzx_road_stream_vdp1_t *)(void *)(commands+1),(uint16_t *)shades,thief.shade_offset,&added);
    if(ok)*count+=added;else ++fzx_road_steal_report[4];
    fzx_road_steal_report[6]+=(uint16_t)(cpu_frt_count_get()-t0);
    return ok;
}
#endif
