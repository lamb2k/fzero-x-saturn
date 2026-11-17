#!/usr/bin/env python3
"""Capture the named original finish-menu graphics into bounded image packets.

No display-list interpreter or source state advancement. Font5 metrics, menu
layout and animated color come from pinned private source. Native point-sampled
sprite scaling is a separately reviewed presentation adaptation.
"""
from pathlib import Path
import argparse
import hashlib
import os
import re
from import_source_art import ROOT,DECOMP,ArtError,output_location,verify_source
from course_menu_source import declaration
from results_source import select_us
from records_source import function,replace_once
from finish_menu_assets import catalog,FONT,MENUS

COMMON=DECOMP/'src/game/common.c'


def multiline_function(source,name):
    match=re.search(r'^(?:s32|void|Gfx\*) '+re.escape(name)+r'\([^;{}]*\) \{.*?^\}',source,re.M|re.S)
    if not match:raise ArtError('Original finish-menu draw function missing: '+name)
    return 'static '+match[0]+'\n'


PRELUDE=r'''/* Generated original finish-menu draw closure; regenerate, never edit. */
#include "fzx_finish_menu_draw.h"
#include <limits.h>
#include <string.h>
typedef int32_t s32;
typedef int16_t s16;
typedef char s8;
typedef uint8_t u8;
typedef float f32;
typedef int Gfx;
typedef int TexturePtr;
typedef struct { uint16_t index,width,height; } unk_80077D50;
static const fzx_finish_menu_snapshot_t *source;
static struct {fzx_finish_menu_draw_snapshot_t snapshot;uint8_t fault;} storage,*m;
static int32_t clip[4];
static uint8_t color[4],empty;
_Static_assert(CHAR_MIN<0,"Original Font5 decoder needs signed char");
#define gGameFrameCount source->input.frame
#define sGeneralRaceMenuScissorBoxTimer source->draw_reveal
#define sRaceMenuOptionIndex source->draw_selection
#define sCannotSaveGhost 1
#define sSaveGhostMenuOpen 0
#define sGhostSaveTimer 0
#define TIME_ATTACK_FINISH_SHIFT 0
#define G_IM_FMT_CI 2
#define func_800783AC(x) (x)
static void source_color(s32 r,s32 g,s32 b,s32 a) {
    if(r<0||r>255||g<0||g>255||b<0||b>255||a<0||a>255){m->fault=1;return;}
    color[0]=(uint8_t)r;color[1]=(uint8_t)g;color[2]=(uint8_t)b;color[3]=(uint8_t)a;
}
static void source_scissor(s32 x,s32 y,s32 x2,s32 y2) {
    if(x<0||y<0||x>320||x2<0||x2>320||y>240||y2<0||y2>240){m->fault=1;return;}
    clip[0]=x;clip[1]=y;clip[2]=x2;clip[3]=y2;empty=(uint8_t)(x2<=x||y2<=y);
}
static fzx_results_draw_t *packet(unsigned kind,s32 x,s32 y,s32 x2,s32 y2) {
    if(x<INT16_MIN||y<INT16_MIN||x2>INT16_MAX||y2>INT16_MAX||x2<x||y2<y){m->fault=1;return NULL;}
    if(empty||x2<=clip[0]||y2<=clip[1]||x>=clip[2]||y>=clip[3]||x2==x||y2==y)return NULL;
    if(m->snapshot.count>=FZX_FINISH_MENU_DRAW_MAX){m->fault=1;return NULL;}
    fzx_results_draw_t *p=&m->snapshot.draws[m->snapshot.count++];
    p->kind=(uint8_t)kind;p->x=(int16_t)x;p->y=(int16_t)y;p->x2=(int16_t)x2;p->y2=(int16_t)y2;
    p->clip_x=(int16_t)clip[0];p->clip_y=(int16_t)clip[1];p->clip_x2=(int16_t)clip[2];p->clip_y2=(int16_t)clip[3];
    p->r=color[0];p->g=color[1];p->b=color[2];p->a=color[3];return p;
}
static void source_rectangle(s32 x,s32 y,s32 x2,s32 y2) {
    if(x%4||y%4||x2%4||y2%4){m->fault=1;return;}
    fzx_results_draw_t *p=packet(FZX_RESULTS_RECT,x/4,y/4,x2/4,y2/4);
    if(p){p->w=(int16_t)((x2-x)/4);p->h=(int16_t)((y2-y)/4);}
}
static Gfx *source_label(Gfx *g,int image,const void *palette,int fmt,int tmem,
                         s32 x,s32 y,s32 w,s32 h,int mode) {
    if(image<0||image>=24||palette||fmt!=G_IM_FMT_CI||tmem!=1||mode!=2||
       w!=source_sizes[image][0]||h!=source_sizes[image][1]){m->fault=1;return g;}
    fzx_results_draw_t *p=packet(FZX_RESULTS_IMAGE,x,y,x+w,y+h);
    if(p){p->image=(uint16_t)image;p->w=(int16_t)w;p->h=(int16_t)h;}return g;
}
static Gfx *source_glyph(Gfx *g,const unk_80077D50 *glyph,s32 x,s32 y,int mode,
                         int flip_x,int flip_y,float sx,float sy) {
    if(!glyph||mode!=3||flip_x||flip_y||sx!=0.8f||sy!=0.8f){m->fault=1;return g;}
    /* Original func_8007CDB0 truncates the positive scaled extent before
     * emitting its texture rectangle. Texture sampling is a native consumer. */
    const s32 w=(s32)(glyph->width*sx),h=(s32)(glyph->height*sy);
    fzx_results_draw_t *p=packet(FZX_RESULTS_GLYPH,x,y,x+w,y+h);
    if(p){p->image=glyph->index;p->w=(int16_t)glyph->width;p->h=(int16_t)glyph->height;}return g;
}
#define gSPDisplayList(...) ((void)0)
#define gDPPipeSync(...) ((void)0)
#define gDPSetAlphaCompare(...) ((void)0)
#define gDPSetCombineMode(...) ((void)0)
#define gDPLoadTLUT_pal256(...) ((void)0)
#define gDPSetTextureLUT(...) ((void)0)
#define gDPSetPrimColor(g,l,d,r,v,b,a) source_color(r,v,b,a)
#define gDPSetScissor(g,m,x,y,x2,y2) source_scissor(x,y,x2,y2)
#define gSPTextureRectangle(g,x,y,x2,y2,t,s,v,dx,dy) source_rectangle(x,y,x2,y2)
#define func_8007E410 source_label
#define func_80077D50_impl(t,x,y) ((void)(t))
#define func_80078EA0_impl(g,t,x,y,mode,a,b,c,d,e) source_glyph(g,t,x,y,mode,a,b,c,d)
'''

BRIDGE=r'''
bool fzx_finish_menu_draw_capture(const fzx_finish_menu_snapshot_t *s,fzx_finish_menu_draw_snapshot_t *out) {
    if(!s||!out||!s->tick||s->draw_selection<0||s->draw_selection>5||s->draw_selection==1||
       s->draw_reveal<0||s->draw_reveal>59)return false;
    memset(&storage,0,sizeof(storage));m=&storage;source=s;
    m->snapshot.tick=s->tick;m->snapshot.frame=s->input.frame;
    source_scissor(0,0,320,240);source_color(255,255,255,255);
    source_finish_draw(NULL);
    if(m->fault)return false;
    *out=m->snapshot;return true;
}
'''


def closure():
    info=catalog();menus=select_us(MENUS.read_text());font=select_us(FONT.read_text())
    sizes='static const int source_sizes[24][2]={'+','.join('{%d,%d}'%(w,h) for _,w,h,_ in info['specs'][:24])+'};\n'
    # The adapter functions need types before this table, and the table before use.
    code=PRELUDE.replace('static Gfx *source_label',sizes+'static Gfx *source_label',1)
    chunks=[code,select_us(declaration(MENUS.with_suffix('.h').read_text(),
        r'^typedef enum TimeAttackFinishOptions \{.*?^\} \w+;','finish option IDs'))]
    label_table=declaration(menus,r'^TexturePtr sRaceMenuTextures\[RACE_MENU_MAX\] = \{.*?^\};','race menu texture table')
    entries=re.findall(r'^\s+(\w+),\s*//\s*(RACE_MENU_\w+)',label_table,re.M)
    if len(entries)!=24 or [name for name,_ in entries]!=[s[0] for s in info['specs'][:24]]:
        raise ArtError('Original menu label IDs changed')
    chunks+=['#define %s %d'%(name,i) for i,(_,name) in enumerate(entries)]
    chunks.append('static const TexturePtr sRaceMenuTextures[24]={'+','.join(map(str,range(24)))+'};')
    chunks.append('static const s32 sRaceMenuDimensions[48]={'+','.join(str(n) for _,w,h,_ in info['specs'][:24] for n in (w,h))+'};')
    for name in ('FontSet','FontSetCase','FontChar'):
        chunks.append(select_us(declaration((DECOMP/'include/fzx_font.h').read_text(),r'^typedef enum '+name+r' \{.*?^\} \w+;',name)))
    table=info['font'];glyphs=[]
    for image in table['images']:
        symbol,w,h,_=info['specs'][image];glyphs.append('{%d,%d,%d}'%(image,info['crops'].get(symbol,w),h))
    chunks.append('static unk_80077D50 source_glyphs[]={'+','.join(glyphs)+'};')
    chunks.append('static unk_80077D50 *sFont5CompTexInfos[]={'+','.join('&source_glyphs[%d]'%i for i in range(len(glyphs)))+'};')
    chunks.append('static '+declaration(font,r'^const s16 kFont5CharWidths\[\] = \{.*?^\};','Font5 widths'))
    for number in (1,2,3,4,6):
        chunks.extend(('#define sFont%dCompTexInfos sFont5CompTexInfos'%number,
                       '#define kFont%dCharWidths kFont5CharWidths'%number))
    for name in ('func_i2_80105EE4','func_i2_80105F48','func_i2_80105F6C','Font_GetCharIndex','Font_DrawScaledString'):
        body=multiline_function(font,name).replace('s8* str','const s8* str')
        if name=='Font_DrawScaledString':
            body=body.replace('{','{\n    if(fontSet!=FONT_SET_5){m->fault=1;return gfx;}',1)
        chunks.append(body)
    common=select_us(COMMON.read_text())
    chunks.append('static '+declaration(common,r'^s32 D_800CD4E0\[\] = \{.*?^\};','animated menu colors'))
    chunks.append(function(common,'func_8007DB28'))
    for name in ('Menus_DrawBeveledBox','Menus_SetOptionColor','Menus_DrawRaceMenuTexture'):
        chunks.append(function(menus,name))
    draw=function(menus,'Menus_DrawTimeAttackFinishMenu')
    start=draw.index('    gDPPipeSync(gfx++);');end=draw.index('    if (sSaveGhostMenuOpen) {',start)
    chunks.append('static Gfx *source_finish_draw(Gfx *gfx) {\n'+draw[start:end]+'    return gfx;\n}\n')
    chunks.append(BRIDGE)
    return ('\n'.join(chunks)+'\n').replace(' << 2',' * 4').encode()


def dependencies():
    return [Path(__file__),ROOT/'include/fzx_finish_menu_draw.h',ROOT/'include/fzx_finish_menu.h',
        ROOT/'include/fzx_records.h',ROOT/'include/fzx_results.h',ROOT/'tools/finish_menu_assets.py',
        ROOT/'tools/records_source.py',ROOT/'tools/results_source.py',ROOT/'tools/course_menu_source.py',
        ROOT/'tools/import_source_art.py',ROOT/'tools/machine_menu_assets.py',FONT,MENUS,MENUS.with_suffix('.h'),COMMON,
        DECOMP/'include/fzx_font.h',DECOMP/'src/game/texture_utils.c',DECOMP/'src/game/object.c',
        DECOMP/'assets/yaml/us/rev0/common_assets_compressed.yaml']


def generate(output):
    output_location(output);verify_source(DECOMP);data=closure()
    receipt=['# fzx generation receipt v1; regenerate, never edit']
    receipt+=['generator %s %s'%(hashlib.sha256(p.read_bytes()).hexdigest(),
                  os.path.relpath(p,ROOT/'tools').replace('\\','/')) for p in dependencies()]
    output.mkdir(parents=True,exist_ok=True);(output/'finish_menu_draw_source.c').write_bytes(data)
    receipt.append('output %s finish_menu_draw_source.c'%hashlib.sha256(data).hexdigest())
    (output/'finish_menu_draw_source.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    print('Original finish-menu draw packets generated; native residency remains separate')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'generated/finish-menu-draw-source')
    generate(parser.parse_args().output)
