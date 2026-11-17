#include "course_menu_native.h"
#include "course_preview_native.h"
#include "texture_cd.h"
#include "course_menu_identity.h"
#include <string.h>
#ifndef FZX_SOURCE_MACHINE_MENU
#define FZX_SOURCE_MACHINE_MENU 0
#endif
#if FZX_SOURCE_MACHINE_MENU
#include "machine_menu_native.h"
#endif

volatile uint32_t fzx_course_menu_status,fzx_course_menu_report[16];
volatile uint32_t fzx_course_menu_debug_buttons,fzx_course_menu_debug_limit,fzx_course_menu_debug_reset;
fzx_course_menu_snapshot_t fzx_course_menu_frame;
static fzx_course_menu_state_t state;
#define REQUEST(name) (*(volatile uint32_t *)((uintptr_t)&(name)|CPU_CACHE_THROUGH))
enum { GUARD=0x5ac92e49u, PALETTE_BYTES=FZX_COURSE_MENU_DRAW_MAX*32u };
_Static_assert(FZX_COURSE_MENU_ATLAS_IMAGES>=FZX_COURSE_MENU_IMAGE_COUNT,"Course atlas IDs disagree");

static uint16_t read_buttons(uint8_t *connected) {
    smpc_peripheral_digital_t pad={0};
    smpc_peripheral_process();smpc_peripheral_digital_port(1,&pad);
    const uint32_t debug=REQUEST(fzx_course_menu_debug_buttons);
    if(debug&0x10000u){*connected=1;return (uint16_t)debug;}
    *connected=pad.connected?1:0;
    if(!*connected)return 0;
    uint16_t buttons=0;
    if(pad.pressed.raw&(PERIPHERAL_DIGITAL_A|PERIPHERAL_DIGITAL_C))buttons|=0x8000u;
    if(pad.pressed.raw&PERIPHERAL_DIGITAL_B)buttons|=0x4000u;
    if(pad.pressed.raw&PERIPHERAL_DIGITAL_START)buttons|=0x1000u;
    if(pad.pressed.raw&PERIPHERAL_DIGITAL_UP)buttons|=0x0800u;
    if(pad.pressed.raw&PERIPHERAL_DIGITAL_DOWN)buttons|=0x0400u;
    if(pad.pressed.raw&PERIPHERAL_DIGITAL_LEFT)buttons|=0x0200u;
    if(pad.pressed.raw&PERIPHERAL_DIGITAL_RIGHT)buttons|=0x0100u;
    return buttons;
}

static bool draw(vdp1_cmdt_t *commands,unsigned capacity,uintptr_t pixels,uintptr_t palettes,
                 vdp1_cmdt_list_t *list,bool blank) {
    fzx_course_preview_report[6]=0;
    if(fzx_course_menu_frame.count>FZX_COURSE_MENU_DRAW_MAX || capacity<4)return false;
    memset(commands,0,3*sizeof(*commands));
    const int16_vec2_t clip=INT16_VEC2_INITIALIZER(319,223),origin=INT16_VEC2_INITIALIZER(0,0);
    vdp1_cmdt_system_clip_coord_set(commands);vdp1_cmdt_vtx_system_clip_coord_set(commands,clip);
    vdp1_cmdt_local_coord_set(commands+1);vdp1_cmdt_vtx_local_coord_set(commands+1,origin);
    vdp1_cmdt_t *clear=commands+2;
    const vdp1_cmdt_draw_mode_t clear_mode={.end_code_disable=1,.trans_pixel_disable=1,
        .color_mode=VDP1_CMDT_CM_RGB_32768,.cc_mode=VDP1_CMDT_CC_REPLACE};
    const rgb1555_t transparent={.raw=0};
    vdp1_cmdt_polygon_set(clear);vdp1_cmdt_draw_mode_set(clear,clear_mode);vdp1_cmdt_color_set(clear,transparent);
    clear->cmd_xa=0;clear->cmd_ya=0;clear->cmd_xb=319;clear->cmd_yb=0;
    clear->cmd_xc=319;clear->cmd_yc=223;clear->cmd_xd=0;clear->cmd_yd=223;
    unsigned count=3;
    if(!blank)for(unsigned i=0;i<fzx_course_menu_frame.count;++i) {
        const fzx_course_menu_draw_t *q=&fzx_course_menu_frame.draws[i];
        if(q->kind==FZX_COURSE_DRAW_PREVIEW) {
            const int next=fzx_course_preview_draw(commands,count,capacity-1,&fzx_course_menu_frame);
            if(next<(int)count || (unsigned)next>=capacity)return false;
            count=(unsigned)next;continue;
        }
        unsigned image=q->image;
        if(q->kind==FZX_COURSE_DRAW_GLYPH) {
            if(q->font==3 && image<sizeof(fzx_course_font3)/sizeof(fzx_course_font3[0]))image=fzx_course_font3[image][0];
            else if(q->font==4 && image<sizeof(fzx_course_font4)/sizeof(fzx_course_font4[0]))image=fzx_course_font4[image][0];
            else return false;
        } else if(q->kind!=FZX_COURSE_DRAW_IMAGE)return false;
        if(image>=FZX_COURSE_MENU_ATLAS_IMAGES || q->scale_x_q8>1024 || q->scale_y_q8>1024)return false;
        if(!q->a || !q->scale_x_q8 || !q->scale_y_q8)continue;
        if(count+1>=capacity)return false;
        vdp1_cmdt_t *c=commands+count++;memset(c,0,sizeof(*c));
        const bool clut=fzx_course_menu_images[image].clut!=0;
        const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,
            .color_mode=clut?VDP1_CMDT_CM_CLUT_16:VDP1_CMDT_CM_RGB_32768,
            /* Wallpaper uses its baked steady-state RGB75 appearance.
             * Source alpha180 framebuffer persistence is not reproduced. */
            .cc_mode=clut&&q->a<192?VDP1_CMDT_CC_HALF_TRANSPARENT:VDP1_CMDT_CC_REPLACE};
        uint32_t offset=fzx_course_menu_images[image].offset;
        if(image>=FZX_COURSE_IMAGE_CUP_JACK && image<=FZX_COURSE_IMAGE_CUP_QUESTION) {
            if(q->r==100 && q->g==100 && q->b==100)offset=fzx_course_menu_images[image].dim_offset;
            else if(q->r<218)offset=fzx_course_menu_images[image].pulse_offset;
        }
        const unsigned width=fzx_course_menu_images[image].width,height=fzx_course_menu_images[image].height;
        if(q->scale_x_q8==256 && q->scale_y_q8==256) {
            vdp1_cmdt_normal_sprite_set(c);
        } else {
            const unsigned scaled_width=(width*q->scale_x_q8+128u)/256u;
            const unsigned scaled_height=(height*q->scale_y_q8+128u)/256u;
            if(!scaled_width || !scaled_height){--count;continue;}
            vdp1_cmdt_distorted_sprite_set(c);
            c->cmd_xb=(int16_t)(q->x+(int)scaled_width-1);c->cmd_yb=(int16_t)(q->y-8);
            c->cmd_xc=c->cmd_xb;c->cmd_yc=(int16_t)(q->y-8+(int)scaled_height-1);
            c->cmd_xd=q->x;c->cmd_yd=c->cmd_yc;
        }
        if(q->flip_x)c->cmd_ctrl|=0x0010u;
        vdp1_cmdt_draw_mode_set(c,mode);vdp1_cmdt_char_base_set(c,(vdp1_vram_t)(pixels+offset));
        vdp1_cmdt_char_size_set(c,width,height);c->cmd_xa=q->x;c->cmd_ya=(int16_t)(q->y-8);
        if(clut) {
            volatile uint16_t *table=(volatile uint16_t *)(palettes+i*32u);
            for(unsigned p=0;p<16;++p) {
                const uint16_t v=fzx_course_menu_images[image].palette[p];
                table[p]=v?(uint16_t)(0x8000u|((v&31u)*q->r/255u)|
                    ((((v>>5)&31u)*q->g/255u)<<5)|((((v>>10)&31u)*q->b/255u)<<10)):0;
            }
            c->cmd_colr=(uint16_t)(((palettes+i*32u)&0x7ffffu)>>3);
        }
    }
    memset(commands+count,0,sizeof(*commands));vdp1_cmdt_end_set(commands+count);list->count=(uint16_t)(count+1);
    fzx_course_menu_report[10]=count+1;
    vdp1_sync_cmdt_list_put(list,0);vdp1_sync_render();vdp1_sync();vdp2_sync();
    return true;
}

bool fzx_course_menu_run(volatile const uint32_t *fields,vdp1_cmdt_t *commands,unsigned capacity,
                        void *scratch,uint32_t scratch_bytes) {
    fzx_course_menu_status=10;
    if(!fields||!commands||capacity<FZX_COURSE_MENU_DRAW_MAX+4||!scratch)return false;
    vdp1_vram_partitions_t partition;vdp1_vram_partitions_get(&partition);
    if(FZX_COURSE_MENU_BYTES+PALETTE_BYTES+96u>partition.texture_size){fzx_course_menu_status=3;return false;}
    const uintptr_t pixels=(uintptr_t)partition.texture_base+32u,palettes=pixels+FZX_COURSE_MENU_BYTES+32u;
    volatile uint32_t *guards[3]={(volatile uint32_t *)partition.texture_base,
        (volatile uint32_t *)(pixels+FZX_COURSE_MENU_BYTES),(volatile uint32_t *)(palettes+PALETTE_BYTES)};
    for(unsigned g=0;g<3;++g)for(unsigned i=0;i<8;++i)guards[g][i]=GUARD;
    if(!fzx_texture_cd_load("COURSEUI.PAK",FZX_COURSE_MENU_BYTES,FZX_COURSE_MENU_HASH,(void *)pixels,scratch,scratch_bytes)) {
        fzx_course_menu_status=4;return false;
    }
    if(!fzx_course_preview_load(scratch,scratch_bytes)){fzx_course_menu_status=8;return false;}
    fzx_course_menu_config_t config={0};config.mode=1;
    if(!fzx_course_menu_reset(&state,&config)){fzx_course_menu_status=6;return false;}
    vdp1_cmdt_list_t list={.cmdts=commands};
    const uint32_t hz=vdp2_tvmd_tv_standard_get()==VDP2_TVMD_TV_STANDARD_PAL?50u:60u;
    uint32_t previous=*fields,remainder=0;uint16_t last_buttons=0;
    vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3,0x1fffe),RGB1555(1,0,0,0));fzx_course_menu_status=1;
    for(;;) {
        vdp1_sync_wait();vdp2_sync_wait();
        for(unsigned g=0;g<3;++g)for(unsigned i=0;i<8;++i)if(guards[g][i]!=GUARD){fzx_course_menu_status=5;return false;}
        if(!fzx_course_preview_guards()){fzx_course_menu_status=9;return false;}
        uint8_t connected;const uint16_t buttons=read_buttons(&connected);
        if(REQUEST(fzx_course_menu_debug_reset) || (state.request && (buttons&~last_buttons&0x4000u))) {
            config.last_course=(uint8_t)state.course;
            if(!fzx_course_menu_reset(&state,&config)){fzx_course_menu_status=6;return false;}
            state.held=buttons;REQUEST(fzx_course_menu_debug_reset)=0;remainder=0;previous=*fields;
        }
        last_buttons=buttons;
        const uint32_t now=*fields;uint32_t elapsed=now-previous;previous=now;
        if(elapsed>5)elapsed=5;
        remainder+=elapsed*60u;
        while(remainder>=hz) {
            const uint32_t limit=REQUEST(fzx_course_menu_debug_limit);
            if(!state.request && (!limit||state.ticks<limit) && !fzx_course_menu_step(&state,buttons,connected,false)) {
                fzx_course_menu_status=6;return false;
            }
            remainder-=hz;
        }
        if(!fzx_course_menu_snapshot(&state,&fzx_course_menu_frame)){fzx_course_menu_status=6;return false;}
        fzx_course_menu_report[0]=0x465a5843u;fzx_course_menu_report[1]=state.ticks;
        fzx_course_menu_report[2]=(uint32_t)state.phase;fzx_course_menu_report[3]=(uint32_t)state.cup_option;
        fzx_course_menu_report[4]=(uint32_t)state.track;fzx_course_menu_report[5]=state.request;
        fzx_course_menu_report[6]=(uint32_t)state.course;fzx_course_menu_report[7]=FZX_COURSE_MENU_HASH;
        fzx_course_menu_report[8]=(uint32_t)pixels;fzx_course_menu_report[9]=(uint32_t)palettes;
        fzx_course_menu_report[11]=state.connected;fzx_course_menu_report[12]=(uint32_t)state.selected_ghost;
        fzx_course_menu_status=state.request?2:1;
        if(!draw(commands,capacity,pixels,palettes,&list,false)){fzx_course_menu_status=7;return false;}
#if FZX_SOURCE_MACHINE_MENU
        if(state.request==FZX_COURSE_REQUEST_MACHINE) {
            vdp1_sync_wait();vdp2_sync_wait();
            for(;;) {
                uint8_t released_connected;if(!read_buttons(&released_connected))break;
                vdp2_sync();vdp2_sync_wait();
            }
            for(unsigned i=0;i<2;++i) {
                if(!draw(commands,capacity,pixels,palettes,&list,true)){fzx_course_menu_status=7;return false;}
                vdp1_sync_wait();vdp2_sync_wait();
            }
            bool race_ready=false;
            if(!fzx_machine_menu_run(fields,commands,capacity,scratch,scratch_bytes,(uint8_t)state.course,&race_ready)) {
                fzx_course_menu_status=9;return false;
            }
            if(race_ready){fzx_course_menu_status=2;return true;}
            if(!fzx_texture_cd_load("COURSEUI.PAK",FZX_COURSE_MENU_BYTES,FZX_COURSE_MENU_HASH,(void *)pixels,scratch,scratch_bytes)) {
                fzx_course_menu_status=4;return false;
            }
            for(unsigned g=0;g<3;++g)for(unsigned i=0;i<8;++i)guards[g][i]=GUARD;
            if(!fzx_course_preview_load(scratch,scratch_bytes)){fzx_course_menu_status=8;return false;}
            config=state.config;config.last_course=(uint8_t)state.course;config.frame=state.frame;
            if(!fzx_course_menu_reset(&state,&config)){fzx_course_menu_status=6;return false;}
            last_buttons=0;remainder=0;previous=*fields;fzx_course_menu_status=1;continue;
        }
#endif
        if(state.request==FZX_COURSE_REQUEST_MAIN_MENU)break;
    }
    vdp1_sync_wait();vdp2_sync_wait();
    for(;;) {
        uint8_t released_connected;if(!read_buttons(&released_connected))break;
        vdp2_sync();vdp2_sync_wait();
    }
    for(unsigned i=0;i<2;++i) {
        if(!draw(commands,capacity,pixels,palettes,&list,true)){fzx_course_menu_status=7;return false;}
        vdp1_sync_wait();vdp2_sync_wait();
    }
    fzx_course_menu_status=0;return true;
}
