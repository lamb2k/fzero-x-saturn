#define _GNU_SOURCE
#include "finish_menu_native.h"
#include "finish_menu_identity.h"
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>
int main(void) {
    void *v=mmap((void *)0x25c00000u,0x80000u,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(v!=MAP_FAILED);
    void *pixels=(uint8_t *)v+0x1000,*cluts=(uint8_t *)pixels+FZX_FINISH_MENU_BYTES;
    vdp1_cmdt_t commands[FZX_FINISH_MENU_COMMANDS];unsigned rows=0,labels=0,glyphs=0;
    for(unsigned frame=0;frame<120;++frame)for(int selection=0;selection<6;++selection) {
        if(selection==1)continue;
        for(int reveal=0;reveal<60;++reveal) {
            fzx_finish_menu_snapshot_t state={0};
            state.tick=1;state.input.frame=frame;state.draw_selection=selection;state.draw_reveal=reveal;
            fzx_finish_menu_draw_snapshot_t f;
            assert(fzx_finish_menu_draw_capture(&state,&f));
            fzx_finish_menu_palette_t palette;unsigned n;
            assert(fzx_finish_menu_native(commands,FZX_FINISH_MENU_COMMANDS,&f,pixels,cluts,&palette,&n));
            assert(n==f.count*2&&palette.frame==frame&&palette.tick==1);
            for(unsigned i=0;i<f.count;++i) {
                const fzx_results_draw_t *d=f.draws+i;
                const vdp1_cmdt_t *c=commands+i*2+1;
                if(d->kind==FZX_RESULTS_RECT)continue;
                const fzx_finish_menu_image_t *a=fzx_finish_menu_images+d->image;
                assert(c->texture==(uintptr_t)pixels+a->offset);
                assert(c->width==a->width&&c->height==a->height);
                const uint16_t *colors;
                if(d->kind==FZX_RESULTS_IMAGE) {
                    assert(c->mode.color_mode==4&&c->cmd_colr>=256&&c->cmd_colr<=768);
                    assert(!(c->cmd_colr&255));
                    colors=palette.words[(c->cmd_colr>>8)-1];labels++;
                } else {
                    assert(c->mode.color_mode==1&&c->kind==2);
                    colors=(const uint16_t *)((uintptr_t)v+((uintptr_t)c->cmd_colr<<3));glyphs++;
                    assert(c->cmd_xc==d->x2-1&&c->cmd_yc==d->y2-9);
                }
                for(unsigned j=0;j<a->palette_count;++j) {
                    unsigned value=a->palette[j],expected=0;
                    if(value) {
                        expected=0x8000;
                        const unsigned tint[3]={d->r,d->g,d->b};
                        for(unsigned channel=0;channel<3;++channel)
                            expected|=(((value>>(channel*5))&31)*tint[channel]/255)<<(channel*5);
                    }
                    assert(colors[j]==expected);
                }
            }
            rows++;
        }
    }
    printf("native source rows=%u labels=%u glyphs=%u\n",rows,labels,glyphs);
    return 0;
}
