#!/usr/bin/env python3
"""Prebake the original Time Attack results sheets and complete font 3.

Only original texels and font metrics enter the bank. Lossless CLUT4 is used
when every RGB5 color fits; otherwise retain RGB1555. Generated art stays local.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import struct

from import_course import extract_rom_course, normalize_rom
from import_source_art import ROOT, DECOMP, YAML, ArtError, SourceArt, bounded, metadata, output_location, verify_source
from hud_assets import HUD_SHEETS
from menu_assets import BASE, rgb_words
from course_menu_assets import font_tables, FONT
from road_pack import fnv

SHEETS = (*HUD_SHEETS, ('aMaxSpeedTex', 64, 16))
COMMON = DECOMP/YAML/'common_assets_compressed.yaml'
TINT_IMAGES = (3,4,5)  # source record pulses: speed digits, km/h and timer


def tint_banks(bank,records):
    """Sixteen prebaked red/white pulse levels, at most1 RGB5 step from
    per-channel source modulation. Runtime selects/copies bytes only."""
    source=bytearray();offsets={}
    for image in TINT_IMAGES:
        row=records[image]
        if row['clut'] or row['bytes']!=row['width']*row['height']*2:
            raise ArtError('Results pulse requires original RGB sheets')
        offsets[image]=len(source)
        source.extend(bank[row['offset']:row['offset']+row['bytes']])
    words=struct.unpack('>%dH'%(len(source)//2),source)
    out=bytearray()
    for level in range(16):
        for v in words:
            out.extend(struct.pack('>H',0 if not v else
                (v&0x801f)|((((v>>5)&31)*level//15)<<5)|((((v>>10)&31)*level//15)<<10)))
    return bytes(out),len(source),offsets


def catalog(font_source=None, rows=None):
    table = font_tables(FONT.read_text() if font_source is None else font_source)[3]
    rows = metadata(COMMON.read_text()) if rows is None else rows
    specs = [(name, width, height, 'RGBA16') for name, width, height in SHEETS]
    ids = {name: i for i, (name, _, _, _) in enumerate(specs)}
    for symbol in table['symbols']:
        if symbol in ids:
            continue
        row = rows.get(symbol, {})
        width, height = row.get('width'), row.get('height')
        if (row.get('type') != 'TEXTURE' or row.get('format') != 'I4' or height != 16 or
                not isinstance(width, int) or not 8 <= width <= 128 or width % 8):
            raise ArtError('Unsupported original results font: '+symbol)
        ids[symbol] = len(specs)
        specs.append((symbol, width, height, 'I4'))
    return dict(specs=specs, font=dict(images=[ids[s] for s in table['symbols']],
                advance=table['advance'], space=table['space'], baseline=table['baseline'],
                crops=table['crops']))


def encode_image(raw, fmt, width, height, crop=None):
    if fmt not in ('RGBA16', 'I4') or not 1 <= width <= 512 or not 1 <= height <= 255:
        raise ArtError('Invalid results image dimensions or format')
    required = width*height*2 if fmt == 'RGBA16' else width*height//2
    if (fmt == 'I4' and width*height % 2) or len(raw) != required:
        raise ArtError('Results image source byte count changed')
    visible = width if crop is None else crop
    if not isinstance(visible, int) or not 1 <= visible <= width:
        raise ArtError('Results image crop exceeds the source')
    stride = (visible+7) & ~7
    words = rgb_words(raw, fmt)
    words = [word for y in range(height) for word in words[y*width:y*width+visible]+[0]*(stride-visible)]
    palette = [0]+sorted(set(words)-{0})
    clut = len(palette) <= 16
    if clut:
        indices = {color: i for i, color in enumerate(palette)}
        pixels = bytes((indices[words[i]] << 4) | indices[words[i+1]] for i in range(0,len(words),2))
        palette += [0]*(16-len(palette))
    else:
        pixels = struct.pack('>%dH'%len(words), *words)
        palette = []
    if len(pixels) % 8:
        raise ArtError('Unaligned results image')
    return pixels, dict(width=stride, source_width=visible, original_width=width,
                        height=height, clut=clut, palette=palette,
                        source_sha256=hashlib.sha256(raw).hexdigest())


def convert(art, common_data, common_rows, info):
    bank = bytearray()
    records = []
    for image, (symbol,width,height,fmt) in enumerate(info['specs']):
        if image < len(SHEETS):
            texture = art.texture(symbol)
            if (texture['width'],texture['height'],texture['format']) != (width,height,fmt):
                raise ArtError('Original results sheet declaration changed: '+symbol)
            raw = texture['texels']
            provenance = dict(family=texture['family'], source_offset=texture['offset'])
        else:
            row = common_rows.get(symbol,{})
            if (row.get('type'),row.get('format'),row.get('width'),row.get('height')) != ('TEXTURE',fmt,width,height):
                raise ArtError('Original results font declaration changed: '+symbol)
            raw = bounded(common_data,row['offset'],width*height//2)
            provenance = dict(family='common_assets_compressed',source_offset=row['offset'])
        pixels, record = encode_image(raw,fmt,width,height,info['font']['crops'].get(symbol))
        record.update(image=image,symbol=symbol,offset=len(bank),bytes=len(pixels),**provenance)
        bank.extend(pixels)
        records.append(record)
    return bytes(bank), records


def identity_header(bank,records,info,tints,tint_bytes,tint_offsets):
    text = '/* Generated original results UI; regenerate, never edit. */\n'
    text += '#define FZX_RESULTS_UI_BYTES %du\n#define FZX_RESULTS_UI_HASH %du\n'%(len(bank),fnv(bank))
    text += '#define FZX_RESULTS_UI_IMAGES %du\n#define FZX_RESULTS_FONT3_GLYPHS %du\n'%(len(records),len(info['font']['images']))
    text += '#include "fzx_results_draw.h"\n'
    text += 'static const fzx_results_image_t fzx_results_images[FZX_RESULTS_UI_IMAGES]={\n'
    text += ',\n'.join(' {%du,%du,%du,%du,%du,%du,0,{%s}}'%(
        row['offset'],row['bytes'],row['width'],row['height'],row['source_width'],row['clut'],
        ','.join(str(v) for v in (row['palette'] or [0]*16))) for row in records)+'\n};\n'
    text += 'static const unsigned short fzx_results_font3[FZX_RESULTS_FONT3_GLYPHS][2]={\n'
    text += ','.join('{%d,%d}'%pair for pair in zip(info['font']['images'],info['font']['advance']))+'\n};\n'
    text += '#define FZX_RESULTS_TINT_BYTES %du\n#define FZX_RESULTS_TINT_BANK_BYTES %du\n#define FZX_RESULTS_TINT_HASH %du\n'%(len(tints),tint_bytes,fnv(tints))
    text += 'static const unsigned int fzx_results_tint_offsets[8]={'+','.join(
        str(tint_offsets[i])+'u' if i in tint_offsets else '0xffffffffu' for i in range(8))+'};\n'
    return text.encode()


def export(rom,output):
    output_location(output)
    verify_source(DECOMP)
    _,provenance = extract_rom_course(rom)
    normalized,_ = normalize_rom(rom)
    art = SourceArt(normalized)
    rows = metadata(COMMON.read_text())
    info = catalog(rows=rows)
    bank,records = convert(art,normalized[BASE:],rows,info)
    tints,tint_bytes,tint_offsets=tint_banks(bank,records)
    report = dict(schema=1,rom_sha256=provenance['normalized_rom_sha256'],images=records,
                  texture_bytes=len(bank),fnv1a32=fnv(bank),font3=info['font'],
                  tint_bytes=len(tints),tint_bank_bytes=tint_bytes,tint_images=TINT_IMAGES,
                  source_viewport=[320,240],native_crop=[0,8,320,224],
                  adaptations=['Original RGB5 channels and binary alpha retained',
                    'I4 intensity becomes RGB5 grayscale; zero stays transparent',
                    'CLUT4 only when every source color fits; otherwise RGB1555',
                    'Rows padded to eight pixels with transparent texels; source crop retained',
                    'Red record pulse uses16 offline levels; at most1 RGB5 step from exact RGB5 modulation'])
    outputs={'RESULTUI.PAK':bank,'RESTINT.PAK':tints,
             'results_identity.h':identity_header(bank,records,info,tints,tint_bytes,tint_offsets),
             'results_ui.json':(json.dumps(report,indent=2)+'\n').encode()}
    dependencies=[Path(__file__),ROOT/'tools/import_course.py',ROOT/'tools/import_source_art.py',
                  ROOT/'tools/hud_assets.py',ROOT/'tools/menu_assets.py',ROOT/'tools/course_menu_assets.py',
                  ROOT/'tools/menu_source.py',ROOT/'tools/road_pack.py',
                  ROOT/'include/fzx_results_draw.h',ROOT/'include/fzx_results.h',FONT,COMMON]
    dependencies += [DECOMP/path for path in art.inputs]
    receipt=['# fzx generation receipt v1; regenerate, never edit']
    receipt += ['generator %s %s'%(hashlib.sha256(path.read_bytes()).hexdigest(),
                os.path.relpath(path,ROOT/'tools').replace('\\','/')) for path in dict.fromkeys(dependencies)]
    receipt += ['input %s owner-rom'%hashlib.sha256(rom).hexdigest()]
    output.mkdir(parents=True,exist_ok=True)
    for name,data in outputs.items():
        (output/name).write_bytes(data)
        receipt.append('output %s %s'%(hashlib.sha256(data).hexdigest(),name))
    (output/'results_ui.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    return dict(images=len(records),font3_glyphs=len(info['font']['images']),texture_bytes=len(bank))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'generated/results_ui')
    parser.add_argument('--config',type=Path)
    args=parser.parse_args()
    path=json.loads(args.config.read_text())['rom'] if args.config else os.environ.get('FZX_TEST_ROM')
    if not path:parser.error('Set FZX_TEST_ROM privately or supply local --config')
    if os.name!='nt' and len(path)>2 and path[1]==':':path='/mnt/'+path[0].lower()+'/'+path[3:].replace('\\','/')
    try:print(json.dumps(export(Path(path).read_bytes(),args.output),sort_keys=True))
    except Exception as exc:
        print('Results UI export failed: '+(str(exc) if isinstance(exc,ArtError) else type(exc).__name__))
        return 1
    return 0


if __name__=='__main__':raise SystemExit(main())
