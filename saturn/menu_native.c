#include "menu_native.h"
#include "texture_cd.h"
#include "menu_identity.h"
#include <string.h>
#ifndef FZX_SOURCE_COURSE_MENU
#define FZX_SOURCE_COURSE_MENU 0
#endif
#if FZX_SOURCE_COURSE_MENU
#include "course_menu_native.h"
#endif

volatile uint32_t fzx_menu_status,fzx_menu_report[12];
volatile uint32_t fzx_menu_debug_buttons,fzx_menu_debug_limit,fzx_menu_debug_reset;
fzx_menu_snapshot_t fzx_menu_frame;
static fzx_menu_state_t state;
#define REQUEST(name) (*(volatile uint32_t *)((uintptr_t)&(name)|CPU_CACHE_THROUGH))
enum { GUARD=0x5ac91e39u, PALETTE_BYTES=FZX_MENU_DRAW_MAX*32u };
_Static_assert(FZX_MENU_ATLAS_IMAGES==FZX_MENU_IMAGE_COUNT,"Menu atlas IDs disagree");

static uint16_t read_buttons(uint8_t *connected) {
    smpc_peripheral_digital_t pad={0};
    smpc_peripheral_process();smpc_peripheral_digital_port(1,&pad);
    const uint32_t debug=REQUEST(fzx_menu_debug_buttons);
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

static bool draw(vdp1_cmdt_t *commands,uintptr_t pixels,uintptr_t palettes,
                 vdp1_cmdt_list_t *list,bool blank) {
    if(fzx_menu_frame.count>FZX_MENU_DRAW_MAX)return false;
    memset(commands,0,(FZX_MENU_DRAW_MAX+4)*sizeof(*commands));
    const int16_vec2_t clip=INT16_VEC2_INITIALIZER(319,223),origin=INT16_VEC2_INITIALIZER(0,0);
    vdp1_cmdt_system_clip_coord_set(commands);vdp1_cmdt_vtx_system_clip_coord_set(commands,clip);
    vdp1_cmdt_local_coord_set(commands+1);vdp1_cmdt_vtx_local_coord_set(commands+1,origin);
    /* Clear the complete buffer, including the bottom lines timed erase misses. */
    vdp1_cmdt_t *clear=commands+2;
    const vdp1_cmdt_draw_mode_t clear_mode={.end_code_disable=1,.trans_pixel_disable=1,
        .color_mode=VDP1_CMDT_CM_RGB_32768,.cc_mode=VDP1_CMDT_CC_REPLACE};
    const rgb1555_t transparent={.raw=0};
    vdp1_cmdt_polygon_set(clear);vdp1_cmdt_draw_mode_set(clear,clear_mode);vdp1_cmdt_color_set(clear,transparent);
    clear->cmd_xa=0;clear->cmd_ya=0;clear->cmd_xb=319;clear->cmd_yb=0;
    clear->cmd_xc=319;clear->cmd_yc=223;clear->cmd_xd=0;clear->cmd_yd=223;
    unsigned count=3;
    if(!blank)for(unsigned i=0;i<fzx_menu_frame.count;++i) {
        const fzx_menu_draw_t *q=&fzx_menu_frame.draws[i];
        if(q->image>=FZX_MENU_ATLAS_IMAGES)return false;
        const unsigned image=q->image;
        if(!q->a)continue;
        vdp1_cmdt_t *c=commands+count++;
        const bool clut=fzx_menu_images[image].clut!=0;
        const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,
            .color_mode=clut?VDP1_CMDT_CM_CLUT_16:VDP1_CMDT_CM_RGB_32768,
            .cc_mode=clut&&q->a<192?VDP1_CMDT_CC_HALF_TRANSPARENT:VDP1_CMDT_CC_REPLACE};
        uint32_t offset=fzx_menu_images[image].offset;
        if(image>=FZX_MENU_GP && image<=FZX_MENU_OPTIONS) {
            if(q->r==128 && q->g==128 && q->b==128)offset=fzx_menu_images[image].dim_offset;
            else if(q->a<224)offset=fzx_menu_images[image].pulse_offset;
        }
        vdp1_cmdt_normal_sprite_set(c);vdp1_cmdt_draw_mode_set(c,mode);
        vdp1_cmdt_char_base_set(c,(vdp1_vram_t)(pixels+offset));
        vdp1_cmdt_char_size_set(c,fzx_menu_images[image].width,fzx_menu_images[image].height);
        c->cmd_xa=q->x;c->cmd_ya=(int16_t)(q->y-8);
        if(clut) {
            volatile uint16_t *table=(volatile uint16_t *)(palettes+i*32u);
            for(unsigned p=0;p<16;++p) {
                const uint16_t v=fzx_menu_images[image].palette[p];
                table[p]=v?(uint16_t)(0x8000u|((v&31u)*q->r/255u)|
                    ((((v>>5)&31u)*q->g/255u)<<5)|((((v>>10)&31u)*q->b/255u)<<10)):0;
            }
            c->cmd_colr=(uint16_t)(((palettes+i*32u)&0x7ffffu)>>3);
        }
    }
    vdp1_cmdt_end_set(commands+count);list->count=(uint16_t)(count+1);
    fzx_menu_report[10]=count+1;
    vdp1_sync_cmdt_list_put(list,0);vdp1_sync_render();vdp1_sync();vdp2_sync();
    return true;
}

bool fzx_menu_run(volatile const uint32_t *fields,vdp1_cmdt_t *commands,unsigned capacity,
                  void *scratch,uint32_t scratch_bytes) {
    fzx_menu_status=10;
    if(!fields||!commands||capacity<FZX_MENU_DRAW_MAX+4||!scratch)return false;
    vdp1_vram_partitions_t partition;vdp1_vram_partitions_get(&partition);
    if(FZX_MENU_BYTES+PALETTE_BYTES+96u>partition.texture_size){fzx_menu_status=3;return false;}
    const uintptr_t pixels=(uintptr_t)partition.texture_base+32u;
    const uintptr_t palettes=pixels+FZX_MENU_BYTES+32u;
    volatile uint32_t *guards[3]={(volatile uint32_t *)partition.texture_base,
        (volatile uint32_t *)(pixels+FZX_MENU_BYTES),(volatile uint32_t *)(palettes+PALETTE_BYTES)};
    for(unsigned g=0;g<3;++g)for(unsigned i=0;i<8;++i)guards[g][i]=GUARD;
    if(!fzx_texture_cd_load("MENU.PAK",FZX_MENU_BYTES,FZX_MENU_HASH,(void *)pixels,scratch,scratch_bytes)) {
        fzx_menu_status=4;return false;
    }
    if(!fzx_menu_reset(&state,0,0,false,0)){fzx_menu_status=6;return false;}
    vdp1_cmdt_list_t list={.cmdts=commands};
    const uint32_t hz=vdp2_tvmd_tv_standard_get()==VDP2_TVMD_TV_STANDARD_PAL?50u:60u;
    uint32_t previous=*fields,remainder=0;
    uint16_t last_buttons=0;
    vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3,0x1fffe),RGB1555(1,0,0,0));
    fzx_menu_status=1;
    for(;;) {
        vdp1_sync_wait();vdp2_sync_wait();
        for(unsigned g=0;g<3;++g)for(unsigned i=0;i<8;++i)if(guards[g][i]!=GUARD){fzx_menu_status=5;return false;}
        uint8_t connected;const uint16_t buttons=read_buttons(&connected);
        if(REQUEST(fzx_menu_debug_reset) || (state.request && (buttons&~last_buttons&0x4000u))) {
            if(!fzx_menu_reset(&state,(uint8_t)state.selected,0,false,0)){fzx_menu_status=6;return false;}
            /* Consuming B here must not send the newly reset menu to title. */
            state.held=buttons;
            REQUEST(fzx_menu_debug_reset)=0;remainder=0;previous=*fields;
        }
        last_buttons=buttons;
        const uint32_t now=*fields;uint32_t elapsed=now-previous;previous=now;
        if(elapsed>5)elapsed=5;
        remainder+=elapsed*60u;
        while(remainder>=hz) {
            const uint32_t limit=REQUEST(fzx_menu_debug_limit);
            if(!state.request && (!limit||state.ticks<limit) && !fzx_menu_step(&state,buttons,connected,false)) {
                fzx_menu_status=6;return false;
            }
            remainder-=hz;
        }
        if(!fzx_menu_snapshot(&state,&fzx_menu_frame)){fzx_menu_status=6;return false;}
        fzx_menu_report[0]=0x465a584du;fzx_menu_report[1]=state.ticks;
        fzx_menu_report[2]=(uint32_t)state.phase;fzx_menu_report[3]=(uint32_t)state.selected;
        fzx_menu_report[4]=(uint32_t)state.suboptions[state.selected];fzx_menu_report[5]=state.request;
        fzx_menu_report[6]=state.cue;fzx_menu_report[7]=FZX_MENU_HASH;
        fzx_menu_report[8]=(uint32_t)pixels;fzx_menu_report[9]=(uint32_t)palettes;
        fzx_menu_report[11]=state.connected;
        fzx_menu_status=state.request?2:1;
        if(!draw(commands,pixels,palettes,&list,false)){fzx_menu_status=7;return false;}
#if FZX_SOURCE_COURSE_MENU
        if(state.request==FZX_MENU_REQUEST_COURSE && state.selected==1) {
            /* Consume the menu confirm before the next screen sees input. */
            vdp1_sync_wait();vdp2_sync_wait();
            for(;;) {
                uint8_t attached;if(!read_buttons(&attached))break;
                vdp2_sync();vdp2_sync_wait();
            }
            for(unsigned i=0;i<2;++i) {
                if(!draw(commands,pixels,palettes,&list,true)){fzx_menu_status=7;return false;}
                vdp1_sync_wait();vdp2_sync_wait();
            }
            if(!fzx_course_menu_run(fields,commands,capacity,scratch,scratch_bytes)) {
                fzx_menu_status=8;return false;
            }
            /* Frontend phases borrow the same VRAM and CD scratch space. */
            if(!fzx_texture_cd_load("MENU.PAK",FZX_MENU_BYTES,FZX_MENU_HASH,(void *)pixels,scratch,scratch_bytes)) {
                fzx_menu_status=4;return false;
            }
            for(unsigned g=0;g<3;++g)for(unsigned i=0;i<8;++i)guards[g][i]=GUARD;
            if(!fzx_menu_reset(&state,1,0,false,0)){fzx_menu_status=6;return false;}
            last_buttons=0;remainder=0;previous=*fields;fzx_menu_status=1;continue;
        }
#endif
        if(state.request==FZX_MENU_REQUEST_TITLE)break;
    }
    vdp1_sync_wait();vdp2_sync_wait();
    for(;;) {
        uint8_t connected;const uint16_t buttons=read_buttons(&connected);
        if(!buttons)break;
        vdp2_sync();vdp2_sync_wait();
    }
    for(unsigned i=0;i<2;++i) {
        if(!draw(commands,pixels,palettes,&list,true)){fzx_menu_status=7;return false;}
        vdp1_sync_wait();vdp2_sync_wait();
    }
    fzx_menu_status=0;
    return true;
}
