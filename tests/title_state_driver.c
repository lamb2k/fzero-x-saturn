#include "fzx_title.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"title check line %u\n",(unsigned)__LINE__);return 1;}}while(0)
int main(void) {
    fzx_title_state_t s;
    fzx_title_reset(&s);
    for(unsigned i=0;i<79;++i)CHECK(fzx_title_step(&s,0,true,false));
    CHECK(s.timer==79 && !s.advance && !fzx_title_push_start_visible(&s));
    CHECK(fzx_title_step(&s,0x1000,true,false));
    CHECK(s.timer==-1 && s.advance && s.cue==62 && s.stop_music && s.unused_random_calls==80);
    const fzx_title_state_t done=s;
    CHECK(fzx_title_step(&s,0,true,false) && !memcmp(&s,&done,sizeof(s)));
    fzx_title_reset(&s);
    for(unsigned i=0;i<78;++i)CHECK(fzx_title_step(&s,0,true,false));
    CHECK(fzx_title_step(&s,0x1000,true,false));
    CHECK(s.timer==79 && !s.advance && !s.cue);
    fzx_title_reset(&s);
    for(unsigned i=0;i<100;++i)CHECK(fzx_title_step(&s,0x8000,false,false));
    CHECK(!s.timer && !s.unused_random_calls && !s.advance && !fzx_title_push_start_visible(&s));
    for(unsigned i=0;i<120;++i)CHECK(fzx_title_step(&s,0x8000,true,false));
    CHECK(s.timer==120 && !s.advance && fzx_title_push_start_visible(&s));
    CHECK(fzx_title_step(&s,0,true,false));
    CHECK(fzx_title_step(&s,0x8000,true,true));
    CHECK(!s.advance && !s.cue);
    CHECK(fzx_title_step(&s,0x8000,true,false) && !s.advance);
    CHECK(fzx_title_step(&s,0,true,false));
    CHECK(fzx_title_step(&s,0x8000,true,false) && s.advance && s.cue==62);
    fzx_title_reset(&s);
    for(unsigned i=0;i<94;++i)CHECK(fzx_title_step(&s,0,true,false));
    CHECK(s.timer==94 && !fzx_title_push_start_visible(&s));
    CHECK(fzx_title_step(&s,0,true,false) && fzx_title_push_start_visible(&s));
    s.timer=INT32_MAX;const fzx_title_state_t invalid=s;
    CHECK(!fzx_title_step(&s,0,true,false) && !memcmp(&s,&invalid,sizeof(s)));
    CHECK(!fzx_title_step(NULL,0,true,false));
    CHECK(!fzx_title_push_start_visible(NULL));
    puts("source title: tick80 accept, tick95 prompt, held/transition/disconnected controls pass");
    return 0;
}
