#include "fzx_results_draw.h"
#include <stdio.h>
#include <string.h>

static fzx_results_sprite_t received[8];static unsigned n,limit=8;
static bool emit(void *ctx,const fzx_results_sprite_t *s) {
    (void)ctx;if(n>=limit)return false;received[n++]=*s;return true;
}
static bool run(fzx_results_snapshot_t *f,fzx_results_image_t *images,unsigned *count) {
    static const uint16_t font[2][2]={{1,12},{1,14}};
    n=0;return fzx_results_ui(f,images,2,font,2,5248,emit,NULL,count);
}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d\n",__LINE__);return 1;}}while(0)
int main(void) {
    fzx_results_image_t images[2]={{.offset=0,.bytes=5120,.width=16,.height=160,.source_width=12},
        {.offset=5120,.bytes=128,.width=16,.height=16,.source_width=16,.clut=1}};
    fzx_results_snapshot_t f={0};f.count=3;
    f.draws[0]=(fzx_results_draw_t){.x=20,.y=6,.x2=30,.y2=22,.sx=2,.sy=144,.w=10,.h=16,
        .clip_x=0,.clip_y=0,.clip_x2=320,.clip_y2=240,.kind=FZX_RESULTS_IMAGE,.r=255,.g=255,.b=255,.a=255};
    f.draws[1]=(fzx_results_draw_t){.x=310,.y=228,.x2=326,.y2=244,.w=16,.h=16,
        .clip_x=0,.clip_y=0,.clip_x2=320,.clip_y2=240,.image=1,.kind=FZX_RESULTS_GLYPH,.g=128,.a=128};
    f.draws[2]=(fzx_results_draw_t){.x=-12,.y=23,.x2=24,.y2=24,.w=36,.h=1,
        .clip_x=7,.clip_y=2,.clip_x2=23,.clip_y2=230,.kind=FZX_RESULTS_RECT,.b=255,.a=255};
    unsigned count=999;CHECK(run(&f,images,&count)&&count==3&&n==3);
    CHECK(received[0].offset==4608&&received[0].width==16&&received[0].height==16&&received[0].x==18&&received[0].y==-2);
    CHECK(received[0].clip[0]==20&&received[0].clip[1]==0&&received[0].clip[2]==30&&received[0].clip[3]==14);
    CHECK(received[1].image==1&&received[1].offset==5120&&received[1].clut==1&&received[1].g==128);
    CHECK(received[1].clip[0]==310&&received[1].clip[1]==220&&received[1].clip[2]==320&&received[1].clip[3]==224);
    CHECK(received[2].clip[0]==7&&received[2].clip[1]==15&&received[2].clip[2]==23&&received[2].clip[3]==16);
    /* Independently solve each visible pixel's source coordinate. Whole-row
     * placement + userclip must match the original cropped source rectangle. */
    for(unsigned i=0;i<2;++i)for(int y=received[i].clip[1];y<received[i].clip[3];++y)
        for(int x=received[i].clip[0];x<received[i].clip[2];++x) {
            const fzx_results_draw_t *q=&f.draws[i];const fzx_results_sprite_t *s=&received[i];
            CHECK(x-s->x==q->sx+x-q->x);
            CHECK(q->sy+y-s->y==q->sy+y+8-q->y);
        }
    const fzx_results_snapshot_t good=f;
    f.draws[0].a=0;CHECK(run(&f,images,&count)&&count==2);f=good;
    f.draws[0].clip_x=31;CHECK(run(&f,images,&count)&&count==2);f=good;
    limit=1;CHECK(!run(&f,images,&count));limit=8;
    f.draws[0].sx=3;CHECK(!run(&f,images,&count));f=good;
    f.draws[0].sy=145;CHECK(!run(&f,images,&count));f=good;
    f.draws[0].x2++;CHECK(!run(&f,images,&count));f=good;
    f.draws[0].clip_y2=241;CHECK(!run(&f,images,&count));f=good;
    f.draws[1].image=2;CHECK(!run(&f,images,&count));f=good;
    images[1].offset++;CHECK(!run(&f,images,&count));images[1].offset--;
    images[1].width=8;CHECK(!run(&f,images,&count));images[1].width=16;
    images[1].bytes=129;CHECK(!run(&f,images,&count));images[1].bytes=128;
    f.count=FZX_RESULTS_DRAW_MAX+1;CHECK(!run(&f,images,&count));
    puts("UI_CROP_PIXELS_AND_REJECTIONS_PASS");return 0;
}
