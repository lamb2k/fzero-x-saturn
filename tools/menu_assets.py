#!/usr/bin/env python3
"""Prebake the first cartridge main menu from original local ROM artwork.

RGB art retains source channels. Grayscale labels use lossless CLUT4 indices
with small tint palettes. Sign opacity uses two bounded ordered-dither levels;
the original assets and full-resolution geometry are retained.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import struct
import sys
from import_course import extract_rom_course, normalize_rom
from import_source_art import (ROOT, DECOMP, YAML, ArtError, metadata, bounded,
                               mio0, output_location, verify_source)
from road_pack import fnv

BASE = 0x2B9EA0
SOURCE = 'src/overlays/ovl_i6/main_menu.c'
# IDs are the public fzx_menu_image_t contract. No expansion-kit or PORT art.
ASSETS = (
    ('aTitleBackgroundMainTex',304,240,'RGBA16'),
    ('aMenuSignGpRaceTex',64,86,'RGBA16'),
    ('aMenuSignTimeAttackTex',64,86,'RGBA16'),
    ('aMenuSignDeathRaceTex',64,86,'RGBA16'),
    ('aMenuSignVsBattleTex',64,86,'RGBA16'),
    ('aMenuSignPracticeTex',64,86,'RGBA16'),
    ('aMenuSignOptionsTex',64,86,'RGBA16'),
    ('aSelectModeTex',128,24,'I4'),
    ('aSelectModeOptionFlamesTex',128,80,'RGBA16'),
    ('aNoviceTex',64,18,'RGBA16'),
    ('aStandardTex',64,18,'RGBA16'),
    ('aExpertTex',64,18,'RGBA16'),
    ('aMasterTex',64,18,'RGBA16'),
    ('a2PlayersTex',64,18,'RGBA16'),
    ('a3PlayersTex',64,18,'RGBA16'),
    ('a4PlayersTex',64,18,'RGBA16'),
    ('aTimeAttackTex',88,18,'RGBA16'),
    ('aRecordsOptionTex',64,18,'RGBA16'),
    ('aOKTex',32,16,'RGBA16'),
)
LABELS = {7, *range(9,19)}
BAYER = (0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5)


def rgb_words(raw, fmt):
    if fmt == 'I4':
        return [0x8000 | (n*17>>3)*1057 if n else 0
                for b in raw for n in (b>>4,b&15)]
    return [0x8000 | ((v>>11)&31) | (((v>>6)&31)<<5) | (((v>>1)&31)<<10)
            if v&1 else 0 for (v,) in struct.iter_unpack('>H',raw)]


def sign_variant(words, width, tint, coverage):
    out=[]
    for i,v in enumerate(words):
        if not v or BAYER[((i//width)&3)*4+(i%width&3)] >= coverage:
            out.append(0)
        else:
            out.append(0x8000 | sum((((v>>s)&31)*tint//255)<<s for s in (0,5,10)))
    return struct.pack('>%dH'%len(out),*out)


def extract(data, rows):
    bank, records = bytearray(), []
    for image,(symbol,w,h,fmt) in enumerate(ASSETS):
        row=rows.get(symbol,{})
        kind='TEXTURE' if fmt=='I4' else 'COMPRESSED_TEXTURE'
        if (row.get('type'),row.get('format'),row.get('width'),row.get('height')) != (kind,fmt,w,h):
            raise ArtError('Original main-menu image declaration changed')
        start=row['offset']
        end=min((r['offset'] for r in rows.values() if r.get('offset',-1)>start),default=len(data))
        span=bounded(data,start,end-start)
        count=w*h//2 if fmt=='I4' else w*h*2
        raw=bounded(span,0,count) if kind=='TEXTURE' else mio0(span,count)
        if len(raw)!=count:
            raise ArtError('Original menu image extent changed')
        words=rgb_words(raw,fmt)
        palette=[]
        if image in LABELS:
            palette=[0]+sorted(set(words)-{0})
            if len(palette)>16 or any((v&31)!=((v>>5)&31) or (v&31)!=((v>>10)&31) for v in palette):
                raise ArtError('Original menu label is not lossless grayscale CLUT4')
            indices={v:i for i,v in enumerate(palette)}
            pixels=bytes((indices[words[i]]<<4)|indices[words[i+1]] for i in range(0,len(words),2))
            palette += [0]*(16-len(palette))
        else:
            # The stock menu overlays black with alpha 180 on the wallpaper.
            # Fold that constant blend into the boot-only atlas offline.
            pixels=sign_variant(words,w,75,16) if image==0 else struct.pack('>%dH'%len(words),*words)
        if len(bank)%8 or len(pixels)%8:
            raise ArtError('Menu image alignment changed')
        record=dict(image=image,symbol=symbol,width=w,height=h,offset=len(bank),bytes=len(pixels),
                    clut=bool(palette),palette=palette,dim_offset=len(bank),pulse_offset=len(bank),
                    source_offset=BASE+start,source_sha256=hashlib.sha256(raw).hexdigest())
        bank+=pixels
        if 1<=image<=6:
            record['dim_offset']=len(bank);bank+=sign_variant(words,w,128,11)
            record['pulse_offset']=len(bank);bank+=sign_variant(words,w,255,13)
        records.append(record)
    if len(bank)>400*1024:
        raise ArtError('Menu atlas exceeds temporary frontend allocation')
    return bytes(bank),records


def export(rom,output):
    output_location(output)
    _,provenance=extract_rom_course(rom)
    normalized,_=normalize_rom(rom)
    verify_source(DECOMP)
    yaml=DECOMP/YAML/'common_assets_compressed.yaml'
    bank,records=extract(normalized[BASE:],metadata(yaml.read_text()))
    report=dict(schema=1,rom_sha256=provenance['normalized_rom_sha256'],images=records,
                texture_bytes=len(bank),source_viewport=[320,240],native_crop=[0,8,320,224],
                adaptations=['First cartridge background only; source alpha-180 black overlay folded into RGB5 at 75/255',
                    'I4 header keeps source intensity with binary transparency',
                    'Unselected signs use RGB tint 128/255 and 11/16 ordered coverage',
                    'Selected sign alpha 180..223 uses 13/16 ordered coverage; 224..255 is opaque',
                    'Label tint palettes use integer RGB5 multiplication'],
                pending=['Background cheat transitions','Audio','Course and machine selection'])
    header='/* Generated original main-menu atlas; regenerate. */\n'
    header+='#define FZX_MENU_BYTES %du\n#define FZX_MENU_HASH %du\n'%(len(bank),fnv(bank))
    header+='#define FZX_MENU_ATLAS_IMAGES %du\n'%len(records)
    header+='static const struct { unsigned int offset,dim_offset,pulse_offset; unsigned short width,height; unsigned char clut; unsigned short palette[16]; } fzx_menu_images[FZX_MENU_ATLAS_IMAGES]={\n'
    header+=',\n'.join(' {%du,%du,%du,%du,%du,%du,{%s}}'%(r['offset'],r['dim_offset'],r['pulse_offset'],r['width'],r['height'],r['clut'],','.join(str(v) for v in (r['palette'] or [0]*16))) for r in records)
    header+='\n};\n'
    output.mkdir(parents=True,exist_ok=True)
    outputs={'MENU.PAK':bank,'menu_identity.h':header.encode(),'menu.json':(json.dumps(report,indent=2)+'\n').encode()}
    deps=[Path(__file__),ROOT/'tools/import_source_art.py',ROOT/'tools/import_course.py',ROOT/'tools/road_pack.py',yaml,DECOMP/SOURCE]
    receipt=['# fzx generation receipt v1; regenerate, never edit']
    receipt+=['generator %s %s'%(hashlib.sha256(p.read_bytes()).hexdigest(),os.path.relpath(p,ROOT/'tools').replace('\\','/')) for p in deps]
    receipt+=['input %s owner-rom'%hashlib.sha256(rom).hexdigest()]
    for name,data in outputs.items():
        (output/name).write_bytes(data)
        receipt.append('output %s %s'%(hashlib.sha256(data).hexdigest(),name))
    (output/'menu.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    return dict(images=len(records),texture_bytes=len(bank))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'generated/menu')
    args=parser.parse_args()
    path=os.environ.get('FZX_TEST_ROM')
    if not path:parser.error('Set FZX_TEST_ROM locally; keep its path out of logs')
    try:print(json.dumps(export(Path(path).read_bytes(),args.output)))
    except (OSError,ValueError,KeyError) as exc:
        print('menu conversion failed: '+(str(exc) if isinstance(exc,ArtError) else type(exc).__name__),file=sys.stderr)
        raise SystemExit(1)
