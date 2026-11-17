#include "fzx_records.h"
#include "records_source_ids.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if(!(c)){fprintf(stderr,"records check line %d: %s\n",__LINE__,#c);return 1;} } while(0)
static fzx_records_state_t state,before;
static fzx_records_snapshot_t shot,again;
static fzx_records_input_t input;
static fzx_results_state_t results;
static uint32_t digest=2166136261u;
static unsigned steps,reads;

static void hash(const void *data,size_t size) {
    const unsigned char *p=data;
    for(size_t i=0;i<size;++i)digest=(digest^p[i])*16777619u;
}
static uint32_t random_word(uint32_t *seed,uint32_t *mask) {
    *seed=(uint32_t)((uint64_t)*seed*UINT64_C(1103515245)+12345u);
    const uint32_t old=*mask;
    *mask=(old>>1)^((old&1u)?0x8810u:0u);
    return *seed^*mask;
}
static int pure_snapshot(void) {
    before=state;
    CHECK(fzx_records_snapshot(&state,&shot));++reads;
    for(unsigned i=0;i<4;++i) {
        CHECK(fzx_records_snapshot(&state,&again));++reads;
        CHECK(!memcmp(&shot,&again,sizeof(shot)));
        CHECK(!memcmp(&state,&before,sizeof(state)));
    }
    return 0;
}
static int advance(uint16_t press,uint16_t held,uint8_t stick_press,uint8_t stick_held,
                   uint8_t retrigger,uint8_t connected) {
    input.button_pressed=press;input.button_current=held;
    input.stick_pressed=stick_press;input.stick_current=stick_held;
    input.retrigger=retrigger;input.connected=connected;
    const fzx_records_input_t copy=input;
    CHECK(fzx_records_step(&state,&input));++steps;
    CHECK(!memcmp(&input,&copy,sizeof(input)));
    CHECK(pure_snapshot()==0);
    CHECK(shot.frame==input.frame && !memcmp(&shot.input,&input,sizeof(input)));
    CHECK(shot.event_count<=FZX_RECORDS_EVENT_MAX);
    for(unsigned i=0;i<shot.event_count;++i)CHECK(shot.events[i].player==0);
    input.rng_seed1=shot.rng_seed1;input.rng_mask1=shot.rng_mask1;
    ++input.frame;hash(&shot,sizeof(shot));return 0;
}
static int idle(void){return advance(0,0,0,0,0,1);}
static int press(uint16_t buttons){return advance(buttons,buttons,0,0,0,1);}
static int has_event(unsigned kind,unsigned id) {
    unsigned count=0;
    for(unsigned i=0;i<shot.event_count;++i)count+=shot.events[i].kind==kind && shot.events[i].id==id;
    return (int)count;
}
static void fixture(unsigned position) {
    memset(&results,0,sizeof(results));
    results.initialized=1;results.phase=FZX_RESULTS_RECORDS_REQUEST;results.outgoing=400;
    strcpy(results.config.name,"TEST COURSE");results.config.course=3;
    results.input.frame=12322;results.record_position=(int32_t)position;results.name_entry=position!=0;
    results.best_lap=2;results.max_speed_record=1;
    results.records.maxSpeed=60;results.records.bestTime=30000;
    for(unsigned i=0;i<5;++i) {
        results.records.timeRecord[i]=100000+(int32_t)i*10000;
        results.records.recordEngines[i]=0.5f;
        results.records.recordMachineInfos[i].character=(uint8_t)i;
        results.records.machine_indices[i]=(int32_t)i+1;
        memcpy(results.records.recordNames[i],"OLD",4);
    }
    if(position)memset(results.records.recordNames[position-1],0,4);
    results.records.machine_indices[5]=6;results.records.machine_indices[6]=7;
}
static int begin(unsigned position,uint32_t seed,uint32_t mask) {
    fixture(position);const fzx_results_state_t copy=results;
    CHECK(fzx_records_begin(&state,&results));
    CHECK(!memcmp(&results,&copy,sizeof(results)));
    CHECK(pure_snapshot()==0);
    CHECK(shot.tick==0 && shot.draw_kind==FZX_RECORDS_DRAW_NONE && shot.event_count==0);
    CHECK(!memcmp(&shot.records,&results.records,sizeof(shot.records)));
    CHECK(shot.record_left==25 && shot.record_top==64 && shot.record_height==35);
    CHECK(shot.max_speed_left==220 && shot.max_speed_top==60 && shot.max_speed_machine_offset==33);
    CHECK(shot.best_time_left==220 && shot.best_time_top==118 && shot.best_lap_top_offset==-12);
    CHECK(shot.record_left_offset==320 && shot.record_transition_timer==30);
    CHECK(shot.record_position==position && shot.enter_index==49 && shot.name_length==0);
    CHECK(shot.name_state==NAME_ENTRY_START && shot.name_entry==(position!=0));
    memset(&input,0,sizeof(input));input.frame=results.input.frame;input.connected=1;
    input.rng_seed1=seed;input.rng_mask1=mask;
    return 0;
}
static int keyboard(unsigned position,uint32_t seed,uint32_t mask) {
    CHECK(begin(position,seed,mask)==0);
    CHECK(idle()==0);
    CHECK(shot.frame==results.input.frame && shot.tick==1);
    CHECK(shot.name_state==NAME_ENTRY_TRANSITION_IN && shot.name_timer==0);
    CHECK(shot.draw_kind==FZX_RECORDS_DRAW_NAME && shot.draw_name_state==NAME_ENTRY_TRANSITION_IN);
    CHECK(shot.rng_calls==100 && !shot.event_count && !shot.spinner_valid);
    for(unsigned i=0;i<FZX_RECORDS_KEY_COUNT;++i) {
        const int x=(int)(random_word(&seed,&mask)%320u);
        const int y=(int)(random_word(&seed,&mask)%240u);
        CHECK(shot.keys[i].rand_left==x && shot.keys[i].rand_top==y);
        CHECK(shot.keys[i].left==x && shot.keys[i].top==y);
    }
    CHECK(shot.rng_seed1==seed && shot.rng_mask1==mask);
    CHECK(shot.keys[0].character[0]=='A' && shot.keys[25].character[0]=='Z');
    CHECK(shot.keys[47].character[0]=='>' && shot.keys[49].character[0]=='<');
    const int random_x=shot.keys[0].rand_left,random_y=shot.keys[0].rand_top;
    for(int timer=0;timer<61;++timer) {
        CHECK(idle()==0);
        /* Font1 A width13 plus spacing1 gives the source's centered x=63. */
        CHECK(shot.keys[0].left==random_x+(63-random_x)*timer/60);
        CHECK(shot.keys[0].top==random_y+(100-random_y)*timer/60);
        CHECK(!shot.rng_calls && !shot.event_count);
        CHECK(shot.draw_name_state==NAME_ENTRY_TRANSITION_IN);
        if(timer<60)CHECK(shot.name_state==NAME_ENTRY_TRANSITION_IN && shot.name_timer==timer+1);
        else CHECK(shot.name_state==NAME_ENTRY_KEYBOARD && shot.name_timer==0);
    }
    CHECK(!shot.keys[0].character[0] && !shot.keys[1].character[0] && !shot.keys[2].character[0]);
    CHECK(idle()==0);
    CHECK(shot.draw_name_state==NAME_ENTRY_KEYBOARD && shot.spinner_valid);
    CHECK(shot.spinner_keyboard_index==0 && shot.spinner_x==62 && shot.spinner_y==100);
    return 0;
}
static int table_and_terminal(int from_name) {
    if(from_name) {
        CHECK(shot.name_state==NAME_ENTRY_TRANSITION_OUT && shot.name_timer==0);
        for(int timer=1;timer<=61;++timer) {
            CHECK(press(BTN_A|BTN_START)==0);
            CHECK(shot.name_timer==timer && shot.draw_kind==FZX_RECORDS_DRAW_NAME);
            CHECK(shot.draw_name_state==NAME_ENTRY_TRANSITION_OUT && !shot.spinner_valid);
            CHECK(!shot.event_count && !shot.rng_calls && shot.save_requests==1);
            CHECK(shot.name_entry==(timer<61));
        }
        CHECK(shot.phase==FZX_RECORDS_TABLE_TRANSITION && shot.record_increase==1);
    }
    CHECK(idle()==0);
    CHECK(shot.draw_kind==FZX_RECORDS_DRAW_TABLE && shot.record_state==RECORDS_ENTRY_TRANSITION_RECORDS);
    CHECK(!shot.record_increase && shot.record_left_offset==320 && shot.record_transition_timer==30);
    for(int timer=30;timer>=0;--timer) {
        CHECK(idle()==0);
        CHECK(shot.record_left_offset==(timer*320)/30);
        CHECK(shot.record_transition_timer==(timer?timer-1:0));
        CHECK(shot.phase==FZX_RECORDS_TABLE_TRANSITION && !shot.event_count && !shot.rng_calls);
    }
    CHECK(shot.record_state==RECORDS_ENTRY_RECORDS);
    CHECK(idle()==0);
    CHECK(shot.phase==FZX_RECORDS_FINISH_MENU_READY && !shot.record_left_offset);
    const fzx_records_state_t terminal=state;
    input.button_pressed=BTN_A|BTN_START;input.frame+=100;
    CHECK(fzx_records_step(&state,&input));
    CHECK(!memcmp(&state,&terminal,sizeof(state)));
    return 0;
}
static int names_and_rows(void) {
    for(unsigned row=1;row<=5;++row) {
        CHECK(keyboard(row,12345u+row,6789u+row)==0);
        CHECK(press(BTN_A)==0);
        CHECK(shot.name_length==1 && shot.name[0]=='A' && shot.selected_timer[0]==1);
        CHECK(shot.keys[0].left==62 && shot.keys[0].top==100);
        CHECK(shot.event_count==1 && has_event(FZX_RESULTS_SOUND_SYSTEM,NA_SE_33)==1);
        CHECK(press(BTN_RIGHT|BTN_A)==0);
        CHECK(shot.name_length==2 && !memcmp(shot.name,"AB",3));
        CHECK(shot.event_count==2 && has_event(FZX_RESULTS_SOUND_SYSTEM,NA_SE_30)==1);
        CHECK(press(BTN_RIGHT|BTN_A)==0);
        CHECK(shot.name_length==3 && !memcmp(shot.name,"ABC",4));
        CHECK(shot.cursor_x==9 && shot.cursor_y==4 && shot.spinner_keyboard_index==2);
        CHECK(shot.spinner_x==102 && shot.spinner_y==100);
        CHECK(press(BTN_B)==0);
        CHECK(shot.name_length==2 && !memcmp(shot.name,"AB",3) && shot.cursor_x==2 && shot.cursor_y==0);
        CHECK(shot.event_count==1 && has_event(FZX_RESULTS_SOUND_SYSTEM,NA_SE_16)==1);
        CHECK(press(BTN_START)==0);
        CHECK(shot.cursor_x==9 && shot.cursor_y==4 && shot.spinner_keyboard_index==2 && shot.save_requests==0);
        uint32_t seed=input.rng_seed1,mask=input.rng_mask1;
        for(unsigned n=0;n<100;++n)(void)random_word(&seed,&mask);
        CHECK(press(BTN_START)==0);
        CHECK(shot.name_state==NAME_ENTRY_TRANSITION_OUT && shot.name_timer==0 && shot.rng_calls==100);
        CHECK(shot.rng_seed1==seed && shot.rng_mask1==mask && shot.save_requests==1);
        CHECK(shot.event_count==2 && has_event(FZX_RESULTS_SAVE_REQUEST,3)==1);
        CHECK(shot.events[0].kind==FZX_RESULTS_SAVE_REQUEST && shot.events[1].id==NA_SE_33);
        CHECK(shot.keys[0].left==62 && shot.keys[0].top==100);
        CHECK(!memcmp(shot.records.recordNames[row-1],"AB\0",4));
        fzx_results_records_t expected=results.records;
        memcpy(expected.recordNames[row-1],"AB\0",4);
        CHECK(!memcmp(&shot.records,&expected,sizeof(expected)));
        CHECK(table_and_terminal(1)==0);
    }
    return 0;
}
static int inputs_and_short_names(void) {
    CHECK(keyboard(1,UINT32_MAX,UINT32_MAX)==0);
    CHECK(advance(0,BTN_RIGHT,0,0,0,1)==0 && shot.cursor_x==0);
    CHECK(advance(0,BTN_RIGHT|BTN_A,0,0,1,1)==0 && shot.cursor_x==1 && !shot.name_length);
    CHECK(shot.input_pressed==(BTN_RIGHT|BTN_A) && !shot.input_button_pressed);
    CHECK(advance(BTN_A,BTN_A,0x10,0x10,1,0)==0);
    CHECK(shot.cursor_x==1 && !shot.name_length && !shot.input_pressed && !shot.event_count);
    CHECK(shot.input.connected==0 && shot.input.button_pressed==BTN_A);
    CHECK(advance(0,0,0x10,0,0,1)==0 && shot.cursor_x==2);
    CHECK(advance(0,0,0,0x20,1,1)==0 && shot.cursor_x==1);
    CHECK(press(BTN_LEFT)==0 && shot.cursor_x==0);
    CHECK(press(BTN_UP)==0 && shot.cursor_y==3 && shot.cursor_x==0); /* skips unused row4 key */
    CHECK(press(BTN_DOWN)==0 && shot.cursor_y==0 && shot.cursor_x==0);
    CHECK(press(BTN_LEFT)==0 && shot.cursor_x==9 && shot.cursor_y==0);
    CHECK(press(BTN_START)==0 && shot.cursor_x==9 && shot.cursor_y==4);
    CHECK(press(BTN_LEFT)==0 && shot.cursor_x==8);
    CHECK(press(BTN_LEFT)==0 && shot.cursor_x==7);
    CHECK(press(BTN_A)==0 && shot.name_length==1 && shot.name[0]==' ');
    CHECK(press(BTN_B)==0 && !shot.name_length && shot.cursor_x==7 && shot.cursor_y==4);
    CHECK(press(BTN_B)==0 && !shot.name_length && shot.cursor_x==0 && shot.cursor_y==0);
    CHECK(press(BTN_START)==0);CHECK(press(BTN_A)==0);
    CHECK(shot.save_requests==1 && !shot.records.recordNames[0][0]);
    CHECK(table_and_terminal(1)==0);
    CHECK(keyboard(5,0,0)==0);
    CHECK(press(BTN_A)==0);CHECK(press(BTN_START)==0);CHECK(press(BTN_START)==0);
    CHECK(!memcmp(shot.records.recordNames[4],"A\0\0",4));
    CHECK(table_and_terminal(1)==0);
    CHECK(begin(0,0x87654321u,0x01234567u)==0);
    CHECK(shot.phase==FZX_RECORDS_TABLE_TRANSITION && shot.record_increase==1);
    CHECK(table_and_terminal(0)==0 && shot.tick==33 && shot.save_requests==0);
    return 0;
}
static int rejection(void) {
    fixture(1);memset(&state,0xa5,sizeof(state));before=state;
    CHECK(!fzx_records_begin(NULL,&results));CHECK(!fzx_records_begin(&state,NULL));
    CHECK(!memcmp(&state,&before,sizeof(state)));
    const fzx_results_state_t good=results;
    for(unsigned i=0;i<7;++i) {
        results=good;
        switch(i) {
        case 0:results.phase=FZX_RESULTS_TIMES;break;case 1:results.record_position=6;break;
        case 2:results.name_entry=0;break;case 3:results.records.recordNames[0][3]='X';break;
        case 4:results.records.timeRecord[0]=MAX_TIMER+1;break;
        case 5:results.config.course=24;break;default:results.outgoing=399;break;
        }
        CHECK(!fzx_records_begin(&state,&results));CHECK(!memcmp(&state,&before,sizeof(state)));
    }
    CHECK(keyboard(1,123,456)==0);
    const fzx_records_state_t valid=state;
    const fzx_records_input_t valid_in=input;
    for(unsigned i=0;i<11;++i) {
        state=valid;input=valid_in;
        switch(i) {
        case 0:state.snapshot.name_length=4;break;case 1:state.snapshot.cursor_x=10;break;
        case 2:state.snapshot.name_timer=62;break;case 3:state.snapshot.rng_calls=101;break;
        case 4:state.snapshot.keys[4].left=-1;break;case 5:input.connected=2;break;
        case 6:input.retrigger=2;break;case 7:input.stick_pressed=1;break;
        case 8:state.snapshot.keys[4].character[0]=(char)0xa4;break;
        case 9:state.snapshot.spinner_keyboard_index=50;break;
        default:state.snapshot.tick=UINT32_MAX;break;
        }
        before=state;CHECK(!fzx_records_step(&state,&input));CHECK(!memcmp(&state,&before,sizeof(state)));
    }
    state=valid;CHECK(!fzx_records_step(&state,NULL));CHECK(!fzx_records_snapshot(&state,NULL));
    memset(&shot,0xa5,sizeof(shot));again=shot;state.fault=1;
    CHECK(!fzx_records_snapshot(&state,&shot) && !memcmp(&shot,&again,sizeof(shot)));
    return 0;
}
static int actual_results_handoff(void) {
    fzx_results_config_t config={0};strcpy(config.name,"TEST COURSE");
    CHECK(fzx_results_reset(&results,&config,NULL));
    fzx_results_input_t race={0};race.connected=1;race.record_eligible=1;
    race.state_flags=RACER_STATE_FINISHED;race.camera_mode=CAMERA_MODE_FINISHED_SUCCESS;
    race.race_time=110639;race.lap_times[0]=37000;race.lap_times[1]=36000;race.lap_times[2]=37639;
    race.speed=40;race.max_speed=60;race.engine=0.5078125f;
    for(unsigned i=0;i<1000 && results.phase!=FZX_RESULTS_RECORDS_REQUEST;++i) {
        race.frame=600+i;race.button_pressed=results.results_timer[0]>100?BTN_A:0;
        CHECK(fzx_results_step(&results,&race));
    }
    CHECK(results.phase==FZX_RESULTS_RECORDS_REQUEST && results.record_position==1 && results.save_requests==1);
    const fzx_results_state_t original=results;
    CHECK(fzx_records_begin(&state,&results));
    CHECK(!memcmp(&results,&original,sizeof(results)));
    memset(&input,0,sizeof(input));input.frame=race.frame;input.connected=1;
    input.rng_seed1=12345;input.rng_mask1=6789;
    CHECK(idle()==0 && shot.frame==race.frame && shot.name_state==NAME_ENTRY_TRANSITION_IN);
    CHECK(shot.rng_calls==100 && shot.records.timeRecord[0]==110639 && shot.save_requests==0);
    CHECK(shot.records.recordEngines[0]==0.5078125f);
    return 0;
}
int main(void) {
    CHECK(names_and_rows()==0);CHECK(inputs_and_short_names()==0);
    CHECK(rejection()==0);CHECK(actual_results_handoff()==0);
    printf("records HOST %u steps %u pure reads digest=%08x source handoff/name/table/events/RNG PASS\n",
           steps,reads,digest);
    return 0;
}
