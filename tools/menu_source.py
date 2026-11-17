#!/usr/bin/env python3
"""Extract the pinned US cartridge main-menu state/draw closure.

Generated code remains private/ignored. Native presentation consumes bounded
typed image records, never N64 display lists. The first wallpaper is fixed;
cheat wallpaper transitions, persistence and audio playback remain outside.
"""
from pathlib import Path
import argparse
import hashlib
import os
import re
from import_source_art import ROOT, DECOMP, ArtError, output_location, verify_source
from title_source import sound_header

SOURCE = DECOMP/'src/overlays/ovl_i6/main_menu.c'
COMMON = DECOMP/'src/game/common.c'
OBJECT = DECOMP/'src/game/object.c'
CONTROLLER = DECOMP/'src/sys/controller.c'
YAML = DECOMP/'assets/yaml/us/rev0/common_assets_compressed.yaml'
IMAGES = ('aTitleBackgroundMainTex','aMenuSignGpRaceTex','aMenuSignTimeAttackTex',
          'aMenuSignDeathRaceTex','aMenuSignVsBattleTex','aMenuSignPracticeTex',
          'aMenuSignOptionsTex','aSelectModeTex','aSelectModeOptionFlamesTex',
          'aNoviceTex','aStandardTex','aExpertTex','aMasterTex','a2PlayersTex',
          'a3PlayersTex','a4PlayersTex','aTimeAttackTex','aRecordsOptionTex','aOKTex')


def cartridge(source):
    """Select only the three known source conditionals; reject new conditions."""
    stack=[]; active=True; result=[]
    for line in source.splitlines():
        match=re.match(r'\s*#(ifdef|ifndef)\s+(\w+)\s*$',line)
        if match:
            directive,symbol=match.groups()
            if symbol not in ('PORT','EXPANSION_KIT','VERSION_JP','AVOID_UB'):
                raise ArtError('Unknown menu source condition: '+symbol)
            condition=directive=='ifndef'
            stack.append((active,condition));active=active and condition
        elif re.match(r'\s*#else\s*$',line):
            if not stack: raise ArtError('Unbalanced menu source else')
            parent,condition=stack[-1];stack[-1]=(parent,not condition)
            active=parent and not condition
        elif re.match(r'\s*#endif\s*$',line):
            if not stack: raise ArtError('Unbalanced menu source endif')
            active=stack.pop()[0]
        elif re.match(r'\s*#(?:if|elif)\b',line):
            raise ArtError('Unsupported menu source conditional')
        elif active and not line.startswith('#include'):
            result.append(line)
    if stack: raise ArtError('Unterminated menu source conditional')
    return '\n'.join(result)+'\n'


def function(source,name):
    match=re.search(r'^(?:s32|void|Gfx\*) '+re.escape(name)+r'\([^\n]*\) \{.*?^\}',source,re.M|re.S)
    if not match: raise ArtError('Original menu function missing: '+name)
    return 'static '+match.group(0)+'\n'


PRELUDE=r'''/* Generated original US cartridge menu closure. Regenerate, never edit. */
#include "fzx_menu.h"
#include "menu_game_modes.h"
#include "menu_sound_ids.h"
#include <limits.h>
#include <string.h>
typedef int32_t s32;
typedef fzx_menu_object_t Object;
typedef int Gfx;
typedef struct { uint16_t image; int32_t width; } unk_80077D50;
static fzx_menu_state_t *m;
#define OBJECT_LEFT(o) ((o)->left)
#define OBJECT_TOP(o) ((o)->top)
#define OBJECT_COUNTER(o) ((o)->counter)
#define OBJECT_STATE(o) ((o)->state)
#define OBJECT_MAIN_MENU_MODE_SIGN_0 0
#define OBJECT_MAIN_MENU_MODE_SIGN_3 3
#define OBJECT_MAIN_MENU_HEADER 8
#define OBJECT_MAIN_MENU_SELECT_NUM_PLAYERS 9
#define OBJECT_MAIN_MENU_OK 10
#define OBJECT_MAIN_MENU_SELECT_DIFFICULTY 11
#define OBJECT_MAIN_MENU_SELECT_TIME_ATTACK_MODE 12
#define OBJECT_MAIN_MENU_BACKGROUND 13
#define OBJECT_MAIN_MENU_UNLOCK_EVERYTHING 14
#define Object_Get(n) (&m->objects[n])
#define UNLOCK_EVERYTHING_ROMDATA(o) 0x997A
#define SQ(x) ((x)*(x))
#define BTN_A 0x8000u
#define BTN_B 0x4000u
#define BTN_START 0x1000u
#define BTN_UP 0x0800u
#define BTN_DOWN 0x0400u
#define BTN_LEFT 0x0200u
#define BTN_RIGHT 0x0100u
#define TRANSITION_INACTIVE 0
#define COURSE_DEATH_RACE 54
#define Controller_SetGlobalInputs(p) ((void)0)
#define Math_Rand1() ((void)++m->unused_random_calls)
static void record_cue(uint16_t cue) {
    m->cue=cue;
    if(m->cue_count<4)m->cues[m->cue_count++]=cue;else m->fault=1;
}
#define Audio_TriggerSystemSE(c) record_cue((uint16_t)(c))
#define Audio_SetPlayerMode(p) ((void)(m->player_audio_mode=(uint8_t)(p)))
#define func_8007E0CC() ((void)(m->stop_music=1))
#define func_8008B1CC() ((void)(m->course_prepare=1))
#define gGameMode GAMEMODE_FLX_MAIN_MENU
/* Both consumers use frame modulo30/300; reduction avoids signed conversion. */
#define gGameFrameCount ((int32_t)(m->frame%300u))
#define gTransitionState m->transition
#define gGameModeChangeState m->change_state
#define gSelectedMode m->selected
#define gNumPlayers m->players
#define gDifficulty m->difficulty
#define gCourseIndex m->course
#define gUnlockableLevel m->unlock_level
#define gSettingEverythingUnlocked m->everything_unlocked
#define gControllersConnected m->connected
#define gModeSubOption m->suboptions
#define gInputPressed m->input
#define gInputButtonPressed m->pressed
#define D_800CD384 m->phase
#define gMenuChangeMode m->menu_change
static int32_t gAntiPiracyAddedDifficulty;
static const char *gCurrentTrackName;
static const char *gTrackNames[55];
static void color(int32_t r,int32_t g,int32_t b,int32_t a) {
    m->color[0]=(uint8_t)r;m->color[1]=(uint8_t)g;
    m->color[2]=(uint8_t)b;m->color[3]=(uint8_t)a;
}
#define gDPSetPrimColor(gfx,x,y,r,g,b,a) color(r,g,b,a)
static Gfx *emit(Gfx *gfx,const unk_80077D50 *tex,int32_t x,int32_t y,unsigned mode) {
    if(m->snapshot.count>=FZX_MENU_DRAW_MAX || x<INT16_MIN || x>INT16_MAX || y<INT16_MIN || y>INT16_MAX){m->fault=1;return gfx;}
    fzx_menu_draw_t *d=&m->snapshot.draws[m->snapshot.count++];
    d->image=tex->image;d->x=(int16_t)x;d->y=(int16_t)y;
    if(mode==0 && tex->image!=FZX_MENU_HEADER){d->r=d->g=d->b=d->a=255;}
    else {d->r=m->color[0];d->g=m->color[1];d->b=m->color[2];d->a=m->color[3];}
    return gfx;
}
#define func_80078EA0_impl(g,t,x,y,mode,a,b,c,d,e) emit(g,t,x,y,mode)
'''

BRIDGE=r'''
static void render_step(void) {
    Gfx dummy=0,*gfx=&dummy;
    memset(&m->snapshot,0,sizeof(m->snapshot));
    color(255,255,255,255);
    /* Background state0 at x=8 with the original black alpha180 overlay. */
    gfx=emit(gfx,&textures[FZX_MENU_BACKGROUND],8,0,0);
    m->snapshot.draws[0].r=m->snapshot.draws[0].g=m->snapshot.draws[0].b=75;
    MainMenu_OkUpdate(&m->objects[10]);
    for(unsigned i=0;i<8;++i)gfx=MainMenu_SignDraw(gfx,&m->objects[i]);
    gfx=MainMenu_HeaderDraw(gfx,&m->objects[8]);
    gfx=MainMenu_NumPlayersDraw(gfx,&m->objects[9]);
    gfx=MainMenu_OkDraw(gfx,&m->objects[10]);
    gfx=MainMenu_DifficultyDraw(gfx,&m->objects[11]);
    (void)MainMenu_TimeAttackModeDraw(gfx,&m->objects[12]);
    m->snapshot.tick=m->ticks;m->snapshot.phase=(uint8_t)m->phase;
    m->snapshot.selected=(uint8_t)m->selected;
    m->snapshot.suboption=(uint8_t)m->suboptions[m->selected];
    m->snapshot.request=m->request;m->snapshot.players=(uint8_t)m->players;
    m->snapshot.difficulty=(uint8_t)m->difficulty;
}
static bool valid(const fzx_menu_state_t *s) {
    if(!s || s->fault || s->phase<0 || s->phase>6 || s->selected<0 || s->selected>7 ||
       s->selected==4 || s->selected==7 || s->request>FZX_MENU_REQUEST_RECORDS ||
       s->unlock_level<0 || s->unlock_level>3 || s->everything_unlocked<0 || s->everything_unlocked>1 ||
       s->players<1 || s->players>4 || s->difficulty<0 || s->difficulty>3 || s->connected>4 ||
       s->snapshot.count>FZX_MENU_DRAW_MAX || s->cue_count>4)return false;
    for(unsigned i=0;i<8;++i)if(s->suboptions[i]<0 || s->suboptions[i]>kMaxSubOptionLimits[i])return false;
    for(unsigned i=0;i<15;++i)if(s->objects[i].counter < -12 || s->objects[i].counter>12 ||
        s->objects[i].left < -320 || s->objects[i].left>320 || s->objects[i].top<0 || s->objects[i].top>240 ||
        s->objects[i].cmdId!=(int32_t)i || s->objects[i].state!=0)return false;
    return true;
}
bool fzx_menu_reset(fzx_menu_state_t *s,uint8_t selected,uint8_t unlock_level,bool everything_unlocked,uint32_t frame) {
    if(!s || selected>6 || selected==4 || unlock_level>3)return false;
    memset(s,0,sizeof(*s));m=s;
    s->selected=selected;s->unlock_level=unlock_level;s->everything_unlocked=everything_unlocked;
    s->frame=frame;s->players=1;s->connected=1;
    for(unsigned i=0;i<15;++i)s->objects[i].cmdId=(int32_t)i;
    s->objects[8].left=100;s->objects[8].top=13;
    s->objects[9].left=s->objects[11].left=s->objects[12].left=96;
    s->objects[9].top=s->objects[11].top=s->objects[12].top=80;
    s->objects[10].left=50;s->objects[13].left=8;
    render_step();m=NULL;return !s->fault;
}
bool fzx_menu_step(fzx_menu_state_t *s,uint16_t buttons,uint8_t connected,bool transition) {
    if(!valid(s) || connected>4 || s->ticks==UINT32_MAX || s->same_input_ticks==UINT32_MAX)return false;
    if(s->request)return true;
    m=s;s->connected=connected;s->transition=transition;
    s->cue=0;s->cue_count=0;memset(s->cues,0,sizeof(s->cues));s->stop_music=0;s->course_prepare=0;
    if(!connected)buttons=0;
    s->pressed=(uint16_t)(buttons & (uint16_t)~s->held);
    if(buttons && buttons==s->held)++s->same_input_ticks;else s->same_input_ticks=0;
    s->input=s->pressed;
    if(s->same_input_ticks>=15 && ((s->same_input_ticks-15)%5)==0)s->input|=buttons;
    s->held=buttons;++s->ticks;++s->frame;
    const int32_t result=MainMenu_Update();
    if(result==GAMEMODE_FLX_TITLE)s->request=FZX_MENU_REQUEST_TITLE;
    else if(result==GAMEMODE_FLX_MACHINE_SELECT)s->request=FZX_MENU_REQUEST_MACHINE;
    else if(result!=GAMEMODE_FLX_MAIN_MENU)s->fault=1;
    if(s->menu_change==MENU_CHANGE_TO_COURSE_SELECT)s->request=FZX_MENU_REQUEST_COURSE;
    else if(s->menu_change==MENU_CHANGE_TO_OPTIONS)s->request=FZX_MENU_REQUEST_OPTIONS;
    else if(s->menu_change==MENU_CHANGE_TO_RECORDS)s->request=FZX_MENU_REQUEST_RECORDS;
    render_step();m=NULL;return !s->fault;
}
bool fzx_menu_snapshot(const fzx_menu_state_t *s,fzx_menu_snapshot_t *out) {
    if(!valid(s) || !out)return false;
    *out=s->snapshot;return true;
}
'''


def closure(source):
    source=cartridge(source)
    for symbol,value in [('D_i6_8011DC60',0),('D_i6_8011DC64',0),('D_i6_8011DC68',0),('D_i6_8011DC6C',180)]:
        if 's32 %s = %d;'%(symbol,value) not in source:
            raise ArtError('Initial wallpaper overlay contract changed')
    init=function(source,'MainMenu_Init')
    for name,x,y,priority in [('HEADER',100,13,10),('SELECT_NUM_PLAYERS',96,80,14),('OK',0,0,14),
                               ('SELECT_DIFFICULTY',96,80,14),('SELECT_TIME_ATTACK_MODE',96,80,14),
                               ('BACKGROUND',0,0,4)]+[('MODE_SIGN_%d'%i,0,0,10)for i in range(8)]:
        expected='Object_Init(OBJECT_MAIN_MENU_%s, %d, %d, %d);'%(name,x,y,priority)
        if init.count(expected)!=1: raise ArtError('Original menu object layout changed: '+name)
    if 'OBJECT_LEFT(backgroundObj) = 8;' not in function(source,'MainMenu_BackgroundInit'):
        raise ArtError('Original wallpaper position changed')
    if 'OBJECT_LEFT(okObj) = 50;' not in function(source,'MainMenu_OkInit'):
        raise ArtError('Original OK entry position changed')
    chunks=[PRELUDE]
    # Widths come from source metadata, not duplicated native layout constants.
    yaml=YAML.read_text(); widths=[]
    for symbol in IMAGES:
        match=re.search(r'^'+symbol+r':\n(.*?)(?=^\S|\Z)',yaml,re.M|re.S)
        if not match: raise ArtError('Menu image metadata missing: '+symbol)
        widths.append(int(re.search(r'^  width: (\d+)$',match.group(1),re.M).group(1)))
    chunks.append('static unk_80077D50 textures[]={'+','.join('{%d,%d}'%(i,w)for i,w in enumerate(widths))+'};\n')
    aliases={'sSelectModeCompTexInfo':7,'sSelectModeOptionFlamesCompTexInfo':8,'sOkCompTexInfo':18}
    chunks.extend('#define %s (&textures[%d])\n'%(name,index)for name,index in aliases.items())
    for name,indices in [('sMenuSignCompTexInfos',[1,2,3,4,1,5,6,1]),('sDifficultyCompTexInfos',[9,10,11,12]),
                         ('sNumPlayersCompTexInfos',[13,14,15]),('sTimeAttackModeCompTexInfos',[16,17])]:
        chunks.append('static unk_80077D50 *'+name+'[]={'+','.join('&textures[%d]'%i for i in indices)+'};\n')
    for name in ('kDefaultSubOptionLimits','kMaxSubOptionLimits'):
        match=re.search(r'^const s32 '+name+r'\[\] = \{.*?\};',source,re.M)
        if not match: raise ArtError('Original submenu limits missing')
        chunks.append('static '+match.group(0)+'\n')
    common=COMMON.read_text()
    palette=re.search(r'^s32 D_800CD4E0\[\] = \{.*?^\};',common,re.M|re.S)
    if not palette: raise ArtError('Original menu pulse palette missing')
    chunks.append('static '+palette.group(0)+'\n')
    chunks.append(function(common,'func_8007DB28'))
    for name in ('Object_LerpPosXToTarget','Object_LerpPosXToClampedTargetMaxStep'):
        chunks.append(function(OBJECT.read_text(),name))
    for name in ('MainMenu_Update','MainMenu_SignDraw','MainMenu_HeaderDraw','MainMenu_NumPlayersDraw',
                 'MainMenu_DifficultyDraw','MainMenu_TimeAttackModeDraw','MainMenu_OkDraw','MainMenu_OkUpdate'):
        chunks.append(function(source,name))
    controller=CONTROLLER.read_text()
    if 's32 D_800CD160 = 15;' not in controller or 's32 D_800CD164 = 5;' not in controller:
        raise ArtError('Source input repeat cadence changed')
    chunks.append(BRIDGE)
    return '\n'.join(chunks).encode()


def generate(output):
    output_location(output);verify_source(DECOMP)
    outputs={'menu_source.c':closure(SOURCE.read_text()),
             'menu_game_modes.h':(DECOMP/'include/fzx_game.h').read_bytes(),
             'menu_sound_ids.h':sound_header((DECOMP/'include/sfx.h').read_text())}
    inputs=[Path(__file__),ROOT/'include/fzx_menu.h',ROOT/'tools/import_source_art.py',
            ROOT/'tools/title_source.py',SOURCE,COMMON,OBJECT,CONTROLLER,YAML,
            DECOMP/'include/fzx_game.h',DECOMP/'include/sfx.h']
    output.mkdir(parents=True,exist_ok=True)
    receipt=['# fzx generation receipt v1; regenerate, never edit']
    receipt+=['generator %s %s'%(hashlib.sha256(p.read_bytes()).hexdigest(),os.path.relpath(p,ROOT/'tools').replace('\\','/'))for p in inputs]
    for name,data in outputs.items():
        (output/name).write_bytes(data)
        receipt.append('output %s %s'%(hashlib.sha256(data).hexdigest(),name))
    (output/'menu_source.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    print('Original US main-menu update/draw closure generated; audio requests recorded, playback unrun')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'generated/menu_state')
    args=parser.parse_args();generate(args.output)
