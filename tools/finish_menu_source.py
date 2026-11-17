#!/usr/bin/env python3
"""Extract the original US Time Attack finish-menu state and action requests.

The bounded closure supports the source no-recorded-ghost path. It ends at an
action request; race reload, nested ghost saving, audio and drawing are separate
consumers. Both original random streams are explicit caller-owned state.
"""
from pathlib import Path
import argparse
import hashlib
import os
import re
from import_source_art import ROOT, DECOMP, ArtError, output_location, verify_source
from course_menu_source import declaration
from results_source import select_us
from records_source import function, replace_once

SOURCE = DECOMP/'src/overlays/ovl_i3/menus.c'
HEADER = SOURCE.with_suffix('.h')
MATH = DECOMP/'src/sys/math.c'


def ids_header():
    source = HEADER.read_text()
    out = ['/* Generated original finish-menu IDs; regenerate, never edit. */']
    for name in ('TimeAttackFinishOptions', 'GhostSaveMenuState'):
        out.append(select_us(declaration(source, r'^typedef enum '+name+r' \{.*?^\} \w+;', name)))
    out.append(declaration(select_us(SOURCE.read_text()),r'^#define TIME_ATTACK_FINISH_LAST\s+[^\n]+','last finish option'))
    for name in ('RETRY','QUIT','SETTINGS','CHANGE_MACHINE','CHANGE_COURSE'):
        out.append(declaration(source, r'^#define MENU_STATE_'+name+r'\s+[^\n]+',name))
    sound = (DECOMP/'include/sfx.h').read_text()
    out.append(select_us(declaration(sound,r'^typedef enum SfxId \{.*?^\} \w+;','SfxId')))
    math = (DECOMP/'include/fzx_math.h').read_text()
    for name in ('LCG_MULTIPLIER','LCG_INCREMENT_1','LCG_INCREMENT_2'):
        out.append(declaration(math,r'^#define '+name+r'\s+[^\n]+',name))
    pad = (DECOMP/'include/PR/os_cont.h').read_text()
    for name in ('A','START','UP','DOWN'):
        out.append(declaration(pad,r'^#define CONT_'+name+r'\s+[^\n]+',name).replace('CONT_'+name,'BTN_'+name))
    return ('\n'.join(out)+'\n').encode()


PRELUDE = r'''/* Generated original finish-menu state; regenerate, never edit. */
#include "fzx_finish_menu.h"
#include "finish_menu_source_ids.h"
#include <string.h>
typedef int32_t s32;
typedef uint32_t u32;
typedef unsigned Gfx;
typedef struct { s32 stickY; uint16_t buttonPressed; } Controller;
static fzx_finish_menu_state_t *m;
static Controller controllers[1];
static const s32 ports[1]={0};
#define gControllers controllers
#define gPlayerControlPorts ports
#define TITLE_DEMO_INACTIVE 0
#define gTitleDemoState TITLE_DEMO_INACTIVE
#define sCannotSaveGhost 1
#define sRaceMenuOptionIndex m->snapshot.selection
#define sMenuHighlightedOptionMoveTimer m->snapshot.move_timer
#define sGeneralRaceMenuScissorBoxTimer m->snapshot.reveal
#define sSaveGhostMenuOptionsScissorBoxTimer m->snapshot.ghost_options_reveal
#define sGhostSaveMenuScissorBoxTimer m->snapshot.ghost_reveal
#define sSaveGhostMenuOptionState m->snapshot.ghost_option_state
#define sSaveGhostMenuOptionIndex m->snapshot.ghost_option
#define sOverwriteGhostOptionIndex m->snapshot.overwrite_option
#define sSaveGhostMenuOpen m->snapshot.ghost_open
#define sGhostSaveTimer m->snapshot.ghost_timer
#define sSaveGhostMenuState m->snapshot.ghost_state
#define sMenuIsBusy m->snapshot.busy
#define sMenuStateFlags m->snapshot.state_flags
/* Unsigned storage expresses the original 32-bit wrapping RNG arithmetic. */
#define gRandSeed1 m->snapshot.seed1
#define gRandMask1 m->snapshot.mask1
#define gRandSeed2 m->snapshot.seed2
#define gRandMask2 m->snapshot.mask2
static void event(unsigned kind,unsigned id) {
    if(m->snapshot.event_count>=FZX_FINISH_MENU_EVENT_MAX){m->fault=1;return;}
    fzx_finish_menu_event_t *e=&m->snapshot.events[m->snapshot.event_count++];
    e->kind=(uint8_t)kind;e->id=(uint16_t)id;
}
static void Audio_TriggerSystemSE(s32 id){event(FZX_FINISH_MENU_SOUND,(unsigned)id);}
static void Audio_LevelSEFadeout(void){event(FZX_FINISH_MENU_LEVEL_FADE,0);}
static void func_8007E0CC(void){event(FZX_FINISH_MENU_AUDIO_STOP,0);}
/* Unreachable in the admitted no-ghost scope. Fail closed if source drift or
 * a future edit enters a nested menu; no save is simulated by these guards. */
static Gfx *Menus_DrawGhostSaveMenu(Gfx *gfx){m->fault=1;return gfx;}
static Gfx *Menus_DrawSaved(Gfx *gfx){m->fault=1;return gfx;}
static Gfx *Menus_DrawSaving(Gfx *gfx){m->fault=1;return gfx;}
static s32 Menus_AttemptSaveGhost(void){m->fault=1;return -1;}
'''

BRIDGE = r'''
static bool valid(const fzx_finish_menu_state_t *s) {
    if(!s||s->initialized!=1||s->fault)return false;
    const fzx_finish_menu_snapshot_t *p=&s->snapshot;
    return p->selection>=0&&p->selection<=TIME_ATTACK_FINISH_LAST&&
        p->selection!=TIME_ATTACK_SAVE_GHOST&&p->move_timer>=0&&p->move_timer<=74&&
        p->reveal>=0&&p->reveal<=60&&!p->ghost_open&&!p->ghost_timer&&!p->ghost_state&&
        p->event_count<=FZX_FINISH_MENU_EVENT_MAX&&p->action<=FZX_FINISH_MENU_QUIT&&
        p->busy==(p->action!=FZX_FINISH_MENU_NONE);
}
bool fzx_finish_menu_begin(fzx_finish_menu_state_t *s,const fzx_records_snapshot_t *records,bool absent) {
    if(!s||!records||!absent||records->phase!=FZX_RECORDS_FINISH_MENU_READY||
       records->config.course!=0)return false;
    fzx_finish_menu_state_t next={0};next.initialized=1;next.snapshot.reveal=60;
    next.snapshot.ghost_options_reveal=next.snapshot.ghost_reveal=90;
    next.snapshot.input.frame=records->frame;
    *s=next;return true;
}
bool fzx_finish_menu_step(fzx_finish_menu_state_t *s,const fzx_finish_menu_input_t *in) {
    if(!valid(s)||!in||in->connected>1||in->stick_y < -80||in->stick_y>80||
       (!in->connected&&(in->pressed||in->stick_y))||s->snapshot.tick==UINT32_MAX)return false;
    if(s->snapshot.action)return true;
    if((!s->snapshot.tick&&in->frame!=s->snapshot.input.frame)||
       (s->snapshot.tick&&(s->snapshot.input.frame==UINT32_MAX||in->frame!=s->snapshot.input.frame+1u)))return false;
    fzx_finish_menu_state_t next=*s;m=&next;
    fzx_finish_menu_snapshot_t *p=&next.snapshot;
    p->input=*in;p->seed1=in->seed1;p->mask1=in->mask1;p->seed2=in->seed2;p->mask2=in->mask2;
    p->rng1_calls=p->rng2_calls=0;p->event_count=0;memset(p->events,0,sizeof(p->events));
    controllers[0].stickY=in->stick_y;controllers[0].buttonPressed=in->pressed;
    Gfx dummy=0;Menus_DrawTimeAttackFinishMenu(&dummy);
    if(p->state_flags&MENU_STATE_RETRY)p->action=FZX_FINISH_MENU_RETRY;
    else if(p->state_flags&MENU_STATE_SETTINGS)p->action=FZX_FINISH_MENU_SETTINGS;
    else if(p->state_flags&MENU_STATE_CHANGE_MACHINE)p->action=FZX_FINISH_MENU_MACHINE;
    else if(p->state_flags&MENU_STATE_CHANGE_COURSE)p->action=FZX_FINISH_MENU_COURSE;
    else if(p->state_flags&MENU_STATE_QUIT)p->action=FZX_FINISH_MENU_QUIT;
    ++p->tick;m=NULL;if(!valid(&next))return false;
    *s=next;return true;
}
bool fzx_finish_menu_snapshot(const fzx_finish_menu_state_t *s,fzx_finish_menu_snapshot_t *out) {
    if(!out||!valid(s))return false;
    *out=s->snapshot;return true;
}
'''


def closure(source):
    source=select_us(source)
    init=function(source,'Menus_Init')
    if 'sGpResultsEndMenuScissorBoxTimer = sGeneralRaceMenuScissorBoxTimer = 60;' not in init:
        raise ArtError('Original finish-menu reveal initialization changed')
    chain=re.search(r'    sGhostSaveTimer =[^;]+;',init)
    zero_names=('sGhostSaveTimer','sOverwriteGhostOptionIndex','sSaveGhostMenuOpen',
        'sSaveGhostMenuOptionIndex','sSaveGhostMenuOptionState','sRaceMenuOptionIndex',
        'sMenuIsBusy','sSaveGhostMenuState','sMenuHighlightedOptionMoveTimer')
    if (not chain or not re.search(r'sMenuHighlightedOptionMoveTimer\s*=\s*0;$',chain[0]) or
        any(not re.search(r'\b'+name+r'\s*=',chain[0]) for name in zero_names)):
        raise ArtError('Original finish-menu selection initialization changed')
    if 'sSaveGhostMenuOptionsScissorBoxTimer = sGhostSaveMenuScissorBoxTimer = 90;' not in init:
        raise ArtError('Original finish-menu ghost reveal initialization changed')
    chunks=[PRELUDE]
    for stream in (1,2):
        code=function(select_us(MATH.read_text()),'Math_Rand%d'%stream)
        code=replace_once(code,'Math_Rand%d(void) {'%stream,
                          'Math_Rand%d(void) {\n    ++m->snapshot.rng%d_calls;'%(stream,stream))
        chunks.append(code)
    chunks.append(function(source,'Menus_UpdateHighlightedOptionVertical'))
    draw=function(source,'Menus_DrawTimeAttackFinishMenu')
    start='    gDPPipeSync(gfx++);'
    end='    if (sSaveGhostMenuOpen) {'
    if draw.count(end)!=1 or start not in draw:raise ArtError('Original finish-menu drawing boundary changed')
    begin=draw.index(start);finish=draw.index(end)
    # This is a state closure, not an interpreter for source display lists.
    # Capture the pre-navigation presentation boundary once per source tick.
    draw=draw[:begin]+('    m->snapshot.draw_selection=sRaceMenuOptionIndex;\n'
                     '    m->snapshot.draw_reveal=sGeneralRaceMenuScissorBoxTimer;\n')+draw[finish:]
    chunks.extend((draw,BRIDGE))
    return ('\n'.join(chunks)+'\n').encode()


def dependencies():
    return [Path(__file__),ROOT/'include/fzx_finish_menu.h',ROOT/'include/fzx_records.h',
            ROOT/'tools/records_source.py',ROOT/'tools/results_source.py',
            ROOT/'tools/course_menu_source.py',ROOT/'tools/import_source_art.py',
            SOURCE,HEADER,MATH,*(DECOMP/'include'/name for name in ('sfx.h','fzx_math.h','PR/os_cont.h'))]


def generate(output):
    output_location(output);verify_source(DECOMP)
    outputs={'finish_menu_source.c':closure(SOURCE.read_text()),'finish_menu_source_ids.h':ids_header()}
    receipt=['# fzx generation receipt v1; regenerate, never edit']
    receipt+=['generator %s %s'%(hashlib.sha256(p.read_bytes()).hexdigest(),
                  os.path.relpath(p,ROOT/'tools').replace('\\','/')) for p in dependencies()]
    output.mkdir(parents=True,exist_ok=True)
    for name,data in outputs.items():
        (output/name).write_bytes(data);receipt.append('output %s %s'%(hashlib.sha256(data).hexdigest(),name))
    (output/'finish_menu_source.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    print('Original finish-menu action state generated; reload, drawing, saving and audio remain separate')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'generated/finish-menu-source')
    generate(parser.parse_args().output)
