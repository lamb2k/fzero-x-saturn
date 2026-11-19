/* E0 HOST = SH-2 equality, pick slice (docs/ENGINE_PLAN.md section 5, gate 3).
 * HOST tier; private stage only: it reads the ROM-derived camera path.
 *
 * Runs the engine's track pick and stable chunk cap
 * (saturn/engine/engine_pick_body.h, the code engine_main.c includes) over a
 * sequence of slave jobs, each at one logic tick, and writes the plan and
 * every piece of pick state big-endian, in the SH-2's layout.
 * tools/e0_pick_eq.py takes the job sequence or the starting state from an
 * SH-2 dump and compares.
 *
 * Build with the engine image's -D set (tools/e0_pick_eq.py build) and
 * -I generated/engine_path.
 *
 *   e0_pick_host ENGPATH.PAK INPUT OUTPUT [--flip-cam]
 *
 * INPUT (big-endian): "E0PI", version 1, has_state, jobs, then jobs ticks;
 * with has_state, the state the first job starts from, in the OUTPUT layout
 * after its header (cold replay: none, all zero as at boot).
 * OUTPUT (big-endian): "E0PO", version 1, jobs, last pick result; then
 * fzx_source_road_plan (796), cap_last[512], cap_frame, cap_kept[512],
 * cap_wait[512], cap_list[128], cap_depth[128], pick_runs[64] (12 bytes each),
 * pick_depth[513], pick_state[513], fzx_e0_pick[0..2], fzx_e0_cap_dropped.
 *
 * --flip-cam (failing control): XOR bit 16 of eye[2] of the last job's camera. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine_path.h"
#include "engine_path_identity.h"
#include "road_plan.h"

/* What engine_pick_body.h uses, as engine_main.c provides it. */
static fzx_engine_path_t path;
RefRoadPlan fzx_source_road_plan;
volatile uint32_t fzx_e0_report[16];
#if E0_CHUNK_CAP
volatile uint32_t fzx_e0_cap_runs;
#endif
static uint16_t cpu_frt_count_get(void){return 0;}
#include "engine_pick_body.h"
#if !E0_CAP_STABLE || E0_POP_LOG || FZX_ROAD_STREAM_PHASES
#error "build with the E0 image default -D set"
#endif

#define FAIL(...) do{fprintf(stderr,"e0_pick_host: " __VA_ARGS__);fputc('\n',stderr);return 1;}while(0)
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint16_t be16(const uint8_t *p){return (uint16_t)((unsigned)p[0]<<8|p[1]);}

/* The state in OUTPUT order: (name, element bytes, count, address). */
typedef struct {const char *name;unsigned size,count;void *at;} field_t;
static field_t fields[]={
    {"plan",0,1,&fzx_source_road_plan},
    {"cap_last",4,FZX_EPATH_MAX_CHUNKS,cap_last},{"cap_frame",4,1,&cap_frame},
    {"cap_kept",1,FZX_EPATH_MAX_CHUNKS,cap_kept},{"cap_wait",1,FZX_EPATH_MAX_CHUNKS,cap_wait},
    {"cap_list",2,CAP_LIST,cap_list},{"cap_depth",4,CAP_LIST,cap_depth},
    {"pick_runs",0,REF_ROAD_GROUPS,pick_runs},
    {"pick_depth",4,FZX_EPATH_MAX_CHUNKS+1u,pick_depth},{"pick_state",1,FZX_EPATH_MAX_CHUNKS+1u,pick_state},
    {"fzx_e0_pick",4,3,(void *)fzx_e0_pick},{"fzx_e0_cap_dropped",4,1,(void *)&fzx_e0_cap_dropped},
};
/* Scalars and arrays of one width; the plan and the runs field by field. */
static void put(uint8_t **o,uint32_t v,unsigned n){for(unsigned i=0;i<n;++i)*(*o)++=(uint8_t)(v>>(8*(n-1-i)));}
static uint32_t get(const uint8_t **in,unsigned n){uint32_t v=0;for(unsigned i=0;i<n;++i)v=v<<8|*(*in)++;return v;}
static void scalar(uint8_t **o,const uint8_t **in,void *at,unsigned n){
    if(o){uint32_t v=0;
        if(n==4)memcpy(&v,at,4);else if(n==2){uint16_t h;memcpy(&h,at,2);v=h;}else v=*(uint8_t *)at;
        put(o,v,n);return;}
    const uint32_t v=get(in,n);
    if(n==4)memcpy(at,&v,4);else if(n==2){const uint16_t h=(uint16_t)v;memcpy(at,&h,2);}else *(uint8_t *)at=(uint8_t)v;
}
/* o != NULL: serialise; else load from *in. */
static void state(uint8_t **o,const uint8_t **in){
    for(unsigned f=0;f<sizeof(fields)/sizeof(*fields);++f){
        if(!strcmp(fields[f].name,"plan")){RefRoadPlan *p=&fzx_source_road_plan;
            scalar(o,in,&p->faults,4);scalar(o,in,&p->count,4);scalar(o,in,&p->visible_chunks,4);
            scalar(o,in,&p->outer,4);scalar(o,in,&p->fog_start,4);
            for(unsigned i=0;i<4;++i)scalar(o,in,&p->fog[i],1);
            for(unsigned i=0;i<4;++i)scalar(o,in,&p->primitive[i],1);
            for(unsigned g=0;g<REF_ROAD_GROUPS;++g){RefRoadGroup *x=&p->groups[g];
                scalar(o,in,&x->start,2);scalar(o,in,&x->end,2);scalar(o,in,&x->direction,4);scalar(o,in,&x->average_depth,4);}
        }else if(!strcmp(fields[f].name,"pick_runs")){
            for(unsigned r=0;r<REF_ROAD_GROUPS;++r){sh2z_segmesh_run_t *x=&pick_runs[r];
                scalar(o,in,&x->start,2);scalar(o,in,&x->end,2);scalar(o,in,&x->direction,2);
                scalar(o,in,&x->reserved,2);scalar(o,in,&x->average,4);}
        }else for(unsigned i=0;i<fields[f].count;++i)scalar(o,in,(uint8_t *)fields[f].at+i*fields[f].size,fields[f].size);
    }
}
_Static_assert(sizeof(RefRoadPlan)==796&&sizeof(sh2z_segmesh_run_t)==12&&sizeof(float)==4,"SH-2 layouts");

static uint8_t *read_file(const char *name,uint32_t *bytes){
    FILE *f=fopen(name,"rb");if(!f)return NULL;
    if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}
    const long n=ftell(f);if(n<=0||n>64000000){fclose(f);return NULL;}
    rewind(f);uint8_t *d=malloc((size_t)n);
    if(!d||fread(d,1,(size_t)n,f)!=(size_t)n){fclose(f);free(d);return NULL;}
    fclose(f);*bytes=(uint32_t)n;return d;
}
int main(int argc,char **argv){
    if(argc!=4&&!(argc==5&&!strcmp(argv[4],"--flip-cam")))FAIL("usage: e0_pick_host ENGPATH.PAK INPUT OUTPUT [--flip-cam]");
    uint32_t pack_bytes=0,in_bytes=0;
    uint8_t *pack=read_file(argv[1],&pack_bytes),*in=read_file(argv[2],&in_bytes);
    if(!pack||!in)FAIL("cannot read inputs");
    if(pack_bytes!=FZX_ENGINE_PATH_BYTES||!fzx_engine_path_open(&path,pack,pack_bytes,FZX_ENGINE_PATH_HASH))
        FAIL("ENGPATH.PAK refused");
    /* The pack is big-endian (the SH-2 reads it in place): cameras and anchors to host order. */
    {   uint8_t *c=pack+((const uint8_t *)path.cams-pack),*a=pack+((const uint8_t *)path.anchors-pack);
        for(uint32_t i=0;i<path.ticks*(uint32_t)sizeof(fzx_engine_cam_t)/4u;++i){const uint32_t v=be32(c+i*4);memcpy(c+i*4,&v,4);}
        for(uint32_t i=0;i<path.chunks*3u;++i){const uint16_t v=be16(a+i*2);memcpy(a+i*2,&v,2);}
    }
    if(in_bytes<16||memcmp(in,"E0PI",4)||be32(in+4)!=1u)FAIL("bad input record");
    const uint32_t has_state=be32(in+8),jobs=be32(in+12);
    if(!jobs||jobs>100000u||in_bytes<16u+jobs*4u)FAIL("bad job count");
    const uint8_t *ticks=in+16,*st=ticks+jobs*4u;
    if(has_state)state(NULL,&st);
    if(st!=in+in_bytes)FAIL("input size mismatch");
    int last=-1;
    for(uint32_t j=0;j<jobs;++j){
        const uint32_t t=be32(ticks+j*4);
        if(!t||t>path.ticks)FAIL("tick %u outside the path",(unsigned)t);
        if(argc==5&&j+1==jobs)((fzx_engine_cam_t *)(uintptr_t)&path.cams[t-1u])->eye[2]^=1<<16;
        /* kick(): job_cam and job_tick, then the slave's pick. */
        job_cam=&path.cams[t-1u];job_tick=t;
        last=fzx_engine_slave_pick()?1:0;
    }
    static uint8_t out[16384];uint8_t *o=out;
    memcpy(o,"E0PO",4);o+=4;put(&o,1u,4);put(&o,jobs,4);put(&o,(uint32_t)last,4);
    state(&o,NULL);
    FILE *f=fopen(argv[3],"wb");
    if(!f||fwrite(out,1,(size_t)(o-out),f)!=(size_t)(o-out)||fclose(f))FAIL("write failed");
    printf("jobs %u last %d groups %u visible %u frame %u\n",(unsigned)jobs,last,(unsigned)fzx_source_road_plan.count,
        (unsigned)fzx_source_road_plan.visible_chunks,(unsigned)cap_frame);
    return 0;
}
