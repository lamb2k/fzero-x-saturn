#!/usr/bin/env python3
"""Bind the unchanged stock Title_Update routine to a bounded native bridge.

The first cartridge title has no 64DD branch. Its random return value is unused:
record that call without consuming the race RNG. Sound requests are recorded for
the future audio consumer, never represented as already played.
"""
from pathlib import Path
import argparse
import hashlib
import os
import re
from import_source_art import ROOT, DECOMP, ArtError, output_location, verify_source

SOURCE = DECOMP/'src/overlays/ovl_i4/title.c'
PRELUDE = '''/* Generated stock Title_Update closure; regenerate, never edit. */
#include "fzx_title.h"
#include "title_game_modes.h"
#include "title_sound_ids.h"
#include <limits.h>
#include <string.h>
#define gControllersConnected (state->connected)
#define gGameMode GAMEMODE_FLX_TITLE
#define gSharedController 0
#define Controller_SetGlobalInputs(p) ((void)0)
#define Math_Rand1() ((void)++state->unused_random_calls)
#define gLeoDriveConnectionState 0
#define OBJECT_COUNTER(o) 0
#define OBJECT_STATE(o) 0
#define D_i4_8011D794 0
#define D_i4_8011D790 (state->timer)
#define gGameModeChangeState (state->transition ? GAMEMODE_CHANGE_START : GAMEMODE_UPDATE)
#define gInputButtonPressed (state->pressed)
#define BTN_A 0x8000u
#define BTN_START 0x1000u
#define Audio_TriggerSystemSE(cue_id) ((void)(state->cue=(uint16_t)(cue_id)))
#define func_8007E0CC() ((void)(state->stop_music=1))
#define Audio_SESeqStart() ((void)0)
'''
BRIDGE = '''
void fzx_title_reset(fzx_title_state_t *state) {
    if(state)memset(state,0,sizeof(*state));
}
bool fzx_title_step(fzx_title_state_t *state,uint16_t buttons,bool connected,bool transition) {
    if(!state || state->timer < -1 || state->timer==INT32_MAX || state->advance>1)return false;
    if(state->advance)return true;
    state->cue=0;state->stop_music=0;
    state->connected=(uint8_t)connected;state->transition=(uint8_t)transition;
    if(!connected)buttons=0;
    state->pressed=(uint16_t)(buttons & (uint16_t)~state->held);
    state->held=buttons;
    ++state->ticks;
    const int32_t result=source_title_update(state);
    if(result==GAMEMODE_FLX_MAIN_MENU)state->advance=1;
    else if(result!=GAMEMODE_FLX_TITLE)return false;
    return true;
}
bool fzx_title_push_start_visible(const fzx_title_state_t *state) {
    return state && state->connected && !(state->timer<95 && state->timer>=0);
}
'''


def closure(source):
    match=re.search(r'^s32 Title_Update\(void\) \{.*?^\}',source,re.M|re.S)
    if not match:
        raise ArtError('Original title update missing')
    body=match.group(0)
    if body.count('Math_Rand1();')!=1 or 'if (D_i4_8011D790 < 80)' not in body:
        raise ArtError('Original title update contract changed')
    if '(D_i4_8011D790 < 95) && (D_i4_8011D790 >= 0)' not in source:
        raise ArtError('Original push-start visibility changed')
    return (PRELUDE+body.replace('s32 Title_Update(void)', 'static int32_t source_title_update(fzx_title_state_t *state)',1)+'\n'+BRIDGE).encode()


def sound_header(source):
    match=re.search(r'^typedef enum SfxId \{.*?^\} SfxId;',source,re.M|re.S)
    if not match:
        raise ArtError('Original sound ID enum missing')
    return (match.group(0)+'\n').encode()


def generate(output):
    output_location(output);verify_source(DECOMP)
    outputs={'title_source.c':closure(SOURCE.read_text()),
             'title_game_modes.h':(DECOMP/'include/fzx_game.h').read_bytes(),
             'title_sound_ids.h':sound_header((DECOMP/'include/sfx.h').read_text())}
    inputs=[Path(__file__),ROOT/'include/fzx_title.h',ROOT/'tools/import_source_art.py',SOURCE,
            DECOMP/'include/fzx_game.h',DECOMP/'include/sfx.h']
    output.mkdir(parents=True,exist_ok=True)
    receipt=['# fzx generation receipt v1; regenerate, never edit']
    receipt+=['generator %s %s'%(hashlib.sha256(p.read_bytes()).hexdigest(),os.path.relpath(p,ROOT/'tools').replace('\\','/'))for p in inputs]
    for name,data in outputs.items():
        (output/name).write_bytes(data)
        receipt.append('output %s %s'%(hashlib.sha256(data).hexdigest(),name))
    (output/'title_source.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    print('Original Title_Update closure generated; audio requests recorded, playback unrun')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'generated/title_state')
    args=parser.parse_args();generate(args.output)
