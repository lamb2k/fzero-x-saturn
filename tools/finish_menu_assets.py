#!/usr/bin/env python3
"""Prepare original race-menu CI8 labels and complete Font5 for native use.

Keep indexed pixels and RGB5 palettes so the source's continuous highlight
color can modulate at most32 colors, without quantizing it to fixed tints.
Generated owner-ROM content remains private; this is an asset-only gate.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re

from import_course import extract_rom_course, normalize_rom
from import_source_art import ROOT, DECOMP, ArtError, bounded, metadata, mio0, output_location, verify_source
from machine_menu_assets import _declarations, _table
from menu_assets import BASE, rgb_words
from results_assets import COMMON, FONT, encode_image
from results_source import select_us
from road_pack import fnv

MENUS = DECOMP/'src/overlays/ovl_i3/menus.c'
PALETTE = 'aMenuTextTLUT'


def asset_rows(text):
    rows = metadata(text)
    current = None
    for line in text.splitlines():
        match = re.fullmatch(r'(\w+):', line)
        if match:
            current = rows[match[1]]
        elif current is not None:
            match = re.fullmatch(r'  (colors|tlut|compression): (\w+)', line)
            if match:
                current[match[1]] = match[2] if match[1] == 'compression' else int(match[2],0)
    return rows


def catalog(menu_source=None, font_source=None, rows=None):
    source = select_us(MENUS.read_text() if menu_source is None else menu_source)
    font = select_us(FONT.read_text() if font_source is None else font_source)
    rows = asset_rows(COMMON.read_text()) if rows is None else rows
    match = re.search(r'\bsRaceMenuTextures\[RACE_MENU_MAX\]\s*=\s*\{(.*?)\};',source,re.S)
    if not match:
        raise ArtError('Original race-menu texture table missing')
    labels = re.findall(r'\baMenu\w+Tex\b',re.sub(r'//[^\n]*','',match[1]))
    if len(labels) != 24 or len(set(labels)) != 24:
        raise ArtError('Original race-menu label extent changed')
    dimensions = re.search(r'\bsRaceMenuDimensions\[RACE_MENU_MAX \* 2\]\s*=\s*\{(.*?)\};',source,re.S)
    pairs = [] if not dimensions else re.findall(r'TEX_(WIDTH|HEIGHT)\((\w+)\)',dimensions[1])
    if pairs != [(axis,symbol) for symbol in labels for axis in ('WIDTH','HEIGHT')]:
        raise ArtError('Original race-menu dimension table differs from image IDs')
    palette = rows.get(PALETTE,{})
    if (palette.get('type'),palette.get('format'),palette.get('colors')) != ('TEXTURE','TLUT',32):
        raise ArtError('Original race-menu palette changed')
    declarations = _declarations(font)
    crops = {}
    for alias,symbol,width,height_symbol in re.findall(
            r'unk_80077D50 (\w+)\[\] =\s*COMP_TEX_INFO_WH\(4, (\w+), (\d+), TEX_HEIGHT\((\w+)\), 0\);',font):
        if symbol != height_symbol:
            raise ArtError('Original Font5 crop height changed')
        declarations[alias] = (symbol,4)
        crops[symbol] = int(width)
    aliases = re.findall(r'\b\w+CompTexInfo\b',_table(font,'sFont5CompTexInfos'))
    advance = [int(v) for v in re.findall(r'\d+',_table(font,'kFont5CharWidths'))]
    if len(aliases) != 235 or len(advance) != 235 or any(not 1 <= v <= 128 for v in advance):
        raise ArtError('Original Font5 index/advance extent changed')
    specs = []
    for symbol in labels:
        row = rows.get(symbol,{})
        if (row.get('type'),row.get('format'),row.get('compression'),row.get('tlut')) != (
                'COMPRESSED_TEXTURE','CI8','MIO0',0xF000000+palette['offset']):
            raise ArtError('Original race-menu label declaration changed: '+symbol)
        specs.append((symbol,row.get('width'),row.get('height'),'CI8'))
    symbols = []
    for alias in aliases:
        if alias not in declarations or declarations[alias][1] not in (4,20):
            raise ArtError('Original Font5 loader changed: '+alias)
        symbol,loader = declarations[alias]
        row = rows.get(symbol,{})
        expected = 'TEXTURE' if loader == 4 else 'COMPRESSED_TEXTURE'
        if row.get('type') != expected or row.get('format') != 'I4' or (
                loader == 20 and row.get('compression') != 'MIO0'):
            raise ArtError('Original Font5 image declaration changed: '+symbol)
        symbols.append(symbol)
        if symbol not in {s[0] for s in specs}:
            specs.append((symbol,row.get('width'),row.get('height'),'I4'))
    for symbol,w,h,fmt in specs:
        if not isinstance(w,int) or not isinstance(h,int) or not 8 <= w <= 128 or w%8 or not 1 <= h <= 32:
            raise ArtError('Original finish-menu dimensions changed: '+symbol)
        if symbol in crops and not 1 <= crops[symbol] <= w:
            raise ArtError('Original Font5 crop exceeds source')
    ids = {s[0]:i for i,s in enumerate(specs)}
    return dict(specs=specs,labels=[ids[s] for s in labels],
                font=dict(images=[ids[s] for s in symbols],advance=advance,space=3,baseline=16),
                crops={s:crops[s] for s in symbols if s in crops})


def common_image(data,rows,symbol,w,h,fmt):
    if fmt not in ('CI8','I4') or not isinstance(w,int) or not isinstance(h,int) or not 1 <= w <= 128 or not 1 <= h <= 32 or (fmt=='I4' and w*h%2):
        raise ArtError('Invalid finish-menu source format or dimensions')
    row = rows.get(symbol,{})
    if (row.get('format'),row.get('width'),row.get('height')) != (fmt,w,h):
        raise ArtError('Finish-menu source image declaration changed')
    start = row.get('offset')
    if not isinstance(start,int) or start < 0:
        raise ArtError('Invalid finish-menu source offset')
    end = min((r['offset'] for r in rows.values() if r.get('offset',-1)>start),default=len(data))
    span = bounded(data,start,end-start)
    size = w*h if fmt == 'CI8' else w*h//2
    if row.get('type') == 'TEXTURE':
        raw = bounded(span,0,size)
    elif row.get('type') == 'COMPRESSED_TEXTURE' and row.get('compression') == 'MIO0':
        raw = mio0(span,size)
    else:
        raise ArtError('Unsupported finish-menu source loader')
    if len(raw) != size:
        raise ArtError('Finish-menu decompressed extent changed')
    return raw,start


def modulate_palette(palette,r,g,b):
    """Reference for bounded runtime source tint; no pixel conversion required."""
    if len(palette)>32 or any(not isinstance(v,int) or not 0 <= v <= 255 for v in (r,g,b)):
        raise ArtError('Invalid finish-menu tint')
    return [0 if not v else 0x8000 | sum((((v>>s)&31)*c//255)<<s for s,c in ((0,r),(5,g),(10,b)))
            for v in palette]


def convert(data,rows,info):
    palrow = rows[PALETTE]
    palette_raw = bounded(data,palrow['offset'],64)
    palette = rgb_words(palette_raw,'RGBA16')
    if palette[0] != 0 or any(not (v & 0x8000) for v in palette[1:]):
        raise ArtError('Race-menu transparent index contract changed')
    bank = bytearray();records = []
    for image,(symbol,w,h,fmt) in enumerate(info['specs']):
        raw,source_offset = common_image(data,rows,symbol,w,h,fmt)
        if fmt == 'CI8':
            if any(v >= len(palette) for v in raw):
                raise ArtError('Race-menu index exceeds original TLUT')
            pixels = raw
            record = dict(width=w,source_width=w,original_width=w,height=h,bpp=8,
                          palette=palette,source_sha256=hashlib.sha256(raw).hexdigest())
        else:
            pixels,record = encode_image(raw,'I4',w,h,info['crops'].get(symbol))
            if not record.pop('clut'):
                raise ArtError('Font5 no longer fits exact CLUT4')
            record['bpp'] = 4
        if len(bank)%8 or len(pixels)%8:
            raise ArtError('Finish-menu texture alignment changed')
        record.update(image=image,symbol=symbol,offset=len(bank),bytes=len(pixels),
                      family='common_assets_compressed',source_offset=source_offset)
        bank.extend(pixels);records.append(record)
    return bytes(bank),records,hashlib.sha256(palette_raw).hexdigest()


def identity_header(bank,records,info):
    text = '/* Generated original race-menu/Font5 assets; regenerate, never edit. */\n'
    text += '#ifndef FZX_FINISH_MENU_IDENTITY_H\n#define FZX_FINISH_MENU_IDENTITY_H\n'
    for name,value in [('BYTES',len(bank)),('HASH',fnv(bank)),('IMAGES',len(records)),('LABELS',len(info['labels'])),('FONT5_GLYPHS',len(info['font']['images']))]:
        text += '#define FZX_FINISH_MENU_%s %du\n'%(name,value)
    text += ('/* bpp8 needs a native color bank or bounded indexed-to-RGB expansion;\n'
             ' * it must not be submitted as the VDP1 CLUT4 lookup mode. */\n'
             'typedef struct { unsigned int offset,bytes;\n'
             ' unsigned short width,height,source_width,bpp,palette_count,reserved;\n'
             ' unsigned short palette[32]; } fzx_finish_menu_image_t;\n')
    text += 'static const fzx_finish_menu_image_t fzx_finish_menu_images[FZX_FINISH_MENU_IMAGES]={\n'
    text += ',\n'.join(' {%du,%du,%du,%du,%du,%du,%du,0,{%s}}'%(
        r['offset'],r['bytes'],r['width'],r['height'],r['source_width'],r['bpp'],len(r['palette']),
        ','.join(str(v) for v in r['palette']+[0]*(32-len(r['palette'])))) for r in records)+'\n};\n'
    text += 'static const unsigned short fzx_finish_menu_labels[FZX_FINISH_MENU_LABELS]={'+','.join(map(str,info['labels']))+'};\n'
    text += 'static const unsigned short fzx_finish_menu_font5[FZX_FINISH_MENU_FONT5_GLYPHS][2]={\n'
    text += ','.join('{%d,%d}'%v for v in zip(info['font']['images'],info['font']['advance']))+'\n};\n'
    text += '#define FZX_FINISH_MENU_FONT5_SPACE 3u\n#define FZX_FINISH_MENU_FONT5_BASELINE 16u\n#endif\n'
    return text.encode()


def export(rom,output):
    output_location(output);verify_source(DECOMP)
    _,provenance = extract_rom_course(rom)
    normalized,_ = normalize_rom(rom)
    rows = asset_rows(COMMON.read_text());info = catalog(rows=rows)
    bank,records,palette_hash = convert(normalized[BASE:],rows,info)
    report = dict(schema=1,rom_sha256=provenance['normalized_rom_sha256'],images=records,
                  labels=info['labels'],font5=info['font'],texture_bytes=len(bank),fnv1a32=fnv(bank),
                  palette_source_sha256=palette_hash,source_viewport=[320,240],native_crop=[0,8,320,224],
                  linked_descriptor_bytes=len(records)*84+len(info['labels'])*2+len(info['font']['images'])*4,
                  scope='Asset preparation only; native color-bank residency and retry remain unqualified',
                  adaptations=['Original CI8 indices, RGB5 channels and binary alpha retained',
                    'I4 becomes RGB5 grayscale in lossless CLUT4; original crops and advances retained',
                    'Continuous source highlight, white and disabled gray use bounded palette modulation',
                    'Rows padded transparently to eight pixels; Font5 includes Font4 aliases and compressed I4'])
    outputs = {'FINMENU.PAK':bank,'finish_menu_identity.h':identity_header(bank,records,info),
               'finish_menu_ui.json':(json.dumps(report,indent=2)+'\n').encode()}
    deps = [Path(__file__),ROOT/'tools/import_course.py',ROOT/'tools/import_source_art.py',
            ROOT/'tools/machine_menu_assets.py',ROOT/'tools/menu_assets.py',ROOT/'tools/results_assets.py',
            ROOT/'tools/results_source.py',ROOT/'tools/course_menu_assets.py',ROOT/'tools/menu_source.py',
            ROOT/'tools/hud_assets.py',ROOT/'tools/road_pack.py',FONT,COMMON,MENUS]
    receipt = ['# fzx generation receipt v1; regenerate, never edit']
    receipt += ['generator %s %s'%(hashlib.sha256(p.read_bytes()).hexdigest(),os.path.relpath(p,ROOT/'tools').replace('\\','/')) for p in dict.fromkeys(deps)]
    receipt += ['input %s owner-rom'%hashlib.sha256(rom).hexdigest()]
    output.mkdir(parents=True,exist_ok=True)
    for name,data in outputs.items():
        (output/name).write_bytes(data);receipt.append('output %s %s'%(hashlib.sha256(data).hexdigest(),name))
    (output/'finish_menu_ui.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    return dict(images=len(records),labels=len(info['labels']),font5_glyphs=len(info['font']['images']),
                texture_bytes=len(bank),linked_descriptor_bytes=report['linked_descriptor_bytes'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'generated/finish_menu_ui')
    parser.add_argument('--config',type=Path);args=parser.parse_args()
    path=json.loads(args.config.read_text())['rom'] if args.config else os.environ.get('FZX_TEST_ROM')
    if not path:parser.error('Set FZX_TEST_ROM privately or supply local --config')
    if os.name!='nt' and len(path)>2 and path[1]==':':path='/mnt/'+path[0].lower()+'/'+path[3:].replace('\\','/')
    try:print(json.dumps(export(Path(path).read_bytes(),args.output),sort_keys=True))
    except Exception as exc:
        print('Finish-menu export failed: '+(str(exc) if isinstance(exc,ArtError) else type(exc).__name__));return 1
    return 0


if __name__=='__main__':raise SystemExit(main())
