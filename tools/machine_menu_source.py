#!/usr/bin/env python3
"""Bind stock cartridge machine choice and tuning to bounded native packets.

Original update functions own selection, confirmation, cancellation and engine
balance. This closure is one-player Time Attack; unsupported race content is
an explicit consumer decision, never silently replaced with Blue Falcon.
"""
from pathlib import Path
import argparse
import hashlib
import os
import re
from import_source_art import ROOT, DECOMP, ArtError, output_location, verify_source
from menu_source import cartridge, function, COMMON, OBJECT, CONTROLLER, YAML
from course_menu_source import declaration
from title_source import sound_header

SOURCE=DECOMP/'src/overlays/ovl_i4/machine.c'
SOURCE_HEADER=SOURCE.parent/'ovl_i4.h'

PRELUDE=r'''/* Generated original US machine selection. Regenerate, never edit. */
#include "fzx_machine_menu.h"
#include "machine_menu_game_modes.h"
#include "machine_menu_sound_ids.h"
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
typedef fzx_machine_menu_object_t Object;
typedef struct { uint16_t buttonCurrent,pressed,input; int8_t stickX; } Controller;
static fzx_machine_menu_state_t *m;
static Controller source_controllers[4];
static Controller source_shared;
static const int32_t source_ports[4]={0,1,2,3};
_Static_assert(CHAR_MIN<0,"Original font indexing requires signed char");
#define OBJECT_STATE(o) ((o)->state)
#define OBJECT_LEFT(o) ((o)->left)
#define OBJECT_TOP(o) ((o)->top)
#define OBJECT_COUNTER(o) ((o)->counter)
#define OBJECT_CACHE_INDEX(o) ((o)->cacheIndex)
#define MACHINE_MINI_STATE(o) ((o)->counter2)
#define PORTRAIT_CHARACTER(o) ((o)->state)
#define NAME_CARD_CHARACTER(o) ((o)->state)
#define SQ(x) ((x)*(x))
#define ARRAY_COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define BTN_A 0x8000u
#define BTN_B 0x4000u
#define BTN_Z 0x2000u
#define BTN_START 0x1000u
#define BTN_UP 0x0800u
#define BTN_DOWN 0x0400u
#define BTN_LEFT 0x0200u
#define BTN_RIGHT 0x0100u
#define BTN_L 0x0020u
#define BTN_R 0x0010u
#define BTN_CUP 0x0008u
#define BTN_CDOWN 0x0004u
#define BTN_CLEFT 0x0002u
#define BTN_CRIGHT 0x0001u
#define TRANSITION_INACTIVE 0
#define COURSE_EDIT_1 24
#define gNumPlayers 1
#define gSelectedMode m->config.mode
#define gCourseIndex m->config.course
#define gGameMode m->game_mode
#define gGameFrameCount m->frame
#define gTransitionState m->transition
#define gSettingEverythingUnlocked m->config.everything_unlocked
#define gMachineSelectState m->select_phase
#define gMachineSettingsState m->settings_phase
#define gRacers m->racers
#define sMachineSelectIndex m->indices
#define sMachinesUnlocked m->unlocked
#define gPlayerSelectionLock m->locks
#define D_800CD3AC m->changed
#define gPlayerEngine m->engine
#define gCharacterLastEngine m->last_engine
#define gInputPressed m->input
#define gInputButtonPressed m->pressed
#define gControllers source_controllers
#define gSharedController source_shared
#define gPlayerControlPorts source_ports
#define Camera_Init() ((void)0)
#define Camera_Update() ((void)++m->camera_updates)
#define func_8008969C() ((void)0)
#define func_8007E008() ((int32_t)m->config.unlock_count)
#define func_8007E0CC() ((void)0)
#define Save_UpdateCharacterSave(course) ((void)(course))
static int32_t source_unused_init;
#define D_800CCFE8 source_unused_init
static float source_scale_x,source_scale_y,source_scale_z,source_mini_x,source_mini_y;
#define D_800CE748 source_scale_x
#define D_800CE74C source_scale_y
#define D_800CE750 source_scale_z
#define D_i4_8011D778 source_mini_x
#define D_i4_8011D77C source_mini_y
static void Controller_SetGlobalInputs(const Controller *c) {
    m->pressed=c->pressed;m->input=c->input;
}
static void record_cue(uint16_t cue) {
    if(m->cue_count>=8){m->fault=1;return;}
    m->cues[m->cue_count++]=cue;
}
#define Audio_TriggerSystemSE(cue) record_cue((uint16_t)(cue))
#define Audio_PlayerTriggerSEStart(player,cue) ((void)(player),record_cue((uint16_t)(cue)))
static void Object_Init(int32_t cmdId,int32_t left,int32_t top,int32_t priority);
static Object *Object_Get(int32_t cmdId);
'''

BRIDGE=r'''
static bool valid_config(const fzx_machine_menu_config_t *c) {
    if(!c || c->mode!=MODE_TIME_ATTACK || c->course>=24 || c->character>=30 ||
       c->skin>=4 || c->unlock_count>12 || c->everything_unlocked>1)return false;
    for(unsigned i=0;i<30;++i)if(!(c->last_engine[i]>=0.0f && c->last_engine[i]<=1.0f))return false;
    return true;
}
static bool valid(const fzx_machine_menu_state_t *s) {
    if(!s || !valid_config(&s->config) || s->fault || s->screen>1 || s->connected>1 ||
       s->select_phase<0 || s->select_phase>3 || s->settings_phase<0 || s->settings_phase>3 ||
       s->object_count>=FZX_MACHINE_MENU_OBJECT_MAX || s->snapshot.count>FZX_MACHINE_MENU_DRAW_MAX ||
       s->cue_count>8 || s->cache_count>16 || s->request>FZX_MACHINE_REQUEST_RACE ||
       s->unlocked<6 || s->unlocked>30 || (s->unlocked%6) ||
       s->game_mode!=(s->screen?GAMEMODE_LX_MACHINE_SETTINGS:GAMEMODE_FLX_MACHINE_SELECT))return false;
    for(unsigned i=0;i<4;++i)if(s->racers[i].character<0 || s->racers[i].character>=30 ||
        s->racers[i].machineSkinIndex<0 || s->racers[i].machineSkinIndex>=4 ||
        s->indices[i]<0 || s->indices[i]>=30 || s->locks[i]<0 || s->locks[i]>1 ||
        s->changed[i]<0 || s->changed[i]>1 || !(s->engine[i]>=0 && s->engine[i]<=1))return false;
    for(unsigned i=0;i<30;++i)if(!(s->last_engine[i]>=0 && s->last_engine[i]<=1))return false;
    for(unsigned i=0;i<s->object_count;++i) {
        const Object *o=&s->objects[i];
        if(o->priority>15 || o->shouldDraw>1 || o->left< -500 || o->left>500 ||
           o->top< -500 || o->top>500 || o->cacheIndex<0 || o->cacheIndex>=16)return false;
    }
    return true;
}
static void capture(void) {
    source_render_step();
    fzx_machine_menu_snapshot_t *s=&m->snapshot;
    s->tick=m->ticks;s->frame=m->frame;s->model_ticks=m->model_ticks;s->camera_updates=m->camera_updates;
    s->screen=m->screen;s->phase=(uint8_t)(m->screen?m->settings_phase:m->select_phase);
    s->character=(uint8_t)m->racers[0].character;s->skin=(uint8_t)m->racers[0].machineSkinIndex;
    s->slot=(uint8_t)Character_GetSlotFromCharacter(s->character);
    s->locked=(uint8_t)m->locks[0];s->unlocked=(uint8_t)m->unlocked;
    s->engine=m->engine[0];s->request=m->request;s->object_count=m->object_count;
    memcpy(s->objects,m->objects,sizeof(s->objects));
}
bool fzx_machine_menu_reset(fzx_machine_menu_state_t *s,const fzx_machine_menu_config_t *config,uint8_t screen) {
    if(!s || !valid_config(config) || screen>1)return false;
    const fzx_machine_menu_config_t copy=*config;
    memset(s,0,sizeof(*s));s->config=copy;s->screen=screen;s->frame=copy.frame;s->connected=1;
    memcpy(s->last_engine,copy.last_engine,sizeof(s->last_engine));
    for(unsigned i=0;i<4;++i){s->engine[i]=0.5f;s->changed[i]=1;}
    s->racers[0].character=copy.character;s->racers[0].machineSkinIndex=copy.skin;
    s->unlocked=copy.everything_unlocked?30:((copy.unlock_count/3)*6+6);
    s->game_mode=screen?GAMEMODE_LX_MACHINE_SETTINGS:GAMEMODE_FLX_MACHINE_SELECT;
    memset(source_controllers,0,sizeof(source_controllers));memset(&source_shared,0,sizeof(source_shared));
    m=s;
    if(screen)MachineSettings_Init();else MachineSelect_Init();
    capture();m=NULL;return valid(s);
}
bool fzx_machine_menu_step(fzx_machine_menu_state_t *s,uint16_t buttons,int8_t stick_x,bool connected,bool transition) {
    if(!valid(s) || (buttons&~0xFF10u) || stick_x< -80 || stick_x>80 ||
       s->ticks==UINT32_MAX || s->frame==UINT32_MAX || s->same_input_ticks==UINT32_MAX ||
       s->model_ticks==UINT32_MAX || s->camera_updates==UINT32_MAX)return false;
    if(s->request)return true;
    s->connected=(uint8_t)connected;s->transition=transition;
    if(!connected){buttons=0;stick_x=0;}
    s->pressed=(uint16_t)(buttons & (uint16_t)~s->held);
    if(buttons && buttons==s->held)++s->same_input_ticks;else s->same_input_ticks=0;
    s->input=s->pressed;
    if(s->same_input_ticks>=15 && ((s->same_input_ticks-15)%5)==0)s->input|=buttons;
    s->held=buttons;s->stick_x=stick_x;s->cue_count=0;memset(s->cues,0,sizeof(s->cues));
    memset(source_controllers,0,sizeof(source_controllers));
    source_controllers[0]=(Controller){buttons,s->pressed,s->input,stick_x};source_shared=source_controllers[0];
    ++s->ticks;++s->frame;m=s;
    const int32_t result=s->screen?MachineSettings_Update():MachineSelect_Update();
    if(result!=s->game_mode) {
        switch(result) {
            case GAMEMODE_FLX_COURSE_SELECT:s->request=FZX_MACHINE_REQUEST_COURSE;break;
            case GAMEMODE_LX_MACHINE_SETTINGS:s->request=FZX_MACHINE_REQUEST_SETTINGS;break;
            case GAMEMODE_FLX_MACHINE_SELECT:s->request=FZX_MACHINE_REQUEST_SELECT;break;
            case GAMEMODE_TIME_ATTACK:s->request=FZX_MACHINE_REQUEST_RACE;break;
            default:s->fault=1;break;
        }
    }
    capture();m=NULL;return valid(s);
}
bool fzx_machine_menu_snapshot(const fzx_machine_menu_state_t *s,fzx_machine_menu_snapshot_t *out) {
    if(!out || !valid(s))return false;
    *out=s->snapshot;return true;
}
'''

def closure(source):
    from machine_menu_draw_source import prelude, fragments
    from machine_menu_assets import census
    source=cartridge(source);common=cartridge(COMMON.read_text())
    chunks=[PRELUDE]
    for path,names in [(SOURCE_HEADER,('MachineSelectState','MachineSettingsState','PlayerSelectionLockState')),
                       (DECOMP/'include/fzx_object.h',('ObjectIds',))]:
        for name in names:
            chunks.append(cartridge(declaration(path.read_text(),r'^typedef enum '+name+r' \{.*?^\} '+name+';',name)))
    chunks.append('static '+declaration(common,r'^const s8 kMachineSelectCharacterList\[\] = \{.*?\};','machine slots'))
    for name in ('Character_GetCharacterFromSlot','Character_GetSlotFromCharacter'):
        chunks.append(function(common,name))
    info=census()
    chunks.append(prelude(info))
    chunks.append(fragments(source))
    for name in ('MachineSelect_Init','MachineSettings_Init','MachineSelect_Update','MachineSettings_Update'):
        body=function(source,name)
        if name=='MachineSelect_Update':
            for kind,var in [('s32','pad'),('s16','pad2')]:
                before='    '+kind+' '+var+';'
                if body.count(before)!=1:raise ArtError('Original unused local changed: '+var)
                body=body.replace(before,before+'\n    (void)sizeof('+var+');')
        chunks.append(body)
    controller=CONTROLLER.read_text()
    if 's32 D_800CD160 = 15;' not in controller or 's32 D_800CD164 = 5;' not in controller:
        raise ArtError('Original input repeat cadence changed')
    chunks.append(BRIDGE)
    return '\n'.join(chunks).encode()

def generate(output):
    from machine_menu_draw_source import dependencies as draw_dependencies
    output_location(output);verify_source(DECOMP)
    outputs={'machine_menu_source.c':closure(SOURCE.read_text()),
             'machine_menu_game_modes.h':(DECOMP/'include/fzx_game.h').read_bytes(),
             'machine_menu_sound_ids.h':sound_header((DECOMP/'include/sfx.h').read_text())}
    inputs=[Path(__file__),ROOT/'include/fzx_machine_menu.h',ROOT/'tools/import_source_art.py',
            ROOT/'tools/menu_source.py',ROOT/'tools/course_menu_source.py',ROOT/'tools/title_source.py',
            ROOT/'tools/machine_menu_assets.py',ROOT/'tools/machine_menu_draw_source.py',
            SOURCE,SOURCE_HEADER,COMMON,OBJECT,CONTROLLER,YAML,DECOMP/'include/fzx_game.h',
            DECOMP/'include/fzx_object.h',DECOMP/'include/sfx.h',*draw_dependencies()]
    output.mkdir(parents=True,exist_ok=True)
    receipt=['# fzx generation receipt v1; regenerate, never edit']
    receipt+=['generator %s %s'%(hashlib.sha256(p.read_bytes()).hexdigest(),os.path.relpath(p,ROOT/'tools').replace('\\','/'))for p in dict.fromkeys(inputs)]
    for name,data in outputs.items():
        (output/name).write_bytes(data);receipt.append('output %s %s'%(hashlib.sha256(data).hexdigest(),name))
    (output/'machine_menu_source.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    print('Original US machine selection/settings closure generated')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'generated/machine-menu-source')
    args=parser.parse_args();generate(args.output)
