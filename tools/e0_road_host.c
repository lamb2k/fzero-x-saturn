/* E0 HOST = SH-2 equality, road build slice (docs/ENGINE_PLAN.md section 5,
 * gate 3). HOST tier; private stage only: it reads the ROM-derived road packs.
 *
 * Runs the engine's road build (native layout, coarse budget guard, direct
 * VDP1 emit: src/fzx_road_stream_draw.c and the SH2ZAM portable kernels) on
 * the pick's output and the view taken from an SH-2 memory dump, and writes
 * the VDP1 commands and Gouraud words big-endian, as they sit in VDP1 VRAM.
 * tools/e0_road_eq.py extracts the input and compares the output.
 *
 * Build with the engine image's -D set (tools/e0_road_eq.py prints it),
 * SH2Z_FORCE_PORTABLE=1 instead of SH2Z_REQUIRE_SH2=1, and
 * -I generated/road_bake for road_lod_identity.h.
 *
 *   e0_road_host ROADLOD.PAK ROADLTX.PAK INPUT OUTPUT [--flip-used]
 *
 * INPUT (big-endian): "E0RI", version 1, tick, texture offset, shade offset
 * (VDP1 VRAM byte offsets: the road texture bank and the displayed shade
 * bank), pa, pb, eye[3]; then the SH-2's fzx_view_t (80 bytes) and RefRoadPlan
 * (796 bytes) exactly as dumped.
 * OUTPUT (big-endian): "E0RO", version 1, count, passes, coarse, dropped, full,
 * flipped vertex (0xffffffff: none); then the user clip command, count
 * commands (32 bytes each) and count x 4 Gouraud words.
 *
 * --flip-used (failing control): XOR bit 6 of the Y of one pack vertex that a
 * visible chunk uses and whose change alters this view's output, after the
 * pack is admitted and before the native layout is built. */
#include "fzx_road_stream_draw.h"
#include "fzx_road_stream_limits.h"
#include "road_lod_identity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {VERTICES=800,NATIVE_COPIES=256,VIEW_BYTES=80,PLAN_BYTES=796,HEAD_WORDS=10};
_Static_assert(sizeof(fzx_view_t)==VIEW_BYTES&&sizeof(RefRoadPlan)==PLAN_BYTES,"SH-2 record sizes");

static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static int16_t be16s(const uint8_t *p){return (int16_t)(uint16_t)((unsigned)p[0]<<8|p[1]);}
static void put32(FILE *f,uint32_t v){const uint8_t b[4]={(uint8_t)(v>>24),(uint8_t)(v>>16),(uint8_t)(v>>8),(uint8_t)v};fwrite(b,1,4,f);}
static void put16(FILE *f,uint16_t v){const uint8_t b[2]={(uint8_t)(v>>8),(uint8_t)v};fwrite(b,1,2,f);}
static uint8_t *read_file(const char *path,uint32_t *bytes){
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}
    const long size=ftell(f);if(size<=0||size>16000000){fclose(f);return NULL;}
    rewind(f);uint8_t *data=malloc((size_t)size);
    if(!data||fread(data,1,(size_t)size,f)!=(size_t)size){fclose(f);free(data);return NULL;}
    fclose(f);*bytes=(uint32_t)size;return data;
}
#define FAIL(...) do{fprintf(stderr,"e0_road_host: " __VA_ARGS__);fputc('\n',stderr);return 1;}while(0)

/* VDP1 command templates as saturn/road_stream_commands.c builds them with
 * Yaul (zeroed, draw mode set, then the command): CMDPMOD user clip inside
 * (2), end code and transparent pixel disabled, Gouraud; RGB1555 and CLUT16
 * distorted sprites (CMDCTRL 2) and the solid polygon (CMDCTRL 4, CMDCOLR
 * 0xc210). Yaul's bit fields are not portable, so the hardware words are
 * written here; every emitted command carries them, so the comparison
 * checks them. */
static void templates(fzx_road_stream_vdp1_t t[3]){
    memset(t,0,3*sizeof(*t));
    for(unsigned i=0;i<3;++i){
        const unsigned color_mode=i==1?1u:5u;
        t[i].pmod=(uint16_t)(2u<<9|1u<<7|1u<<6|color_mode<<3|4u);
        t[i].ctrl=i==2?4u:2u;
        if(i==2)t[i].colr=0xc210u;
    }
}

typedef struct {
    fzx_road_stream_pack_t pack;
    fzx_road_stream_draw_work_t work;
    fzx_road_stream_vdp1_t commands[FZX_ROAD_STREAM_MAX_DRAWS];
    uint16_t shades[FZX_ROAD_STREAM_SHADE_BYTES/2];
    uint32_t count;
    fzx_road_stream_budget_t info;
} run_t;

/* As saturn/road_stream_native.c: the work tables, prepare, then the guarded draw. */
/* flip_at (control only): byte of meta to XOR with 0x40 once the pack is admitted. */
static int build(run_t *r,uint8_t *meta,uint32_t meta_bytes,const uint8_t *tex,uint32_t tex_bytes,
    const fzx_view_t *view,const int32_t eye[3],const RefRoadPlan *plan,int32_t pa,int32_t pb,
    uint32_t texture_offset,uint32_t shade_offset,uint32_t flip_at){
    static uint16_t *remap;static sh2z_pos16_t *positions,*npos;static sh2z_view_vertex_t *projected;
    static fzx_road_stream_image_t *images;static fzx_road_stream_nquad_t *nquads;
    static fzx_road_stream_nspan_t *nspans;static fzx_road_stream_nchunk_t *nchunks;
    if(!fzx_road_stream_open(&r->pack,meta,meta_bytes,FZX_ROAD_LOD_FILE_HASH,FZX_ROAD_LOD_TEXTURE_BYTES))FAIL("ROADLOD.PAK refused");
    if(flip_at<meta_bytes)meta[flip_at]^=0x40u;
    const uint32_t nv=r->pack.count[FZX_RLD_VERTICES],nq=r->pack.count[FZX_RLD_QUADS],nc=r->pack.count[FZX_RLD_CHUNKS];
    if(!remap){
        remap=calloc(nv,sizeof(*remap));positions=calloc(VERTICES,sizeof(*positions));
        projected=calloc(VERTICES,sizeof(*projected));images=calloc(r->pack.count[FZX_RLD_IMAGES],sizeof(*images));
        npos=calloc(nv+NATIVE_COPIES,sizeof(*npos));nquads=calloc(nq,sizeof(*nquads));
        nspans=calloc(nq,sizeof(*nspans));nchunks=calloc(nc,sizeof(*nchunks));
        if(!remap||!positions||!projected||!images||!npos||!nquads||!nspans||!nchunks)FAIL("out of memory");
    }
    fzx_road_stream_draw_work_t *w=&r->work;memset(w,0,sizeof(*w));
    w->remap=remap;w->positions=positions;w->projected=projected;w->images=images;
    w->npos_capacity=nv+NATIVE_COPIES;w->npos=npos;w->nquads=nquads;w->nspans=nspans;w->nchunks=nchunks;
    w->remap_capacity=nv;w->vertex_capacity=VERTICES;w->image_capacity=r->pack.count[FZX_RLD_IMAGES];
    if(!fzx_road_stream_draw_prepare(w,&r->pack,tex,tex_bytes,FZX_ROAD_LOD_TEXTURE_FILE_HASH))FAIL("prepare refused");
    fzx_road_stream_emit_t emit;templates(emit.templates);
    memset(r->commands,0,sizeof(r->commands));memset(r->shades,0,sizeof(r->shades));
    emit.commands=r->commands;emit.shades=r->shades;
    emit.texture_offset=texture_offset;emit.shade_offset=shade_offset;
    /* fzx_road_stream_native: capacity is SCENE_CAPACITY - RESERVE = MAX_DRAWS + 8, held to MAX_DRAWS + 1. */
    const uint32_t capacity=FZX_ROAD_STREAM_MAX_DRAWS+1u;
    const uint32_t budget=capacity-1u<FZX_ROAD_STREAM_BUDGET_COMMANDS-1u?capacity-1u:FZX_ROAD_STREAM_BUDGET_COMMANDS-1u;
    sh2z_view_center_t center;fzx_road_stream_draw_stats_t stats;
    if(!fzx_view16_from_source_center(&center,view,eye))FAIL("centre view refused");
    if(!fzx_road_stream_draw_vdp1_guarded(w,&center,plan,pa,pb,0,&emit,budget,&r->count,&r->info,&stats))
        FAIL("draw refused: fault %u quad %u",(unsigned)stats.fault,(unsigned)stats.error_quad);
    return 0;
}
static bool same(const run_t *a,const run_t *b){
    return a->count==b->count&&!memcmp(a->commands,b->commands,a->count*sizeof(*a->commands))&&
           !memcmp(a->shades,b->shades,a->count*8u);
}

int main(int argc,char **argv){
    if(argc!=5&&!(argc==6&&!strcmp(argv[5],"--flip-used")))
        FAIL("usage: e0_road_host ROADLOD.PAK ROADLTX.PAK INPUT OUTPUT [--flip-used]");
    uint32_t meta_bytes=0,tex_bytes=0,in_bytes=0;
    uint8_t *meta=read_file(argv[1],&meta_bytes),*tex=read_file(argv[2],&tex_bytes),*in=read_file(argv[3],&in_bytes);
    if(!meta||!tex||!in)FAIL("cannot read inputs");
    if(meta_bytes!=FZX_ROAD_LOD_BYTES||tex_bytes!=FZX_ROAD_LOD_TEXTURE_BYTES)FAIL("pack sizes differ from the identity");
    if(fzx_road_hash(meta,meta_bytes)!=FZX_ROAD_LOD_FILE_HASH||fzx_road_hash(tex,tex_bytes)!=FZX_ROAD_LOD_TEXTURE_FILE_HASH)
        FAIL("pack hashes differ from the identity");
    if(in_bytes!=HEAD_WORDS*4u+VIEW_BYTES+PLAN_BYTES||memcmp(in,"E0RI",4)||be32(in+4)!=1u)FAIL("bad input record");
    const uint32_t tick=be32(in+8),texture_offset=be32(in+12),shade_offset=be32(in+16);
    const int32_t pa=(int32_t)be32(in+20),pb=(int32_t)be32(in+24);
    const int32_t eye[3]={(int32_t)be32(in+28),(int32_t)be32(in+32),(int32_t)be32(in+36)};
    /* fzx_view_t: rotation[3][3], translation[3], focal[2], center[2], near, far (int32), clip[4] (int16). */
    const uint8_t *v=in+HEAD_WORDS*4u;fzx_view_t view;
    for(unsigned i=0;i<9;++i)view.rotation[i/3][i%3]=(int32_t)be32(v+i*4);
    for(unsigned i=0;i<3;++i)view.translation[i]=(int32_t)be32(v+36+i*4);
    for(unsigned i=0;i<2;++i){view.focal[i]=(int32_t)be32(v+48+i*4);view.center[i]=(int32_t)be32(v+56+i*4);}
    view.near_plane=(int32_t)be32(v+64);view.far_plane=(int32_t)be32(v+68);
    for(unsigned i=0;i<4;++i)view.clip[i]=be16s(v+72+i*2);
    /* RefRoadPlan: faults, count, visible_chunks, outer, fog_start, fog[4], primitive[4], groups[64] of
     * {u16 start, u16 end, int direction, float average_depth}. */
    const uint8_t *p=v+VIEW_BYTES;RefRoadPlan plan;memset(&plan,0,sizeof(plan));
    plan.faults=be32(p);plan.count=be32(p+4);plan.visible_chunks=be32(p+8);plan.outer=be32(p+12);
    plan.fog_start=(int)be32(p+16);memcpy(plan.fog,p+20,4);memcpy(plan.primitive,p+24,4);
    for(unsigned g=0;g<REF_ROAD_GROUPS;++g){const uint8_t *q=p+28+g*12;uint32_t bits=be32(q+8);
        plan.groups[g].start=(unsigned short)(q[0]<<8|q[1]);plan.groups[g].end=(unsigned short)(q[2]<<8|q[3]);
        plan.groups[g].direction=(int)be32(q+4);memcpy(&plan.groups[g].average_depth,&bits,4);}
    if(plan.count>REF_ROAD_GROUPS)FAIL("plan count %u",(unsigned)plan.count);

    static run_t base,flipped;uint32_t flip=UINT32_MAX;
    if(build(&base,meta,meta_bytes,tex,tex_bytes,&view,eye,&plan,pa,pb,texture_offset,shade_offset,UINT32_MAX))return 1;
    run_t *out=&base;
    if(argc==6){
        /* Candidates: the corners of the quads of the plan's visible chunks, nearest group first. */
        fzx_road_stream_pack_t *pk=&base.pack;const uint32_t chunks=pk->count[FZX_RLD_CHUNKS];
        uint8_t *copy=malloc(meta_bytes);if(!copy)FAIL("out of memory");
        unsigned tries=0;
        for(unsigned g=plan.count;g-->0&&flip==UINT32_MAX;){
            const RefRoadGroup *gr=plan.groups+g;unsigned c=gr->start,n=0;
            do{const uint8_t *cr=fzx_road_stream_record(pk,FZX_RLD_CHUNKS,c);
                const uint32_t first=be32(cr),number=be32(cr+4);
                for(uint32_t qi=first;qi<first+number&&flip==UINT32_MAX&&tries<256;++qi){
                    const uint8_t *qr=fzx_road_stream_record(pk,FZX_RLD_QUADS,qi);
                    const uint32_t id=(uint32_t)qr[0]<<8|qr[1];
                    memcpy(copy,meta,meta_bytes);++tries;
                    /* bit 6 of the vertex's big-endian Y */
                    const uint32_t at=(uint32_t)(fzx_road_stream_record(pk,FZX_RLD_VERTICES,id)-meta)+3u;
                    if(build(&flipped,copy,meta_bytes,tex,tex_bytes,&view,eye,&plan,pa,pb,texture_offset,shade_offset,at))continue;
                    if(!same(&base,&flipped)){flip=id;fprintf(stderr,"flip: vertex %u (chunk %u, quad %u), %u tries\n",
                        (unsigned)id,c,(unsigned)qi,tries);}
                }
                c=c+1u<chunks?c+1u:0u;
            }while(c!=gr->end&&++n<chunks&&flip==UINT32_MAX);
        }
        if(flip==UINT32_MAX)FAIL("no used vertex found");
        out=&flipped;
    }
    FILE *f=fopen(argv[4],"wb");if(!f)FAIL("cannot write output");
    fwrite("E0RO",1,4,f);put32(f,1u);put32(f,out->count);put32(f,out->info.passes);put32(f,out->info.coarse);
    put32(f,out->info.dropped);put32(f,out->info.full);put32(f,flip);
    /* The user clip command fzx_road_stream_native writes ahead of the road (Yaul: CMDCTRL 8). */
    {uint16_t clip[16]={0};clip[0]=8u;clip[6]=(uint16_t)view.clip[0];clip[7]=(uint16_t)view.clip[1];
     clip[10]=(uint16_t)(view.clip[2]-1);clip[11]=(uint16_t)(view.clip[3]-1);for(unsigned i=0;i<16;++i)put16(f,clip[i]);}
    for(uint32_t i=0;i<out->count;++i){const uint16_t *c=(const uint16_t *)(const void *)&out->commands[i];
        for(unsigned k=0;k<16;++k)put16(f,c[k]);}
    for(uint32_t i=0;i<out->count*4u;++i)put16(f,out->shades[i]);
    if(fclose(f))FAIL("write failed");
    printf("tick %u commands %u passes %u coarse %u dropped %u full %u\n",(unsigned)tick,(unsigned)out->count,
        (unsigned)out->info.passes,(unsigned)out->info.coarse,(unsigned)out->info.dropped,(unsigned)out->info.full);
    return 0;
}
