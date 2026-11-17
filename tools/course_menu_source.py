#!/usr/bin/env python3
"""Bind original US cartridge course selection to bounded state/draw packets.

State, object motion, image placement, text centering and glyph metrics come
from the pinned source. Texture/model loading, camera execution, persistence
and sound playback are consumers outside this closure. Their requests are
explicit; no unsupported selection is redirected into the first race.
"""
from pathlib import Path
import argparse
import hashlib
import os
import re
import struct
from import_source_art import ROOT, DECOMP, ArtError, output_location, verify_source
from menu_source import cartridge, function, COMMON, OBJECT, CONTROLLER, YAML
from title_source import sound_header

SOURCE = DECOMP/'src/overlays/course_select/course_select.c'
SOURCE_HEADER = SOURCE.with_suffix('.h')
FONT = DECOMP/'src/overlays/ovl_i2/font.c'
MATH = DECOMP/'src/sys/math.c'
MODEL = DECOMP/'src/overlays/course_select/course_model.c'
CAMERA = DECOMP/'src/game/camera.c'
IMAGES = ('aTitleBackgroundMainTex','aCupSelectJackTex','aCupSelectQueenTex','aCupSelectKingTex',
          'aCupSelectJokerTex','aCupSelectXTex','aCupSelectEditTex','aCupSelectQuestionMarkTex',
          'aSelectCourseTex','aRecordsTex','aOKTex','aYellowArrowTex','aOptionsFalconHelmetTex',
          'aCupCleared1Tex','aCupCleared2Tex','aCupCleared3Tex','aCupCleared4Tex',
          'aHasGhostMarkerTex','aStaffGhostBeatenTex','aMenuWithGhostTex','aMenuWithoutGhostTex',
          'aMenuStaffGhostTex','aMenuCelebrityGhostTex','aMenuChampGhostTex')


def us_source(source):
    # This is the only compound condition in this source, and both flags are off.
    source=source.replace('#if defined(PORT) && defined(EXPANSION_KIT)','#ifdef PORT')
    return cartridge(source)


def declaration(source,pattern,description):
    match=re.search(pattern,source,re.M|re.S)
    if not match: raise ArtError('Original course menu declaration missing: '+description)
    return match.group(0)+'\n'


def sine_table():
    """Offline binary32 values of the original Math_SinTableInit recurrence.

    Arrow phase changes are multiples of16, so only256 of4096 entries are
    reachable. Retain source double operations and round once to binary32.
    """
    source=function(MATH.read_text(),'Math_SinTableInit')
    expected=('x = (i * (2 * 3.1415926535)) / ARRAY_COUNT(gSinTable);',
              'numerator = -x * x * x;', 'minusSquareX = -x * x;',
              'denominator = 1 * 2 * 3;', 'for (j = 2; j < 14; j++)',
              'x += (numerator / denominator);','numerator *= minusSquareX;',
              'denominator *= j * ((j << 2) + 2);','gSinTable[i] = x;')
    if any(part not in source for part in expected):
        raise ArtError('Original sine-table recurrence changed')
    values=[]
    for i in range(0,4096,16):
        x=(i*(2*3.1415926535))/4096
        numerator=-x*x*x;minus_square=-x*x;denominator=6.0
        for j in range(2,14):
            x+=numerator/denominator
            numerator*=minus_square;denominator*=j*((j<<2)+2)
        values.append(struct.unpack('<f',struct.pack('<f',x))[0])
    return 'static const float arrow_sine[256]={'+','.join(v.hex()+'f' for v in values)+'};\n'


PRELUDE=r'''/* Generated stock US course-menu closure; regenerate, never edit. */
#include "fzx_course_menu.h"
#include "course_menu_game_modes.h"
#include "course_menu_sound_ids.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>
typedef int32_t s32;
typedef int16_t s16;
typedef char s8;
typedef uint8_t u8;
typedef float f32;
typedef struct { int32_t state; } Camera;
typedef struct { float targetYaw,targetPitch; } CameraTypeOrbit;
typedef struct { CameraTypeOrbit orbit; } CameraSettings;
typedef struct { uint16_t buttonCurrent; } Controller;
typedef fzx_course_menu_object_t Object;
typedef int Gfx;
typedef struct { int32_t raceTime,courseIndex,encodedCourseIndex; } GhostInfo;
typedef struct { uint16_t image; uint8_t kind,font; int32_t width,height,format; } unk_80077D50;
_Static_assert(CHAR_MIN<0,"Source EUC-JP glyph selection requires signed char");
static fzx_course_menu_state_t *m;
#define OBJECT_STATE(o) ((o)->state)
#define OBJECT_LEFT(o) ((o)->left)
#define OBJECT_TOP(o) ((o)->top)
#define OBJECT_COUNTER(o) ((o)->counter)
#define LEFT_ARROW_LEFT(o) ((o)->left)
#define RIGHT_ARROW_LEFT(o) ((o)->top)
#define LEFT_ARROW_ROTATION_CHANGE(o) ((o)->state)
#define RIGHT_ARROW_ROTATION_CHANGE(o) ((o)->state2)
#define LEFT_ARROW_ROTATION(o) ((o)->counter)
#define RIGHT_ARROW_ROTATION(o) ((o)->counter2)
#define GHOST_MARKER_COURSE(o) ((o)->state)
#define GHOST_MARKER_OFFSET(o) ((o)->counter)
#define SQ(x) ((x)*(x))
#define ARRAY_COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define SCREEN_WIDTH 320
#define BTN_A 0x8000u
#define BTN_B 0x4000u
#define BTN_START 0x1000u
#define BTN_UP 0x0800u
#define BTN_DOWN 0x0400u
#define BTN_LEFT 0x0200u
#define BTN_RIGHT 0x0100u
#define BTN_CUP 0x0008u
#define BTN_CDOWN 0x0004u
#define TRANSITION_INACTIVE 0
#define COURSE_EDIT_1 24
#define COURSE_X_1 48
#define CAMERA_COURSE_SELECT_DISABLED 1
#define CAMERA_COURSE_SELECT_ACCEPT_INPUTS 0
#define D_800CCFE8 source_unused_init
static int32_t source_unused_init;
#define gGameMode (m->config.records?GAMEMODE_FLX_RECORDS_COURSE_SELECT:GAMEMODE_FLX_COURSE_SELECT)
#define gGameFrameCount ((int32_t)(m->frame%300u))
#define gSelectedMode m->config.mode
#define gUnlockableLevel m->config.unlock_level
#define gSettingEverythingUnlocked m->config.everything_unlocked
#define gGameModeChangeState m->change_state
#define gTransitionState m->transition
#define gMenuChangeMode m->menu_change
#define sCourseSelectState m->phase
#define gCupSelectOption m->cup_option
#define sCourseSelectCup m->cup
#define sCourseSelectTrackNo m->track
#define gCourseIndex m->course
#define gCupType m->cup_type
#define gLastCourseSelectCourseIndex m->last_course
#define gLastRecordsCourseIndex m->last_records
#define sLastCourseIndex m->last_ghost_course
#define sSelectedGhostOption m->selected_ghost
#define sUnlockedGhosts m->unlocked_ghosts
#define sStaffGhostTimeBeaten m->staff_beaten
#define gCurrentGhostType m->ghost_type
#define D_i5_801190D0 m->ui_flag
#define D_i5_801190B4 m->preview_timer
#define gCourseModelCupType m->preview_cup
#define gInputPressed m->input
#define gInputButtonPressed m->pressed
#define Controller_SetGlobalInputs(p) ((void)0)
#define gCurrentTrackName gTrackNames[m->source_name_course]
#define D_800CD3C4 0
/* Array entries4/5 are deliberately zero: original CupDraw reads this
 * four-entry array before testing whether the cup has completion markers. */
#define gCupNumDifficultiesCleared source_cup_cleared
static int32_t source_cup_cleared[6];
static Camera source_camera[1];
static Controller source_controllers[1];
static const int32_t source_ports[1]={0};
#define gCameras source_camera
#define gControllers source_controllers
#define gPlayerControlPorts source_ports
#define gNumPlayers 1
#define Camera_Init() ((void)(m->camera_yaw_degrees=0.0f,m->camera_pitch_degrees=20.0f))
static void source_camera_update(void);
#define Camera_Update() source_camera_update()
#define func_i5_80115DF0() ((void)0)
#define func_i5_80115E10() ((void)++m->preview_updates)
#define func_i5_801164A8(x) ((void)(m->carousel_x=(x)))
#define func_i5_80116678(cup) ((void)0)
static void CourseModel_Init(int32_t cup) {
    m->preview_cup=cup;m->preview_timer=8;++m->preview_loads;
}
static Gfx *func_i5_80115E64(Gfx *gfx) {
    m->snapshot.preview_visible=1;
    if(m->snapshot.count>=FZX_COURSE_MENU_DRAW_MAX){m->fault=1;return gfx;}
    m->snapshot.draws[m->snapshot.count++].kind=FZX_COURSE_DRAW_PREVIEW;
    return gfx;
}
static void record_cue(uint16_t cue) {
    if(m->cue_count<8)m->cues[m->cue_count++]=cue;else m->fault=1;
}
#define Audio_TriggerSystemSE(c) record_cue((uint16_t)(c))
static int32_t Save_LoadStaffGhostRecord(GhostInfo *info,int32_t course) {
    memset(info,0,sizeof(*info));
    if(course<0 || course>=24 || !m->config.staff_time[course])return 1;
    info->raceTime=m->config.staff_time[course];return 0;
}
static int32_t Save_LoadGhostInfo(GhostInfo *info) {
    memset(info,0,sizeof(*info));
    info->courseIndex=m->config.player_ghost_course;
    info->encodedCourseIndex=m->config.player_ghost_valid?1:0;
    return m->config.player_ghost_valid?0:1;
}
static int32_t source_best_time(int32_t course) {
    if(course<0 || course>=24 || !m->config.best_time[course])return INT32_MAX;
    return m->config.best_time[course];
}
static void color(int32_t r,int32_t g,int32_t b,int32_t a) {
    m->color[0]=(uint8_t)r;m->color[1]=(uint8_t)g;m->color[2]=(uint8_t)b;m->color[3]=(uint8_t)a;
}
#define gDPSetPrimColor(gfx,x,y,r,g,b,a) color(r,g,b,a)
static Gfx *emit(Gfx *gfx,const unk_80077D50 *tex,int32_t x,int32_t y,unsigned mode,
                 int32_t flip_x,int32_t flip_y,float scale_x,float scale_y) {
    if(m->snapshot.count>=FZX_COURSE_MENU_DRAW_MAX || x<INT16_MIN || x>INT16_MAX ||
       y<INT16_MIN || y>INT16_MAX || !tex || mode>5 || flip_y ||
       !(scale_x>=0.0f && scale_x<=1.0f) || !(scale_y>=0.0f && scale_y<=1.0f)) {
        m->fault=1;return gfx;
    }
    fzx_course_menu_draw_t *d=&m->snapshot.draws[m->snapshot.count++];
    d->image=tex->image;d->kind=tex->kind;d->font=tex->font;d->x=(int16_t)x;d->y=(int16_t)y;
    d->scale_x_q8=(uint16_t)(scale_x*256.0f+0.5f);d->scale_y_q8=(uint16_t)(scale_y*256.0f+0.5f);
    d->flip_x=(uint8_t)(mode==5 || (mode==2 && flip_x));
    if(mode==0 && tex->format!=4){d->r=d->g=d->b=d->a=255;}
    else {d->r=m->color[0];d->g=m->color[1];d->b=m->color[2];d->a=m->color[3];}
    if(tex->kind==FZX_COURSE_DRAW_IMAGE && tex->image==FZX_COURSE_IMAGE_BACKGROUND && m->config.records) {
        d->r=135;d->g=135;d->b=255; /* Source BackgroundInit's texture tint. */
    }
    return gfx;
}
#define func_80078EA0_impl(g,t,x,y,mode,a,b,c,d,e) emit(g,t,x,y,mode,a,b,c,d)
#define func_80077D50_impl(t,x,y) ((void)(t))
#define func_8007A828(p,n,r,g,b) ((void)(n))
static void Object_Init(int32_t cmdId,int32_t left,int32_t top,int32_t priority);
static Object *Object_Get(int32_t cmdId) {
    for(unsigned i=0;i<m->object_count;++i)if(m->objects[i].cmdId==cmdId)return &m->objects[i];
    m->fault=1;return &m->objects[FZX_COURSE_MENU_OBJECT_MAX-1];
}
'''

BRIDGE=r'''
static void Object_Init(int32_t cmdId,int32_t left,int32_t top,int32_t priority) {
    unsigned i=0;
    while(i<m->object_count && m->objects[i].cmdId!=OBJECT_FREE)++i;
    if(i>=FZX_COURSE_MENU_OBJECT_MAX-1){m->fault=1;return;}
    if(i==m->object_count)++m->object_count;
    Object *o=&m->objects[i];memset(o,0,sizeof(*o));
    o->cmdId=cmdId;o->left=left;o->top=top;o->priority=(uint8_t)priority;o->shouldDraw=1;
    switch(cmdId) {
        case OBJECT_COURSE_SELECT_BACKGROUND:CourseSelect_BackgroundInit(o);break;
        case OBJECT_COURSE_SELECT_MODEL:CourseSelect_ModelInit();break;
        case OBJECT_COURSE_SELECT_CUP_0:case OBJECT_COURSE_SELECT_CUP_1:
        case OBJECT_COURSE_SELECT_CUP_2:case OBJECT_COURSE_SELECT_CUP_3:
        case OBJECT_COURSE_SELECT_CUP_4:case OBJECT_COURSE_SELECT_CUP_5:CourseSelect_CupInit(o);break;
        case OBJECT_COURSE_SELECT_HEADER:CourseSelect_HeaderInit(o);break;
        case OBJECT_COURSE_SELECT_OK:CourseSelect_OkInit(o);break;
        case OBJECT_COURSE_SELECT_ARROWS:CourseSelect_ArrowsInit(o);break;
        case OBJECT_COURSE_SELECT_GHOST_MARKER:CourseSelect_GhostMarkerInit(o);break;
        case OBJECT_COURSE_SELECT_GHOST_OPTION:CourseSelect_GhostOptionInit(o);break;
        case OBJECT_FRAMEBUFFER:case OBJECT_COURSE_SELECT_NAME:break;
        default:m->fault=1;break;
    }
}
static void bind_state(void) {
    memset(source_cup_cleared,0,sizeof(source_cup_cleared));
    for(unsigned i=0;i<4;++i)source_cup_cleared[i]=m->config.cup_cleared[i];
    source_camera[0].state=m->camera_state;
    source_controllers[0].buttonCurrent=m->held;
}
static void render_step(void) {
    Gfx dummy=0,*gfx=&dummy;
    memset(&m->snapshot,0,sizeof(m->snapshot));color(255,255,255,255);
    for(unsigned i=0;i<m->object_count;++i) {
        Object *o=&m->objects[i];
        switch(o->cmdId) {
            case OBJECT_COURSE_SELECT_MODEL:CourseSelect_ModelUpdate(o);break;
            case OBJECT_COURSE_SELECT_CUP_0:case OBJECT_COURSE_SELECT_CUP_1:
            case OBJECT_COURSE_SELECT_CUP_2:case OBJECT_COURSE_SELECT_CUP_3:
            case OBJECT_COURSE_SELECT_CUP_4:case OBJECT_COURSE_SELECT_CUP_5:CourseSelect_CupUpdate(o);break;
            case OBJECT_COURSE_SELECT_OK:CourseSelect_OkUpdate(o);break;
            case OBJECT_COURSE_SELECT_ARROWS:
                CourseSelect_ArrowsUpdate(o);
                /* SIN masks12 bits. Normalize here to avoid signed overflow on long soaks. */
                o->counter&=4095;o->counter2&=4095;break;
            case OBJECT_COURSE_SELECT_GHOST_MARKER:CourseSelect_GhostMarkerUpdate(o);break;
            case OBJECT_COURSE_SELECT_GHOST_OPTION:CourseSelect_GhostOptionUpdate(o);break;
            default:break;
        }
    }
    /* Source Object_UpdateAndDrawAll order: priority then allocation index. */
    for(unsigned p=0;p<16;++p)for(unsigned i=0;i<m->object_count;++i) {
        Object *o=&m->objects[i];if(o->priority!=p || !o->shouldDraw)continue;
        switch(o->cmdId) {
            case OBJECT_COURSE_SELECT_BACKGROUND:gfx=CourseSelect_BackgroundDraw(gfx,o);break;
            case OBJECT_COURSE_SELECT_MODEL:gfx=CourseSelect_ModelDraw(gfx,o);break;
            case OBJECT_COURSE_SELECT_CUP_0:case OBJECT_COURSE_SELECT_CUP_1:
            case OBJECT_COURSE_SELECT_CUP_2:case OBJECT_COURSE_SELECT_CUP_3:
            case OBJECT_COURSE_SELECT_CUP_4:case OBJECT_COURSE_SELECT_CUP_5:gfx=CourseSelect_CupDraw(gfx,o);break;
            case OBJECT_COURSE_SELECT_HEADER:gfx=CourseSelect_HeaderDraw(gfx,o);break;
            case OBJECT_COURSE_SELECT_OK:gfx=CourseSelect_OkDraw(gfx,o);break;
            case OBJECT_COURSE_SELECT_ARROWS:gfx=CourseSelect_ArrowsDraw(gfx,o);break;
            case OBJECT_COURSE_SELECT_NAME:gfx=CourseSelect_NameDraw(gfx);break;
            case OBJECT_COURSE_SELECT_GHOST_MARKER:gfx=CourseSelect_GhostMarkerDraw(gfx,o);break;
            case OBJECT_COURSE_SELECT_GHOST_OPTION:gfx=CourseSelect_GhostOptionDraw(gfx,o);break;
            default:break;
        }
    }
    fzx_course_menu_snapshot_t *s=&m->snapshot;
    for(unsigned i=0;i<m->object_count;++i) {
        const Object *o=&m->objects[i];if(o->cmdId==OBJECT_FREE)continue;
        fzx_course_menu_object_snapshot_t *d=&s->objects[s->object_count++];
        d->kind=(uint8_t)(o->cmdId==OBJECT_FRAMEBUFFER?FZX_COURSE_OBJECT_FRAMEBUFFER:o->cmdId-OBJECT_COURSE_SELECT_BACKGROUND);
        d->x=o->left;d->y=o->top;d->state=o->state;d->counter=o->counter;
        d->priority=o->priority;d->visible=o->shouldDraw;
        if(o->cmdId==OBJECT_COURSE_SELECT_ARROWS){d->rotation_right=o->counter2;d->rotation_delta_left=o->state;d->rotation_delta_right=o->state2;}
    }
    s->tick=m->ticks;s->frame=m->frame;s->phase=(uint8_t)m->phase;
    s->cup_option=(uint8_t)m->cup_option;s->cup_type=(uint8_t)m->cup_type;
    s->course=(uint8_t)m->course;s->track=(uint8_t)m->track;
    s->ghost_option=(uint8_t)m->selected_ghost;s->ghost_type=(uint8_t)m->ghost_type;
    s->unlocked_ghosts=(uint8_t)m->unlocked_ghosts;s->staff_beaten=(uint8_t)m->staff_beaten;
    s->request=m->request;s->records=m->config.records;
    s->preview_cup=(uint8_t)m->preview_cup;s->preview_updates=m->preview_updates;s->preview_loads=m->preview_loads;
    s->camera_updates=m->camera_updates;s->carousel_x=m->carousel_x;
    s->camera_orbit_ticks=m->camera_orbit_ticks;
    s->camera_yaw_degrees=m->camera_yaw_degrees;s->camera_pitch_degrees=m->camera_pitch_degrees;
    s->camera_disable_requested=(uint8_t)(source_camera[0].state==CAMERA_COURSE_SELECT_DISABLED);
    m->camera_state=source_camera[0].state;
}
static bool valid_config(const fzx_course_menu_config_t *c) {
    if(!c || (c->mode>3 && c->mode!=5) || c->records>1 || c->last_course>53 ||
       c->ghost_type>4 || c->unlock_level>3 || c->everything_unlocked>1 ||
       c->player_ghost_valid>1 || c->player_ghost_course>53)return false;
    for(unsigned i=0;i<24;++i)if(c->staff_time[i]<0 || c->staff_time[i]>18000000 ||
        c->best_time[i]<0 || c->best_time[i]>18000000)return false;
    for(unsigned i=0;i<4;++i)if(c->cup_cleared[i]>4)return false;
    return true;
}
static bool valid(const fzx_course_menu_state_t *s) {
    if(!s || !valid_config(&s->config) || s->fault || s->phase<0 || s->phase>6 ||
       s->cup_option<0 || s->cup_option>4 || s->cup<0 || s->cup>5 || s->cup_type<0 || s->cup_type>5 ||
       s->track<0 || s->track>5 || s->course<0 || s->course>53 || s->source_name_course<0 || s->source_name_course>54 ||
       s->selected_ghost<0 || s->selected_ghost>4 || s->ghost_type<0 || s->ghost_type>4 ||
       s->unlocked_ghosts<1 || s->unlocked_ghosts>2 || s->staff_beaten<0 || s->staff_beaten>1 ||
       s->last_ghost_course< -1 || s->last_ghost_course>53 || s->request>FZX_COURSE_REQUEST_RECORDS_EXIT ||
       s->connected>4 || s->cue_count>8 || s->object_count>=FZX_COURSE_MENU_OBJECT_MAX ||
       s->snapshot.count>FZX_COURSE_MENU_DRAW_MAX)return false;
    for(unsigned i=0;i<s->object_count;++i) {
        const Object *o=&s->objects[i];
        if(o->cmdId==OBJECT_FREE)continue;
        if(o->cmdId!=OBJECT_FRAMEBUFFER && (o->cmdId<OBJECT_COURSE_SELECT_BACKGROUND || o->cmdId>OBJECT_COURSE_SELECT_GHOST_OPTION))return false;
        if(o->left< -6500 || o->left>2000 || o->top< -100 || o->top>240 || o->priority>15 || o->shouldDraw>1)return false;
        if(o->cmdId==OBJECT_COURSE_SELECT_ARROWS) {
            if(o->counter<0 || o->counter>4095 || (o->counter%16) || o->counter2<0 || o->counter2>4095 || (o->counter2%16) ||
               o->state<128 || o->state>640 || o->state2<128 || o->state2>640)return false;
        } else if(o->cmdId==OBJECT_COURSE_SELECT_GHOST_MARKER) {
            if(o->state<0 || o->state>53 || o->counter< -1625 || o->counter>100)return false;
        } else if(o->counter<0 || o->counter>12 || o->state<0 || o->state>3)return false;
    }
    return true;
}
bool fzx_course_menu_reset(fzx_course_menu_state_t *s,const fzx_course_menu_config_t *config) {
    if(!s || !valid_config(config))return false;
    const fzx_course_menu_config_t copy=*config;
    memset(s,0,sizeof(*s));s->config=copy;m=s;
    s->frame=copy.frame;s->connected=1;s->unlocked_ghosts=1;
    s->last_course=s->last_records=copy.last_course;s->ghost_type=copy.ghost_type;
    bind_state();CourseSelect_Init();render_step();m=NULL;
    return valid(s);
}
bool fzx_course_menu_step(fzx_course_menu_state_t *s,uint16_t buttons,uint8_t connected,bool transition) {
    if(!valid(s) || connected>4 || s->ticks==UINT32_MAX || s->same_input_ticks==UINT32_MAX ||
       s->preview_updates==UINT32_MAX || s->preview_loads==UINT32_MAX || s->camera_updates==UINT32_MAX ||
       s->camera_orbit_ticks==UINT32_MAX)return false;
    if(s->request)return true;
    m=s;bind_state();s->connected=connected;s->transition=transition;
    s->cue_count=0;memset(s->cues,0,sizeof(s->cues));
    if(!connected)buttons=0;
    s->pressed=(uint16_t)(buttons & (uint16_t)~s->held);
    if(buttons && buttons==s->held)++s->same_input_ticks;else s->same_input_ticks=0;
    s->input=s->pressed;
    if(s->same_input_ticks>=15 && ((s->same_input_ticks-15)%5)==0)s->input|=buttons;
    s->held=buttons;source_controllers[0].buttonCurrent=buttons;++s->ticks;++s->frame;
    const int32_t result=CourseSelect_Update();
    if(result==GAMEMODE_FLX_MACHINE_SELECT)s->request=FZX_COURSE_REQUEST_MACHINE;
    else if(result==GAMEMODE_RECORDS)s->request=FZX_COURSE_REQUEST_RECORDS;
    else if(result!=gGameMode)s->fault=1;
    if(s->menu_change==MENU_CHANGE_EXIT_COURSE_SELECT)s->request=FZX_COURSE_REQUEST_MAIN_MENU;
    else if(s->menu_change==MENU_CHANGE_EXIT_RECORDS)s->request=FZX_COURSE_REQUEST_RECORDS_EXIT;
    render_step();m=NULL;return !s->fault;
}
bool fzx_course_menu_snapshot(const fzx_course_menu_state_t *s,fzx_course_menu_snapshot_t *out) {
    if(!valid(s) || !out)return false;
    *out=s->snapshot;return true;
}
'''


def closure(source):
    source=us_source(source)
    font=us_source(FONT.read_text());common=us_source(COMMON.read_text())
    chunks=[PRELUDE]
    # Keep original numeric object/state/font enums; no engine header is needed.
    for path,name in [(DECOMP/'include/fzx_object.h','ObjectIds'),(SOURCE_HEADER,'CourseSelectState'),
                      (DECOMP/'include/fzx_font.h','FontSet'),(DECOMP/'include/fzx_font.h','FontSetCase'),
                      (DECOMP/'include/fzx_font.h','FontChar')]:
        enum=declaration(path.read_text(),r'^typedef enum '+name+r' \{.*?^\} '+name+';',name)
        chunks.append(us_source(enum))
    yaml=YAML.read_text();rows=[]
    for i,symbol in enumerate(IMAGES):
        block=declaration(yaml,r'^'+symbol+r':\n.*?(?=^\S|\Z)',symbol)
        def field(name):
            match=re.search(r'^  '+name+r': (\w+)$',block,re.M)
            if not match: raise ArtError('Original image metadata missing: '+symbol+' '+name)
            return match.group(1)
        fmt=field('format')
        if fmt not in ('RGBA16','I4'): raise ArtError('Unsupported course image format: '+fmt)
        rows.append('{%d,0,0,%d,%d,%d}'%(i,int(field('width')),int(field('height')),4 if fmt=='I4' else 17))
    chunks.append('static unk_80077D50 textures[]={'+','.join(rows)+'};\n')
    aliases={'sSelectCourseCompTexInfo':8,'sRecordsCompTexInfo':9,'sOKCompTexInfo':10,
             'sYellowArrowCompTexInfo':11,'sOptionsFalconHelmetCompTexInfo':12,
             'sHasGhostMarkerCompTexInfo':17,'sStaffGhostBeatenCompTexInfo':18}
    chunks.extend('#define %s (&textures[%d])\n'%(name,i)for name,i in aliases.items())
    for name,ids in [('sTitleBackgroundCompTexInfos',[0]),('sCupSelectCompTexInfos',list(range(1,8))),
                     ('sCupClearedDifficultyCompTexInfos',list(range(13,17))),
                     ('sTimeAttackGhostOptionCompTexInfos',list(range(19,24)))]:
        chunks.append('static unk_80077D50 *'+name+'[]={'+','.join('&textures[%d]'%i for i in ids)+'};\n')
    for name in ('sGhostOptionTypeMap','sCourseSelectCupColors'):
        chunks.append('static '+declaration(source,r'^(?:s8|s32) '+name+r'\[\] = \{.*?\};',name))
    chunks.append('static '+declaration(source,r'^const char\* sTrackSubtitles\[\] = \{.*?^\};','subtitles'))
    names=declaration(common,r'^const char\* sTrackNames\[\] = \{.*?^\};','track names')
    strings=re.findall(r'"(?:[^"\\]|\\.)*"',names)
    if len(strings)!=32: raise ArtError('Cartridge track-name table changed')
    # Fresh cartridge name initialization from common.c, without edited names.
    full_names=strings[:30]+[strings[31]]*18+[strings[30]]*6+[strings[31]]
    chunks.append('static const char *gTrackNames[55]={'+','.join(full_names)+'};\n')
    for number in range(1,7):
        name='kFont%dCharWidths'%number
        chunks.append('static '+declaration(font,r'^const s16 '+name+r'\[\] = \{.*?^\};',name))
    for number in (3,4):
        # Glyph identity is its source table index, not a Unicode codepoint.
        chunks.append('static unk_80077D50 glyphs%d[256]={'%number+
                      ','.join('{%d,1,%d,%d,%d,4}'%(i,number,16 if number==3 else 8,16 if number==3 else 8)
                               for i in range(256))+'};\n')
        chunks.append('static unk_80077D50 *sFont%dCompTexInfos[256]={'%number+
                      ','.join('&glyphs%d[%d]'%(number,i)for i in range(256))+'};\n')
    for number in (1,2,5,6):
        chunks.append('#define sFont%dCompTexInfos sFont3CompTexInfos\n'%number)
    chunks.append(sine_table())
    chunks.append('#define SIN(x) (arrow_sine[((uint32_t)(x)&4095u)>>4])\n')
    for name in ('Font_IntToString','func_i2_80105EE4','func_i2_80105F48','func_i2_80105F6C',
                 'Font_GetCharIndex','Font_GetStringWidth','Font_DrawString'):
        body=function(font,name)
        if name!='Font_IntToString': body=body.replace('s8* str','const s8* str')
        if name in ('Font_GetStringWidth','Font_DrawString'):
            result='0' if name=='Font_GetStringWidth' else 'gfx'
            body=body.replace('{','{\n    if(fontSet!=FONT_SET_3 && fontSet!=FONT_SET_4){m->fault=1;return '+result+';}',1)
        chunks.append(body)
    chunks.append('static '+declaration(common,r'^s32 D_800CD4E0\[\] = \{.*?^\};','pulse palette'))
    chunks.append(function(common,'func_8007DB28'))
    for name in ('Object_LerpPosXToTarget','Object_LerpPosXToClampedTargetMaxStep',
                 'Object_LerpPosYToTarget','Object_LerpPosYToClampedTarget'):
        chunks.append(function(OBJECT.read_text(),name))
    chunks.append(function(CAMERA.read_text(),'Camera_UpdateCourseSelect'))
    chunks.append(r'''
static void source_camera_update(void) {
    CameraSettings settings={{m->camera_yaw_degrees,m->camera_pitch_degrees}};
    ++m->camera_updates;
    if(m->ui_flag)++m->camera_orbit_ticks;
    Camera_UpdateCourseSelect(&source_camera[0],&settings);
    m->camera_yaw_degrees=settings.orbit.targetYaw;m->camera_pitch_degrees=settings.orbit.targetPitch;
}
''')
    for name in ('CourseSelect_UpdateUnlockedGhosts','func_i5_80116910','CourseSelect_Init',
                 'CourseSelect_BackgroundInit','CourseSelect_ModelInit','CourseSelect_CupInit',
                 'CourseSelect_HeaderInit','CourseSelect_OkInit','CourseSelect_ArrowsInit',
                 'CourseSelect_GhostMarkerInit','CourseSelect_GhostOptionInit','CourseSelect_Update',
                 'CourseSelect_BackgroundDraw','CourseSelect_ModelDraw','CourseSelect_CupDraw',
                 'CourseSelect_HeaderDraw','CourseSelect_OkDraw','CourseSelect_ArrowsDraw',
                 'CourseSelect_NameDraw','CourseSelect_GhostMarkerDraw','CourseSelect_GhostOptionDraw',
                 'CourseSelect_ModelUpdate','CourseSelect_CupUpdate','CourseSelect_OkUpdate',
                 'CourseSelect_ArrowsUpdate','CourseSelect_GhostMarkerUpdate','CourseSelect_GhostOptionUpdate'):
        body=function(source,name)
        unused={'CourseSelect_UpdateUnlockedGhosts':['staffTime'],
                'CourseSelect_Update':['sp110'],
                'CourseSelect_Init':['cupCompletion','var_a1'],
                'CourseSelect_GhostMarkerInit':['i'],
                'CourseSelect_GhostMarkerDraw':['i','cupCourseIndex','left'],
                'CourseSelect_ModelUpdate':['var_v1']}.get(name,[])
        # The original US branches retain EK-only local declarations.
        for var in unused:
            match=re.search(r'^    (?:s32|s8\*|bool) '+var+r';$',body,re.M)
            if not match: raise ArtError('Original unused local changed: '+name+' '+var)
            body=body[:match.end()]+'\n    (void)sizeof('+var+');'+body[match.end():]
        if name=='CourseSelect_ModelDraw':body=body.replace('{','{\n    (void)modelObj;',1)
        if name=='CourseSelect_UpdateUnlockedGhosts':
            before='gCourseInfos[gCourseIndex].timeRecord[0]'
            if body.count(before)!=1: raise ArtError('Course time-record read changed')
            body=body.replace(before,'source_best_time(gCourseIndex)')
        if name=='func_i5_80116910':
            before='gCurrentTrackName = gTrackNames[gCourseIndex];'
            if body.count(before)!=1: raise ArtError('Course name identity update changed')
            body=body.replace(before,'m->source_name_course = gCourseIndex;')
        chunks.append(body)
    controller=CONTROLLER.read_text()
    if 's32 D_800CD160 = 15;' not in controller or 's32 D_800CD164 = 5;' not in controller:
        raise ArtError('Source input repeat cadence changed')
    if 'D_i5_801190B4 = 8;' not in function(MODEL.read_text(),'CourseModel_Init'):
        raise ArtError('Source model load demand changed')
    chunks.append(BRIDGE)
    return '\n'.join(chunks).encode()


def generate(output):
    output_location(output);verify_source(DECOMP)
    outputs={'course_menu_source.c':closure(SOURCE.read_text()),
             'course_menu_game_modes.h':(DECOMP/'include/fzx_game.h').read_bytes(),
             'course_menu_sound_ids.h':sound_header((DECOMP/'include/sfx.h').read_text())}
    inputs=[Path(__file__),ROOT/'include/fzx_course_menu.h',ROOT/'tools/import_source_art.py',
            ROOT/'tools/menu_source.py',ROOT/'tools/title_source.py',SOURCE,SOURCE_HEADER,COMMON,OBJECT,
            FONT,MATH,MODEL,CAMERA,CONTROLLER,YAML,DECOMP/'include/fzx_game.h',DECOMP/'include/fzx_object.h',
            DECOMP/'include/fzx_font.h',DECOMP/'include/sfx.h']
    output.mkdir(parents=True,exist_ok=True)
    receipt=['# fzx generation receipt v1; regenerate, never edit']
    receipt+=['generator %s %s'%(hashlib.sha256(p.read_bytes()).hexdigest(),os.path.relpath(p,ROOT/'tools').replace('\\','/'))for p in inputs]
    for name,data in outputs.items():
        (output/name).write_bytes(data)
        receipt.append('output %s %s'%(hashlib.sha256(data).hexdigest(),name))
    (output/'course_menu_source.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    print('Original US course-menu closure generated; preview, persistence and sound consumers remain separate')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'generated/course-menu-source')
    args=parser.parse_args();generate(args.output)
