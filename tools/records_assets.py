#!/usr/bin/env python3
"""Prebake original Time Attack name-entry and records UI artwork.

The bank extends the admitted results sheet IDs with complete fonts 1, 3 and
4 and the original BEST label. It prepares assets only: records state, native
residency, machine previews and durable saving remain separate gates.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re

import results_assets as results
from import_course import extract_rom_course, normalize_rom
from import_source_art import ROOT, DECOMP, ArtError, SourceArt, bounded, metadata, mio0, output_location, verify_source
from machine_menu_assets import _declarations, _table
from course_menu_assets import font_tables
from menu_assets import BASE
from menu_source import cartridge
from road_pack import fnv

FONT = results.FONT
COMMON = results.COMMON


def catalog(font_source=None, rows=None):
    source = cartridge(FONT.read_text() if font_source is None else font_source)
    rows = metadata(COMMON.read_text()) if rows is None else rows
    fonts = font_tables(source)
    declarations = _declarations(source)
    aliases = re.findall(r'\b\w+CompTexInfo\b', _table(source, 'sFont1CompTexInfos'))
    widths = [int(n) for n in re.findall(r'\d+', _table(source, 'kFont1CharWidths'))]
    if len(aliases) != 48 or len(widths) != 48 or any(not 1 <= n <= 128 for n in widths):
        raise ArtError('Original records Font1 extent changed')
    if any(a not in declarations or declarations[a][1] not in (17,18) for a in aliases):
        raise ArtError('Original records Font1 loader changed')
    fonts[1] = dict(symbols=[declarations[a][0] for a in aliases], advance=widths,
                    crops={}, space=7, baseline=16)
    specs = [(n,w,h,'RGBA16') for n,w,h in results.SHEETS]
    specs.append(('aBestTex',16,12,'RGBA16'))
    seen = {s[0] for s in specs}
    crops = {}
    for number in (1,3,4):
        table = fonts[number]
        for symbol in table['symbols']:
            if symbol in seen:
                continue
            row = rows.get(symbol,{})
            w,h,fmt = row.get('width'),row.get('height'),row.get('format')
            kind = 'COMPRESSED_TEXTURE' if number == 1 else 'TEXTURE'
            expected = 'RGBA16' if number == 1 else 'I4'
            if (row.get('type') != kind or fmt != expected or not isinstance(w,int) or
                    not 1 <= w <= 128 or h != table['baseline']):
                raise ArtError('Original records font format changed: '+symbol)
            specs.append((symbol,w,h,fmt));seen.add(symbol)
        for symbol,crop in table['crops'].items():
            if symbol in crops and crops[symbol] != crop:
                raise ArtError('Conflicting original records font crops')
            crops[symbol] = crop
    best = rows.get('aBestTex',{})
    if (best.get('type'),best.get('format'),best.get('width'),best.get('height')) != ('TEXTURE','RGBA16',16,12):
        raise ArtError('Original BEST label changed')
    ids = {s[0]:i for i,s in enumerate(specs)}
    tables = {n:dict(images=[ids[s] for s in fonts[n]['symbols']], advance=fonts[n]['advance'],
                     space=fonts[n]['space'], baseline=fonts[n]['baseline']) for n in (1,3,4)}
    return dict(specs=specs,fonts=tables,crops=crops)


def common_image(data,rows,symbol,w,h,fmt):
    row = rows.get(symbol,{})
    kind = row.get('type')
    if ((row.get('format'),row.get('width'),row.get('height')) != (fmt,w,h) or
            kind not in ('TEXTURE','COMPRESSED_TEXTURE') or (fmt == 'I4' and kind != 'TEXTURE')):
        raise ArtError('Records image declaration changed: '+symbol)
    start = row.get('offset')
    if not isinstance(start,int) or start < 0:
        raise ArtError('Invalid records source offset')
    end = min((r['offset'] for r in rows.values() if r.get('offset',-1)>start),default=len(data))
    span = bounded(data,start,end-start)
    size = w*h//2 if fmt == 'I4' else w*h*2
    raw = bounded(span,0,size) if kind == 'TEXTURE' else mio0(span,size)
    if len(raw) != size:
        raise ArtError('Records decompressed extent changed')
    return raw,start


def convert(art,data,rows,info):
    bank = bytearray();records = []
    for image,(symbol,w,h,fmt) in enumerate(info['specs']):
        if image < len(results.SHEETS):
            source = art.texture(symbol)
            if (source['width'],source['height'],source['format']) != (w,h,fmt):
                raise ArtError('Original records sheet changed: '+symbol)
            raw = source['texels'];family = source['family'];offset = source['offset']
        else:
            raw,offset = common_image(data,rows,symbol,w,h,fmt)
            family = 'common_assets_compressed'
        pixels,record = results.encode_image(raw,fmt,w,h,info['crops'].get(symbol))
        record.update(image=image,symbol=symbol,offset=len(bank),bytes=len(pixels),
                      family=family,source_offset=offset)
        bank.extend(pixels);records.append(record)
    return bytes(bank),records


def identity_header(bank,records,info,tints,tint_bytes,tint_offsets):
    # Keep one image descriptor and pulse contract with the existing renderer.
    legacy = dict(font=info['fonts'][3])
    text = results.identity_header(bank,records,legacy,tints,tint_bytes,tint_offsets).decode()
    text = text.replace('FZX_RESULTS_','FZX_RECORDS_').replace('fzx_results_images','fzx_records_images')
    text = text.replace('fzx_results_font3','fzx_records_font3').replace('fzx_results_tint_offsets','fzx_records_tint_offsets')
    for number in (1,4):
        font = info['fonts'][number]
        text += '#define FZX_RECORDS_FONT%d_GLYPHS %du\n'%(number,len(font['images']))
        text += 'static const unsigned short fzx_records_font%d[FZX_RECORDS_FONT%d_GLYPHS][2]={\n'%(number,number)
        text += ','.join('{%d,%d}'%v for v in zip(font['images'],font['advance']))+'\n};\n'
    text += '#define FZX_RECORDS_BEST_IMAGE 8u\n'
    return text.encode()


def export(rom,output):
    output_location(output);verify_source(DECOMP)
    _,provenance = extract_rom_course(rom)
    normalized,_ = normalize_rom(rom)
    art = SourceArt(normalized);rows = metadata(COMMON.read_text());info = catalog(rows=rows)
    bank,records = convert(art,normalized[BASE:],rows,info)
    tints,stride,offsets = results.tint_banks(bank,records)
    report = dict(schema=1,rom_sha256=provenance['normalized_rom_sha256'],images=records,
                  texture_bytes=len(bank),fnv1a32=fnv(bank),fonts=info['fonts'],
                  tint_bytes=len(tints),tint_bank_bytes=stride,source_viewport=[320,240],native_crop=[0,8,320,224],
                  scope='Asset preparation only; no native residency, records flow or saving acceptance',
                  adaptations=['Original RGB5 channels and binary alpha retained; I4 becomes RGB5 grayscale',
                    'Lossless CLUT4 only where all colors fit; otherwise RGB1555',
                    'Original font crops and advances retained; rows padded transparently to eight pixels',
                    'Existing sixteen-level red record pulse retained'])
    outputs={'RECORDUI.PAK':bank,'RECTINT.PAK':tints,
             'records_identity.h':identity_header(bank,records,info,tints,stride,offsets),
             'records_ui.json':(json.dumps(report,indent=2)+'\n').encode()}
    deps=[Path(__file__),ROOT/'tools/results_assets.py',ROOT/'tools/machine_menu_assets.py',
          ROOT/'tools/course_menu_assets.py',ROOT/'tools/menu_assets.py',ROOT/'tools/menu_source.py',
          ROOT/'tools/hud_assets.py',ROOT/'tools/import_course.py',ROOT/'tools/import_source_art.py',
          ROOT/'tools/road_pack.py',ROOT/'include/fzx_results_draw.h',ROOT/'include/fzx_results.h',FONT,COMMON]
    deps += [DECOMP/path for path in art.inputs]
    receipt=['# fzx generation receipt v1; regenerate, never edit']
    receipt += ['generator %s %s'%(hashlib.sha256(p.read_bytes()).hexdigest(),os.path.relpath(p,ROOT/'tools').replace('\\','/')) for p in dict.fromkeys(deps)]
    receipt += ['input %s owner-rom'%hashlib.sha256(rom).hexdigest()]
    output.mkdir(parents=True,exist_ok=True)
    for name,data in outputs.items():
        (output/name).write_bytes(data);receipt.append('output %s %s'%(hashlib.sha256(data).hexdigest(),name))
    (output/'records_ui.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    return dict(images=len(records),glyphs={n:len(t['images']) for n,t in info['fonts'].items()},texture_bytes=len(bank))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'generated/records_ui')
    parser.add_argument('--config',type=Path);args=parser.parse_args()
    path=json.loads(args.config.read_text())['rom'] if args.config else os.environ.get('FZX_TEST_ROM')
    if not path:parser.error('Set FZX_TEST_ROM privately or supply local --config')
    if os.name!='nt' and len(path)>2 and path[1]==':':path='/mnt/'+path[0].lower()+'/'+path[3:].replace('\\','/')
    try:print(json.dumps(export(Path(path).read_bytes(),args.output),sort_keys=True))
    except Exception as exc:
        print('Records UI export failed: '+(str(exc) if isinstance(exc,ArtError) else type(exc).__name__));return 1
    return 0


if __name__=='__main__':raise SystemExit(main())
