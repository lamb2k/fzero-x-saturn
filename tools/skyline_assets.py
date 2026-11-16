#!/usr/bin/env python3
"""Prebake original Mute City skyline CI4 textures for Saturn CLUT drawing."""
from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess
from import_source_art import ROOT,ArtError,output_location
import backdrop_state,skyline_state

BAYER=((0,8,2,10),(12,4,14,6),(3,11,1,9),(15,7,13,5))

def convert(texels,palette,tint):
    if len(texels)!=2048 or len(palette)!=32 or len(tint)!=3 or any(not 0<=c<=255 for c in tint):
        raise ArtError('Skyline requires 64x64 CI4, 16 RGB1555 colors and source tint')
    colors=struct.unpack('>16H',palette)
    if colors[0]&0x8000:raise ArtError('Skyline index zero must be transparent')
    lookup=[]
    for color in colors:
        lookup.append(0 if not color&0x8000 else 0x8000|sum(
            ((((color>>(5*c))&31)*tint[c]+127)//255)<<(5*c) for c in range(3)))
    output=bytearray(2048)
    for y in range(64):
        for x in range(64):
            at=y*64+x;index=(texels[at//2]>>(0 if at&1 else 4))&15
            # Source alpha goes 255->0 from T=0->64. Sample at texel centers.
            # Ordered coverage is the explicit Saturn alpha adaptation.
            if not lookup[index] or (127-2*y)*16 <= (BAYER[y&3][x&3]*2+1)*64:index=0
            output[at//2]|=index<<(0 if at&1 else 4)
    return bytes(output),struct.pack('>16H',*lookup)

def generate(source,out):
    output_location(out)
    subprocess.run(['bash',str(ROOT/'tools/check_receipt.sh'),str(source/'backdrop.receipt')],check=True)
    manifest=json.loads((source/'backdrop.json').read_text())
    bindings=manifest['bindings'];sprites=bindings['sprites']
    if len(sprites)!=6:raise ArtError('Unexpected source skyline instance count')
    assets={r['symbol']:r for r in manifest['assets']}
    unique=[]
    for sprite in sprites:
        pair=(sprite['id'],sprite['texture'],sprite['palette'])
        if pair not in unique:unique.append(pair)
    if [r[0] for r in unique]!=['BG_SPRITE_CITY_SKYLINE_'+str(i) for i in range(1,5)]:
        raise ArtError('Unexpected source skyline bindings')
    dependencies=[Path(__file__),ROOT/'tools/skyline_state.py',ROOT/'tools/backdrop_state.py',
                  ROOT/'tools/import_source_art.py',backdrop_state.SOURCE,
                  backdrop_state.SOURCE.with_suffix('.h'),ROOT/'ref/skyline_state.h',
                  source/'backdrop.receipt',source/'backdrop.json']
    textures=bytearray();palettes=bytearray()
    for _,texture,palette in unique:
        row=assets[texture];pal=assets[palette]
        if (row['format'],row['width'],row['height'],row['palette'])!=('CI4',64,64,palette):
            raise ArtError('Unexpected source skyline format')
        raw=source/row['raw_file'];lut=source/pal['native_file']
        if hashlib.sha256(raw.read_bytes()).hexdigest()!=row['source_sha256'] or \
           hashlib.sha256(lut.read_bytes()).hexdigest()!=pal['native_sha256']:
            raise ArtError('Source skyline data mismatch')
        tex,colors=convert(raw.read_bytes(),lut.read_bytes(),bindings['sky']['sprite_rgb'])
        textures+=tex;palettes+=colors;dependencies.extend((raw,lut))
    data=textures+palettes
    header=('/* Generated local source skyline; regenerate, never edit. */\n'
            '#define FZX_SKYLINE_FIXTURE 0\n'
            'static const uint8_t fzx_skyline_pixels[8320] __attribute__((aligned(8))) = {\n'+
            ''.join('    '+','.join('0x%02x'%b for b in data[i:i+16])+',\n' for i in range(0,len(data),16))+'};\n')
    state=('#include "global.h"\n#include "fzx_game.h"\n#include "fzx_camera.h"\n#include "background.h"\n'
           'extern Camera gCameras[];\nextern s32 gNumPlayers;\nextern s32 gCupType;\n')
    state+=skyline_state.native(backdrop_state.SOURCE.read_text())
    out.mkdir(parents=True,exist_ok=True)
    (out/'skyline_pixels.h').write_bytes(header.encode())
    (out/'skyline_state.c').write_bytes(state.encode())
    description={'textures':4,'instances':6,'width':64,'height':64,'bytes':len(data),
        'bindings':sprites,'tint':bindings['sky']['sprite_rgb'],
        'adaptation':'source palette tint baked to RGB1555; 4x4 ordered coverage for original vertical alpha',
        'source_backdrop_sha256':hashlib.sha256((source/'backdrop.json').read_bytes()).hexdigest(),
        'native_sha256':hashlib.sha256(data).hexdigest()}
    (out/'skyline.json').write_bytes((json.dumps(description,indent=2)+'\n').encode())
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    receipt=['# fzx generation receipt v1; regenerate, never edit']
    receipt+=['generator %s %s'%(digest(p),os.path.relpath(p,ROOT/'tools')) for p in dependencies]
    receipt+=['output %s %s'%(digest(out/name),name) for name in ('skyline_pixels.h','skyline_state.c','skyline.json')]
    (out/'skyline.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    print('native skyline: four original 64x64 CI4 images, six placements, 8320 bytes')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=ROOT/'generated/backdrop')
    parser.add_argument('--output',type=Path,default=ROOT/'generated/skyline')
    args=parser.parse_args();generate(args.source,args.output)
