#!/usr/bin/env python3
"""Extract original US Time Attack name-entry/records state, ending at menu-ready.

Generated source and font metrics remain private. Geometry, drawing, retry,
durable saving and audio playback are separate consumers. The original RNG1
words are supplied after each source tick and returned for caller writeback.
"""
from pathlib import Path
import argparse
import hashlib
import os
import re
from import_source_art import ROOT, DECOMP, ArtError, output_location, verify_source
from course_menu_source import declaration
from results_source import select_us

SOURCE = DECOMP/'src/overlays/ovl_i3/records_entry.c'
HEADER = SOURCE.with_suffix('.h')
COMMON = DECOMP/'src/game/common.c'
MATH = DECOMP/'src/sys/math.c'
FONT = DECOMP/'src/overlays/ovl_i2/font.c'


def function(source, name):
    match = re.search(r'^(?:s32|u32|void|bool|Gfx\*) '+re.escape(name)+
                      r'\([^\n]*\) \{.*?^\}', source, re.M|re.S)
    if not match:
        raise ArtError('Original records function missing: '+name)
    return 'static '+match[0]+'\n'


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ArtError('Original records anchor changed: '+old)
    return text.replace(old, new)


def between(text, start, end):
    if text.count(start) != 1 or text.count(end) != 1:
        raise ArtError('Original records span changed: '+start)
    return text.split(start, 1)[1].split(end, 1)[0]


def ids_header():
    out = ['/* Generated original records identifiers; regenerate, never edit. */']
    for path, names in [(HEADER, ('NameEntryState','RecordsEntryState')),
                        (DECOMP/'include/fzx_font.h', ('FontSet','FontSetCase','FontChar')),
                        (DECOMP/'include/fzx_game.h', ('GameMode',)),
                        (DECOMP/'include/fzx_camera.h', ('CameraMode',)),
                        (DECOMP/'include/sfx.h', ('SfxId',))]:
        source = path.read_text()
        for name in names:
            out.append(select_us(declaration(source, r'^typedef enum '+name+r' \{.*?^\} \w+;', name)))
    for path, names in [(HEADER, ('KEYBOARD_COLUMN_COUNT','KEYBOARD_ROW_COUNT','KEYBOARD_TOTAL_COUNT',
                                    'RECORDS_ENTRY_DRAW_NAME_BOX','RECORDS_ENTRY_HIGHLIGHT_CURRENT',
                                    'RECORDS_ENTRY_LOAD_ARROWS','RECORDS_ENTRY_DRAW_ENGINE_SETTINGS',
                                    'RECORDS_ENTRY_FILTER_BACKGROUND','RECORDS_ENTRY_DRAW_COURSE_WITH_ARROWS')),
                        (DECOMP/'include/macros.h', ('MAX_TIMER',)),
                        (DECOMP/'include/fzx_math.h', ('LCG_MULTIPLIER','LCG_INCREMENT_1')),
                        (DECOMP/'include/fzx_racer.h', ('RACER_STATE_FINISHED',)),
                        (DECOMP/'include/controller.h', ('STICK_TO_BUTTON',))]:
        source = path.read_text()
        for name in names:
            out.append(declaration(source, r'^#define '+name+r'(?:\([^\n]*?\))?\s+[^\n]+', name))
    pad = (DECOMP/'include/PR/os_cont.h').read_text()
    for name in ('A','B','START','UP','DOWN','LEFT','RIGHT'):
        macro = declaration(pad, r'^#define CONT_'+name+r'\s+[^\n]+', 'button '+name)
        out.append(macro.replace('CONT_'+name, 'BTN_'+name))
    return ('\n'.join(out)+'\n').encode()


PRELUDE = r'''/* Generated original cartridge records state. Regenerate, never edit. */
#include "fzx_records.h"
#include "records_source_ids.h"
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
typedef fzx_records_key_t KeyboardObject;
typedef fzx_results_records_t CourseInfo;
typedef struct {
    u16 buttonPressed,buttonCurrent,retriggerCurrentButtonPress;
    u8 stickPressed,stickCurrent;
} Controller;
static fzx_records_state_t *m;
static Controller source_controller[1];
static const s32 source_ports[1]={0};
_Static_assert(CHAR_MIN<0,"Original font indexing requires signed char");
_Static_assert(KEYBOARD_TOTAL_COUNT==FZX_RECORDS_KEY_COUNT,"Original keyboard extent");
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define gGameMode GAMEMODE_TIME_ATTACK
#define gControllers source_controller
#define gPlayerControlPorts source_ports
#define gCourseIndex m->snapshot.config.course
#define gCurrentTimeAttackRecordPosition m->snapshot.record_position
#define gShowNameEntryMenu m->snapshot.name_entry
#define gRecordNameEntered m->snapshot.name
#define gRecordNameEnteredLength m->snapshot.name_length
#define gInputButtonPressed m->snapshot.input_button_pressed
#define gInputPressed m->snapshot.input_pressed
#define gRetriggeredButtonCurrentPressed m->snapshot.input_retriggered
#define gRandSeed1 m->snapshot.rng_seed1
#define gRandMask1 m->snapshot.rng_mask1
#define sKeyboardObjects m->snapshot.keys
#define sKeyboardCursorX m->snapshot.cursor_x
#define sKeyboardCursorY m->snapshot.cursor_y
#define sEnterKeyboardIndex m->snapshot.enter_index
#define sRecordsEntryNameEntryState m->snapshot.name_state
#define sPreviousRecordsEntryNameEntryState m->snapshot.previous_name_state
#define sRecordsEntryNameEntryTransitionTimer m->snapshot.name_timer
#define sSelectedCharacterTransitionTimer m->snapshot.selected_timer
#define sRecordsEntryFlags m->snapshot.record_flags
#define sRecordsEntryIncreaseState m->snapshot.record_increase
#define sRecordsEntryState m->snapshot.record_state
#define sRecordsEntryRecordLeftOffset m->snapshot.record_left_offset
#define sRecordsEntryRecordsTransitionTimer m->snapshot.record_transition_timer
#define sRecordsEntryRecordLeft m->snapshot.record_left
#define sRecordsEntryRecordTop m->snapshot.record_top
#define sRecordsEntryRecordHeight m->snapshot.record_height
#define sRecordsEntryMaxSpeedLeft m->snapshot.max_speed_left
#define sRecordsEntryMaxSpeedTop m->snapshot.max_speed_top
#define sRecordsEntryMaxSpeedTopMachineOffset m->snapshot.max_speed_machine_offset
#define sRecordsEntryBestTimeLeft m->snapshot.best_time_left
#define sRecordsEntryBestTimeTop m->snapshot.best_time_top
#define sRecordsEntryBestLapWordsLeftOffset m->snapshot.best_lap_left_offset
#define sRecordsEntryBestLapWordsTopOffset m->snapshot.best_lap_top_offset
static void source_event(uint8_t kind,uint16_t id) {
    fzx_records_snapshot_t *p=&m->snapshot;
    if(p->event_count>=FZX_RECORDS_EVENT_MAX){m->fault=1;return;}
    fzx_results_event_t *e=&p->events[p->event_count++];
    e->kind=kind;e->id=id;e->player=0;
}
#define Audio_TriggerSystemSE(id) source_event(FZX_RESULTS_SOUND_SYSTEM,id)
static void Save_SaveCourseRecordProfiles(s32 course) {
    ++m->snapshot.save_requests;
    source_event(FZX_RESULTS_SAVE_REQUEST,(uint16_t)course);
}
static s32 RecordsEntry_GetCharacterKeyboardIndex(s8 character);
static void RecordsEntry_InitKeyboardObjects(bool in_position);
static void RecordsEntry_UpdateNameEntryStart(void);
static void RecordsEntry_UpdateNameEntryTransition(bool away);
static void RecordsEntry_UpdateNameEntryKeyboard(void);
static void RecordsEntry_SaveRecordName(CourseInfo *course);
'''


BRIDGE = r'''
static bool valid_records(const fzx_results_records_t *r) {
    if(!(r->maxSpeed>=0.0f && r->maxSpeed<=462.9f) || r->bestTime<0 || r->bestTime>MAX_TIMER)return false;
    for(unsigned i=0;i<5;++i) {
        if(r->timeRecord[i]<0 || r->timeRecord[i]>MAX_TIMER ||
           (i && r->timeRecord[i]<r->timeRecord[i-1]) ||
           !(r->recordEngines[i]>=0.0f && r->recordEngines[i]<=1.0f) ||
           r->recordNames[i][3]!=0 || r->recordMachineInfos[i].character>=30 ||
           r->recordMachineInfos[i].customType!=0)return false;
        for(unsigned j=0;j<3;++j) {
            const unsigned char c=(unsigned char)r->recordNames[i][j];
            if(c && (c<32 || c>126))return false;
        }
    }
    return r->maxSpeedMachine.character<30 && r->maxSpeedMachine.customType==0;
}
static bool valid_config(const fzx_results_config_t *c) {
    if(c->course>=24 || !c->name[0])return false;
    unsigned i;
    for(i=0;i<FZX_RESULTS_NAME_MAX && c->name[i];++i)
        if((unsigned char)c->name[i]<32 || (unsigned char)c->name[i]>126)return false;
    return i<FZX_RESULTS_NAME_MAX;
}
static bool valid(const fzx_records_state_t *s) {
    if(!s || s->initialized!=1 || s->fault)return false;
    const fzx_records_snapshot_t *p=&s->snapshot;
    if(!valid_config(&p->config) || !valid_records(&p->records) ||
       p->phase>FZX_RECORDS_FINISH_MENU_READY || p->draw_kind>FZX_RECORDS_DRAW_TABLE ||
       p->event_count>FZX_RECORDS_EVENT_MAX || p->record_position>5 || p->best_lap>3 ||
       p->max_speed_record>1 || p->name_entry>1 || p->spinner_valid>1 ||
       p->name_state<NAME_ENTRY_START || p->name_state>NAME_ENTRY_TRANSITION_OUT ||
       p->previous_name_state<NAME_ENTRY_START || p->previous_name_state>NAME_ENTRY_TRANSITION_OUT ||
       p->draw_name_state<NAME_ENTRY_START || p->draw_name_state>NAME_ENTRY_TRANSITION_OUT ||
       p->name_timer<0 || p->name_timer>61 || p->name_length<0 || p->name_length>3 ||
       p->cursor_x<0 || p->cursor_x>=KEYBOARD_COLUMN_COUNT ||
       p->cursor_y<0 || p->cursor_y>=KEYBOARD_ROW_COUNT ||
       p->enter_index<0 || p->enter_index>=KEYBOARD_TOTAL_COUNT ||
       p->record_state<RECORDS_ENTRY_WAIT_NAME_ENTRY || p->record_state>RECORDS_ENTRY_RECORDS ||
       p->record_increase>1 || p->record_transition_timer<0 || p->record_transition_timer>30 ||
       p->record_left_offset<0 || p->record_left_offset>SCREEN_WIDTH ||
       p->rng_calls>100 || p->save_requests>1 || p->name[3]!=0 ||
       (p->spinner_valid && (p->spinner_keyboard_index<0 || p->spinner_keyboard_index>=KEYBOARD_TOTAL_COUNT)) ||
       (p->name_entry && !p->record_position) ||
       (p->name_length==3 && p->name_state==NAME_ENTRY_KEYBOARD &&
        p->cursor_y*KEYBOARD_COLUMN_COUNT+p->cursor_x!=p->enter_index))return false;
    for(unsigned i=0;i<3;++i) {
        const unsigned char c=(unsigned char)p->name[i];
        if(p->selected_timer[i]<0 || p->selected_timer[i]>=15 || (c && (c<32 || c>126)))return false;
    }
    for(unsigned i=0;i<FZX_RECORDS_KEY_COUNT;++i) {
        const fzx_records_key_t *k=&p->keys[i];
        const unsigned char c=(unsigned char)k->character[0];
        if((c && (c<32 || c>126)) || k->character[1]!=0 || k->rand_left<0 || k->rand_left>=SCREEN_WIDTH ||
           k->rand_top<0 || k->rand_top>=SCREEN_HEIGHT || k->left<0 || k->left>=SCREEN_WIDTH ||
           k->top<0 || k->top>=SCREEN_HEIGHT)return false;
    }
    return true;
}
static bool valid_input(const fzx_records_input_t *in) {
    return in && in->connected<=1 && in->retrigger<=1 &&
           !(in->stick_pressed&15u) && !(in->stick_current&15u);
}
bool fzx_records_begin(fzx_records_state_t *s,const fzx_results_state_t *results) {
    if(!s || !results || results->initialized!=1 || results->fault ||
       results->phase!=FZX_RESULTS_RECORDS_REQUEST || results->outgoing!=400 ||
       results->record_position<0 || results->record_position>5 ||
       results->best_lap<0 || results->best_lap>3 ||
       results->max_speed_record<0 || results->max_speed_record>1 ||
       results->name_entry!=(results->record_position!=0) ||
       !valid_config(&results->config) || !valid_records(&results->records))return false;
    fzx_records_state_t next={0};m=&next;next.initialized=1;
    fzx_records_snapshot_t *p=&next.snapshot;
    p->config=results->config;p->records=results->records;
    p->record_position=(uint8_t)results->record_position;
    p->best_lap=(uint8_t)results->best_lap;p->max_speed_record=(uint8_t)results->max_speed_record;
    p->name_entry=(uint8_t)results->name_entry;p->frame=results->input.frame;
    RecordsEntry_Init();RecordsEntry_InitNameEntry();
    /* The source already issued this request while outgoing was 392..399. */
    if(!p->record_position)RecordsEntry_ToRecordsState();
    p->phase=p->name_entry?FZX_RECORDS_NAME_ENTRY:FZX_RECORDS_TABLE_TRANSITION;
    m=NULL;if(!valid(&next))return false;*s=next;return true;
}
bool fzx_records_step(fzx_records_state_t *s,const fzx_records_input_t *in) {
    if(!valid(s) || !valid_input(in) || s->snapshot.tick==UINT32_MAX)return false;
    if(s->snapshot.phase==FZX_RECORDS_FINISH_MENU_READY)return true;
    fzx_records_state_t next=*s;m=&next;fzx_records_snapshot_t *p=&next.snapshot;
    p->input=*in;p->frame=in->frame;++p->tick;
    p->rng_seed1=in->rng_seed1;p->rng_mask1=in->rng_mask1;p->rng_calls=0;
    p->event_count=0;memset(p->events,0,sizeof(p->events));
    p->spinner_valid=0;
    memset(source_controller,0,sizeof(source_controller));
    if(in->connected) {
        source_controller[0].buttonPressed=in->button_pressed;
        source_controller[0].buttonCurrent=in->button_current;
        source_controller[0].stickPressed=in->stick_pressed;
        source_controller[0].stickCurrent=in->stick_current;
        source_controller[0].retriggerCurrentButtonPress=in->retrigger;
    }
    if(gShowNameEntryMenu) {
        p->draw_kind=FZX_RECORDS_DRAW_NAME;
        RecordsEntry_UpdateNameEntry();source_name_draw_state();
        p->phase=gShowNameEntryMenu?FZX_RECORDS_NAME_ENTRY:FZX_RECORDS_TABLE_TRANSITION;
    } else {
        p->draw_kind=FZX_RECORDS_DRAW_TABLE;
        const bool ready=RecordsEntry_Update();
        p->phase=ready?FZX_RECORDS_FINISH_MENU_READY:FZX_RECORDS_TABLE_TRANSITION;
    }
    m=NULL;if(!valid(&next))return false;*s=next;return true;
}
bool fzx_records_snapshot(const fzx_records_state_t *s,fzx_records_snapshot_t *out) {
    if(!out || !valid(s))return false;
    *out=s->snapshot;return true;
}
'''


def closure(source):
    text = select_us(source)
    chunks = [PRELUDE]
    rng = function(select_us(MATH.read_text()), 'Math_Rand1')
    # The source SH-2/N64 LCG wraps at 32 bits. Binding the seed to uint32_t
    # makes that exact arithmetic defined under HOST UBSan, without -fwrapv.
    chunks.append(replace_once(rng, 'Math_Rand1(void) {',
                               'Math_Rand1(void) {\n    ++m->snapshot.rng_calls;'))
    chunks.append(function(select_us(COMMON.read_text()), 'Controller_SetGlobalInputs'))
    font = select_us(FONT.read_text())
    for number in range(1, 7):
        chunks.append('static '+declaration(font, r'^const s16 kFont%dCharWidths\[\] = \{.*?^\};'%number,
                                           'font widths'))
    for name in ('func_i2_80105EE4','func_i2_80105F48','func_i2_80105F6C',
                 'Font_GetCharIndex','Font_GetStringWidth'):
        chunks.append(function(font, name))
    chunks.append('static '+declaration(text, r'^signed char sNameKeyboardCharacters\[\] = [^\n]+;',
                                       'name keyboard').replace('signed char', 's8'))
    init = function(text, 'RecordsEntry_Init')
    state = between(init, 'RecordsEntry_Init(void) {', '    Matrix_SetLockedLookAt(')
    chunks.append('static void RecordsEntry_Init(void) {'+state+'}\n')
    update = function(text, 'RecordsEntry_Update')
    state = between(update, '    isOnRecordsScreen = false;', '    vp = gGfxPool->recordsMachineVp;')
    chunks.append('static bool RecordsEntry_Update(void) {\n    bool isOnRecordsScreen=false;'+state+
                  '\n    return isOnRecordsScreen;\n}\n')
    for name in ('RecordsEntry_ToRecordsState','RecordsEntry_GetCharacterKeyboardIndex',
                 'RecordsEntry_InitNameEntry','RecordsEntry_InitKeyboardObjects',
                 'RecordsEntry_UpdateNameEntryStart','RecordsEntry_UpdateNameEntryTransition',
                 'RecordsEntry_SaveRecordName'):
        chunks.append(function(text, name))
    keyboard = function(text, 'RecordsEntry_UpdateNameEntryKeyboard')
    state = between(keyboard, '    courseInfo = &gCourseInfos[gCourseIndex];',
                    '        spinningCharacterCompTexInfo =')
    chunks.append('static void RecordsEntry_UpdateNameEntryKeyboard(void) {\n'
                  '    KeyboardObject *keyboardObject;\n    s32 i,keyboardIndex,previousKeyboardIndex;\n'
                  '    s32 keyboardCursorX,keyboardCursorY;\n    bool nameLengthIncreased;\n'
                  '    CourseInfo *courseInfo=&m->snapshot.records;'+state+
                  '        m->snapshot.spinner_keyboard_index=(int16_t)keyboardIndex;\n'
                  '        m->snapshot.spinner_x=(int16_t)keyboardCursorX;\n'
                  '        m->snapshot.spinner_y=(int16_t)keyboardCursorY;\n'
                  '        m->snapshot.spinner_valid=1;\n    }\n}\n')
    chunks.append(function(text, 'RecordsEntry_UpdateNameEntry'))
    draw = function(text, 'RecordsEntry_DrawNameEntry')
    state = between(draw, '    state = sRecordsEntryNameEntryState;', '    switch (state) {')
    chunks.append('static void source_name_draw_state(void) {\n'
                  '    s16 state=sRecordsEntryNameEntryState;'+state+
                  '    m->snapshot.draw_name_state=state;\n'
                  '    sPreviousRecordsEntryNameEntryState=sRecordsEntryNameEntryState;\n}\n')
    chunks.append(BRIDGE)
    code = '\n'.join(chunks)+'\n'
    for old, new in (('keyboardCharacter','character'), ('randLeftTarget','rand_left'),
                     ('randTopTarget','rand_top')):
        for member in ('->', '.'):
            code = code.replace(member+old, member+new)
    return code.encode()


def dependencies():
    return [Path(__file__), ROOT/'include/fzx_records.h', ROOT/'include/fzx_results.h',
            ROOT/'tools/import_source_art.py', ROOT/'tools/course_menu_source.py',
            ROOT/'tools/results_source.py', SOURCE, HEADER, COMMON, MATH, FONT,
            *(DECOMP/'include'/name for name in ('fzx_font.h','fzx_game.h','fzx_camera.h','sfx.h',
                'macros.h','fzx_math.h','fzx_racer.h','controller.h','PR/os_cont.h'))]


def generate(output):
    output_location(output);verify_source(DECOMP)
    outputs = {'records_source.c': closure(SOURCE.read_text()), 'records_source_ids.h': ids_header()}
    output.mkdir(parents=True, exist_ok=True)
    receipt = ['# fzx generation receipt v1; regenerate, never edit']
    receipt += ['generator %s %s'%(hashlib.sha256(p.read_bytes()).hexdigest(),
                  os.path.relpath(p, ROOT/'tools').replace('\\','/')) for p in dependencies()]
    for name, data in outputs.items():
        (output/name).write_bytes(data)
        receipt.append('output %s %s'%(hashlib.sha256(data).hexdigest(),name))
    (output/'records_source.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    print('Original records state generated; presentation, retry, durable save and audio consumers remain separate')


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'generated/records-source')
    generate(parser.parse_args().output)
