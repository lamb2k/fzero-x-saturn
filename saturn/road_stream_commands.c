#include "road_stream_commands.h"
#include <string.h>
#include "fzx_road_stream_phase.h"

static bool vram_span(uintptr_t address,uint32_t bytes){
    const uintptr_t base=(uintptr_t)VDP1_VRAM(0);
    return address>=base&&address<base+0x80000u&&!(address&7u)&&
        bytes<=base+0x80000u-address;
}
static bool overlap(uintptr_t a,uintptr_t a_bytes,uintptr_t b,uintptr_t b_bytes){
    return a<b?b-a<a_bytes:a-b<b_bytes;
}
/* Fully initialized templates: RGB1555 and CLUT16 distorted sprites, solid polygon. */
static void templates_init(vdp1_cmdt_t templates[3]){
    memset(templates,0,3*sizeof(*templates));
    for(unsigned i=0;i<3;++i){
        const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,.trans_pixel_disable=1,
            .user_clipping_mode=2,.color_mode=i==1?VDP1_CMDT_CM_CLUT_16:VDP1_CMDT_CM_RGB_32768,
            .cc_mode=VDP1_CMDT_CC_GOURAUD};
        vdp1_cmdt_draw_mode_set(templates+i,mode);
        if(i==2){vdp1_cmdt_polygon_set(templates+i);
            const rgb1555_t neutral={.raw=0xc210};vdp1_cmdt_color_set(templates+i,neutral);}
        else vdp1_cmdt_distorted_sprite_set(templates+i);
    }
}
#if FZX_ROAD_STREAM_EMIT
bool fzx_road_stream_emit_setup(fzx_road_stream_emit_t *e,vdp1_cmdt_t *commands,uint32_t capacity,
    const void *pixels,uint32_t texture_bytes,void *shades,uint32_t shade_bytes){
    _Static_assert(sizeof(vdp1_cmdt_t)==sizeof(fzx_road_stream_vdp1_t),"VDP1 command layout");
    const uintptr_t pixel=(uintptr_t)pixels,shade=(uintptr_t)shades,command_bytes=(uintptr_t)capacity*sizeof(*commands);
    if(!e||!commands||(uintptr_t)commands%_Alignof(vdp1_cmdt_t)||!capacity||capacity>0x80000u/8u||
       !texture_bytes||!vram_span(pixel,texture_bytes)||!vram_span(shade,shade_bytes)||capacity>shade_bytes/8u||
       overlap(pixel,texture_bytes,shade,shade_bytes)||overlap((uintptr_t)commands,command_bytes,pixel,texture_bytes)||
       overlap((uintptr_t)commands,command_bytes,shade,shade_bytes))return false;
    vdp1_cmdt_t templates[3];templates_init(templates);
    memcpy(e->templates,templates,sizeof(templates));
    e->commands=(fzx_road_stream_vdp1_t *)(void *)commands;e->shades=(uint16_t *)shades;
    e->texture_offset=(uint32_t)(pixel&0x7ffffu);e->shade_offset=(uint32_t)(shade&0x7ffffu);
    return true;
}
#endif
bool fzx_road_stream_commands(vdp1_cmdt_t *commands,uint32_t capacity,
    const fzx_road_stream_command_t *packets,uint32_t packet_count,
    const void *pixels,uint32_t texture_bytes,void *shades,uint32_t shade_bytes,
    uint32_t *count){
    FZX_RSP(CONVERT);
    if(!count||(uintptr_t)count%_Alignof(uint32_t))return false;
    *count=0;
    const uintptr_t pixel=(uintptr_t)pixels,shade=(uintptr_t)shades;
    if(!commands||!packets||!pixels||!shades||
       (uintptr_t)commands%_Alignof(vdp1_cmdt_t)||
       (uintptr_t)packets%_Alignof(fzx_road_stream_command_t)||
       packet_count>capacity||packet_count>0x80000u/8u||
       !texture_bytes||!vram_span(pixel,texture_bytes)||!vram_span(shade,shade_bytes)||
       packet_count>shade_bytes/8u||overlap(pixel,texture_bytes,shade,shade_bytes))return false;
    const uintptr_t command_bytes=(uintptr_t)packet_count*sizeof(*commands);
    const uintptr_t packet_bytes=(uintptr_t)packet_count*sizeof(*packets);
    if(overlap((uintptr_t)commands,command_bytes,(uintptr_t)packets,packet_bytes)||
       overlap((uintptr_t)commands,command_bytes,pixel,texture_bytes)||
       overlap((uintptr_t)commands,command_bytes,shade,shade_bytes)||
       overlap((uintptr_t)packets,packet_bytes,shade,shade_bytes))return false;
    /* Copy fully initialized templates; no callback or per-command memset. */
    vdp1_cmdt_t templates[3];templates_init(templates);
    for(uint32_t i=0;i<packet_count;++i){
        const fzx_road_stream_command_t *p=packets+i;
        const fzx_road_stream_material_t *m=&p->material;
        if(p->solid>1||m->reserved)return false;
        if(!p->solid){
            if(m->mode>1||!m->width||(m->width&7u)||m->width>504||!m->height||m->height>255||
               (m->pixels&7u)||m->pixels>texture_bytes)return false;
            const uint32_t bytes=(uint32_t)m->width*m->height*(m->mode?1u:4u)/2u;
            if(bytes>texture_bytes-m->pixels)return false;
            if(m->mode&&((m->palette&7u)||m->palette>texture_bytes||32u>texture_bytes-m->palette))return false;
        }
        vdp1_cmdt_t *c=commands+i;*c=templates[p->solid?2:m->mode];
        if(!p->solid){
            vdp1_cmdt_char_base_set(c,(vdp1_vram_t)(pixel+m->pixels));
            vdp1_cmdt_char_size_set(c,m->width,m->height);
            if(m->mode)c->cmd_colr=(uint16_t)(((pixel+m->palette)&0x7ffffu)>>3);
        }
        c->cmd_xa=p->x[0];c->cmd_ya=p->y[0];c->cmd_xb=p->x[1];c->cmd_yb=p->y[1];
        c->cmd_xc=p->x[2];c->cmd_yc=p->y[2];c->cmd_xd=p->x[3];c->cmd_yd=p->y[3];
        uint16_t *g=(uint16_t *)(shade+(uintptr_t)i*8u);memcpy(g,p->shade,8);
        c->cmd_grda=(uint16_t)(((uintptr_t)g&0x7ffffu)>>3);
    }
    *count=packet_count;return true;
}
