#!/usr/bin/env python3
"""Bounded original US records/name draw capture, separate from source ticking.

Generated code and source font metadata stay private. There is no runtime
display-list interpreter. Geometry is explicitly represented by six machine
instances and one spinner packet after the ordered UI commands.
"""
from pathlib import Path
import argparse
import hashlib
import os
import re
import json
import shutil
import subprocess
from import_source_art import ROOT, DECOMP, ArtError, output_location, verify_source
from course_menu_source import declaration
from results_source import select_us
import results_draw_source as draw
import records_source as state
import results_source as results
from records_assets import catalog

PRELUDE = r'''
#include "fzx_records_draw.h"
#include "records_source_ids.h"
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef int32_t s32;
typedef int16_t s16;
typedef char s8;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef float f32;
typedef int Gfx;
typedef fzx_records_key_t KeyboardObject;
typedef fzx_results_records_t CourseInfo;
typedef fzx_results_machine_t MachineInfo;
static struct { fzx_records_draw_snapshot_t snapshot; uint8_t fault; } storage,*m;
static const fzx_records_snapshot_t *source;
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define gGameFrameCount source->frame
#define gCurrentTimeAttackRecordPosition source->record_position
#define gCurrentTimeAttackHasMaxSpeed source->max_speed_record
#define gBestTimedLap source->best_lap
#define gTrackNames source_track_names
static char *source_track_names[24];
#define G_CC_DECALRGBA 0
#define D_80149A0 2
#define D_303A810 3
#define D_3000088 0
#define G_IM_FMT_RGBA 0
#define G_IM_SIZ_16b 0
#define RECORDS_ENTRY_DRAW_ARROWS (1 << 5)
#define func_800783AC(x) (x)
#define gDPSetColorImage(...) ((void)0)
#define gDPSetFillColor(...) source_color(0,0,0,255)
#define gDPFillRectangle(g,x,y,x2,y2) source_fill(x,y,x2,y2)
static Gfx *RecordsEntry_DrawSpeed(Gfx *,s32,s32,f32,bool,bool);
static Gfx *RecordsEntry_DrawRecordMachine(Gfx *,s32,MachineInfo *);
static Gfx *RecordsEntry_DrawRecordEngineSetting(Gfx *g,s32 x,s32 y,f32 e) {
    (void)x;(void)y;(void)e;m->fault=1;return g;
}
static Gfx *func_8007B14C(Gfx *g,...) {m->fault=1;return g;}
#define aYellowArrowTex 0
'''

HELPERS = r'''
static void source_fill(s32 x,s32 y,s32 x2,s32 y2) {
    draw_primitive=1;source_rectangle(x*4,y*4,(x2+1)*4,(y2+1)*4,0,0,1024,1024);
}
static Gfx *RecordsEntry_DrawRecordMachine(Gfx *g,s32 slot,MachineInfo *info) {
    if(slot<0||slot>=6||info->character>=30||info->customType||m->snapshot.machine_count>=6){m->fault=1;return g;}
    fzx_records_machine_draw_t *p=&m->snapshot.machines[m->snapshot.machine_count++];
    p->slot=(uint8_t)(slot+1);p->character=info->character;
    p->r=info->bodyR;p->g=info->bodyG;p->b=info->bodyB;
    p->x=(int16_t)(slot<5?((slot&1)?-source->record_left_offset:source->record_left_offset)+source->record_left+45:
                         source->record_left_offset+source->max_speed_left+36);
    p->y=(int16_t)(slot<5?source->record_top+source->record_height*slot-4:
                         source->max_speed_top+source->max_speed_machine_offset);
    return g;
}
static void source_spinner(void) {
    if(!source->spinner_valid||source->spinner_keyboard_index<0||source->spinner_keyboard_index>=50){m->fault=1;return;}
    s32 i=Font_GetCharIndex(&sNameKeyboardCharacters[source->spinner_keyboard_index],FONT_SET_UPPERCASE_ONLY);
    if(i<0||i>=48){m->fault=1;return;}
    const unk_80077D50 *t=sFont1CompTexInfos[i];
    fzx_records_spinner_draw_t *p=&m->snapshot.spinner;
    p->image=t->index;p->width=t->width;p->height=t->height;
    p->x=source->spinner_x+8;p->y=source->spinner_y-t->height/2;p->visible=1;
}
'''

BRIDGE = r'''
bool fzx_records_draw_capture(const fzx_records_snapshot_t *s,fzx_records_draw_snapshot_t *out) {
    if(!s||!out||s->draw_kind>FZX_RECORDS_DRAW_TABLE||s->config.course>=24||
       !memchr(s->config.name,0,sizeof(s->config.name))||s->draw_name_state<0||s->draw_name_state>3||
       s->cursor_x<0||s->cursor_x>=10||s->cursor_y<0||s->cursor_y>=5||s->name_length<0||s->name_length>3||
       s->name[3]||s->name_timer<0||s->name_timer>61||s->record_left_offset<0||s->record_left_offset>320||
       s->record_left<0||s->record_left>320||s->record_top<0||s->record_top>240||
       s->record_height<0||s->record_height>40||!(s->records.maxSpeed>=0&&s->records.maxSpeed<=462.9f))return false;
    if(s->record_flags&~(RECORDS_ENTRY_DRAW_NAME_BOX|RECORDS_ENTRY_HIGHLIGHT_CURRENT))return false;
    if(s->max_speed_left<0||s->max_speed_left>320||s->max_speed_top<0||s->max_speed_top>240||
       s->max_speed_machine_offset<0||s->max_speed_machine_offset>240||
       s->records.bestTime<0||s->records.bestTime>MAX_TIMER)return false;
    for(unsigned i=0;i<50;++i)if(s->keys[i].character[1]||((unsigned char)s->keys[i].character[0]>126)||
       s->keys[i].left<0||s->keys[i].left>=320||s->keys[i].top<0||s->keys[i].top>=240)return false;
    for(unsigned i=0;i<3;++i)if((unsigned char)s->name[i]>126)return false;
    for(unsigned i=0;i<5;++i) {
        if(s->records.recordNames[i][3]||s->records.timeRecord[i]<0||s->records.timeRecord[i]>MAX_TIMER)return false;
        for(unsigned j=0;j<3;++j)if((unsigned char)s->records.recordNames[i][j]>126)return false;
    }
    memset(&storage,0,sizeof(storage));m=&storage;source=s;
    m->snapshot.tick=s->tick;m->snapshot.frame=s->frame;
    draw_clip[0]=0;draw_clip[1]=0;draw_clip[2]=320;draw_clip[3]=240;
    draw_copy=draw_primitive=draw_modulate=0;source_color(255,255,255,255);
    source_track_names[s->config.course]=(char *)s->config.name;
    if(s->draw_kind==FZX_RECORDS_DRAW_NAME)RecordsEntry_DrawNameEntry(NULL);
    if(s->draw_kind==FZX_RECORDS_DRAW_TABLE)RecordsEntry_DrawRecords(NULL,s->config.course);
    if(m->fault)return false;
    *out=m->snapshot;return true;
}
'''


def closure():
    text=select_us(state.SOURCE.read_text());font=select_us(draw.FONT.read_text())
    asset=catalog();chunks=[PRELUDE]
    defines=state.PRELUDE.split('#define sKeyboardObjects',1)[1].split('static void source_event',1)[0]
    defines='#define sKeyboardObjects'+defines
    chunks.append(defines.replace('m->snapshot.','source->'))
    chunks += ['#define gRecordNameEntered source->name', '#define gRecordNameEnteredLength source->name_length']
    chunks.append('static const int32_t source_sheet_sizes[9][2]={'+','.join('{%d,%d}'%(w,h) for _,w,h,_ in asset['specs'][:9])+'};')
    chunks.extend('#define %s %d'%(n,i) for i,(n,_,_,_) in enumerate(asset['specs'][:9]))
    adapter=draw.ADAPTER.replace('FZX_RESULTS_DRAW_MAX','FZX_RECORDS_DRAW_MAX')
    adapter=adapter.replace('fzx_results_draw_t','fzx_results_draw_t').replace('image>=8','image>=9')
    adapter=adapter.replace('if(setup!=0 && setup!=1)', 'if(setup<0 || setup>3)')
    adapter=adapter.replace('draw_copy=(uint8_t)setup;', 'draw_copy=(uint8_t)(setup==1);')
    adapter=adapter.replace('if(mode!=G_CC_PRIMITIVE && mode!=G_CC_MODULATEIDECALA_PRIM)', 'if(mode<0 || mode>2)')
    adapter=adapter.replace('!glyph || mode ||','!glyph || mode<0 || mode>1 ||')
    adapter=adapter.replace('p->image=glyph->index;', 'if(glyph->index<54 && !mode)p->r=p->g=p->b=p->a=255;\n    p->image=glyph->index;')
    chunks.append(adapter)
    chunks.append('static void source_fill(s32,s32,s32,s32);')
    for num in range(1,7):
        chunks.append('static '+declaration(font,r'^const s16 kFont%dCharWidths\[\] = \{.*?^\};'%num,'font widths'))
        actual=num if num in (1,3,4) else 3
        rows=asset['fonts'][actual]['images']
        # COMP_TEX_INFO_WH can expose fewer columns than the stored texture.
        # In particular, the empty-time Font4 minus is 8 columns of a 16-wide
        # source image. Match the original descriptor and the offline crop.
        chunks.append('static unk_80077D50 source_font%d[]={%s};'%(num,','.join(
            '{%d,%d,%d}'%(i,asset['crops'].get(asset['specs'][i][0],asset['specs'][i][1]),
                          asset['specs'][i][2]) for i in rows)))
        chunks.append('static unk_80077D50 *sFont%dCompTexInfos[]={%s};'%(num,','.join('&source_font%d[%d]'%(num,i) for i in range(len(rows)))))
    for name in ('func_i2_80105EE4','func_i2_80105F48','func_i2_80105F6C','Font_GetCharIndex','Font_GetStringWidth','Font_DrawString'):
        chunks.append(state.function(font,name).replace('s8* str','const s8* str'))
    chunks.append(state.function(select_us(draw.MATH.read_text()),'Math_Round'))
    texture=select_us(draw.TEXTURE.read_text())
    chunks.append(state.function(texture,'func_8007F090'))
    # Source color pulse lives in texture_utils.c rather than texture_utils2.c.
    common=select_us(state.COMMON.read_text())
    chunks.append('static '+declaration(common,r'^s32 D_800CD4E0\[\] = \{.*?^\};','pulse colors'))
    chunks.append(state.function(common,'func_8007DB28'))
    chunks.append(declaration((DECOMP/'include/fzx_hud.h').read_text(),r'^typedef enum TimerDigits \{.*?^\} TimerDigits;','timer digits'))
    for name in ('Hud_DrawTimerDigitScisRectangle','Hud_DrawTimerScisThousandths'):
        chunks.append(state.function(select_us(draw.HUD.read_text()),name))
    menus=select_us((DECOMP/'src/overlays/ovl_i3/menus.c').read_text())
    for name in ('Menus_DrawBeveledBox','Menus_DrawBox'):
        chunks.append(state.function(menus,name))
    chunks.append(state.function(texture,'func_8007A440') if 'Gfx* func_8007A440' in texture else state.function(select_us((DECOMP/'src/game/texture_utils.c').read_text()),'func_8007A440'))
    for name in ('sNameKeyboardCharacters','sNameEntryStr'):
        chunks.append('static '+declaration(text,r'^signed char '+name+r'\[\] = [^\n]+;',name).replace('signed char','s8'))
    chunks.append(HELPERS)
    chunks.append(state.function(text,'RecordsEntry_DrawSpeed'))
    records=state.function(text,'RecordsEntry_DrawRecords')
    start=records.index('    gSPLoadUcodeL(');end=records.index('    for (i = 0; i < 5; i++) {',start)
    records=records[:start]+records[end:]
    chunks.append(records)
    name=state.function(text,'RecordsEntry_DrawNameEntry')
    start=name.index('    state = sRecordsEntryNameEntryState;');end=name.index('    switch (state)',start)
    name=name[:start]+'    state = source->draw_name_state;\n'+name[end:]
    start=name.index('            // Draw Cursor Character');end=name.index('            break;',start)
    name=name[:start]+'            source_spinner();\n'+name[end:]
    name=name.replace('    sPreviousRecordsEntryNameEntryState = sRecordsEntryNameEntryState;','')
    chunks.extend([name,BRIDGE])
    code='\n'.join(chunks).replace(' << 2',' * 4').replace('->keyboardCharacter','->character').replace('.keyboardCharacter','.character')
    code=code.replace('CourseInfo* courseInfo;','const CourseInfo* courseInfo;').replace('MachineInfo *info','const MachineInfo *info').replace('s32,MachineInfo *','s32,const MachineInfo *').replace('KeyboardObject* keyboardObject;','const KeyboardObject* keyboardObject;')
    code=code.replace('&gCourseInfos[courseIndex]','&source->records')
    return code.encode()


def dependencies():
    return list(dict.fromkeys([Path(__file__),ROOT/'include/fzx_records_draw.h',ROOT/'tools/records_assets.py',
        ROOT/'tools/results_draw_source.py',DECOMP/'src/game/texture_utils.c',DECOMP/'src/overlays/ovl_i3/menus.c',
        ROOT/'tools/results_source.py',DECOMP/'src/game/object.c',DECOMP/'src/game/camera.c',
        DECOMP/'src/game/racer.c',DECOMP/'include/fzx_camera.h',*state.dependencies(),*draw.dependencies()]))


MATRIX = r'''
static int32_t source_records_word(const Mtx *mtx,unsigned row,unsigned column) {
#if defined(GDIFFUSER_PORT)
    column^=1;
#endif
    const uint32_t bits=((uint32_t)(uint16_t)mtx->u.i[row][column]<<16)|mtx->u.f[row][column];
    int32_t result;memcpy(&result,&bits,sizeof(result));return result;
}
static void source_records_matrix(const MtxF *model,const MtxF *view,const MtxF *projection,int32_t out[18]) {
    Mtx combined,proj;MtxF combined_f;
    Matrix_AffineMultiply((MtxF *)view,(MtxF *)model,&combined_f);
    Matrix_ToMtx(&combined_f,&combined);Matrix_ToMtx((MtxF *)projection,&proj);
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)out[i*4+j]=source_records_word(&combined,i,j);
    out[16]=source_records_word(&proj,0,0);out[17]=source_records_word(&proj,1,1);
}
bool fzx_records_machine_matrix(int32_t out[18]) {
    if(!out)return false;
    Mtx model;MtxF model_f;
    source_records_model(&model,&model_f);
    source_records_matrix(&model_f,&gCameras[1].viewMtx,&gCameras[1].projectionMtx,out);
    return true;
}
bool fzx_records_spinner_matrix(const fzx_records_draw_snapshot_t *p,int32_t out[18]) {
    if(!p||!out||p->spinner.visible!=1||p->spinner.height!=16||
       (p->spinner.width!=8&&p->spinner.width!=16))return false;
    Mtx model,view,projection;MtxF model_f,view_f,projection_f;
    source_spinner_matrices(p->frame,&model,&model_f,&view,&view_f,&projection,&projection_f);
    source_records_matrix(&model_f,&view_f,&projection_f,out);
    return true;
}
'''


def matrix_bridge():
    source=select_us(state.SOURCE.read_text())
    init=state.function(source,'RecordsEntry_Init')
    call=re.search(r'    Matrix_SetLockedLookAt\(.*?;',init,re.S)[0]
    call=state.replace_once(call,'&D_8024DC80, NULL','model, model_f')
    model='static void source_records_model(Mtx *model,MtxF *model_f) {\n'+call+'\n}\n'
    update=state.function(source,'RecordsEntry_UpdateNameEntryKeyboard')
    start=update.index('        t = gGameFrameCount % 128;')
    end=update.index('    }\n}',start)
    spin=update[start:end].replace('gGameFrameCount','frame')
    for old,new in [('&gGfxPool->unk_2B2C8[1], &mtxF','projection, projection_f'),
                    ('&gGfxPool->unk_2B2C8[2], &mtxF','view, view_f'),
                    ('&gGfxPool->unk_2B2C8[3], &mtxF','model, model_f'),
                    ('&sSpinningKeyboardCharacterPerspectiveScale','&perspective')]:
        spin=state.replace_once(spin,old,new)
    spin=('static void source_spinner_matrices(uint32_t frame,Mtx *model,MtxF *model_f,\n'
          'Mtx *view,MtxF *view_f,Mtx *projection,MtxF *projection_f) {\n'
          's32 t,i;f32 temp_fv0,spB8;u16 perspective;\n'+spin+'}\n')
    return ('/* Generated original records matrices; regenerate, never edit. */\n'
            '#include "global.h"\n#include "fzx_camera.h"\n#include "fzx_records_draw.h"\n#include <string.h>\n'
            'extern f32 D_800CE748,D_800CE74C,D_800CE750;\n'+model+spin+MATRIX).encode()


def camera_capture_source():
    """Original fixed records orbit, independently executable on the HOST."""
    camera=select_us((DECOMP/'src/game/camera.c').read_text())
    header=(DECOMP/'include/fzx_camera.h').read_text()
    chunks=[results.matrix_host_support().decode(), '#include <stdio.h>\n#include <string.h>',
            '#include "fzx_records_draw.h"', 'typedef struct { Vec3f x,y,z; } Mtx3F;']
    for name in ('CameraType','CameraParameters','CameraTypeOrbit','CameraOrbitData'):
        chunks.append(declaration(header,r'^typedef (?:struct|enum) '+name+r' \{.*?^\} '+name+r';',name))
    chunks.append('typedef struct { s32 type;CameraParameters parameters;CameraTypeOrbit orbit; } CameraSettings;')
    chunks.append('typedef struct { MtxF viewMtx,projectionMtx; } Camera;static Camera gCameras[4];')
    chunks.append(declaration((DECOMP/'include/fzx_math.h').read_text(),r'^#define DEG_TO_FZXANG2\(x\)[^\n]+','degree conversion'))
    chunks.append(declaration(camera,r'^const CameraOrbitData D_800D4F58 = \{.*?^\};','records orbit'))
    for name in ('Camera_SettingsSetOrbitWithParameters','Camera_SettingsUpdateOrbit'):
        chunks.append(state.function(camera,name))
    racer=(DECOMP/'src/game/racer.c').read_text()
    for name in ('D_800CE748','D_800CE74C','D_800CE750'):
        chunks.append(declaration(racer,r'^f32 '+name+r' = [^\n]+;',name))
    chunks.append(matrix_bridge().decode().replace('#include "global.h"','').replace('#include "fzx_camera.h"',''))
    branch=re.search(r'        case CAMERA_MODE_RECORDS_ENTRY:(.*?)            break;',camera,re.S)[1]
    setup=branch.split('            break;',1)[0].replace('camera->','cam.').replace('cameraSettings','&settings')
    # Actual source light values are part of the capture and its receipt.
    light=re.search(r'Lights_SetSource\(&D_8024DCC0,\s*([^;]+)\);',state.SOURCE.read_text())
    if not light:raise ArtError('Original records light missing')
    light_values=[int(s.strip()) for s in light[1].split(',')]
    if len(light_values)!=9:raise ArtError('Original records light extent changed')
    chunks.append(r'''
static void print_matrix(const float *p,unsigned n) {
    putchar('[');for(unsigned i=0;i<n;++i)printf("%s%.9g",i?",":"",(double)p[i]);putchar(']');
}
int main(void) {
    Math_SinTableInit();CameraSettings settings;
    struct {float near,far,fovScaleX,fovScaleY;} cam;
'''+setup+r'''
    Camera_SettingsUpdateOrbit(&settings);
    const CameraParameters *p=&settings.parameters;
    Mtx model,view,projection;MtxF model_f;u16 scale;int32_t q[18];
    source_records_model(&model,&model_f);
    Matrix_SetLookAt(&view,&gCameras[1].viewMtx,p->eye.x,p->eye.y,p->eye.z,
                     p->at.x,p->at.y,p->at.z,p->up.x,p->up.y,p->up.z);
    Matrix_SetFrustrum(&projection,&gCameras[1].projectionMtx,p->fov,cam.near,cam.far,
                      cam.fovScaleX,p->frustrumCenterX,cam.fovScaleY,p->frustrumCenterY,&scale);
    if(!fzx_records_machine_matrix(q))return 1;
    printf("{\"model\":");print_matrix(&model_f.m[0][0],16);
    printf(",\"view\":");print_matrix(&gCameras[1].viewMtx.m[0][0],16);
    printf(",\"projection\":[%.9g,%.9g],\"matrix_q16\":[",(double)gCameras[1].projectionMtx.m[0][0],(double)gCameras[1].projectionMtx.m[1][1]);
    for(unsigned i=0;i<18;++i)printf("%s%d",i?",":"",q[i]);
'''+ '    printf("],\\\"light\\\":'+str(light_values).replace(' ','')+',\\\"viewport_scale\\\":[160,120]}\\n");\n    return 0;\n}\n')
    return '\n'.join(chunks).encode()


def capture_camera(output):
    c=output/'records_camera_capture.c';exe=output/'records_camera_capture'
    c.write_bytes(camera_capture_source())
    if shutil.which('gcc'):
        subprocess.run(['gcc','-std=c11','-O2','-fno-strict-aliasing','-I'+str(ROOT/'include'),str(c),'-lm','-o',str(exe)],check=True,capture_output=True)
        data=subprocess.check_output([str(exe.resolve())])
    elif os.name=='nt' and shutil.which('wsl'):
        def linux(p):
            p=p.resolve();return '/mnt/'+p.drive[0].lower()+p.as_posix()[2:]
        subprocess.run(['wsl','--','gcc','-std=c11','-O2','-fno-strict-aliasing','-I'+linux(ROOT/'include'),linux(c),'-lm','-o',linux(exe)],check=True,capture_output=True)
        data=subprocess.check_output(['wsl','--',linux(exe)])
    else:
        raise ArtError('Original records camera capture requires GCC (or WSL GCC)')
    parsed=json.loads(data)
    if len(parsed['model'])!=16 or len(parsed['matrix_q16'])!=18:raise ArtError('Malformed camera capture')
    return data


def generate(output):
    output_location(output);verify_source(DECOMP);output.mkdir(parents=True,exist_ok=True)
    outputs={'records_draw_source.c':closure(),'records_source_ids.h':state.ids_header(),
             'records_matrix_bridge.c':matrix_bridge(), 'records_camera_capture.c':camera_capture_source(),
             'records_camera.json':capture_camera(output)}
    receipt=['# fzx generation receipt v1; regenerate, never edit']
    receipt += ['generator %s %s'%(hashlib.sha256(p.read_bytes()).hexdigest(),os.path.relpath(p,ROOT/'tools').replace('\\','/')) for p in dependencies()]
    for name,data in outputs.items():
        (output/name).write_bytes(data);receipt.append('output %s %s'%(hashlib.sha256(data).hexdigest(),name))
    (output/'records_draw_source.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'generated/records-draw-source')
    generate(parser.parse_args().output)
