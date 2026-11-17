#include "fzx_results.h"
#include "results_source_ids.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if(!(c)){fprintf(stderr,"results check line %d: %s\n",__LINE__,#c);return 1;} } while(0)
#ifdef FZX_RESULTS_MATRIX_DRIVER
int main(void) {
    fzx_results_snapshot_t s={0};s.finish.visible=1;s.finish.scale=0.07f;s.finish.z=180;
    Math_SinTableInit();
    uint32_t digest=2166136261u;int32_t words[18],again[18];
    for(int position=0;position<3;++position)for(int angle=0;angle<4096;++angle) {
        s.finish.angle=angle;s.finish.y=(float)(-20*position);s.finish.z=(float)(180+20*position);
        const fzx_results_snapshot_t before=s;
        CHECK(fzx_results_finish_matrix(&s,words));CHECK(fzx_results_finish_matrix(&s,again));
        CHECK(!memcmp(words,again,sizeof(words)) && !memcmp(&s,&before,sizeof(s)));
        Mtx model;MtxF model_f;
        Matrix_SetLockedLookAt(&model,&model_f,s.finish.scale,s.finish.scale,s.finish.scale,
                               0,SIN(angle),-COS(angle),0,COS(angle),SIN(angle),0,s.finish.y,s.finish.z);
        for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col) {
            int32_t expected=source_result_word(&model,row,col);
            if(col==0 || col==2)expected=-expected;
            CHECK(words[row*4+col]==expected);
        }
        CHECK(words[16]>158000 && words[16]<159000 && words[17]>210000 && words[17]<212000);
        if(angle==0)CHECK(words[0]==4588 && words[5]==4588 && words[10]==4588);
        if(angle==1024)CHECK(words[5]==0 && words[6]==-4588 && words[9]==4588 && words[10]==0);
        CHECK(words[12]==0 && words[13]==-20*position*65536 && words[14]==-(180+20*position)*65536);
        for(unsigned i=0;i<18;++i)digest=(digest^(uint32_t)words[i])*16777619u;
    }
    memcpy(again,words,sizeof(words));s.finish.visible=0;
    CHECK(!fzx_results_finish_matrix(&s,words) && !memcmp(words,again,sizeof(words)));
    s.finish.visible=1;s.finish.angle=-1;CHECK(!fzx_results_finish_matrix(&s,words));
    printf("FINISH matrix HOST 12288 source poses digest=%08x pure/Q16/axes PASS\n",digest);
    return 0;
}
#else
static fzx_results_state_t state,other,saved;
static fzx_results_snapshot_t snapshot,second;
static unsigned steps,reads;
static uint32_t digest=2166136261u;
static void hash(const void *data,size_t n) {
    const unsigned char *p=data;
    for(size_t i=0;i<n;++i)digest=(digest^p[i])*16777619u;
}
static fzx_results_config_t config(void) {
    fzx_results_config_t c={0};strcpy(c.name,"TEST COURSE");return c;
}
static fzx_results_input_t input(void) {
    fzx_results_input_t in={0};
    in.camera_mode=CAMERA_MODE_RACE;in.speed=40.0f;in.max_speed=60.0f;
    in.lap_times[0]=37000;in.lap_times[1]=36000;in.lap_times[2]=37639;in.race_time=110639;
    in.engine=0.5078125f;in.connected=1;in.machine.bodyR=32;in.machine.bodyG=96;in.machine.bodyB=255;
    return in;
}
static int advance(fzx_results_state_t *s,fzx_results_input_t *in) {
    ++in->frame;++steps;
    CHECK(fzx_results_step(s,in));
    CHECK(fzx_results_snapshot(s,&snapshot));++reads;
    saved=*s;
    for(unsigned i=0;i<3;++i) {
        CHECK(fzx_results_snapshot(s,&second));++reads;
        CHECK(memcmp(&snapshot,&second,sizeof(snapshot))==0);
        CHECK(memcmp(&saved,s,sizeof(saved))==0);
    }
    return 0;
}
static unsigned events(unsigned kind,unsigned id) {
    unsigned count=0;
    for(unsigned i=0;i<snapshot.event_count;++i)
        count+=(snapshot.events[i].kind==kind && snapshot.events[i].id==id);
    return count;
}
static int packet_check(const fzx_results_snapshot_t *p) {
    CHECK(p->count<=FZX_RESULTS_DRAW_MAX);
    for(unsigned i=0;i<p->count;++i) {
        const fzx_results_draw_t *d=&p->draws[i];
        CHECK(d->kind<=FZX_RESULTS_RECT && d->w>0 && d->h>0);
        CHECK(d->x2-d->x==d->w && d->y2-d->y==d->h);
        CHECK(d->clip_x>=0 && d->clip_y>=0 && d->clip_x2<=320 && d->clip_y2<=240);
        CHECK(d->clip_x<d->clip_x2 && d->clip_y<d->clip_y2);
        CHECK(d->x2>d->clip_x && d->y2>d->clip_y && d->x<d->clip_x2 && d->y<d->clip_y2);
        if(d->kind==FZX_RESULTS_IMAGE)CHECK(d->image<=FZX_RESULTS_MAX_SPEED);
    }
    return 0;
}
static int fresh_defaults(void) {
    fzx_results_config_t original;
    CHECK(fzx_results_default_config(&original) && original.course==0 && original.name[0]);
    CHECK(!fzx_results_default_config(NULL));
    fzx_results_config_t c=config();CHECK(fzx_results_reset(&state,&c,NULL));
    for(unsigned i=0;i<5;++i) {
        CHECK(state.records.timeRecord[i]==MAX_TIMER);
        CHECK(state.records.recordEngines[i]==0.5f);
        CHECK(state.records.recordMachineInfos[i].character==CAPTAIN_FALCON);
        CHECK(state.records.recordMachineInfos[i].customType==CUSTOM_MACHINE_DEFAULT);
        for(unsigned j=0;j<4;++j)CHECK(state.records.recordNames[i][j]==0);
    }
    CHECK(MAX_TIMER==3599999);
    CHECK(state.records.bestTime==MAX_TIMER && state.records.maxSpeed==0.0f);
    CHECK(state.finished_success_time==441 && !state.finish_timer[0] && !state.finish_saved);
    CHECK(fzx_results_snapshot(&state,&snapshot));
    CHECK(snapshot.phase==FZX_RESULTS_RACING && !snapshot.count && !snapshot.finish.visible && !snapshot.event_count);
    return 0;
}
static int finish_timeline(void) {
    CHECK(fresh_defaults()==0);
    fzx_results_input_t in=input();
    for(unsigned i=0;i<7;++i) {
        CHECK(advance(&state,&in)==0);
        CHECK(snapshot.phase==FZX_RESULTS_RACING && !snapshot.count && !snapshot.finish_ticks);
    }
    in.state_flags|=RACER_STATE_FINISHED;in.camera_mode=CAMERA_MODE_FINISHED_SUCCESS;in.record_eligible=1;
    unsigned bgm=0,saves=0,stops=0;float first_rotation=0;
    for(int finish=1;finish<=442;++finish) {
        CHECK(advance(&state,&in)==0);CHECK(snapshot.finish_ticks==finish);
        saves+=events(FZX_RESULTS_SAVE_REQUEST,0);bgm+=events(FZX_RESULTS_SOUND_BGM,BGM_22);
        for(unsigned e=0;e<snapshot.event_count;++e)stops+=snapshot.events[e].kind==FZX_RESULTS_SOUND_LEVEL_STOP;
        CHECK(snapshot.finish.visible==(finish>=2));
        if(finish==1)CHECK(!state.special_timer[0] && !state.menu_flags);
        if(finish==2) {
            first_rotation=snapshot.finish.rotation;
            CHECK(first_rotation==2.0f && snapshot.finish.angle==1303);
            CHECK(snapshot.finish.x==0 && snapshot.finish.y==0 && snapshot.finish.z==180.0f);
            CHECK(snapshot.finish.scale==0.07f && snapshot.finish.timer==0);
            CHECK(state.special_timer[0]==1 && !state.menu_flags);
        }
        if(finish==3)CHECK(state.menu_flags==MENU_STATE_RACE_FINISH_SAVE && !state.finish_saved && !saves);
        if(finish==4) {
            CHECK(state.finish_saved && saves==1 && snapshot.save_pending);
            CHECK(snapshot.record_position==1 && snapshot.max_speed_record && snapshot.best_lap==2);
            CHECK(state.records.timeRecord[0]==110639 && state.records.timeRecord[1]==MAX_TIMER);
            CHECK(state.records.maxSpeed==60.0f && state.records.bestTime==36000);
            CHECK(state.records.recordEngines[0]==in.engine);
        }
        if(finish==61)CHECK(events(FZX_RESULTS_SOUND_BGM,BGM_22)==1 && snapshot.event_count==8);
        if(finish<442)CHECK(snapshot.phase==FZX_RESULTS_FINISH && snapshot.count==0 && snapshot.results_ticks==0);
        else CHECK(snapshot.phase==FZX_RESULTS_TIMES && snapshot.results_ticks==1 && snapshot.name_entry_pending);
        CHECK(packet_check(&snapshot)==0);hash(&snapshot,sizeof(snapshot));
    }
    CHECK(bgm==1 && saves==1 && stops==7 && state.save_requests==1);
    CHECK(first_rotation!=snapshot.finish.rotation);
    CHECK(advance(&state,&in)==0 && !snapshot.finish.visible && snapshot.results_ticks==2);
    /* The panel's own tick increments after its draw. The total settles at
     * source draw timer90; timer101 is the first allowed A/Start edge. */
    unsigned arrival=0,record_cue=0;
    while(snapshot.results_ticks<101) {
        if(snapshot.results_ticks==99)in.button_pressed=0x8000;
        else in.button_pressed=0;
        CHECK(advance(&state,&in)==0);
        CHECK(snapshot.outgoing==0 && snapshot.phase==FZX_RESULTS_TIMES);
        arrival+=events(FZX_RESULTS_SOUND_SYSTEM,NA_SE_34);
        record_cue+=events(FZX_RESULTS_SOUND_SYSTEM,NA_SE_42);
        CHECK(packet_check(&snapshot)==0);hash(&snapshot,sizeof(snapshot));
        if(snapshot.results_ticks==91) {
            unsigned total=0,max=0,lap=0,glyphs=0;
            for(unsigned i=0;i<snapshot.count;++i) {
                const fzx_results_draw_t *p=&snapshot.draws[i];
                if(p->kind==FZX_RESULTS_GLYPH) {
                    CHECK((p->y==22 && p->r==250 && p->g==250 && p->b==0) ||
                          (p->y==24 && p->r==0 && p->g==0 && p->b==0));
                    if(!glyphs)CHECK(p->image==19 && p->x==95 && p->y==24);
                    ++glyphs;
                }
                if(p->kind!=FZX_RESULTS_IMAGE)continue;
                if(p->image==FZX_RESULTS_TIMER_SYMBOLS && p->y==150 && p->x==145) {
                    CHECK(p->w==8 && p->h==16 && p->sy==0);++total;
                }
                if(p->image==FZX_RESULTS_MAX_SPEED) {
                    CHECK(p->x==132 && p->y==176 && p->w==64 && p->h==16);++max;
                }
                if(p->image==FZX_RESULTS_LAP && p->y==48) {
                    CHECK(p->x==110 && p->w==16 && p->h==12);++lap;
                }
            }
            CHECK(total==1 && max==1 && lap==1 && glyphs==20);
        }
    }
    CHECK(arrival==4 && record_cue==1);
    in.button_pressed=0x1000;CHECK(advance(&state,&in)==0);
    CHECK(snapshot.phase==FZX_RESULTS_EXITING && snapshot.outgoing==9);
    in.button_pressed=0;
    for(unsigned n=1;n<50;++n) {
        CHECK(advance(&state,&in)==0);CHECK(packet_check(&snapshot)==0);
        if(n<49)CHECK(snapshot.outgoing==(int)(9+n*8) && snapshot.phase==FZX_RESULTS_EXITING);
        else CHECK(snapshot.outgoing==400 && snapshot.phase==FZX_RESULTS_RECORDS_REQUEST);
        hash(&snapshot,sizeof(snapshot));
    }
    CHECK(snapshot.name_entry_pending && snapshot.records_pending && snapshot.ghost_check_pending);
    CHECK(state.save_requests==1);
    saved=state;CHECK(fzx_results_step(&state,&in));CHECK(!memcmp(&saved,&state,sizeof(state)));
    return 0;
}
static int prior_input_save(void) {
    CHECK(fresh_defaults()==0);fzx_results_input_t in=input();
    in.state_flags=RACER_STATE_FINISHED;in.camera_mode=CAMERA_MODE_FINISHED_SUCCESS;in.record_eligible=1;
    for(unsigned i=0;i<3;++i)CHECK(advance(&state,&in)==0);
    in.race_time=120000;in.max_speed=99;in.engine=1;in.lap_times[1]=35000;
    in.machine.bodyR=255;in.machine_index=5;
    CHECK(advance(&state,&in)==0);
    CHECK(state.records.timeRecord[0]==110639 && state.records.maxSpeed==60.0f);
    CHECK(state.records.bestTime==36000 && state.records.recordEngines[0]==0.5078125f);
    CHECK(state.records.recordMachineInfos[0].bodyR==32 && state.records.machine_indices[0]==0);
    CHECK(state.save_requests==1);
    return 0;
}
static int record_cases(void) {
    fzx_results_config_t c=config();fzx_results_input_t in=input();
    CHECK(fresh_defaults()==0);
    fzx_results_records_t r=state.records;
    for(unsigned i=0;i<5;++i) {
        r.timeRecord[i]=100000+(int)i*10000;r.recordEngines[i]=0.125f*(float)i;
        r.recordNames[i][0]=(char)('A'+i);r.recordNames[i][1]='\0';
        r.recordMachineInfos[i].character=(uint8_t)i;r.machine_indices[i]=(int)i;
    }
    r.bestTime=10000;r.maxSpeed=100;
    CHECK(fzx_results_reset(&state,&c,&r));
    in.state_flags=RACER_STATE_FINISHED;in.camera_mode=CAMERA_MODE_FINISHED_SUCCESS;in.record_eligible=1;
    for(unsigned i=0;i<4;++i)CHECK(advance(&state,&in)==0);
    CHECK(snapshot.record_position==3 && !snapshot.best_lap && !snapshot.max_speed_record);
    CHECK(state.records.timeRecord[0]==100000 && state.records.timeRecord[1]==110000);
    CHECK(state.records.timeRecord[2]==110639 && state.records.timeRecord[3]==120000);
    CHECK(state.records.recordNames[2][0]=='\0' && state.records.recordNames[3][0]=='C');
    CHECK(state.records.recordMachineInfos[3].character==2 && state.records.machine_indices[3]==2);
    in.race_time=200000;CHECK(fzx_results_reset(&state,&c,&r));
    for(unsigned i=0;i<550;++i)CHECK(advance(&state,&in)==0);
    CHECK(!snapshot.record_position && !snapshot.name_entry_pending && !snapshot.max_speed_record && !snapshot.best_lap);
    in.button_pressed=0x8000;
    for(unsigned i=0;i<50;++i){CHECK(advance(&state,&in)==0);in.button_pressed=0;}
    CHECK(snapshot.phase==FZX_RESULTS_RECORDS_REQUEST && snapshot.records_pending && !snapshot.name_entry_pending);
    CHECK(!memcmp(&state.records,&r,sizeof(r)));
    /* A tie uses strict less-than and does not insert before an equal entry. */
    in.race_time=140000;CHECK(fzx_results_reset(&state,&c,&r));
    for(unsigned i=0;i<4;++i)CHECK(advance(&state,&in)==0);
    CHECK(!snapshot.record_position);
    in.record_eligible=0;CHECK(fzx_results_reset(&state,&c,NULL));
    for(unsigned i=0;i<4;++i)CHECK(advance(&state,&in)==0);
    CHECK(!snapshot.record_position && !snapshot.best_lap && !snapshot.max_speed_record);
    CHECK(state.records.timeRecord[0]==MAX_TIMER && state.save_requests==1);
    return 0;
}
static int cadence_and_rejects(void) {
    fzx_results_config_t c=config();fzx_results_input_t in=input();
    CHECK(fzx_results_reset(&state,&c,NULL));CHECK(fzx_results_reset(&other,&c,NULL));
    in.state_flags=RACER_STATE_FINISHED;in.camera_mode=CAMERA_MODE_FINISHED_SUCCESS;in.record_eligible=1;
    for(unsigned i=0;i<650;++i) {
        ++in.frame;in.button_pressed=i==550?0x8000:0;
        CHECK(fzx_results_step(&state,&in));CHECK(fzx_results_step(&other,&in));
        CHECK(fzx_results_snapshot(&state,&snapshot));
        if(i%17==0){CHECK(fzx_results_snapshot(&other,&second));CHECK(!memcmp(&snapshot,&second,sizeof(snapshot)));}
        CHECK(!memcmp(&state,&other,sizeof(state)));
    }
    CHECK(fzx_results_reset(&state,&c,NULL));
    in.connected=0;in.button_pressed=0x8000;
    for(unsigned i=0;i<650;++i){++in.frame;CHECK(fzx_results_step(&state,&in));}
    CHECK(state.phase==FZX_RESULTS_TIMES && state.outgoing==0);
    /* A consumer requests continuation on a real source press edge. */
    in.connected=1;in.button_pressed=0;++in.frame;CHECK(fzx_results_step(&state,&in));
    CHECK(state.phase==FZX_RESULTS_TIMES);
    in.button_pressed=0x8000;++in.frame;CHECK(fzx_results_step(&state,&in));CHECK(state.outgoing==9);
    saved=state;in.machine.customType=CUSTOM_MACHINE_EDITED;
    CHECK(!fzx_results_step(&state,&in) && !memcmp(&saved,&state,sizeof(state)));
    in.machine.customType=CUSTOM_MACHINE_DEFAULT;in.camera_mode=CAMERA_MODE_FINISHED_LOSER;
    CHECK(!fzx_results_step(&state,&in));in.camera_mode=CAMERA_MODE_FINISHED_SUCCESS;
    in.state_flags|=RACER_STATE_RETIRED;CHECK(!fzx_results_step(&state,&in));
    c.name[0]='\0';CHECK(!fzx_results_reset(&state,&c,NULL));
    CHECK(!fzx_results_snapshot(&state,NULL));
    state.fault=1;CHECK(!fzx_results_snapshot(&state,&snapshot));
    return 0;
}
int main(void) {
    CHECK(finish_timeline()==0);CHECK(prior_input_save()==0);CHECK(record_cases()==0);
    CHECK(cadence_and_rejects()==0);
    printf("results HOST ticks=%u reads=%u digest=%08x timing/records/packets/cadence/terminal PASS\n",steps,reads,digest);
    return 0;
}
#endif
