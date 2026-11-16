#!/usr/bin/env python3
"""Extract original backdrop vertex updates into bounded native storage.

Only presentation storage and explicit pause/pool inputs change. Stock source
math, conversion to source s16 vertices, and both scroll advances are retained.
The source Background_Update remains available to the HOST oracle.
"""
from pathlib import Path
import argparse
import hashlib
import os
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT/'upstream/G-Diffuser/decomp/src/overlays/ovl_i3/background.c'
SOURCE_SHA = '96eb8bc773037505480ce77bc590cb014a293744a8c2b09d5bcabc9a7f25773f'
FUNCTIONS = ('Background_InitBackgroundInfo', 'Background_UpdateSkyboxVtx',
             'Background_UpdateVenueFloorVtx', 'Background_UpdateCloudVtx', 'Background_Update')


def function(source, name):
    match = re.search(r'^void '+name+r'\([^;]*?\) \{\n.*?^\}\n', source, re.M|re.S)
    if not match:
        raise ValueError('Missing source backdrop function: '+name)
    return match[0]


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Source backdrop extraction boundary changed: '+old[:60])
    return text.replace(old, new)


def native(source):
    if hashlib.sha256(source.encode()).hexdigest() != SOURCE_SHA:
        raise ValueError('Source backdrop changed; review native extraction')
    chunks = [function(source, name) for name in FUNCTIONS]
    update = chunks[-1]
    update = once(update, '''        if (sSkyboxFlags & SKYBOX_STARRY) {
            Background_UpdateStars(i, background, camera);
        }
''', '')
    update = once(update, '''    if (sBackgroundSpriteCount != 0) {
        Background_UpdateBackgroundSprites();
    }
''', '')
    chunks[-1] = update
    body = '\n'.join(chunks)
    # The compiler selects the source's stock conditional arms; no fork hooks
    # or EK clamping/widescreen differences are accepted by this module.
    names = {name:'native_'+name for name in FUNCTIONS}
    names.update(sBackgrounds='native_backgrounds', sBackgroundCount='1',
                 sCloudCount='1', gGamePaused='native_paused',
                 sBackgroundScale='native_background_scale', gNumPlayers='1',
                 kDefaultCloudInfo='native_default_cloud')
    body = re.sub(r'\b('+'|'.join(names)+r')\b', lambda m:names[m[0]], body)
    body = body.replace('gGfxPool->unk_29B48', 'native_vertices[native_pool]')
    for name in FUNCTIONS:
        body = body.replace('void native_'+name+'(', 'static void native_'+name+'(')
    floor = re.search(r'CourseVenueFloor sVenueFloorMuteCity = (\{[^\n]+\});', source)
    cloud = re.search(r'const ScrollingBackground kDefaultCloudInfo = (\{[^\n]+\});', source)
    scale = re.search(r'f32 sBackgroundScale = ([^;]+);', source)
    if not floor or not cloud or not scale:
        raise ValueError('Source backdrop constants changed')
    floor_values = once(floor[1], 'aVenueMuteCityTex', 'NULL')
    prefix = '''
#include "backdrop_state.h"
#if defined(PORT) || defined(EXPANSION_KIT)
#error Native backdrop requires stock source conditionals
#endif
_Static_assert(sizeof(Background)==96, "source backdrop state ABI");
_Static_assert(sizeof(RefBackdropVertex)==16, "source backdrop vertex ABI");
_Static_assert(sizeof(RefBackdrop)==424, "backdrop packet ABI");
static Background native_backgrounds[1];
static Vtx native_vertices[2][28];
static unsigned int native_pool;
static int native_paused, native_ready;
'''
    prefix += 'static const f32 native_background_scale = '+scale[1]+';\n'
    prefix += 'static const ScrollingBackground native_default_cloud = '+cloud[1]+';\n'
    prefix += 'static CourseVenueFloor native_floor = '+floor_values+';\n'
    suffix = '''
static void ref_backdrop_copy(RefBackdrop* out, const Background* background, const Vtx* vertices) {
    unsigned int i,j;
    out->faults=0;out->count=20;
    memcpy(out->state,background,sizeof(out->state));
    for(i=0;i<20;++i) {
        for(j=0;j<3;++j)out->vertices[i].position[j]=vertices[i].v.ob[j];
        out->vertices[i].flag=vertices[i].v.flag;
        for(j=0;j<2;++j)out->vertices[i].uv[j]=vertices[i].v.tc[j];
        for(j=0;j<4;++j)out->vertices[i].color[j]=vertices[i].v.cn[j];
    }
}
int ref_backdrop_init(float cloud_x_speed,float cloud_z_speed) {
    const float maximum_speed = 10 * 0.0005f; /* Source integer-to-float product. */
    native_ready=0;
    if(gNumPlayers!=1 || !(cloud_x_speed>=-maximum_speed && cloud_x_speed<=maximum_speed) ||
       !(cloud_z_speed>=-maximum_speed && cloud_z_speed<=maximum_speed)) return 1;
    memset(native_backgrounds,0,sizeof(native_backgrounds));
    memset(native_vertices,0,sizeof(native_vertices));
    native_pool=0;native_paused=0;
    native_Background_InitBackgroundInfo(&gCameras[0],native_backgrounds,&native_floor,
                                        cloud_x_speed,cloud_z_speed);
    native_ready=1;return 0;
}
void ref_backdrop_tick(RefBackdrop* out,unsigned int pool,int paused) {
    if(!out)return;
    memset(out,0,sizeof(*out));
    if(!native_ready || pool>1 || gNumPlayers!=1) {out->faults=1;return;}
    native_pool=pool;native_paused=paused!=0;
    native_Background_Update();
    ref_backdrop_copy(out,native_backgrounds,native_vertices[native_pool]);
}
'''
    return prefix + body + suffix


def generate(source_path, output):
    import skyline_state
    source = source_path.read_text()
    result = source+'\n'+native(source)+'\n'+skyline_state.native(source)
    result += '\n#ifdef REF_BACKDROP_ORACLE\n#include "backdrop_oracle.inc"\n#endif\n'
    result += '\n#ifdef REF_BACKDROP_ORACLE\n#include "skyline_oracle.inc"\n#endif\n'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(result.encode())
    files = [Path(__file__), source_path, source_path.with_suffix('.h'), ROOT/'ref/backdrop_state.h', ROOT/'ref/backdrop_oracle.inc']
    files += [ROOT/'tools/skyline_state.py', ROOT/'ref/skyline_state.h', ROOT/'ref/skyline_oracle.inc']
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    receipt = ['# fzx generation receipt v1; regenerate, never edit']
    receipt += ['generator %s %s'%(digest(p), os.path.relpath(p, ROOT/'tools')) for p in files]
    receipt += ['output %s %s'%(digest(output), output.name)]
    output.with_suffix('.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=SOURCE)
    parser.add_argument('--output',type=Path,default=ROOT/'generated/backdrop_state/background.c')
    args=parser.parse_args();generate(args.source,args.output)
