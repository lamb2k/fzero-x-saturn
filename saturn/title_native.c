#include "title_native.h"
#include "texture_cd.h"
#include "title_identity.h"
#include <string.h>

volatile uint32_t fzx_title_status,fzx_title_report[8];
volatile uint32_t fzx_title_debug_buttons,fzx_title_debug_limit,fzx_title_debug_reset;
static fzx_title_state_t state;
#define REQUEST(name) (*(volatile uint32_t *)((uintptr_t)&(name)|CPU_CACHE_THROUGH))
enum {GUARD=0x5ac71e39u};

static uint16_t read_buttons(bool *connected) {
    smpc_peripheral_digital_t pad={0};
    smpc_peripheral_process();smpc_peripheral_digital_port(1,&pad);
    const uint32_t debug=REQUEST(fzx_title_debug_buttons);
    if(debug&0x10000u){*connected=true;return (uint16_t)debug;}
    *connected=pad.connected!=0;
    if(!*connected)return 0;
    uint16_t buttons=0;
    if(pad.pressed.raw&(PERIPHERAL_DIGITAL_A|PERIPHERAL_DIGITAL_C))buttons|=0x8000u;
    if(pad.pressed.raw&PERIPHERAL_DIGITAL_START)buttons|=0x1000u;
    return buttons;
}

static void draw(vdp1_cmdt_t *commands,void *pixels,vdp1_cmdt_list_t *list,bool blank) {
    memset(commands,0,7*sizeof(*commands));
    const int16_vec2_t clip=INT16_VEC2_INITIALIZER(319,223),origin=INT16_VEC2_INITIALIZER(0,0);
    vdp1_cmdt_system_clip_coord_set(commands);vdp1_cmdt_vtx_system_clip_coord_set(commands,clip);
    vdp1_cmdt_local_coord_set(commands+1);vdp1_cmdt_vtx_local_coord_set(commands+1,origin);
    unsigned count=2;
    if(blank) {
        /* Timed erase may leave the lower lines untouched. Write transparent
         * pixels explicitly before the next phase borrows these framebuffers. */
        vdp1_cmdt_t *c=commands+count++;
        const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,.trans_pixel_disable=1,
            .color_mode=VDP1_CMDT_CM_RGB_32768,.cc_mode=VDP1_CMDT_CC_REPLACE};
        const rgb1555_t clear={.raw=0};
        vdp1_cmdt_polygon_set(c);vdp1_cmdt_draw_mode_set(c,mode);vdp1_cmdt_color_set(c,clear);
        c->cmd_xa=0;c->cmd_ya=0;c->cmd_xb=319;c->cmd_yb=0;
        c->cmd_xc=319;c->cmd_yc=223;c->cmd_xd=0;c->cmd_yd=223;
    }
    if(!blank)for(unsigned i=0;i<FZX_TITLE_IMAGE_COUNT;++i) {
        if(i==2 && !fzx_title_push_start_visible(&state))continue;
        if(i==4 && state.connected)continue;
        vdp1_cmdt_t *c=commands+count++;
        const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,.color_mode=VDP1_CMDT_CM_RGB_32768,.cc_mode=VDP1_CMDT_CC_REPLACE};
        vdp1_cmdt_normal_sprite_set(c);vdp1_cmdt_draw_mode_set(c,mode);
        vdp1_cmdt_char_base_set(c,(vdp1_vram_t)((uintptr_t)pixels+fzx_title_images[i].offset));
        vdp1_cmdt_char_size_set(c,fzx_title_images[i].width,fzx_title_images[i].height);
        c->cmd_xa=fzx_title_images[i].left;c->cmd_ya=(int16_t)(fzx_title_images[i].top-8);
    }
    vdp1_cmdt_end_set(commands+count);list->count=(uint16_t)(count+1);
    vdp1_sync_cmdt_list_put(list,0);vdp1_sync_render();vdp1_sync();vdp2_sync();
}

bool fzx_title_run(volatile const uint32_t *fields,vdp1_cmdt_t *commands,unsigned capacity,
                   void *scratch,uint32_t scratch_bytes) {
    fzx_title_status=10;
    if(!fields||!commands||capacity<7||!scratch)return false;
    vdp1_vram_partitions_t partition;vdp1_vram_partitions_get(&partition);
    if(FZX_TITLE_BYTES+64u>partition.texture_size){fzx_title_status=3;return false;}
    volatile uint32_t *before=(volatile uint32_t *)partition.texture_base;
    void *pixels=(void *)((uintptr_t)partition.texture_base+32u);
    volatile uint32_t *after=(volatile uint32_t *)((uintptr_t)pixels+FZX_TITLE_BYTES);
    for(unsigned i=0;i<8;++i)before[i]=after[i]=GUARD;
    if(!fzx_texture_cd_load("TITLE.PAK",FZX_TITLE_BYTES,FZX_TITLE_HASH,pixels,scratch,scratch_bytes)) {
        fzx_title_status=4;return false;
    }
    for(unsigned i=0;i<8;++i)if(before[i]!=GUARD||after[i]!=GUARD){fzx_title_status=5;return false;}
    fzx_title_reset(&state);
    vdp1_cmdt_list_t list={.cmdts=commands};
    const uint32_t hz=vdp2_tvmd_tv_standard_get()==VDP2_TVMD_TV_STANDARD_PAL?50u:60u;
    uint32_t previous=*fields,remainder=0;
    vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3,0x1fffe),RGB1555(1,0,0,0));
    fzx_title_status=1;
    while(!state.advance) {
        vdp1_sync_wait();vdp2_sync_wait();
        if(REQUEST(fzx_title_debug_reset)){fzx_title_reset(&state);REQUEST(fzx_title_debug_reset)=0;remainder=0;previous=*fields;}
        bool connected;const uint16_t buttons=read_buttons(&connected);
        const uint32_t now=*fields;uint32_t elapsed=now-previous;previous=now;
        if(elapsed>5)elapsed=5;
        remainder+=elapsed*60u;
        while(remainder>=hz) {
            const uint32_t limit=REQUEST(fzx_title_debug_limit);
            if((!limit||state.ticks<limit)&&!fzx_title_step(&state,buttons,connected,false)){fzx_title_status=6;return false;}
            remainder-=hz;
        }
        fzx_title_report[0]=0x465a5854u;fzx_title_report[1]=state.ticks;fzx_title_report[2]=(uint32_t)state.timer;
        fzx_title_report[3]=state.advance;fzx_title_report[4]=state.pressed;fzx_title_report[5]=state.cue;
        fzx_title_report[6]=FZX_TITLE_HASH;fzx_title_report[7]=fzx_title_push_start_visible(&state)?1u:0u;
        draw(commands,pixels,&list,false);
    }
    vdp1_sync_wait();vdp2_sync_wait();
    for(unsigned i=0;i<8;++i)if(before[i]!=GUARD||after[i]!=GUARD){fzx_title_status=5;return false;}
    fzx_title_status=2;
    for(;;) {
        bool connected;const uint16_t buttons=read_buttons(&connected);
        if(!(buttons&0x9000u))break;
        vdp2_sync();vdp2_sync_wait();
    }
    /* Both VDP1 framebuffers must be cleared before title VRAM is reused. */
    for(unsigned i=0;i<2;++i) {
        draw(commands,pixels,&list,true);vdp1_sync_wait();vdp2_sync_wait();
    }
    vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3,0x1fffe),RGB1555(1,1,2,6));
    return true;
}
