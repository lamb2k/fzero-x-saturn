#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "results_native.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u\n",__LINE__);return 1;}}while(0)

/* Independent synthetic geometry producer. Portable projection is tested by
 * test_finish_draw; this fixture isolates the adapter's VDP1 API contract. */
bool fzx_finish_image(const fzx_finish_pack_t *p,unsigned i,fzx_finish_image_t *o) {
    if(!p||i!=7||!o)return false;
    *o=(fzx_finish_image_t){.texture=128,.width=16,.height=16,.triangle=1};return true;
}
bool fzx_finish_draw(const fzx_finish_pack_t *p,const sh2z_view_t *v,fzx_finish_work_t *w,
                     fzx_finish_emit_t emit,void *context,unsigned *count) {
    if(!p||!v||!w||!emit||!count)return false;
    *count=0;
    for(unsigned i=0;i<2;++i){
        fzx_finish_quad_t q={.x={20,40,25,25},.y={30,30,60,60},.material=(uint16_t)i,.image=7};
        if(!emit(context,&q))return false;
        ++*count;
    }
    return true;
}
int main(void) {
    void *mapped=mmap((void *)0x25c00000u,0x80000u,PROT_READ|PROT_WRITE,
                       MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    CHECK(mapped!=MAP_FAILED);
    uint8_t *pixels=(uint8_t *)mapped+0x10000,*palettes=pixels+512,*tints=palettes+6144;
    struct {vdp1_cmdt_t c[8];uint32_t guard;} commands;
    memset(&commands,0,sizeof(commands));commands.guard=0xab32e917u;
    fzx_results_snapshot_t f={0};f.count=3;
    for(unsigned i=0;i<3;++i)f.draws[i]=(fzx_results_draw_t){.x=20,.y=30,.x2=24,.y2=32,
        .sx=2,.sy=2,.w=4,.h=2,.clip_x=21,.clip_y=31,.clip_x2=320,.clip_y2=240,
        .image=0,.kind=FZX_RESULTS_IMAGE,.r=255,.g=255,.b=255,.a=255};
    f.draws[0].image=3;f.draws[0].g=f.draws[0].b=102;
    f.draws[1].image=0;f.draws[1].kind=FZX_RESULTS_GLYPH;f.draws[1].r=128;
    f.draws[2].kind=FZX_RESULTS_RECT;f.draws[2].a=128;
    unsigned n=99;
    CHECK(fzx_results_ui_native(commands.c,6,&f,pixels,palettes,6144,tints,6,&n)&&n==6);
    CHECK(commands.guard==0xab32e917u);
    for(unsigned i=0;i<6;i+=2)CHECK(commands.c[i].kind==8&&commands.c[i].cmd_xa==21&&commands.c[i].cmd_ya==23&&commands.c[i].cmd_xc==23&&commands.c[i].cmd_yc==23);
    CHECK(commands.c[1].kind==0&&commands.c[1].texture==(uintptr_t)tints+32&&commands.c[1].width==8&&commands.c[1].height==2);
    CHECK(commands.c[1].cmd_xa==18&&commands.c[1].cmd_ya==22&&commands.c[1].mode.user_clipping_mode==2);
    CHECK(commands.c[1].mode.end_code_disable==1&&commands.c[1].mode.trans_pixel_disable==0&&commands.c[1].mode.color_mode==5);
    CHECK(commands.c[3].mode.color_mode==1&&commands.c[3].texture==(uintptr_t)pixels+72);
    CHECK(commands.c[3].cmd_colr==((((uintptr_t)palettes+32)&0x7ffffu)>>3));
    const uint16_t *clut=(const uint16_t *)(palettes+32);
    CHECK(clut[0]==0&&clut[1]==0xffefu);
    CHECK(commands.c[5].kind==4&&commands.c[5].mode.trans_pixel_disable==1&&commands.c[5].mode.cc_mode==3);
    CHECK(commands.c[5].cmd_xa==21&&commands.c[5].cmd_xb==23&&commands.c[5].cmd_ya==23&&commands.c[5].cmd_yd==23);
    CHECK(!fzx_results_ui_native(commands.c,5,&f,pixels,palettes,6144,tints,6,&n)&&n==0);
    CHECK(!fzx_results_ui_native(commands.c,6,&f,pixels,pixels,6144,tints,6,&n)&&n==0);
    CHECK(!fzx_results_ui_native(commands.c,6,&f,pixels,palettes,6144,pixels,6,&n)&&n==0);
    CHECK(!fzx_results_ui_native(commands.c,6,&f,pixels+1,palettes,6144,tints,6,&n)&&n==0);
    CHECK(!fzx_results_ui_native(commands.c,6,&f,pixels,palettes,6143,tints,6,&n)&&n==0);
    CHECK(!fzx_results_ui_native(commands.c,6,&f,pixels,palettes,6144,tints,7,&n)&&n==0);
    f.draws[0].r=254;
    CHECK(!fzx_results_ui_native(commands.c,6,&f,pixels,palettes,6144,tints,6,&n)&&n==0);
    fzx_finish_pack_t p={.texture_bytes=512,.palette_at=0,.solid_color=0x8421};sh2z_view_t v={0};fzx_finish_work_t work={0};
    CHECK(fzx_finish_native(commands.c,3,&p,&v,&work,pixels,&n)&&n==3);
    CHECK(commands.c[0].kind==8&&commands.c[0].cmd_xc==319&&commands.c[0].cmd_yc==223);
    CHECK(commands.c[1].kind==4&&commands.c[1].cmd_colr==0x8421&&commands.c[1].mode.color_mode==5);
    CHECK(commands.c[2].kind==2&&commands.c[2].texture==(uintptr_t)pixels+128&&commands.c[2].width==16&&commands.c[2].height==16);
    CHECK(commands.c[2].mode.color_mode==1&&commands.c[2].mode.end_code_disable==1&&commands.c[2].mode.trans_pixel_disable==1);
    CHECK(commands.c[2].cmd_colr==(((uintptr_t)pixels&0x7ffffu)>>3));
    CHECK(commands.c[2].cmd_xa==20&&commands.c[2].cmd_xb==40&&commands.c[2].cmd_xc==25&&commands.c[2].cmd_xd==25);
    CHECK(!fzx_finish_native(commands.c,2,&p,&v,&work,pixels,&n)&&n==0);
    CHECK(!fzx_finish_native(commands.c,0,&p,&v,&work,pixels,&n)&&n==0);
    CHECK(!fzx_finish_native(commands.c,3,&p,&v,&work,pixels+1,&n)&&n==0);
    CHECK(commands.guard==0xab32e917u);
    puts("NATIVE_RESULTS_API_AND_REJECTIONS_PASS");return 0;
}
