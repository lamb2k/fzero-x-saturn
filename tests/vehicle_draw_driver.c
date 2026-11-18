#include "fzx_vehicle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static fzx_vehicle_work_t work;
static unsigned emits;
static bool allow_solid;
static unsigned solid_emits;
static uint32_t solid_color_hash;
/* FNV-1a over every emitted quad's fields (arm-identity witness across all draws). */
static uint32_t quad_hash=2166136261u;
static void mix(uint32_t value){for(unsigned i=0;i<4;++i)quad_hash=(quad_hash^((value>>(8*i))&255u))*16777619u;}
static bool emit(void *ctx,const fzx_vehicle_quad_t *q){
    (void)ctx;
    if((q->solid&&!allow_solid)||q->width%8||q->height%2||q->palette>=64)return false;
    for(unsigned i=0;i<4;++i)if(q->x[i]<-1023||q->x[i]>1023||q->y[i]<-1023||q->y[i]>1023||q->gouraud[i]&0x8000)return false;
    if(q->solid){if(!(q->color&0x8000))return false;++solid_emits;solid_color_hash=(solid_color_hash^q->color)*16777619u;}
    for(unsigned i=0;i<4;++i){mix((uint16_t)q->x[i]);mix((uint16_t)q->y[i]);mix(q->gouraud[i]);}
    mix(q->palette);mix(q->face);mix(q->width);mix(q->height);mix(q->color);mix(q->texture);mix(q->solid);
    ++emits;return true;
}
static uint32_t hash(const uint8_t *p,unsigned n){uint32_t value=2166136261u;for(unsigned i=0;i<n;++i)value=(value^p[i])*16777619u;return value;}
static uint32_t read32(const uint8_t*p){return (uint32_t)p[0]*16777216u+(uint32_t)p[1]*65536u+(uint32_t)p[2]*256u+p[3];}
int main(int argc,char **argv){
    if(argc!=2)return 1;
    FILE*f=fopen(argv[1],"rb");if(!f)return 2;
    uint8_t data[65536];unsigned size=(unsigned)fread(data,1,sizeof(data),f);fclose(f);
    fzx_vehicle_pack_t pack;uint32_t expected=hash(data,size),textures=read32(data+12);
    if(!fzx_vehicle_pack_open(&pack,data,size,textures,expected,true))return 3;
    fzx_vehicle_pack_t bad;
    if(fzx_vehicle_pack_open(&bad,data,size-1,textures,expected,true)||bad.data)return 4;
    data[size-1]^=1;
    if(fzx_vehicle_pack_open(&bad,data,size,textures,expected,true)||bad.data)return 5;
    data[size-1]^=1;
    RefCamera camera={0};RefMachine state={0};state.model_pos[2]=-400;
    state.model_basis[0][2]=state.model_basis[1][1]=state.model_basis[2][0]=1;
    for(unsigned i=0;i<3;++i){state.model_scale[i]=1;state.ambient[i]=100;state.light_rgb[i]=255;state.body[i]=100;state.fog[i]=200;}
    state.light_dir[2]=127;state.reflection[2]=127;state.source_lod=1;state.normal_body_visible=1;
    const fzx_view_t view={.rotation={{1073741824,0,0},{0,1073741824,0},{0,0,-1073741824}},
        .focal={160*65536,160*65536},.center={160*65536,112*65536},.near_plane=16*4096,.far_plane=2000*4096,.clip={12,16,308,224}};
    fzx_vehicle_stats_t stats;
    if(!fzx_vehicle_draw(&pack,&view,&camera,&state,&work,256,emit,0,&stats)||stats.draws!=8||emits!=8||stats.palette_updates!=pack.palettes)return 6;
    uint16_t saved[FZX_VEHICLE_PALETTES][16];memcpy(saved,work.palette,sizeof(saved));emits=0;
    if(!fzx_vehicle_draw(&pack,&view,&camera,&state,&work,256,emit,0,&stats)||stats.draws!=8||stats.palette_updates||memcmp(saved,work.palette,sizeof(saved)))return 7;
    state.body[2]=250;
    if(!fzx_vehicle_draw(&pack,&view,&camera,&state,&work,256,emit,0,&stats)||!stats.palette_updates||!memcmp(saved,work.palette,sizeof(saved)))return 8;
    if(fzx_vehicle_draw(&pack,&view,&camera,&state,&work,1,emit,0,&stats)||stats.fault!=8||stats.draws!=1)return 9;
    /* The right panel crosses both the native coordinate guard and the view
     * edge here, exercising the software-clipped solid fallback. */
    allow_solid=true;state.model_pos[2]=-19;
    state.reflection[0]=127;state.reflection[2]=0;
    memset(state.fog,0,sizeof(state.fog));
    solid_emits=0;solid_color_hash=2166136261u;
    if(!fzx_vehicle_draw(&pack,&view,&camera,&state,&work,256,emit,0,&stats)||!stats.clipped||!solid_emits)return 11;
    const uint32_t dark=solid_color_hash;const unsigned clipped=stats.clipped,solids=solid_emits;
    state.light_dir[2]=-127;solid_emits=0;solid_color_hash=2166136261u;
    if(!fzx_vehicle_draw(&pack,&view,&camera,&state,&work,256,emit,0,&stats)||stats.clipped!=clipped||solid_emits!=solids||solid_color_hash==dark)return 12;
    const uint32_t lit=solid_color_hash;
    memset(state.fog,255,sizeof(state.fog));solid_emits=0;solid_color_hash=2166136261u;
    if(!fzx_vehicle_draw(&pack,&view,&camera,&state,&work,256,emit,0,&stats)||stats.clipped!=clipped||solid_emits!=solids||solid_color_hash==lit)return 13;
    state.normal_body_visible=0;
    if(!fzx_vehicle_draw(&pack,&view,&camera,&state,&work,0,emit,0,&stats)||stats.draws)return 10;
    printf("vehicle prepared pack, draw, shared palette cache, clipped lighting/fog and bounds PASS quads=%08x\n",(unsigned)quad_hash);return 0;
}
