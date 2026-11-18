#define _GNU_SOURCE
#include "finish_menu_native.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
static fzx_results_draw_t label(void) {
    return (fzx_results_draw_t){.x=230,.y=140,.x2=238,.y2=148,.w=8,.h=8,
        .clip_x=205,.clip_y=132,.clip_x2=305,.clip_y2=225,
        .kind=FZX_RESULTS_IMAGE,.r=255,.g=255,.b=255,.a=255};
}
int main(void) {
    void *v=mmap((void *)0x25c00000u,0x80000u,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(v!=MAP_FAILED);
    void *pixels=(uint8_t *)v+0x1000,*cluts=(uint8_t *)v+0x2000;
    fzx_finish_menu_draw_snapshot_t f={.tick=3,.frame=8000,.count=4};
    for(unsigned i=0;i<4;++i)f.draws[i]=label();
    f.draws[1].r=0;f.draws[1].g=127;f.draws[1].b=255;
    f.draws[2].r=f.draws[2].g=f.draws[2].b=128;
    f.draws[3].kind=FZX_RESULTS_GLYPH;f.draws[3].image=24;
    f.draws[3].x2=236;f.draws[3].y2=146;
    struct {vdp1_cmdt_t c[32];uint32_t guard;} commands={0};commands.guard=0x12345678;
    fzx_finish_menu_palette_t p={0},before;unsigned n;
    assert(fzx_finish_menu_native(commands.c,32,&f,pixels,cluts,&p,&n));
    assert(n==8&&p.used==7&&p.tick==3&&p.frame==8000&&commands.guard==0x12345678);
    for(unsigned i=0;i<3;++i) {
        const vdp1_cmdt_t *c=commands.c+i*2+1;
        assert(c->kind==0&&c->mode.color_mode==4&&c->mode.cc_mode==0);
        assert(c->mode.end_code_disable==1&&c->mode.trans_pixel_disable==0);
        assert(c->cmd_colr==(i+1)*256&&c->cmd_ya==132);
        assert(commands.c[i*2].cmd_xc==237&&commands.c[i*2].cmd_yc==139);
        assert(c->texture==(uintptr_t)pixels&&c->width==8&&c->height==8);
    }
    assert(commands.c[7].kind==2&&commands.c[7].mode.color_mode==1);
    assert(commands.c[7].cmd_xc==235&&commands.c[7].cmd_yc==137);
    assert(commands.c[7].cmd_colr==0x400);
    assert(((uint16_t *)cluts)[1]==0xffff&&((uint16_t *)cluts)[2]==0x8000);
    assert(p.words[0][1]==0xffff&&p.words[0][2]==0x8000);
    assert(p.words[1][1]==(0x8000|(15<<5)|(31<<10)));
    assert(p.words[2][1]==(0x8000|15|(15<<5)|(15<<10)));
    uint16_t cram[2048];for(unsigned i=0;i<2048;++i)cram[i]=0x6b5a;
    assert(fzx_finish_menu_palette_upload(&p,cram,1024));
    for(unsigned i=0;i<2048;++i) {
        unsigned bank=i/256;
        if(bank>=1&&bank<=3&&i%256<32)assert(cram[i]==p.words[bank-1][i%256]);
        else assert(cram[i]==0x6b5a);
    }
    assert(!fzx_finish_menu_palette_upload(&p,cram,1023));
    before=p;
    assert(!fzx_finish_menu_native(commands.c,7,&f,pixels,cluts,&p,&n));
    assert(!n&&!memcmp(&p,&before,sizeof(p)));
    assert(!fzx_finish_menu_native(commands.c,32,&f,pixels,pixels,&p,&n));
    f.draws[3]=f.draws[1];f.draws[3].r=200;
    assert(!fzx_finish_menu_native(commands.c,32,&f,pixels,cluts,&p,&n));
    assert(!n&&!memcmp(&p,&before,sizeof(p)));
    f.count=1;f.draws[0].a=128;
    assert(!fzx_finish_menu_native(commands.c,32,&f,pixels,cluts,&p,&n));
    f.draws[0].kind=FZX_RESULTS_RECT;
    assert(fzx_finish_menu_native(commands.c,32,&f,pixels,cluts,&p,&n));
    assert(n==2&&p.used==0&&commands.c[1].mode.trans_pixel_disable==1);
    assert(commands.c[1].mode.cc_mode==3);
    puts("FINISH_MENU_NATIVE_API_PASS");return 0;
}
