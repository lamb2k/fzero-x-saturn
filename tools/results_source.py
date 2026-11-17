#!/usr/bin/env python3
"""Extract original single-player Time Attack finish and initial results.

The closure ends at an explicit records/name-entry request. It neither jumps
to retry nor reports saves/audio as implemented. Original draw-side state is
advanced once per source tick while bounded presentation packets are captured;
snapshot reads never call the extracted source again.
"""
from pathlib import Path
import argparse
import hashlib
import os
import re
from import_source_art import ROOT, DECOMP, ArtError, output_location, verify_source
from menu_source import function
from course_menu_source import declaration

SOURCE=DECOMP/'src/overlays/ovl_i3/menus.c'
RACER=DECOMP/'src/game/racer.c'
CAMERA=DECOMP/'src/game/camera.c'
SAVE=DECOMP/'src/overlays/ovl_i2/save.c'
RECORDS=DECOMP/'src/overlays/ovl_i3/records_entry.c'
FINISH_CAMERA=DECOMP/'src/overlays/ovl_i3/C2160.c'
COMMON=DECOMP/'src/game/common.c'
MATH=DECOMP/'src/sys/math.c'


def select_us(source):
    """Resolve only known source feature symbols, rejecting unfamiliar code."""
    values={'PORT':False,'EXPANSION_KIT':False,'VERSION_JP':False,'VERSION_US':True,
            'AVOID_UB':False,'NON_MATCHING':False,'GDIFFUSER_PORT':False}
    stack=[];active=True;out=[]
    def condition(directive,expression):
        expression=expression.split('//',1)[0].strip()
        if directive in ('ifdef','ifndef'):
            if expression not in values:raise ArtError('Unknown results source condition: '+expression)
            return values[expression] == (directive=='ifdef')
        def defined(match):
            symbol=match[1]
            if symbol not in values:raise ArtError('Unknown results source condition: '+symbol)
            return str(int(values[symbol]))
        expression=re.sub(r'defined\((\w+)\)',defined,expression)
        if not re.fullmatch(r'[01()!&|\s]+',expression):raise ArtError('Unsupported results source expression')
        expression=expression.replace('&&',' and ').replace('||',' or ').replace('!',' not ')
        return bool(eval(expression,{'__builtins__':{}},{}))
    for line in source.splitlines():
        match=re.match(r'\s*#(ifdef|ifndef|if|elif)\s+(.*?)\s*$',line)
        if match:
            directive,expression=match.groups();choice=condition(directive,expression)
            if directive=='elif':
                if not stack:raise ArtError('Unbalanced results elif')
                parent,taken=stack[-1];active=parent and not taken and choice
                stack[-1]=(parent,taken or choice)
            else:
                stack.append((active,choice));active=active and choice
        elif re.match(r'\s*#else\s*$',line):
            if not stack:raise ArtError('Unbalanced results else')
            parent,taken=stack[-1];active=parent and not taken;stack[-1]=(parent,True)
        elif re.match(r'\s*#endif\s*$',line):
            if not stack:raise ArtError('Unbalanced results endif')
            active=stack.pop()[0]
        elif active and not line.startswith('#include'):out.append(line)
    if stack:raise ArtError('Unterminated results source condition')
    return '\n'.join(out)+'\n'


def span(source,start,end):
    if source.count(start)!=1:raise ArtError('Results extraction start changed: '+start)
    tail=source.split(start,1)[1]
    if end not in tail:raise ArtError('Results extraction end changed: '+end)
    return tail.split(end,1)[0]


def block(source,anchor):
    """Return one known brace body. Source drift fails before generated writes."""
    if source.count(anchor)!=1:raise ArtError('Results block changed: '+anchor)
    start=source.index(anchor)+len(anchor)
    if source[start-1]!='{':raise ArtError('Results block must name its brace')
    depth=1
    for i in range(start,len(source)):
        depth+=(source[i]=='{')-(source[i]=='}')
        if depth==0:return source[start:i]
    raise ArtError('Unbalanced results block')


PRELUDE=r'''/* Generated original US Time Attack results; regenerate, never edit. */
#include "fzx_results.h"
#include "results_source_ids.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>
typedef int32_t s32;
typedef int16_t s16;
typedef char s8;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef float f32;
typedef int Gfx;
typedef struct { float x,y,z; } Vec3f;
typedef fzx_results_machine_t MachineInfo;
typedef fzx_results_records_t CourseInfo;
typedef struct { MachineInfo unk_00; } unk_80141C88_unk_1D;
typedef struct {
    s32 timeRecord[5],bestTime;
    f32 engines[5],maxSpeed;
    char name[5][4];
    unk_80141C88_unk_1D unk_50[5],unk_F0;
} SaveCourseRecords;
typedef struct {
    u32 stateFlags;
    f32 speed,maxSpeed,bodyRF,bodyGF,bodyBF;
    s32 raceTime,lapTimes[3],character,customType,machineIndex;
} SourceRacer;
typedef struct { u16 buttonPressed; } Controller;
static fzx_results_state_t *m;
static SourceRacer source_racers[1];
static Controller source_controllers[1];
static const s32 source_ports[1]={0};
static f32 source_engine[1];
_Static_assert(CHAR_MIN<0,"Original font indexing requires signed char");
#define gRacers source_racers
#define gPlayerEngine source_engine
#define gControllers source_controllers
#define gPlayerControlPorts source_ports
#define gGameMode GAMEMODE_TIME_ATTACK
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define gNumPlayers 1
#define gTotalLapCount 3
#define gTitleDemoState TITLE_DEMO_INACTIVE
#define TITLE_DEMO_INACTIVE 0
#define gCurrentGhostType GHOST_NONE
#define gCourseIndex m->config.course
#define gCurrentTrackName m->config.name
#define gCurrentCourseInfo (&m->records)
#define gGameFrameCount m->input.frame
#define D_800F5DE8 m->input.record_eligible
#define sRecordsMachineIndices m->records.machine_indices
#define gCurrentTimeAttackRecordPosition m->record_position
#define gCurrentTimeAttackHasMaxSpeed m->max_speed_record
#define gBestTimedLap m->best_lap
#define gShowNameEntryMenu m->name_entry
#define sPlayerFinishTimer m->finish_timer
#define sPlayerFinishState m->finish_state
#define sPlayerFinishInitialized m->finish_initialized
#define sPlayerWinnerLoserFinishGameoverTimer m->special_timer
#define sPlayerLoserFinishTimer m->loser_timer
#define sFinishGameoverTimer m->finish_gameover_timer
#define sFinishGameoverRotation m->rotation
#define sFinishGameoverRotationSpeed m->rotation_speed
#define sPlayerResultsTimer m->results_timer
#define D_i3_80141B78 m->results_started
#define sFinishedSuccessTime m->finished_success_time
#define sTimeAttackResultsTimer m->outgoing
#define sMenuStateFlags m->menu_flags
#define sRaceFinishSaveTriggered m->finish_saved
#define sResultsTimesLapLeft m->layout_left
#define sResultsTimesTop m->layout_top
#define sResultsTimesVerticalSpacing m->layout_spacing
#define sResultsTimesLapLeftFromLap m->layout_lap_offset
#define sResultsTimesStartTopOffset m->layout_start
#define sResultsTimesStartLapSpacing m->layout_start_spacing
#define sResultsTimesSpeed m->layout_speed
#define RESULTS_LAYOUT_LAPS gTotalLapCount
#define RESULTS_SHOWN_LAPS gTotalLapCount
#define COURSE_X_1 48
#define COURSE_EDIT_1 24
#define BTN_A 0x8000u
#define BTN_START 0x1000u
#define Save_ClearData(p,n) memset(p,0,n)
static void source_event(uint8_t kind,uint16_t id,uint8_t player) {
    if(m->snapshot.event_count>=FZX_RESULTS_EVENT_MAX){m->fault=1;return;}
    fzx_results_event_t *e=&m->snapshot.events[m->snapshot.event_count++];
    e->kind=kind;e->id=id;e->player=player;
}
#define Audio_TriggerSystemSE(id) source_event(FZX_RESULTS_SOUND_SYSTEM,id,0)
#define Audio_RomBgmStart(id) source_event(FZX_RESULTS_SOUND_BGM,id,0)
#define Audio_PlayerLevelSEStop(player,id) source_event(FZX_RESULTS_SOUND_LEVEL_STOP,id,player)
static void Save_SaveCourseRecordProfiles(int32_t course) {
    ++m->save_requests;m->save_pending=1;
    source_event(FZX_RESULTS_SAVE_REQUEST,(uint16_t)course,0);
}
#define RecordsEntry_ToRecordsState() ((void)(m->records_pending=1))
#define Menus_CheckGhostCanSave() ((void)(m->ghost_check_pending=1))
static void source_sync_input(void) {
    SourceRacer *r=&source_racers[0];const fzx_results_input_t *in=&m->input;
    memset(r,0,sizeof(*r));r->stateFlags=in->state_flags;r->speed=in->speed;r->maxSpeed=in->max_speed;
    r->raceTime=in->race_time;memcpy(r->lapTimes,in->lap_times,sizeof(r->lapTimes));
    r->character=in->machine.character;r->customType=in->machine.customType;
    r->bodyRF=in->machine.bodyR;r->bodyGF=in->machine.bodyG;r->bodyBF=in->machine.bodyB;
    r->machineIndex=in->machine_index;source_engine[0]=in->engine;
    source_controllers[0].buttonPressed=in->connected?in->button_pressed:0;
}
static void source_finish_packet(const Vec3f *pos,int32_t angle,float scale) {
    fzx_results_finish_t *f=&m->snapshot.finish;
    f->visible=1;f->x=pos->x;f->y=pos->y;f->z=pos->z;f->angle=angle;f->scale=scale;
    f->rotation=m->rotation[0];f->rotation_speed=m->rotation_speed[0];
    f->timer=m->finish_gameover_timer[0];
}
'''


BRIDGE=r'''
bool fzx_results_default_config(fzx_results_config_t *config) {
    if(!config)return false;
    memset(config,0,sizeof(*config));
    memcpy(config->name,source_default_name,sizeof(source_default_name));
    return true;
}
static bool valid_records(const fzx_results_records_t *r) {
    if(!r || !(r->maxSpeed>=0.0f && r->maxSpeed<=462.9f) || r->bestTime<0 || r->bestTime>MAX_TIMER)return false;
    for(unsigned i=0;i<5;++i) {
        if(r->timeRecord[i]<0 || r->timeRecord[i]>MAX_TIMER ||
           !(r->recordEngines[i]>=0.0f && r->recordEngines[i]<=1.0f) ||
           (i && r->timeRecord[i]<r->timeRecord[i-1]))return false;
    }
    return true;
}
static bool valid_config(const fzx_results_config_t *c) {
    if(!c || c->course>=24 || !c->name[0])return false;
    unsigned i;
    for(i=0;i<FZX_RESULTS_NAME_MAX && c->name[i];++i)
        if((unsigned char)c->name[i]<32 || (unsigned char)c->name[i]>126)return false;
    return i<FZX_RESULTS_NAME_MAX;
}
static bool valid_input(const fzx_results_input_t *in) {
    if(!in || in->camera_mode<CAMERA_MODE_RACE_INTRO || in->camera_mode>CAMERA_MODE_FINISHED_SPECTATE ||
       in->race_time<0 || in->race_time>MAX_TIMER || in->machine_index<0 || in->machine_index>=30 ||
       in->record_eligible>1 || in->connected>1 || in->machine.character>=30 ||
       in->machine.customType!=CUSTOM_MACHINE_DEFAULT ||
       !(in->engine>=0.0f && in->engine<=1.0f) || !(in->speed>=0.0f && in->speed<=1000.0f) ||
       !(in->max_speed>=0.0f && in->max_speed<=462.9f))return false;
    for(unsigned i=0;i<3;++i)if(in->lap_times[i]<0 || in->lap_times[i]>MAX_TIMER)return false;
    if((in->state_flags&RACER_STATE_FINISHED) &&
       ((in->state_flags&(RACER_STATE_CRASHED|RACER_STATE_RETIRED)) ||
        (in->camera_mode!=CAMERA_MODE_FINISHED_SUCCESS && in->camera_mode!=CAMERA_MODE_FINISHED_SPECTATE)))return false;
    return true;
}
static bool valid(const fzx_results_state_t *s) {
    return s && s->initialized==1 && !s->fault && valid_config(&s->config) && valid_records(&s->records) &&
        s->phase<=FZX_RESULTS_RECORDS_REQUEST && s->snapshot.count<=FZX_RESULTS_DRAW_MAX &&
        s->snapshot.event_count<=FZX_RESULTS_EVENT_MAX && s->finish_timer[0]>=0 && s->finish_timer[0]<=36000 &&
        s->finish_state[0]>=PLAYER_FINISH_NONE && s->finish_state[0]<=PLAYER_FINISH_SUCCESS_COMPLETE &&
        s->finish_initialized[0]>=0 && s->finish_initialized[0]<=1 &&
        s->finish_gameover_timer[0]>=0 && s->finish_gameover_timer[0]<=600 &&
        s->special_timer[0]>=0 && s->special_timer[0]<=36000 &&
        s->results_timer[0]>=0 && s->results_timer[0]<=3000 && s->outgoing>=0 && s->outgoing<=400 &&
        s->record_position>=0 && s->record_position<=5 && s->best_lap>=0 && s->best_lap<=3 &&
        s->max_speed_record>=0 && s->max_speed_record<=1 && s->name_entry>=0 && s->name_entry<=1;
}
static void source_snapshot_fields(void) {
    fzx_results_snapshot_t *p=&m->snapshot;
    p->tick=m->ticks;p->frame=m->input.frame;p->phase=m->phase;
    p->finish_ticks=m->finish_timer[0];p->results_ticks=m->results_timer[0];p->outgoing=m->outgoing;
    p->record_position=(uint8_t)m->record_position;p->best_lap=(uint8_t)m->best_lap;
    p->max_speed_record=(uint8_t)m->max_speed_record;p->name_entry_pending=(uint8_t)m->name_entry;
    p->save_pending=m->save_pending;p->ghost_check_pending=m->ghost_check_pending;
    p->records_pending=m->records_pending;
}
bool fzx_results_reset(fzx_results_state_t *s,const fzx_results_config_t *config,
                       const fzx_results_records_t *records) {
    if(!s || !valid_config(config) || (records && !valid_records(records)))return false;
    const fzx_results_config_t copy=*config;
    /* Support resetting with the state's own session records without aliasing
     * the memset. This small value copy contains no generated texture data. */
    fzx_results_records_t record_copy;
    if(records)record_copy=*records;
    memset(s,0,sizeof(*s));s->config=copy;s->initialized=1;m=s;
    if(records)s->records=record_copy;else source_fresh_records();
    source_results_init();source_snapshot_fields();m=NULL;
    return valid(s);
}
bool fzx_results_step(fzx_results_state_t *s,const fzx_results_input_t *input) {
    if(!valid(s) || !valid_input(input) || s->ticks==UINT32_MAX)return false;
    if(s->phase==FZX_RESULTS_RECORDS_REQUEST)return true;
    const fzx_results_input_t copy=*input;
    memset(&s->snapshot,0,sizeof(s->snapshot));m=s;
    /* Menus_Update ran before Racer_Update in the original. Consume its
     * pending save against the prior tick, before binding the new racer. */
    source_sync_input();source_consume_save();
    s->input=copy;source_sync_input();++s->ticks;
    source_color(255,255,255,255);source_setup(0);source_scissor(0,0,320,240);
    Gfx *gfx=NULL;
    gfx=source_special(gfx);
    source_finish_state();
    if(s->finish_state[0]==PLAYER_FINISH_SUCCESS_COMPLETE) {
        gfx=source_results(gfx);
        if(s->outgoing>=400)s->phase=FZX_RESULTS_RECORDS_REQUEST;
        else s->phase=s->outgoing?FZX_RESULTS_EXITING:FZX_RESULTS_TIMES;
    } else s->phase=s->finish_timer[0]?FZX_RESULTS_FINISH:FZX_RESULTS_RACING;
    (void)gfx;source_snapshot_fields();m=NULL;
    return valid(s);
}
bool fzx_results_snapshot(const fzx_results_state_t *s,fzx_results_snapshot_t *out) {
    if(!out || !valid(s))return false;
    *out=s->snapshot;return true;
}
'''


def ids_header():
    chunks=['/* Generated source enum/constant identities; regenerate, never edit. */',
            (DECOMP/'include/fzx_game.h').read_text()]
    for path,names in [('include/fzx_camera.h',('CameraMode',)),
                       ('include/fzx_save.h',('GhostType',)),
                       ('include/fzx_machine.h',('CustomMachineType',)),
                       ('src/overlays/ovl_i3/menus.h',('PlayerFinishState',)),
                       ('include/sfx.h',('SfxId','LevelSfxId','BgmId'))]:
        text=(DECOMP/path).read_text()
        for name in names:chunks.append(select_us(declaration(text,r'^typedef enum '+name+r' \{.*?^\} '+name+';',name)))
    for path,names in [('include/fzx_racer.h',('RACER_STATE_FINISHED','RACER_STATE_CRASHED','RACER_STATE_RETIRED')),
                       ('src/overlays/ovl_i3/menus.h',('MENU_STATE_RACE_FINISH_SAVE',)),
                       ('include/macros.h',('MAX_TIMER',)),
                       ('include/fzx_math.h',('D_TWO_PI','M_TWO_PI','RAD_TO_FZXANG'))]:
        text=(DECOMP/path).read_text()
        for name in names:
            match=re.search(r'^#define '+name+r'(?:\([^\n]*?\))?\s+[^\n]+',text,re.M)
            if not match:raise ArtError('Source result constant changed: '+name)
            chunks.append(match[0])
    return ('\n'.join(chunks)+'\n').encode()


def closure(source):
    from results_draw_source import fragments
    menus=select_us(source);racer=select_us(RACER.read_text());save=select_us(SAVE.read_text())
    chunks=[PRELUDE]
    names=declaration(select_us(COMMON.read_text()),r'^const char\* sTrackNames\[\] = \{.*?^\};','track names')
    strings=re.findall(r'"(?:[^"\\]|\\.)*"',names)
    if len(strings)!=32:raise ArtError('Original track-name table changed')
    chunks.append('static const char source_default_name[]='+strings[0]+';\n'
                  '_Static_assert(sizeof(source_default_name)<=FZX_RESULTS_NAME_MAX,"Source name capacity");\n')
    for name in ('func_i2_80101C78','Save_InitCourseRecord','func_i2_80102A7C'):
        chunks.append(function(save,name))
    load=span(function(save,'Save_LoadCourseRecord'),'    courseInfo = &gCourseInfos[courseIndex];\n\n', '\n}\n')
    chunks.append('static void source_fresh_records(void) {\n    s32 i,j;\n    SaveCourseRecords saved;\n'
                  '    Save_InitCourseRecord(&saved,true);\n    SaveCourseRecords *courseRecord=&saved;\n'
                  '    CourseInfo *courseInfo=gCurrentCourseInfo;\n'+load+'\n}\n')
    machine=function(racer,'func_80089A74')
    prefix=machine.split('    if (customType == CUSTOM_MACHINE_EDITED) {',1)[0]
    default=block(machine,'} else {')
    prefix=prefix.replace('    CustomMachineInfo* temp_v0;','').replace('    u8 customType;','')
    prefix=prefix.replace('customType = gRacers','gRacers')
    chunks.append(prefix+default+'\n}\n')
    record=function(racer,'func_80089BD0')
    update=block(record,'    if (D_800F5DE8 != 0) {')
    before='*(s32*) &gCurrentCourseInfo->recordNames[j + 1] = *(s32*) &gCurrentCourseInfo->recordNames[j];'
    if update.count(before)!=1:raise ArtError('Source record-name shift changed')
    update=update.replace(before,'memcpy(gCurrentCourseInfo->recordNames[j + 1], gCurrentCourseInfo->recordNames[j], 4);')
    # Original j is read only after a lap improved the prior best. Initialize
    # its otherwise unused path to keep both host compilers' warnings strict.
    chunks.append('static void func_80089BD0(void) {\n    s32 i,j=0,var_a1_3;\n'
                  '    if (D_800F5DE8 != 0) {'+update+'\n    }\n}\n')
    clear=function(RECORDS.read_text(),'RecordsEntry_ClearCurrentRecordName')
    if clear.count('&gCourseInfos[gCourseIndex]')!=1:raise ArtError('Record-name course binding changed')
    chunks.append(clear.replace('&gCourseInfos[gCourseIndex]','gCurrentCourseInfo'))
    consume=block(function(menus,'Menus_Update'),
                  'if ((sMenuStateFlags & MENU_STATE_RACE_FINISH_SAVE) && !sRaceFinishSaveTriggered) {')
    ta=block(consume,'if (gGameMode == GAMEMODE_TIME_ATTACK) {')
    chunks.append('static void source_consume_save(void) {\n'
                  '    if ((sMenuStateFlags & MENU_STATE_RACE_FINISH_SAVE) && !sRaceFinishSaveTriggered) {\n'+ta+
                  '\n        sRaceFinishSaveTriggered = true;\n    }\n}\n')
    chunks.append(fragments(menus,select_us))
    chunks.append(function(menus,'Menus_InitFinish'))
    finish=function(menus,'Menus_DrawFinish')
    scale=re.search(r'Matrix_SetLockedLookAt\(gGfxPool->unk_21988, NULL, ([0-9.]+f), ([0-9.]+f), ([0-9.]+f),',finish)
    if not scale or len(set(scale.groups()))!=1:raise ArtError('Original FINISH scale changed')
    transform=span(finish,'    if (sFinishGameoverTimer[playerIndex] <= 10) {','    lookAt.x = 0;')
    transform='    if (sFinishGameoverTimer[playerIndex] <= 10) {'+transform
    events=span(finish,'    sPlayerWinnerLoserFinishGameoverTimer[playerIndex]++;','    return gfx;')
    events='    sPlayerWinnerLoserFinishGameoverTimer[playerIndex]++;'+events
    chunks.append('static Gfx *Menus_DrawFinish(Gfx *gfx,s32 playerIndex) {\n'
                  '    f32 temp,temp_fv0_2;\n    s32 i,angle;\n    Vec3f pos;\n'+transform+
                  '    source_finish_packet(&pos,angle,'+scale[1]+');\n'+events+'    return gfx;\n}\n')
    special=function(menus,'Menus_Player1SpecialDraw')
    success=block(special,'} else if (sPlayerFinishState[playerIndex] == PLAYER_FINISH_CAMERA_SUCCESS) {')
    branch=block(success,'((gGameMode == GAMEMODE_TIME_ATTACK) && (gCurrentGhostType < GHOST_STAFF))) {')
    chunks.append('static Gfx *source_special(Gfx *gfx) {\n    s32 playerIndex=0;\n'
                  '    if (sPlayerFinishState[playerIndex] == PLAYER_FINISH_CAMERA_SUCCESS) {'+branch+
                  '\n    }\n    return gfx;\n}\n')
    draw=function(menus,'Menus_Draw')
    timer=block(draw,'        if (gRacers[i].stateFlags & RACER_STATE_FINISHED) {')
    chunks.append('static void source_finish_state(void) {\n    s32 i=0;\n'
                  '    if (gRacers[i].stateFlags & RACER_STATE_FINISHED) {'+timer+'\n    }\n}\n')
    results=span(draw,'                if (sTimeAttackResultsTimer < 400) {',
                 '                if (sTimeAttackResultsTimer >= 400) {')
    results='                if (sTimeAttackResultsTimer < 400) {'+results
    chunks.append('static Gfx *source_results(Gfx *gfx) {\n    s32 playerIndex=0;\n'+results+
                  '    if (sTimeAttackResultsTimer >= 400) {\n'
                  '        sTimeAttackResultsTimer = 400;\n        m->records_pending=1;\n'
                  '    }\n    return gfx;\n}\n')
    # Menu initialization obtains the duration from the five camera-script
    # entries, rather than a parallel hand-maintained timer constant.
    camera_table=declaration(CAMERA.read_text(),r'^FinishedSuccessScript gFinishedSuccessScript\[\] = \{.*?^\};','finish camera script')
    durations=re.findall(r'\{\s*FINISHED_SUCCESS_\w+,\s*(\d+),',camera_table)
    if len(durations)!=5:raise ArtError('Source finish camera script extent changed')
    init=span(function(menus,'Menus_Init'),'    sFinishedSuccessTime = 0;','    for (i = 0; i < 4; i++) {')
    chunks.append('static void source_results_init(void) {\n    s32 i;\n'
                  '    const struct { s32 time; } gFinishedSuccessScript[]={'+','.join('{'+v+'}' for v in durations)+'};\n'
                  '    sFinishedSuccessTime = 0;'+init+'}\n')
    chunks.append(BRIDGE)
    return ('\n'.join(chunks)+'\n').encode()


MATRIX_BRIDGE=r'''
static int32_t source_result_word(const Mtx *mtx,unsigned row,unsigned column) {
#if defined(GDIFFUSER_PORT)
    column^=1;
#endif
    const uint32_t bits=((uint32_t)(uint16_t)mtx->u.i[row][column]<<16)|mtx->u.f[row][column];
    int32_t result;memcpy(&result,&bits,sizeof(result));return result;
}
bool fzx_results_finish_matrix(const fzx_results_snapshot_t *snapshot,int32_t out[18]) {
    if(!snapshot || !out || snapshot->finish.visible!=1)return false;
    const fzx_results_finish_t *f=&snapshot->finish;
    if(f->angle<0 || f->angle>4096 || !(f->scale>0.0f && f->scale<=1.0f) ||
       !(f->x>=-32760.0f && f->x<=32760.0f) || !(f->y>=-32760.0f && f->y<=32760.0f) ||
       !(f->z>=16.0f && f->z<=32760.0f))return false;
    Mtx model,view,projection,combined;
    MtxF model_f,view_f,projection_f,combined_f;
    source_finish_camera(&projection,&projection_f,&view,&view_f);
    source_finish_model(&model,&model_f,f);
    /* Original FINISH camera is an exact unit reflection with no translation.
     * Source model Q16 rounding therefore commutes with this multiplication;
     * HOST tests compare the separately rounded original matrices as well. */
    Matrix_AffineMultiply(&view_f,&model_f,&combined_f);
    Matrix_ToMtx(&combined_f,&combined);
    int32_t result[18];
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)result[i*4+j]=source_result_word(&combined,i,j);
    result[16]=source_result_word(&projection,0,0);result[17]=source_result_word(&projection,1,1);
    memcpy(out,result,sizeof(result));return true;
}
'''


def matrix_bridge():
    """Source calls only; parent links these with existing original math."""
    camera=function(FINISH_CAMERA.read_text(),'func_i3_8012EE90')
    camera=camera.replace('static Gfx* func_i3_8012EE90(Gfx* gfx, s32 playerIndex)',
                          'static void source_finish_camera(Mtx *projection,MtxF *projection_f,Mtx *view,MtxF *view_f)')
    replacements={
        'if (gNumPlayers == 2)':'if (0)',
        '&gGfxPool->unk_20008[playerIndex], NULL':'projection, projection_f',
        '&gGfxPool->unk_20108[playerIndex], NULL':'view, view_f',
        '    gSPPerspNormalize(gfx++, perspectiveScale);':'',
        '    return gfx;':'',
    }
    for before,after in replacements.items():
        if camera.count(before)!=1:raise ArtError('FINISH camera adapter changed: '+before)
        camera=camera.replace(before,after)
    draw=function(select_us(SOURCE.read_text()),'Menus_DrawFinish')
    vectors=span(draw,'    lookAt.x = 0;','    gfx = func_i3_8012EE90')
    vectors='    lookAt.x = 0;'+vectors
    call=re.search(r'    Matrix_SetLockedLookAt\(.*?;',draw,re.S)
    if not call:raise ArtError('FINISH original model call missing')
    call=call[0].replace('gGfxPool->unk_21988, NULL','model, model_f')
    scales=re.findall(r'0\.07f',call)
    if len(scales)!=3:raise ArtError('FINISH source scale changed')
    call=call.replace('0.07f','f->scale')
    model=('static void source_finish_model(Mtx *model,MtxF *model_f,const fzx_results_finish_t *f) {\n'
           '    const s32 angle=f->angle;\n    Vec3f pos={f->x,f->y,f->z},lookAt,up;\n'+vectors+call+'\n}\n')
    return ('/* Generated original FINISH model/camera adapter; regenerate, never edit. */\n'
            '#include "global.h"\n#include "fzx_results.h"\n#include <string.h>\n'+camera+model+MATRIX_BRIDGE).encode()


def matrix_host_support():
    """Private HOST test harness for the same original math functions."""
    chunks=[r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <math.h>
typedef int32_t s32;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int16_t s16;
typedef float f32;
typedef double f64;
typedef struct { float x,y,z; } Vec3f;
typedef struct { float m[4][4]; } MtxF;
typedef struct { struct { int16_t i[4][4];uint16_t f[4][4]; } u; } Mtx;
static Mtx sDefaultMtx;
static MtxF sDefaultMtxF;
static f32 gSinTable[0x1000];
#define ARRAY_COUNT(a) ((s32)(sizeof(a)/sizeof((a)[0])))
#define SQ(x) ((x)*(x))
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
''']
    header=(DECOMP/'include/fzx_math.h').read_text()
    for name in ('SIN','COS','TAN'):
        chunks.append(declaration(header,r'^#define '+name+r'\(x\)[^\n]+',name))
    math=MATH.read_text()
    for name in ('Math_SinTableInit','Math_Round','Matrix_ToMtx','Matrix_SetTransRot',
                 'Matrix_SetLockedLookAt','Matrix_SetLookAt','Matrix_SetFrustrum','Matrix_AffineMultiply'):
        match=re.search(r'^(?:void|s32) '+name+r'\([^;{]+?\) \{.*?^\}',math,re.M|re.S)
        if not match:raise ArtError('Original matrix function missing: '+name)
        body=select_us(match[0])
        if name=='Matrix_SetTransRot':body=body.replace('{','{\n    (void)scale;',1)
        if name=='Matrix_SetFrustrum':body=body.replace('    s32 pad;','    s32 pad;\n    (void)sizeof(pad);')
        chunks.append(body)
    return ('\n'.join(chunks)+'\n').encode()


def generate(output):
    from results_draw_source import dependencies
    output_location(output);verify_source(DECOMP)
    outputs={'results_source.c':closure(SOURCE.read_text()),'results_source_ids.h':ids_header(),
             'results_matrix_bridge.c':matrix_bridge()}
    paths=[Path(__file__),ROOT/'include/fzx_results.h',ROOT/'tools/results_draw_source.py',
           ROOT/'tools/import_source_art.py',ROOT/'tools/menu_source.py',ROOT/'tools/course_menu_source.py',
           ROOT/'tools/course_menu_assets.py',ROOT/'tools/hud_assets.py',SOURCE,RACER,CAMERA,SAVE,RECORDS,
           DECOMP/'include/fzx_game.h',DECOMP/'include/fzx_camera.h',DECOMP/'include/fzx_save.h',
           DECOMP/'include/fzx_machine.h',DECOMP/'include/fzx_racer.h',DECOMP/'include/fzx_math.h',
           DECOMP/'include/macros.h',DECOMP/'include/sfx.h',SOURCE.with_suffix('.h'),
           FINISH_CAMERA,COMMON,MATH,*dependencies()]
    output.mkdir(parents=True,exist_ok=True)
    receipt=['# fzx generation receipt v1; regenerate, never edit']
    receipt+=['generator %s %s'%(hashlib.sha256(p.read_bytes()).hexdigest(),os.path.relpath(p,ROOT/'tools').replace('\\','/'))
              for p in dict.fromkeys(paths)]
    for name,data in outputs.items():
        (output/name).write_bytes(data);receipt.append('output %s %s'%(hashlib.sha256(data).hexdigest(),name))
    (output/'results_source.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    print('Original Time Attack finish/results closure generated; records continuation, save and audio consumers pending')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'generated/results-source')
    args=parser.parse_args();generate(args.output)
