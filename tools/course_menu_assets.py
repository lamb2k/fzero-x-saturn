#!/usr/bin/env python3
"""Convert original cartridge course-selection artwork and fonts offline.

The atlas retains source texels and lossless CLUT4 where possible. Cup tint
uses three explicit baked levels; source pulse state remains in the snapshot.
Generated assets, source font tables and course names remain private.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import struct

from import_course import extract_rom_course, normalize_rom
from import_source_art import ROOT, DECOMP, YAML, ArtError, metadata, bounded, mio0, output_location, verify_source
from menu_assets import BASE, rgb_words, sign_variant
from menu_source import cartridge
from road_pack import fnv

FONT = DECOMP/'src/overlays/ovl_i2/font.c'
ASSETS = (
    ('aTitleBackgroundMainTex',304,240,'RGBA16'),
    ('aCupSelectJackTex',64,96,'RGBA16'),
    ('aCupSelectQueenTex',64,96,'RGBA16'),
    ('aCupSelectKingTex',64,96,'RGBA16'),
    ('aCupSelectJokerTex',64,96,'RGBA16'),
    ('aCupSelectXTex',64,96,'RGBA16'),
    ('aCupSelectEditTex',64,64,'RGBA16'),
    ('aCupSelectQuestionMarkTex',64,96,'RGBA16'),
    ('aSelectCourseTex',160,24,'I4'),
    ('aRecordsTex',96,24,'I4'),
    ('aOKTex',32,16,'RGBA16'),
    ('aYellowArrowTex',32,32,'RGBA16'),
    ('aOptionsFalconHelmetTex',64,64,'RGBA16'),
    ('aCupCleared1Tex',16,16,'RGBA16'),
    ('aCupCleared2Tex',16,16,'RGBA16'),
    ('aCupCleared3Tex',16,16,'RGBA16'),
    ('aCupCleared4Tex',16,16,'RGBA16'),
    ('aHasGhostMarkerTex',32,16,'I4'),
    ('aStaffGhostBeatenTex',16,16,'RGBA16'),
    ('aMenuWithGhostTex',64,16,'I4'),
    ('aMenuWithoutGhostTex',64,16,'I4'),
    ('aMenuStaffGhostTex',64,16,'I4'),
    ('aMenuCelebrityGhostTex',64,16,'I4'),
    ('aMenuChampGhostTex',64,16,'I4'),
)


def font_tables(source):
    """Retain the source's full index tables, including US lowercase glyphs."""
    source=cartridge(source)
    fonts={}
    declarations=dict(re.findall(r'unk_80077D50 (sFont\w+CompTexInfo)\[\] =\s*COMP_TEX_INFO\(4, (\w+), 0\);',source))
    crops={}
    for name,symbol,width,height_symbol in re.findall(r'unk_80077D50 (sFont\w+CompTexInfo)\[\] =\s*COMP_TEX_INFO_WH\(4, (\w+), (\d+), TEX_HEIGHT\((\w+)\), 0\);',source):
        if symbol!=height_symbol or int(width)!=8: raise ArtError('Course font crop contract changed')
        declarations[name]=symbol;crops[symbol]=int(width)
    for font in (3,4):
        table=re.search(r'unk_80077D50\* sFont%dCompTexInfos\[\] = \{(.*?)\};'%font,source,re.S)
        widths=re.search(r'const s16 kFont%dCharWidths\[\] = \{(.*?)\};'%font,source,re.S)
        if not table or not widths: raise ArtError('Source course font table missing')
        try: images=[declarations[n] for n in re.findall(r'\b(s\w+CompTexInfo)\b',table.group(1))]
        except KeyError as exc: raise ArtError('Course font references an unsupported image') from exc
        advance=[int(v) for v in re.findall(r'\d+',widths.group(1))]
        if len(images)!=len(advance) or not 74<=len(images)<=512 or not all(0<v<=128 for v in advance):
            raise ArtError('Source course font index/advance contract changed')
        fonts[font]=dict(symbols=images,advance=advance,crops={n:crops[n] for n in images if n in crops},space=6 if font==3 else 3,baseline=16 if font==3 else 8)
    return fonts


def specs_with_fonts(fonts,rows):
    specs=list(ASSETS)
    used={s[0] for s in specs}
    for font,table in fonts.items():
        for symbol in table['symbols']:
            if symbol in used: continue
            row=rows.get(symbol,{})
            w,h=row.get('width'),row.get('height')
            if row.get('type')!='TEXTURE' or row.get('format')!='I4' or h not in (8,16) or not isinstance(w,int) or w%8 or not 8<=w<=128:
                raise ArtError('Unsupported source course font extent: '+symbol)
            specs.append((symbol,w,h,'I4'));used.add(symbol)
    return specs


def extract(data,rows,specs,crops=None):
    bank=bytearray();records=[]
    for image,(symbol,w,h,fmt) in enumerate(specs):
        row=rows.get(symbol,{})
        kind='TEXTURE' if fmt=='I4' else 'COMPRESSED_TEXTURE'
        if (row.get('type'),row.get('format'),row.get('width'),row.get('height'))!=(kind,fmt,w,h):
            raise ArtError('Course-menu image declaration changed: '+symbol)
        start=row['offset'];end=min((r['offset'] for r in rows.values() if r.get('offset',-1)>start),default=len(data))
        span=bounded(data,start,end-start);size=w*h//2 if fmt=='I4' else w*h*2
        raw=bounded(span,0,size) if fmt=='I4' else mio0(span,size)
        if len(raw)!=size: raise ArtError('Course-menu image extent changed')
        words=rgb_words(raw,fmt)
        source_width=w
        if crops and symbol in crops:
            crop=crops[symbol]
            if not 0<crop<=w or crop%8: raise ArtError('Invalid course glyph crop')
            words=[v for y in range(h) for v in words[y*w:y*w+crop]];w=crop
        # No quantization: RGB images only become CLUT4 when every color fits.
        palette=[0]+sorted(set(words)-{0})
        clut=len(palette)<=16 and not 1<=image<=7 and image!=0
        if clut:
            indices={v:i for i,v in enumerate(palette)}
            pixels=bytes(indices[words[i]]*16+indices[words[i+1]] for i in range(0,len(words),2))
            palette += [0]*(16-len(palette))
        else:
            palette=[]
            # Source MODULATEIA_PRIM/XLU_SURF keeps old framebuffer color;
            # repeated alpha180 wallpaper converges to its RGB75 tint.
            # Bake that steady appearance and explicitly omit persistence.
            pixels=sign_variant(words,w,75 if image==0 else 255,16)
        record=dict(image=image,symbol=symbol,width=w,height=h,offset=len(bank),bytes=len(pixels),
                    clut=clut,palette=palette,dim_offset=len(bank),pulse_offset=len(bank),
                    source_width=source_width,source_offset=BASE+start,source_sha256=hashlib.sha256(raw).hexdigest())
        bank+=pixels
        if 1<=image<=7:
            record['dim_offset']=len(bank);bank+=sign_variant(words,w,100,16)
            record['pulse_offset']=len(bank);bank+=sign_variant(words,w,180,16)
        if len(bank)%8: raise ArtError('Course-menu atlas alignment changed')
        records.append(record)
    if len(bank)>432*1024: raise ArtError('Course-menu atlas exceeds frontend texture budget')
    return bytes(bank),records


def export(rom,output):
    output_location(output);verify_source(DECOMP)
    _,provenance=extract_rom_course(rom);normalized,_=normalize_rom(rom)
    yaml=DECOMP/YAML/'common_assets_compressed.yaml';rows=metadata(yaml.read_text())
    fonts=font_tables(FONT.read_text());specs=specs_with_fonts(fonts,rows)
    crops={n:w for table in fonts.values() for n,w in table['crops'].items()}
    bank,records=extract(normalized[BASE:],rows,specs,crops)
    ids={r['symbol']:r['image'] for r in records}
    for table in fonts.values(): table['images']=[ids[n] for n in table['symbols']]
    report=dict(schema=1,rom_sha256=provenance['normalized_rom_sha256'],images=records,fonts=fonts,
                texture_bytes=len(bank),source_viewport=[320,240],native_crop=[0,8,320,224],
                adaptations=['Time Attack wallpaper uses opaque RGB75 steady-state bake; source alpha180 framebuffer persistence is omitted',
                             'Cup tint uses exact offline levels100/180/255; native selects nearest pulse level',
                             'I4 intensity retains all16 values as RGB5 grayscale with binary transparency',
                             'Font4 source width overrides crop the left8 pixels at the original16-pixel stride',
                             'CLUT4 is lossless; other images retain original RGB5 channels'])
    header='/* Generated original course-menu atlas; regenerate, never edit. */\n'
    header+='#define FZX_COURSE_MENU_BYTES %du\n#define FZX_COURSE_MENU_HASH %du\n'%(len(bank),fnv(bank))
    header+='#define FZX_COURSE_MENU_ATLAS_IMAGES %du\n'%len(records)
    header+='static const struct { unsigned int offset,dim_offset,pulse_offset; unsigned short width,height; unsigned char clut; unsigned short palette[16]; } fzx_course_menu_images[FZX_COURSE_MENU_ATLAS_IMAGES]={\n'
    header+=',\n'.join(' {%du,%du,%du,%du,%du,%du,{%s}}'%(r['offset'],r['dim_offset'],r['pulse_offset'],r['width'],r['height'],r['clut'],','.join(str(v) for v in (r['palette'] or [0]*16)))for r in records)+'\n};\n'
    for font,table in fonts.items():
        header+='static const unsigned short fzx_course_font%d[][2]={\n'%font
        header+=','.join('{%d,%d}'%pair for pair in zip(table['images'],table['advance']))+'\n};\n'
    outputs={'COURSEUI.PAK':bank,'course_menu_identity.h':header.encode(),
             'course_menu.json':(json.dumps(report,indent=2)+'\n').encode()}
    dependencies=[Path(__file__),ROOT/'tools/import_source_art.py',ROOT/'tools/import_course.py',
                  ROOT/'tools/menu_assets.py',ROOT/'tools/menu_source.py',ROOT/'tools/road_pack.py',FONT,yaml]
    output.mkdir(parents=True,exist_ok=True)
    receipt=['# fzx generation receipt v1; regenerate, never edit']
    receipt+=['generator %s %s'%(hashlib.sha256(p.read_bytes()).hexdigest(),os.path.relpath(p,ROOT/'tools').replace('\\','/'))for p in dependencies]
    receipt+=['input %s owner-rom'%hashlib.sha256(rom).hexdigest()]
    for name,data in outputs.items():
        (output/name).write_bytes(data);receipt.append('output %s %s'%(hashlib.sha256(data).hexdigest(),name))
    (output/'course_menu.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    return dict(images=len(records),texture_bytes=len(bank),fonts={k:len(v['images']) for k,v in fonts.items()})


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'generated/course_menu')
    args=parser.parse_args();path=os.environ.get('FZX_TEST_ROM')
    if not path: parser.error('Set FZX_TEST_ROM privately')
    print(export(Path(path).read_bytes(),args.output))
