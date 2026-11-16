#!/usr/bin/env python3
"""Bound the pinned Mute City skyline state; no loaders or runtime Gfx calls."""
import hashlib
import re
from backdrop_state import SOURCE_SHA, function, once


def native(source):
    if hashlib.sha256(source.encode()).hexdigest() != SOURCE_SHA:
        raise ValueError('Source skyline changed; review extraction')
    init = function(source, 'Background_InitBackgroundSprites')
    body = init[init.index('    backgroundSprite = sBackgroundSprites;'):]
    body = body[:body.index('    sSpritePaletteReplacementCount = 0;')]
    body = once(body, '''        backgroundSprite->texture = func_80078104(texturePalettePair[0], 64 * 64 / 2, 0, 0, 0);
        backgroundSprite->palette = func_80078104(texturePalettePair[1], 16 * 1 * sizeof(u16), 0, 0, 0);''',
        '''        backgroundSprite->texture = NULL;
        backgroundSprite->palette = NULL;''')
    body = once(body, '        texturePalettePair = sBackgroundSpritePalettePairs[backgroundSprite->spriteId];\n', '')
    body = once(body, '    for (i = 0, gfxPool = D_8024DCE0; i < 2; i++, gfxPool++) {',
                '    for (i = 0; i < 2; i++) {')
    body = body.replace('gfxPool->unk_2C4E8', 'native_skyline_vertices[i]')
    update = function(source, 'Background_UpdateBackgroundSprites')
    update = update[:update.index('    if (sSpritePaletteReplacementCount > 0) {')]+'}\n'
    update = once(update, 'void Background_UpdateBackgroundSprites(void)',
                  'static void native_skyline_update(void)')
    update = update.replace('gGfxPool->unk_2C368[i]', 'native_skyline_matrices[native_skyline_pool][i]')
    names = dict(sBackgroundSprites='native_skyline_sprites',
                 sBackgroundSpriteCount='native_skyline_count',
                 sBackgroundSpriteScale='native_skyline_scale',
                 sBackgroundSpriteHeight='native_skyline_height')
    replace = lambda text: re.sub(r'\b('+'|'.join(names)+r')\b', lambda m:names[m[0]], text)
    body, update = replace(body), replace(update)
    table = re.search(r'BackgroundSpriteInitData sBackgroundSpriteInitMuteCity\[\] = (\{.*?\n\});', source, re.S)
    if not table or table[1].count('BG_SPRITE_') != 7:
        raise ValueError('Source skyline count changed')
    constants = {}
    for name in ('Scale','Height'):
        match = re.search(r'f32 sBackgroundSprite'+name+r' = ([^;]+);', source)
        if not match:
            raise ValueError('Source skyline constant changed')
        constants[name] = match[1]
    sky = re.search(r'CourseSkyboxes sSkyboxPurple = \{([^\n]+)\};', source)
    tint = [x.strip() for x in sky[1].split(',')][7:10]
    if len(tint)!=3 or any(not value.isdigit() for value in tint):
        raise ValueError('Source skyline tint changed')
    prefix = '''
#include "skyline_state.h"
_Static_assert(sizeof(RefSkylineInstance)==84, "skyline instance ABI");
_Static_assert(sizeof(RefSkyline)==580, "skyline packet ABI");
static BackgroundSprite native_skyline_sprites[REF_SKYLINE_COUNT];
static Vtx native_skyline_vertices[2][4];
static Mtx native_skyline_matrices[2][REF_SKYLINE_COUNT];
static unsigned int native_skyline_pool;
static int native_skyline_ready, native_skyline_count;
'''
    prefix += 'static const f32 native_skyline_scale = '+constants['Scale']+';\n'
    prefix += 'static const f32 native_skyline_height = '+constants['Height']+';\n'
    prefix += 'static BackgroundSpriteInitData native_skyline_list[] = '+table[1]+';\n'
    prefix += 'static const unsigned char native_skyline_tint[4] = {'+','.join(tint)+',255};\n'
    prefix += '''
int ref_skyline_init(void) {
    s32 i;
    BackgroundSprite* backgroundSprite;
    BackgroundSpriteInitData* bgSpriteInitData=native_skyline_list;
    native_skyline_ready=0;native_skyline_count=0;
    if(gNumPlayers!=1)return 1;
    memset(native_skyline_sprites,0,sizeof(native_skyline_sprites));
    memset(native_skyline_vertices,0,sizeof(native_skyline_vertices));
    memset(native_skyline_matrices,0,sizeof(native_skyline_matrices));
'''
    suffix = '''
static void skyline_copy(RefSkyline* out, const BackgroundSprite* sprites,
                         const Mtx* matrices,const Vtx* vertices) {
    unsigned int i,j;
    out->count=REF_SKYLINE_COUNT;
    memcpy(out->tint,native_skyline_tint,4);
    for(i=0;i<4;++i) {
        for(j=0;j<3;++j)out->vertices[i].position[j]=vertices[i].v.ob[j];
        out->vertices[i].flag=vertices[i].v.flag;
        for(j=0;j<2;++j)out->vertices[i].uv[j]=vertices[i].v.tc[j];
        for(j=0;j<4;++j)out->vertices[i].color[j]=vertices[i].v.cn[j];
    }
    for(i=0;i<REF_SKYLINE_COUNT;++i) {
        out->instances[i].sprite_id=sprites[i].spriteId;
        out->instances[i].visible=sprites[i].isVisible;
        memcpy(out->instances[i].position,&sprites[i].pos,12);
        for(j=0;j<16;++j) {
            const unsigned int value=((unsigned int)(unsigned short)matrices[i].u.i[j/4][j%4]<<16) |
                                     (unsigned short)matrices[i].u.f[j/4][j%4];
            memcpy(&out->instances[i].matrix[j],&value,4);
        }
    }
}
void ref_skyline_tick(RefSkyline* out,unsigned int pool) {
    if(!out)return;
    memset(out,0,sizeof(*out));
    if(!native_skyline_ready || pool>1 || gNumPlayers!=1) {out->faults=1;return;}
    native_skyline_pool=pool;
    native_skyline_update();
    skyline_copy(out,native_skyline_sprites,native_skyline_matrices[pool],native_skyline_vertices[pool]);
}
'''
    result = prefix+body+'    native_skyline_ready=native_skyline_count==REF_SKYLINE_COUNT;\n    return !native_skyline_ready;\n}\n'+update+suffix
    if re.search(r'\bgGfxPool\b|\bgfxPool\b|\bfunc_80078104\b|\bMath_Rand1\b', result):
        raise ValueError('Unexpected renderer/loader/RNG in native skyline')
    return result
