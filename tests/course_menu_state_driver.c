#include "fzx_course_menu.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if(!(c)){fprintf(stderr,"course menu check line %u\n",(unsigned)__LINE__);return 1;} } while(0)
#define A 0x8000u
#define B 0x4000u
#define START 0x1000u
#define UP 0x0800u
#define DOWN 0x0400u
#define LEFT 0x0200u
#define RIGHT 0x0100u
#define CUP 0x0008u
#define CDOWN 0x0004u
static uint32_t timeline_hash=2166136261u,timeline_count;
static void capture(const fzx_course_menu_state_t *s) {
    /* render_step clears the whole snapshot, including struct padding. */
    const unsigned char *bytes=(const unsigned char *)&s->snapshot;
    for(unsigned i=0;i<sizeof(s->snapshot);++i)timeline_hash=(timeline_hash^bytes[i])*16777619u;
    ++timeline_count;
}
static bool reset_capture(fzx_course_menu_state_t *s,const fzx_course_menu_config_t *c) {
    const bool ok=fzx_course_menu_reset(s,c);if(ok)capture(s);return ok;
}
static bool step_capture(fzx_course_menu_state_t *s,uint16_t buttons,uint8_t connected,bool transition) {
    const bool ok=fzx_course_menu_step(s,buttons,connected,transition);if(ok)capture(s);return ok;
}
#define fzx_course_menu_reset reset_capture
#define fzx_course_menu_step step_capture
static void config(fzx_course_menu_config_t *c,unsigned mode) {
    memset(c,0,sizeof(*c));c->mode=(uint8_t)mode;c->ghost_type=1;
}
static int idle(fzx_course_menu_state_t *s,unsigned count) {
    while(count--)if(!fzx_course_menu_step(s,0,1,false))return 0;
    return 1;
}
static int press(fzx_course_menu_state_t *s,uint16_t button) {
    return fzx_course_menu_step(s,0,1,false) && fzx_course_menu_step(s,button,1,false);
}
static const fzx_course_menu_draw_t *image(const fzx_course_menu_snapshot_t *p,unsigned id) {
    for(unsigned i=0;i<p->count;++i)if(p->draws[i].kind==FZX_COURSE_DRAW_IMAGE && p->draws[i].image==id)return &p->draws[i];
    return NULL;
}
static const fzx_course_menu_object_snapshot_t *object(const fzx_course_menu_snapshot_t *p,unsigned kind) {
    for(unsigned i=0;i<p->object_count;++i)if(p->objects[i].kind==kind)return &p->objects[i];
    return NULL;
}
static uint32_t digest(const fzx_course_menu_snapshot_t *p) {
    uint32_t h=2166136261u;
    for(unsigned i=0;i<p->count;++i) {
        const fzx_course_menu_draw_t *d=&p->draws[i];
        const uint32_t words[]={d->image,d->kind,d->font,(uint32_t)(int32_t)d->x,(uint32_t)(int32_t)d->y,
            d->scale_x_q8,d->scale_y_q8,d->r,d->g,d->b,d->a,d->flip_x};
        for(unsigned j=0;j<sizeof(words)/sizeof(words[0]);++j)h=(h^words[j])*16777619u;
    }
    return h;
}
static void show(const char *label,const fzx_course_menu_state_t *s) {
    const fzx_course_menu_snapshot_t *p=&s->snapshot;
    printf("%s tick=%u phase=%u cup=%u course=%u track=%u ghost=%u/%u request=%u draws=%u hash=%08x orbit=%u yaw=%.1f pitch=%.3f\n",
           label,p->tick,p->phase,p->cup_option,p->course,p->track,p->ghost_option,p->unlocked_ghosts,p->request,
           p->count,digest(p),p->camera_orbit_ticks,(double)p->camera_yaw_degrees,(double)p->camera_pitch_degrees);
}
int main(void) {
    fzx_course_menu_config_t c;
    fzx_course_menu_state_t s,other;
    fzx_course_menu_snapshot_t p;
    config(&c,1);CHECK(fzx_course_menu_reset(&s,&c));
    CHECK(s.phase==0 && s.cup_option==0 && s.course==0 && s.selected_ghost==0 && s.ghost_type==1);
    CHECK(s.snapshot.count==8 && s.snapshot.object_count==11);
    CHECK(image(&s.snapshot,FZX_COURSE_IMAGE_BACKGROUND)->x==8);
    CHECK(image(&s.snapshot,FZX_COURSE_IMAGE_BACKGROUND)->r==75 && image(&s.snapshot,FZX_COURSE_IMAGE_BACKGROUND)->a==180);
    CHECK(image(&s.snapshot,FZX_COURSE_IMAGE_HEADER)->x==80 && image(&s.snapshot,FZX_COURSE_IMAGE_HEADER)->y==198);
    CHECK(image(&s.snapshot,FZX_COURSE_IMAGE_HEADER)->r==250 && image(&s.snapshot,FZX_COURSE_IMAGE_HEADER)->b==0);
    CHECK(!s.snapshot.preview_visible && s.snapshot.preview_updates==1 && s.snapshot.preview_loads==1);
    CHECK(!s.snapshot.camera_updates && s.snapshot.camera_pitch_degrees==20.0f);
    const fzx_course_menu_state_t initial=s;
    for(unsigned i=0;i<20;++i)CHECK(fzx_course_menu_snapshot(&s,&p));
    CHECK(!memcmp(&s,&initial,sizeof(s)));
    config(&c,0);c.unlock_level=2;c.cup_cleared[0]=4;
    CHECK(fzx_course_menu_reset(&other,&c));CHECK(press(&other,RIGHT));
    CHECK(!memcmp(&s,&initial,sizeof(s)));show("entry",&s);
    CHECK(idle(&s,16));
    CHECK(object(&s.snapshot,FZX_COURSE_OBJECT_CUP_0)->x==75);
    CHECK(object(&s.snapshot,FZX_COURSE_OBJECT_CUP_1)->x==128);
    CHECK(object(&s.snapshot,FZX_COURSE_OBJECT_CUP_2)->x==181);
    CHECK(idle(&s,18));
    CHECK(object(&s.snapshot,FZX_COURSE_OBJECT_CUP_0)->x==54);
    CHECK(object(&s.snapshot,FZX_COURSE_OBJECT_CUP_2)->x==202);
    CHECK(press(&s,RIGHT) && s.cup_option==1 && s.course==6 && s.cues[0]==30);
    CHECK(press(&s,RIGHT) && s.cup_option==2 && s.course==12);
    CHECK(press(&s,RIGHT) && s.cup_option==2);
    CHECK(press(&s,LEFT) && s.cup_option==1);CHECK(press(&s,LEFT) && s.cup_option==0);
    CHECK(fzx_course_menu_step(&s,A,1,false) && s.phase==2 && s.cues[0]==33);
    CHECK(s.snapshot.preview_visible && s.snapshot.carousel_x==299);
    unsigned preview=FZX_COURSE_MENU_DRAW_MAX,first_glyph=FZX_COURSE_MENU_DRAW_MAX;
    for(unsigned i=0;i<s.snapshot.count;++i) {
        const fzx_course_menu_draw_t *d=&s.snapshot.draws[i];
        if(d->kind==FZX_COURSE_DRAW_PREVIEW)preview=i;
        if(d->kind==FZX_COURSE_DRAW_GLYPH && first_glyph==FZX_COURSE_MENU_DRAW_MAX)first_glyph=i;
    }
    CHECK(preview<s.snapshot.count && first_glyph>preview);
    for(unsigned i=0;i<preview;++i)CHECK(s.snapshot.draws[i].kind==FZX_COURSE_DRAW_IMAGE);
    const fzx_course_menu_draw_t *prefix=&s.snapshot.draws[first_glyph];
    CHECK(prefix->image==33 && prefix->font==3 && prefix->x==99 && prefix->y==184);
    CHECK(prefix->r==192 && prefix->g==64 && prefix->b==64);
    CHECK(s.snapshot.draws[first_glyph+2].image==12 && s.snapshot.draws[first_glyph+2].x==125);
    bool subtitle=false;
    for(unsigned i=first_glyph;i<s.snapshot.count;++i) {
        const fzx_course_menu_draw_t *d=&s.snapshot.draws[i];
        if(d->kind==FZX_COURSE_DRAW_GLYPH && d->font==4){CHECK(d->x==127 && d->y==202 && d->image==5);subtitle=true;break;}
    }
    CHECK(subtitle);show("jack-track1",&s);
    /* A remains edge-only, including controller retrigger ticks. */
    for(unsigned i=0;i<30;++i)CHECK(fzx_course_menu_step(&s,A,1,false) && s.phase==2);
    CHECK(press(&s,RIGHT) && s.track==1 && s.course==1 && s.snapshot.camera_disable_requested);
    CHECK(press(&s,RIGHT) && s.track==2);CHECK(press(&s,RIGHT) && s.track==3);
    CHECK(press(&s,RIGHT) && s.track==4);CHECK(press(&s,RIGHT) && s.track==5 && s.course==5);
    CHECK(press(&s,RIGHT) && s.track==5);CHECK(idle(&s,40));
    CHECK(s.snapshot.carousel_x==-6400 && object(&s.snapshot,FZX_COURSE_OBJECT_ARROWS)->y==100);
    CHECK(press(&s,LEFT) && s.track==4);
    CHECK(press(&s,B) && s.phase==0 && s.track==4);
    CHECK(press(&s,RIGHT) && s.cup_option==1 && s.track==0 && s.course==6);
    CHECK(press(&s,A) && s.phase==2);CHECK(press(&s,A) && s.phase==3);
    const float yaw=s.camera_yaw_degrees;
    CHECK(idle(&s,10) && s.camera_yaw_degrees==yaw && s.phase==3);
    CHECK(press(&s,B) && s.phase==2 && !s.request);
    CHECK(press(&s,A) && s.phase==3);CHECK(press(&s,START) && s.phase==4);
    CHECK(s.request==FZX_COURSE_REQUEST_MACHINE && s.last_course==6 && s.course==6);
    const fzx_course_menu_state_t done=s;
    CHECK(fzx_course_menu_step(&s,B,1,false) && !memcmp(&s,&done,sizeof(s)));show("machine",&s);
    /* All cup/track limits, records and distinct destinations. */
    for(unsigned mode=0;mode<6;++mode) {
        if(mode==4)continue;
        config(&c,mode);c.unlock_level=2;CHECK(fzx_course_menu_reset(&s,&c));
        for(unsigned i=0;i<6;++i)CHECK(press(&s,RIGHT));
        CHECK(s.cup_option==(mode==1?3:4));
        CHECK(s.course==(mode==1?18:48));
        CHECK(press(&s,A) && s.phase==(mode==1?2:3));
        if(mode==1)CHECK(press(&s,A) && s.phase==3);
        CHECK(press(&s,A) && s.request==FZX_COURSE_REQUEST_MACHINE);
    }
    config(&c,0);CHECK(fzx_course_menu_reset(&s,&c));
    CHECK(press(&s,A) && s.phase==3 && s.track==0);CHECK(press(&s,B) && s.phase==0);
    config(&c,1);c.records=1;c.unlock_level=3;CHECK(fzx_course_menu_reset(&s,&c));
    CHECK(image(&s.snapshot,FZX_COURSE_IMAGE_RECORDS)->x==112 && image(&s.snapshot,FZX_COURSE_IMAGE_RECORDS)->y==21);
    CHECK(image(&s.snapshot,FZX_COURSE_IMAGE_BACKGROUND)->r==135 && image(&s.snapshot,FZX_COURSE_IMAGE_BACKGROUND)->b==255);
    CHECK(!image(&s.snapshot,FZX_COURSE_IMAGE_WITH_GHOST));
    for(unsigned i=0;i<5;++i)CHECK(press(&s,RIGHT));
    CHECK(s.cup_option==3);
    CHECK(press(&s,A) && s.phase==2);CHECK(press(&s,RIGHT) && s.course==19);
    CHECK(press(&s,A) && s.phase==3);CHECK(press(&s,A) && s.request==FZX_COURSE_REQUEST_RECORDS && s.last_records==19);
    CHECK(fzx_course_menu_reset(&s,&c));CHECK(press(&s,B) && s.request==FZX_COURSE_REQUEST_RECORDS_EXIT);
    config(&c,1);CHECK(fzx_course_menu_reset(&s,&c));CHECK(idle(&s,15));
    CHECK(press(&s,B) && s.phase==5 && !s.request);
    CHECK(idle(&s,11) && s.phase==5 && !s.request);
    CHECK(idle(&s,1) && s.phase==6 && !s.request);
    CHECK(idle(&s,1) && s.request==FZX_COURSE_REQUEST_MAIN_MENU);show("main-menu",&s);
    CHECK(fzx_course_menu_reset(&s,&c));CHECK(idle(&s,15));CHECK(press(&s,B));
    CHECK(press(&s,A) && s.phase==0 && !s.request);
    /* Source staff threshold is strict; comparison changes one tick after moving. */
    config(&c,1);c.staff_time[0]=100000;c.best_time[0]=115000;
    CHECK(fzx_course_menu_reset(&s,&c) && s.unlocked_ghosts==1 && !s.staff_beaten);
    c.best_time[0]=114999;CHECK(fzx_course_menu_reset(&s,&c) && s.unlocked_ghosts==2 && !s.staff_beaten);
    c.best_time[0]=99999;CHECK(fzx_course_menu_reset(&s,&c) && s.staff_beaten);
    c.player_ghost_valid=1;c.player_ghost_course=0;CHECK(fzx_course_menu_reset(&s,&c));
    CHECK(press(&s,A) && s.phase==2);CHECK(idle(&s,20));
    CHECK(image(&s.snapshot,FZX_COURSE_IMAGE_GHOST_MARKER) && image(&s.snapshot,FZX_COURSE_IMAGE_STAFF_BEATEN));
    CHECK(press(&s,DOWN) && s.selected_ghost==1 && s.ghost_type==0);
    CHECK(press(&s,DOWN) && s.selected_ghost==2 && s.ghost_type==2);
    CHECK(press(&s,DOWN) && s.selected_ghost==2);
    CHECK(press(&s,RIGHT) && s.course==1 && s.unlocked_ghosts==2);
    CHECK(idle(&s,1) && s.unlocked_ghosts==1 && s.selected_ghost==0 && s.ghost_type==1);
    CHECK(idle(&s,30) && !image(&s.snapshot,FZX_COURSE_IMAGE_GHOST_MARKER));
    CHECK(press(&s,UP) && s.selected_ghost==0);
    /* Source reload/clamp behavior, including TA's forbidden X cup. */
    config(&c,1);c.last_course=23;CHECK(fzx_course_menu_reset(&s,&c));
    CHECK(s.cup_option==2 && s.track==5 && s.course==23);
    CHECK(idle(&s,1) && s.course==17);
    c.last_course=48;c.unlock_level=2;CHECK(fzx_course_menu_reset(&s,&c));
    CHECK(s.cup_option==3 && s.track==0 && s.course==48);CHECK(idle(&s,1) && s.course==18);
    /* Fresh input, transitions, disconnected pad and repeat cadence. */
    config(&c,1);c.unlock_level=1;CHECK(fzx_course_menu_reset(&s,&c));
    CHECK(fzx_course_menu_step(&s,RIGHT,1,false) && s.cup_option==1);
    for(unsigned i=0;i<14;++i)CHECK(fzx_course_menu_step(&s,RIGHT,1,false) && s.cup_option==1);
    CHECK(fzx_course_menu_step(&s,RIGHT,1,false) && s.cup_option==2);
    for(unsigned i=0;i<4;++i)CHECK(fzx_course_menu_step(&s,RIGHT,1,false) && s.cup_option==2);
    CHECK(fzx_course_menu_step(&s,RIGHT,1,false) && s.cup_option==3);
    CHECK(fzx_course_menu_reset(&s,&c));CHECK(fzx_course_menu_step(&s,A,1,true) && s.phase==0);
    CHECK(fzx_course_menu_step(&s,A,1,false) && s.phase==0);CHECK(press(&s,A) && s.phase==2);
    CHECK(fzx_course_menu_reset(&s,&c));CHECK(fzx_course_menu_step(&s,A,0,false) && s.phase==0);
    CHECK(fzx_course_menu_step(&s,START,1,false) && s.phase==2);
    /* Camera target update occurs before the course state and keeps source clamps. */
    CHECK(fzx_course_menu_reset(&s,&c));CHECK(idle(&s,1));
    CHECK(s.camera_updates==1 && !s.camera_orbit_ticks && s.camera_yaw_degrees==0.0f);
    CHECK(fzx_course_menu_step(&s,CUP,1,false) && s.camera_orbit_ticks==1 && s.camera_yaw_degrees==1.0f);
    CHECK(s.camera_pitch_degrees>20.09f && s.camera_pitch_degrees<20.11f);
    for(unsigned i=0;i<800;++i)CHECK(fzx_course_menu_step(&s,CUP,1,false));
    CHECK(s.camera_pitch_degrees==90.0f);
    for(unsigned i=0;i<1500;++i)CHECK(fzx_course_menu_step(&s,CDOWN,1,false));
    CHECK(s.camera_pitch_degrees==-50.0f);
    CHECK(press(&s,A));CHECK(press(&s,RIGHT));CHECK(idle(&s,1));CHECK(s.camera_pitch_degrees==20.0f);
    CHECK(idle(&s,10000));CHECK(s.camera_yaw_degrees==(float)(s.camera_orbit_ticks%360));show("soak",&s);
    /* Every source static-course string and ghost row remains bounded. */
    for(unsigned course=0;course<24;++course) {
        config(&c,1);c.unlock_level=2;c.last_course=(uint8_t)course;
        CHECK(fzx_course_menu_reset(&s,&c));CHECK(press(&s,A));CHECK(idle(&s,15));
        CHECK(s.course==(int32_t)course && s.snapshot.count<FZX_COURSE_MENU_DRAW_MAX);
        CHECK(s.snapshot.preview_visible);show("course",&s);
    }
    /* Rejection is atomic, including aliased config reset and corrupt state. */
    config(&c,1);c.frame=UINT32_MAX;CHECK(fzx_course_menu_reset(&s,&c));CHECK(idle(&s,1) && s.frame==0);
    CHECK(fzx_course_menu_reset(&s,&s.config));
    const fzx_course_menu_state_t before=s;
    CHECK(!fzx_course_menu_step(&s,0,5,false) && !memcmp(&s,&before,sizeof(s)));
    c.mode=4;CHECK(!fzx_course_menu_reset(&s,&c) && !memcmp(&s,&before,sizeof(s)));
    s.objects[0].left=INT32_MIN;const fzx_course_menu_state_t bad=s;
    CHECK(!fzx_course_menu_step(&s,0,1,false) && !memcmp(&s,&bad,sizeof(s)));
    CHECK(!fzx_course_menu_snapshot(&s,&p));CHECK(!fzx_course_menu_snapshot(NULL,&p));
    CHECK(!fzx_course_menu_reset(NULL,&c));CHECK(!fzx_course_menu_step(NULL,0,1,false));
    printf("timeline snapshots=%u hash=%08x\n",timeline_count,timeline_hash);
    puts("source course menu: original navigation, draws, ghosts, camera targets, exits and bounds pass");
    return 0;
}
