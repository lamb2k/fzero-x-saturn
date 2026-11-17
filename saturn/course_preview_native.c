#include "course_preview_native.h"
#include "fzx_course_preview.h"
#include "course_preview_identity.h"
#include "texture_cd.h"
#include <string.h>

volatile uint32_t fzx_course_preview_report[8];
static fzx_course_preview_pack_t pack;
static sh2z_pos16_t *positions;
static sh2z_view_vertex_t *projected;
static volatile uint32_t *guards[4];
enum { GUARD=0x67d123abu };
_Static_assert(FZX_COURSE_PREVIEW_MAX_VERTICES<=FZX_COURSE_PREVIEW_VERTEX_LIMIT,"Preview scratch too small");
static uintptr_t aligned(uintptr_t p){return (p+31u)&~(uintptr_t)31u;}

bool fzx_course_preview_guards(void) {
    if(!pack.data)return false;
    for(unsigned g=0;g<4;++g)for(unsigned i=0;i<8;++i)if(guards[g][i]!=GUARD)return false;
    return true;
}

bool fzx_course_preview_load(void *memory,uint32_t bytes) {
    pack=(fzx_course_preview_pack_t){0};memset((void *)fzx_course_preview_report,0,sizeof(fzx_course_preview_report));
    if(!memory || ((uintptr_t)memory&31u))return false;
    const uintptr_t base=(uintptr_t)memory,end=base+bytes;
    if(end<base)return false;
    const uintptr_t payload=base+32u,g1=aligned(payload+FZX_COURSE_PREVIEW_BYTES);
    const uintptr_t pos=g1+32u,g2=aligned(pos+sizeof(*positions)*FZX_COURSE_PREVIEW_MAX_VERTICES);
    const uintptr_t proj=g2+32u,g3=aligned(proj+sizeof(*projected)*FZX_COURSE_PREVIEW_MAX_VERTICES);
    const uintptr_t cd=g3+32u;
    if(cd<base || cd>end || fzx_texture_cd_scratch_bytes()>end-cd)return false;
    guards[0]=(volatile uint32_t *)base;guards[1]=(volatile uint32_t *)g1;
    guards[2]=(volatile uint32_t *)g2;guards[3]=(volatile uint32_t *)g3;
    for(unsigned g=0;g<4;++g) {
        fzx_course_preview_report[2+g]=(uint32_t)(uintptr_t)guards[g];
        for(unsigned i=0;i<8;++i)guards[g][i]=GUARD;
    }
    if(!fzx_texture_cd_load("CPREVIEW.PAK",FZX_COURSE_PREVIEW_BYTES,FZX_COURSE_PREVIEW_HASH,
         (void *)payload,(void *)cd,(uint32_t)(end-cd)) ||
       !fzx_course_preview_open(&pack,(const void *)payload,FZX_COURSE_PREVIEW_BYTES,FZX_COURSE_PREVIEW_HASH))return false;
    positions=(sh2z_pos16_t *)pos;projected=(sh2z_view_vertex_t *)proj;
    fzx_course_preview_report[0]=0x43505256u;fzx_course_preview_report[1]=FZX_COURSE_PREVIEW_HASH;
    fzx_course_preview_report[7]=1;
    return fzx_course_preview_guards();
}

typedef struct { vdp1_cmdt_t *commands;unsigned next,capacity;uint16_t color; } output_t;
static bool emit(void *context,const fzx_course_preview_triangle_t *t) {
    output_t *o=context;if(o->next>=o->capacity)return false;
    vdp1_cmdt_t *c=o->commands+o->next++;memset(c,0,sizeof(*c));
    const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,.trans_pixel_disable=1,
        .color_mode=VDP1_CMDT_CM_RGB_32768,.cc_mode=VDP1_CMDT_CC_REPLACE};
    vdp1_cmdt_polygon_set(c);vdp1_cmdt_draw_mode_set(c,mode);c->cmd_colr=o->color;
    c->cmd_xa=t->x[0];c->cmd_ya=t->y[0];c->cmd_xb=t->x[1];c->cmd_yb=t->y[1];
    c->cmd_xc=c->cmd_xd=t->x[2];c->cmd_yc=c->cmd_yd=t->y[2];return true;
}

int fzx_course_preview_draw(vdp1_cmdt_t *commands,unsigned first,unsigned capacity,
                           const fzx_course_menu_snapshot_t *frame) {
    fzx_course_preview_report[6]=0;
    if(!commands || !frame || first>capacity || !fzx_course_preview_guards())return -1;
    if(!frame->preview_visible)return (int)first;
    if(frame->preview_cup>=4 || frame->cup_option>=4 || frame->carousel_x < -6400 ||
       frame->carousel_x>400 || !(frame->camera_yaw_degrees>=0.0f && frame->camera_yaw_degrees<360.0f))return -1;
    static const uint8_t colors[4][3]={{192,64,64},{100,150,255},{255,255,100},{0,255,0}};
    const uint8_t *rgb=colors[frame->cup_option];
    output_t o={commands,first,capacity,(uint16_t)(0x8000u|(rgb[0]>>3)|((rgb[1]>>3)<<5)|((rgb[2]>>3)<<10))};
    /* Offline camera orbit preserves the original pitch20 view. Optional N64
     * C-button tilt is not mapped to the Saturn pad in this adaptation. */
    const unsigned yaw=(unsigned)frame->camera_yaw_degrees;
    for(unsigned slot=0;slot<6;++slot) {
        unsigned count;
        if(!fzx_course_preview_render(&pack,frame->preview_cup*6u+slot,yaw,
            frame->carousel_x+(int32_t)slot*1280,positions,projected,FZX_COURSE_PREVIEW_MAX_VERTICES,
            emit,&o,&count)){fzx_course_preview_report[7]=2;return -1;}
        fzx_course_preview_report[6]+=count;
    }
    if(!fzx_course_preview_guards()){fzx_course_preview_report[7]=3;return -1;}
    return (int)o.next;
}
