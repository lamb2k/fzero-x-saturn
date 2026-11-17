#!/usr/bin/env python3
"""Extract ordinary US Time Attack retry schedule and Menus_Init RNG history.

The generated event packet specifies source integration work. It does not
execute missing racer/HUD/controller/effects/menu reset closures or play audio.
"""
from pathlib import Path
import argparse
import hashlib
import os
import re
from import_source_art import ROOT, DECOMP, ArtError, output_location, verify_source
from records_source import function, replace_once
from course_menu_source import declaration
from results_source import select_us

GAME = DECOMP/'src/game/game.c'
RACE = DECOMP/'src/overlays/ovl_i2/race.c'
TRANSITION = DECOMP/'src/overlays/ovl_i2/transition.c'
TRANSITION_H = TRANSITION.with_suffix('.h')
MENUS = DECOMP/'src/overlays/ovl_i3/menus.c'
MATH = DECOMP/'src/sys/math.c'


def span(text, start, end):
    if text.count(start) != 1 or text.count(end) != 1:
        raise ArtError('Retry source span changed: '+start)
    return text[text.index(start):text.index(end)]


def ids_header():
    out = ['/* Generated original retry identifiers; never hand-edit. */']
    header = TRANSITION_H.read_text()
    for name in ('TransitionState','TransitionAppearType','TransitionType','TransitionFadeState'):
        out.append(declaration(header, r'^typedef enum '+name+r' \{.*?^\} \w+;', name))
    for line in header.splitlines():
        if re.match(r'#define (FADE_|TRANSITION_STATE_INACTIVE|TRANSITION_FLAG_(DRAW|FINISHED))',line):
            out.append(line)
    game_h = (DECOMP/'include/fzx_game.h').read_text()
    # Pull the source enums, including their original numerical values.
    for name in ('GameMode','GameModeChangeState','MenuChangeMode'):
        out.append(select_us(declaration(game_h,r'^typedef enum '+name+r' \{.*?^\} \w+;',name)))
    out.append(declaration(MENUS.with_suffix('.h').read_text(),r'^#define MENU_STATE_RETRY\s+[^\n]+','retry'))
    math_h = (DECOMP/'include/fzx_math.h').read_text()
    for name in ('LCG_MULTIPLIER','LCG_INCREMENT_1','LCG_INCREMENT_2'):
        out.append(declaration(math_h,r'^#define '+name+r'\s+[^\n]+',name))
    return ('\n'.join(out)+'\n').encode()


PRELUDE = r'''/* Generated retry schedule; regenerate, never edit. */
#include "fzx_retry_flow.h"
#include "retry_flow_ids.h"
#include <string.h>
typedef int32_t s32;
typedef uint32_t u32;
typedef fzx_retry_fade_t Transition;
typedef fzx_retry_fade_info_t TransitionInfo;
static fzx_retry_flow_t *m;
#define gGameMode GAMEMODE_TIME_ATTACK
#define gGameModeChangeState m->mode_state
#define gMenuChangeMode m->menu_change
#define sMenuStateFlags m->menu_flags
#define sMenuOptionTriggered m->triggered
#define D_i3_801419A6 m->delay
#define gTransitionState m->transition_state
static void event(unsigned id) {
    if(m->event_count>=FZX_RETRY_EVENT_MAX){m->fault=1;return;}
    m->events[m->event_count++]=(uint8_t)id;
}
static void func_80095144(void){event(FZX_RETRY_SETTLE_CONTROLLERS_LIVES);}
static void func_8007E08C(void){event(FZX_RETRY_AUDIO_RESTORE);}
static void func_80068DCC(void){event(FZX_RETRY_AUDIO_MODE);}
static void func_80079F1C(void){event(FZX_RETRY_OBJECTS_RESET);}
static void func_i3_8012F324(void){event(FZX_RETRY_HUD_RESET);}
static void Controller_Reset(void){event(FZX_RETRY_CONTROLLER_RESET);}
static void Racer_Init(void){event(FZX_RETRY_RACER_INIT);}
static void Camera_Init(void){event(FZX_RETRY_CAMERA_INIT);}
static void Effects_Init(void){event(FZX_RETRY_EFFECTS_INIT);}
static void func_8006D414(void){event(FZX_RETRY_GADGETS_RESET);}
static void Menus_Init(void){event(FZX_RETRY_MENUS_INIT);m->triggered=m->delay=m->menu_flags=0;}
static void Hud_ResetLivesChangeCounter(void){event(FZX_RETRY_LIVES_COUNTER_RESET);}
static void Hud_InitRacePortraits(void){event(FZX_RETRY_PORTRAITS_INIT);}
'''

BRIDGE = r'''
static bool valid(const fzx_retry_flow_t *s) {
    return s && s->initialized==1 && !s->fault && s->event_count<=FZX_RETRY_EVENT_MAX &&
        (s->mode_state==GAMEMODE_UPDATE || s->mode_state==GAMEMODE_CHANGE_WAIT_TRANSITION_RELOAD) &&
        (s->menu_change==MENU_CHANGE_INACTIVE || s->menu_change==MENU_CHANGE_RETRY) &&
        (s->menu_flags==0 || s->menu_flags==MENU_STATE_RETRY) &&
        (s->triggered==0 || s->triggered==1) && s->delay==0 && s->reload_count<=1 &&
        s->transition_state>=TRANSITION_INACTIVE && s->transition_state<=TRANSITION_HIDING &&
        s->fade.timer>=0 && s->fade.timer<=21 && s->fade.argument==0 &&
        s->fade.state>=0 && s->fade.state<=FADE_FINISHED &&
        (s->fade.activeTransitionType==TRANSITION_TYPE_NONE || s->fade.activeTransitionType==TRANSITION_TYPE_FADE) &&
        (s->fade.queuedTransitionType==TRANSITION_TYPE_NONE || s->fade.queuedTransitionType==TRANSITION_TYPE_FADE);
}
bool fzx_retry_flow_begin(fzx_retry_flow_t *out,uint32_t frame) {
    if(!out || frame==UINT32_MAX)return false;
    fzx_retry_flow_t next={0};next.initialized=1;next.frame=frame;
    next.mode_state=GAMEMODE_UPDATE;next.menu_change=MENU_CHANGE_INACTIVE;
    next.menu_flags=MENU_STATE_RETRY;*out=next;return true;
}
bool fzx_retry_flow_step(fzx_retry_flow_t *state,uint32_t frame) {
    if(!valid(state) || state->frame==UINT32_MAX || frame!=state->frame+1 || state->tick==UINT32_MAX)return false;
    fzx_retry_flow_t next=*state;m=&next;next.frame=frame;++next.tick;
    next.event_count=0;memset(next.events,0,sizeof(next.events));
    source_game_before_race();
    source_menu_dispatch();
    event(FZX_RETRY_RACE_UPDATE_REMAINDER);
    /* game.c tail follows Race_Update and func_80068F04 (inactive title demo). */
    if(gGameModeChangeState==GAMEMODE_UPDATE)Transition_Update();
    m=NULL;if(!valid(&next))return false;*state=next;return true;
}
'''


def closure(game=None, race=None, transition=None, menus=None):
    game=GAME.read_text() if game is None else game
    game=select_us(re.sub(r'(?m)^(\s*#endif)\s*/\* PORT \*/\s*$',r'\1',game))
    race=select_us(RACE.read_text() if race is None else race)
    transition=select_us(TRANSITION.read_text() if transition is None else transition)
    menus=select_us(MENUS.read_text() if menus is None else menus)
    chunks=[PRELUDE]
    for name in ('Transition_FadeInit','Transition_FadeUpdate'):
        chunks.append(function(transition,name))
    pop=function(transition,'Transition_PopQueue')
    pop_prefix=span(pop,'    transition->activeTransitionType =','    switch (transition->activeTransitionType)')
    fade_case=span(pop,'        case TRANSITION_TYPE_FADE:', '        case TRANSITION_TYPE_WIPE:')
    chunks.append('static void Transition_PopQueue(Transition *transition) {\n'+pop_prefix+
                  'switch(transition->activeTransitionType){\n'+fade_case+'default:m->fault=1;break;}}\n')
    update=function(transition,'Transition_Update')
    update=replace_once(update,'Transition* transition = &sTransition;','Transition* transition = &m->fade;')
    switch=span(update,'        switch (transition->activeTransitionType)',
                '    if (gTransitionState != TRANSITION_INACTIVE')
    update=replace_once(update,switch,'''        switch (transition->activeTransitionType) {
            case TRANSITION_TYPE_NONE: break;
            case TRANSITION_TYPE_FADE: Transition_FadeUpdate(transition); break;
            default:m->fault=1;break;
        }
''')
    chunks.append(update)
    # Validate Time Attack selects ordinary fades, with no random transition.
    for name,mode,direction in [('Transition_HideSet','gGameMode','HIDE'),('Transition_AppearSet','gQueuedGameMode','APPEAR')]:
        original=function(transition,name)
        ta=original[original.index('case GAMEMODE_TIME_ATTACK:'):]
        first_call=re.search(r'Transition_Queue\([^;]+;',ta)
        expected='Transition_Queue(TRANSITION_'+direction+', TRANSITION_TYPE_FADE);'
        if not first_call or first_call[0]!=expected:raise ArtError('Time Attack fade changed: '+name)
        chunks.append('static void '+name+'(void){\n'
                      'gTransitionState=TRANSITION_'+('HIDING' if direction=='HIDE' else 'APPEARING')+';\n'
                      'm->fade.queuedTransitionType=TRANSITION_TYPE_FADE;\n'
                      'm->fade.appearType=TRANSITION_'+direction+';m->fade.argument=0;}\n')
    reset=function(race,'func_i2_80103A70')
    reset=replace_once(reset,'    gGamePaused = false;', '    event(FZX_RETRY_CLEAR_PAUSE);')
    reset=replace_once(reset,'    Hud_InitRacePortraits();','    Hud_InitRacePortraits();\n    ++m->reload_count;')
    chunks.append(reset)
    dispatch=function(menus,'Menus_Update')
    dispatch=span(dispatch,'        if (!sMenuOptionTriggered && (D_i3_801419A6 == 0)) {',
                  '            if (sMenuStateFlags & MENU_STATE_QUIT) {')+'        }\n'
    chunks.append('static void source_menu_dispatch(void){\n'+dispatch+'    sMenuStateFlags=0;\n}\n')
    process=function(game,'func_80068BC0')
    retry=span(process,'        case MENU_CHANGE_RETRY:', '        case MENU_CHANGE_QUIT:')
    before=function(game,'func_800690FC')
    # Verify the ordering on which next-tick dispatch and same-tick appear rely.
    if not (before.index('func_80068BC0();') < before.index('gMenuChangeMode = MENU_CHANGE_INACTIVE;') <
            before.index('gQueuedGameMode = sGamemodeUpdateFuncs') < before.rindex('Transition_Update();')):
        raise ArtError('Original retry game/race/transition ordering changed')
    cases=span(before[:before.index('    gQueuedGameMode = sGamemodeUpdateFuncs')],
               '        case GAMEMODE_CHANGE_START_RELOAD:', '        case GAMEMODE_UPDATE:')
    chunks.append('static void source_game_before_race(void){\n'
                  'if(gGameModeChangeState==GAMEMODE_UPDATE){switch(gMenuChangeMode){\n'+retry+
                  'default:break;}}\ngMenuChangeMode=MENU_CHANGE_INACTIVE;\n'
                  'switch(gGameModeChangeState){\n'+cases+'default:break;}}\n')
    chunks.append(BRIDGE)
    chunks.append(rng_closure(menus))
    return ('\n'.join(chunks)+'\n').encode()


def rng_closure(menus):
    init=function(menus,'Menus_Init')
    # Preserve original evaluation order and expressions, not a magic advance27.
    first=span(init,'    sVsSlotInitialSpeed[VS_SLOT_0] =', '    gRacersRemaining =')
    initial=init[:init.index('        sPlayerRetireGameoverFadeTransitionTimer')]
    loop=initial[initial.index('        for (j = 0; j < 3; j++) {'):]
    if first.count('Math_Rand1()')!=1 or first.count('Math_Rand2()')!=2 or loop.count('Math_Rand2()')!=2:
        raise ArtError('Menus_Init RNG span changed')
    if init.count('for (i = 0; i < 4; i++) {')!=1:raise ArtError('Menus_Init player loop changed')
    out=[r'''
static fzx_retry_rng_t *r;
#define gRandSeed1 r->seed1
#define gRandMask1 r->mask1
#define gRandSeed2 r->seed2
#define gRandMask2 r->mask2
''']
    for stream in (1,2):
        fn=function(select_us(MATH.read_text()),'Math_Rand%d'%stream)
        fn=replace_once(fn,'Math_Rand%d(void) {'%stream,'Math_Rand%d(void) {\n    ++r->calls%d;'%(stream,stream))
        out.append(fn)
    out.append('''bool fzx_retry_menus_init_rng(fzx_retry_rng_t *rng){
    if(!rng)return false;
    fzx_retry_rng_t next=*rng;r=&next;r->calls1=r->calls2=0;
    enum{VS_SLOT_0,VS_SLOT_1,VS_SLOT_2};
    float sVsSlotInitialSpeed[3],sVsSlotScrollPosition[4][3];
    s32 sVsSlotResultIndex[4][3],i,j;
'''+first+'    for(i=0;i<4;i++){\n'+loop+'    }\n'+'''
    (void)sVsSlotInitialSpeed;(void)sVsSlotScrollPosition;(void)sVsSlotResultIndex;
    r=NULL;*rng=next;return true;
}
''')
    return '\n'.join(out)


def dependencies():
    return [Path(__file__),ROOT/'include/fzx_retry_flow.h',ROOT/'tools/records_source.py',
            ROOT/'tools/results_source.py',ROOT/'tools/course_menu_source.py',ROOT/'tools/import_source_art.py',
            GAME,RACE,TRANSITION,TRANSITION_H,MENUS,MENUS.with_suffix('.h'),MATH,
            DECOMP/'include/fzx_game.h',DECOMP/'include/fzx_math.h']


def generate(output):
    output_location(output);verify_source(DECOMP)
    outputs={'retry_flow_source.c':closure(),'retry_flow_ids.h':ids_header()}
    receipt=['# fzx generation receipt v1; regenerate, never edit']
    receipt+=['generator %s %s'%(hashlib.sha256(p.read_bytes()).hexdigest(),
              os.path.relpath(p,ROOT/'tools').replace('\\','/')) for p in dependencies()]
    output.mkdir(parents=True,exist_ok=True)
    for name,data in outputs.items():
        (output/name).write_bytes(data);receipt.append('output %s %s'%(hashlib.sha256(data).hexdigest(),name))
    (output/'retry_flow_source.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    print('Original retry schedule generated; reset event implementations remain integration work')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'generated/retry-flow-source')
    generate(parser.parse_args().output)
