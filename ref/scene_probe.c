/* Authored synthetic cameras for cross-target backdrop/skyline state checks.
 * No course, ROM, retail camera trace or asset bytes are embedded here. */
#include "global.h"
#include "fzx_camera.h"
#include "backdrop_state.h"
#include "skyline_state.h"
#include "scene_probe.h"

extern s32 gNumPlayers;
extern Camera gCameras[];
#ifdef REF_BACKDROP_ORACLE
int ref_backdrop_check_init(float,float);
int ref_backdrop_check(RefBackdrop*,unsigned int,int);
int ref_skyline_check_init(void);
int ref_skyline_check(RefSkyline*,unsigned int);
#endif

/* Hash numeric fields in canonical big-endian order, never struct padding. */
static void mix(unsigned int* h,unsigned int value,unsigned int bytes) {
    while(bytes) {*h=(*h ^ ((value >> (--bytes*8))&255u))*16777619u;}
}
static void mix_float(unsigned int* h,float value) {
    unsigned int bits;memcpy(&bits,&value,4);mix(h,bits,4);
}
static void mix_vertex(unsigned int* h,const RefBackdropVertex* v) {
    unsigned int j;
    for(j=0;j<3;++j)mix(h,(unsigned short)v->position[j],2);
    mix(h,v->flag,2);
    for(j=0;j<2;++j)mix(h,(unsigned short)v->uv[j],2);
    for(j=0;j<4;++j)mix(h,v->color[j],1);
}
static void camera(unsigned int frame) {
    Camera* c=&gCameras[0];
    const int angle=(frame*73)&4095;
    const float tilt=(frame%3==0) ? 0.6f : 0.0f;
    const float flat=(frame%3==0) ? 0.8f : 1.0f;
    const float heights[]={-700.0f,0.0f,900.0f,949.0f,950.0f,951.0f,1000.0f};
    memset(c,0,sizeof(*c));
    c->eye.x=((int)(frame%31)-15)*1600.0f;
    c->eye.z=((int)(frame%29)-14)*1600.0f;
    c->eye.y=heights[frame%7];
    c->xzNormalizedX=SIN(angle);c->xzNormalizedZ=COS(angle);
    c->basis.x.x=SIN(angle)*flat;c->basis.x.y=tilt;c->basis.x.z=COS(angle)*flat;
    c->basis.y.x=0.0f-SIN(angle)*tilt;c->basis.y.y=flat;c->basis.y.z=0.0f-COS(angle)*tilt;
    c->basis.z.x=COS(angle);c->basis.z.y=0.0f;c->basis.z.z=0.0f-SIN(angle);
    c->fov=60.0f+(frame%5)*5.0f;
    c->fovScaleX=1.0f;c->fovScaleY=0.75f;
    c->frustrumCenterX=(frame%2) ? 0.125f : -0.125f;
    c->frustrumCenterY=(frame%3) ? 0.0f : 0.0625f;
}
int ref_scene_probe_init(void) {
    gNumPlayers=1;Math_SinTableInit();camera(0);
#ifdef REF_BACKDROP_ORACLE
    if(ref_backdrop_check_init(0.0035f,-0.002f) || ref_skyline_check_init())return 1;
#else
    if(ref_backdrop_init(0.0035f,-0.002f) || ref_skyline_init())return 1;
#endif
    return 0;
}
#ifdef REF_BACKDROP_ORACLE
int ref_backdrop_catchup_oracle(RefBackdrop*,unsigned int,unsigned int,int);
void ref_backdrop_catchup_wrap_preset(void);
/* FZX_BACKDROP_CATCHUP: one catch-up of SPAN ticks with the probe camera FRAME,
 * against SPAN original Background_Update calls. Pass 1 starts at maximum wind
 * with both scrolls at the wrap points. 0 = identical. */
int ref_scene_probe_catchup(unsigned int pass,unsigned int frame,unsigned int span,int paused) {
    RefBackdrop backdrop;
    if(frame>=REF_SCENE_PROBE_CASES)return 1;
    if(frame==0) {
        camera(0);
        if(ref_backdrop_check_init(pass ? 0.005f : 0.0035f,pass ? -0.005f : -0.002f))return 2;
        if(pass)ref_backdrop_catchup_wrap_preset();
    }
    camera(frame);
    return ref_backdrop_catchup_oracle(&backdrop,span,(frame/3)&1,paused);
}
#endif
int ref_scene_probe_step(unsigned int frame,unsigned int hashes[2]) {
    RefBackdrop backdrop;
    RefSkyline skyline;
    unsigned int i,j;
    const unsigned int pool=(frame/3)&1;
    if(frame>=REF_SCENE_PROBE_CASES || !hashes)return 1;
    camera(frame);
#ifdef REF_BACKDROP_ORACLE
    if(ref_backdrop_check(&backdrop,pool,frame%11==0) || ref_skyline_check(&skyline,pool))return 2;
#else
    ref_backdrop_tick(&backdrop,pool,frame%11==0);ref_skyline_tick(&skyline,pool);
    if(backdrop.faults || skyline.faults)return 2;
#endif
    hashes[0]=hashes[1]=2166136261u;
    mix(&hashes[0],backdrop.count,4);
    for(i=0;i<24;++i)mix_float(&hashes[0],backdrop.state[i]);
    for(i=0;i<20;++i)mix_vertex(&hashes[0],&backdrop.vertices[i]);
    mix(&hashes[1],skyline.count,4);
    for(i=0;i<4;++i)mix(&hashes[1],skyline.tint[i],1);
    for(i=0;i<4;++i)mix_vertex(&hashes[1],&skyline.vertices[i]);
    for(i=0;i<REF_SKYLINE_COUNT;++i) {
        const RefSkylineInstance* v=&skyline.instances[i];
        mix(&hashes[1],v->sprite_id,4);mix(&hashes[1],v->visible,4);
        for(j=0;j<3;++j)mix_float(&hashes[1],v->position[j]);
        for(j=0;j<16;++j)mix(&hashes[1],(unsigned int)v->matrix[j],4);
    }
    return 0;
}
