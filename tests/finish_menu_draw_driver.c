#include "fzx_finish_menu_draw.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    unsigned rows=0,images=0,glyphs=0;
    uint32_t digest=2166136261u;
    for(unsigned frame=0;frame<120;frame++)for(int sel=0;sel<6;sel++) {
        if(sel==1)continue;
        for(int reveal=0;reveal<60;reveal++) {
            fzx_finish_menu_snapshot_t s={0},before;
            fzx_finish_menu_draw_snapshot_t a,b;
            s.tick=1;s.input.frame=frame;s.draw_selection=sel;s.draw_reveal=reveal;
            before=s;
            assert(fzx_finish_menu_draw_capture(&s,&a));
            assert(!memcmp(&s,&before,sizeof(s)));
            assert(fzx_finish_menu_draw_capture(&s,&b));
            assert(!memcmp(&a,&b,sizeof(a)));
            const unsigned char *bytes=(const unsigned char *)&a;
            for(unsigned i=0;i<sizeof(a);i++)digest=(digest^bytes[i])*16777619u;
            assert(a.tick==1&&a.frame==frame&&a.count<=FZX_FINISH_MENU_DRAW_MAX);
            if(reveal>=47)assert(a.count==0);
            unsigned labels=0,arrows=0;
            for(unsigned i=0;i<a.count;i++) {
                const fzx_results_draw_t *p=&a.draws[i];
                assert(p->clip_x==205+reveal&&p->clip_y==132+reveal);
                assert(p->clip_x2==305-reveal&&p->clip_y2==225-reveal);
                if(p->kind==FZX_RESULTS_IMAGE) {
                    labels++;images++;
                    assert(p->x==230&&p->y>=140&&p->y<=200&&(p->y-140)%12==0);
                    int option=(p->y-140)/12;
                    static const uint16_t label_ids[6]={0,5,1,3,4,2};
                    assert(p->image==label_ids[option]);
                    if(option==1)assert(p->r==128&&p->g==128&&p->b==128);
                    else if(option!=sel)assert(p->r==255&&p->g==255&&p->b==255);
                    assert(p->a==255);
                } else if(p->kind==FZX_RESULTS_GLYPH) {
                    arrows++;glyphs++;
                    assert(p->image==126&&p->w==16&&p->h==16);
                    assert(p->x2-p->x==12&&p->y2-p->y==12);
                } else assert(p->kind==FZX_RESULTS_RECT);
            }
            if(!reveal)assert(labels==6&&arrows==1);
            rows++;
        }
    }
    fzx_finish_menu_snapshot_t bad={0};
    fzx_finish_menu_draw_snapshot_t output,saved;
    memset(&output,0x5a,sizeof(output));saved=output;
    assert(!fzx_finish_menu_draw_capture(&bad,&output));
    assert(!memcmp(&output,&saved,sizeof(output)));
    bad.draw_selection=0;bad.draw_reveal=60;
    assert(!fzx_finish_menu_draw_capture(&bad,&output));
    assert(!memcmp(&output,&saved,sizeof(output)));
    bad.draw_reveal=-1;
    assert(!fzx_finish_menu_draw_capture(&bad,&output));
    assert(!memcmp(&output,&saved,sizeof(output)));
    assert(!fzx_finish_menu_draw_capture(NULL,&output));
    assert(!fzx_finish_menu_draw_capture(&bad,NULL));
    bad.tick=1;bad.draw_selection=1;
    assert(!fzx_finish_menu_draw_capture(&bad,&output));
    assert(!memcmp(&output,&saved,sizeof(output)));
    printf("finish draw rows=%u images=%u glyphs=%u digest=%08x\n",rows,images,glyphs,digest);
    return 0;
}
