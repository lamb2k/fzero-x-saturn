#!/usr/bin/env python3
"""Convert original cartridge machine/settings UI assets into bounded native banks.

All texture indices and character mappings come from the pinned US source.
Generated artwork, source tables and manifests remain local. RGB conversion is
lossless at RGB5 precision; CLUT4 is used only when all original colors fit.
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
from menu_assets import BASE, rgb_words
from menu_source import cartridge
from road_pack import fnv

SOURCE = DECOMP/'src/overlays/ovl_i4/machine.c'
FONT = DECOMP/'src/overlays/ovl_i2/font.c'
ASSET_YAML = DECOMP/YAML/'common_assets_compressed.yaml'
SPECIAL_TABLES = ('sMachineStatValueCompTexInfos', 'sMachineWeightDigitCompTexInfos')


def _declarations(source):
    pattern = (r'(?:static\s+)?unk_80077D50\s+(\w+)\[\]\s*=\s*'
               r'COMP_TEX_INFO(?P<suffix>_DEF_PAD|_DEF)?\(\s*(\d+),\s*(\w+)(?:,\s*0)?\s*\);')
    return {name: (symbol, int(kind)) for name, _, kind, symbol in re.findall(pattern, source)}


def _table(source, name):
    match = re.search(r'\b'+re.escape(name)+r'\[\]\s*=\s*\{(.*?)\};', source, re.S)
    if not match:
        raise ArtError('Original machine asset table missing: '+name)
    return re.sub(r'//[^\n]*|/\*.*?\*/', '', match.group(1), flags=re.S)


def census(source=None, font_source=None, rows=None):
    """Return stable specs, alias->image IDs, source tables and font metrics.

    IDs first follow machine.c declaration order, then the used font.c
    declaration order. This function reads source only; it does not need a ROM.
    Optional text/metadata arguments support source-free synthetic controls.
    """
    source = cartridge(SOURCE.read_text() if source is None else source)
    font_source = cartridge(FONT.read_text() if font_source is None else font_source)
    rows = metadata(ASSET_YAML.read_text()) if rows is None else rows
    declarations = _declarations(source)
    if not declarations:
        raise ArtError('Original machine texture declarations missing')
    font_declarations = _declarations(font_source)
    selected = {}
    table_aliases = {}
    for name in ('sFont1CompTexInfos', 'sFont2CompTexInfos', *SPECIAL_TABLES):
        names = re.findall(r'\b\w+CompTexInfo\b', _table(font_source, name))
        expected = 48 if name.startswith('sFont') else 5 if name == SPECIAL_TABLES[0] else 10
        if len(names) != expected:
            raise ArtError('Original machine font table extent changed: '+name)
        for alias in names:
            if alias not in font_declarations:
                raise ArtError('Unsupported machine font declaration: '+alias)
            selected[alias] = font_declarations[alias]
        table_aliases[name] = names
    for alias in font_declarations:
        if alias in selected:
            declarations[alias] = selected[alias]
    if any(kind not in (4, 17, 18) for _, kind in declarations.values()):
        raise ArtError('Unsupported original machine texture loader type')
    symbols = list(dict.fromkeys(symbol for symbol, _ in declarations.values()))
    specs = []
    for symbol in symbols:
        row = rows.get(symbol, {})
        w, h, fmt = row.get('width'), row.get('height'), row.get('format')
        if (not isinstance(w, int) or not isinstance(h, int) or not 1 <= w <= 512 or
                not 1 <= h <= 255 or fmt not in ('RGBA16', 'I4')):
            raise ArtError('Unsupported machine texture extent: '+symbol)
        expected = 'TEXTURE' if fmt == 'I4' else 'COMPRESSED_TEXTURE'
        if row.get('type') != expected or (fmt == 'I4' and w*h % 2):
            raise ArtError('Unsupported machine texture format: '+symbol)
        specs.append((symbol, w, h, fmt))
    ids = {symbol: i for i, symbol in enumerate(symbols)}
    aliases = {name: ids[symbol] for name, (symbol, _) in declarations.items()}
    for name, body in re.findall(r'unk_80077D50\*\s+(\w+)\[\]\s*=\s*\{(.*?)\};', source, re.S):
        body = re.sub(r'//[^\n]*|/\*.*?\*/', '', body, flags=re.S)
        names = re.findall(r'\b\w+CompTexInfo\b', body)
        if not names or any(n not in aliases for n in names):
            raise ArtError('Unsupported machine image table: '+name)
        table_aliases[name] = names
    tables = {name: [aliases[alias] for alias in names] for name, names in table_aliases.items()}
    # Portrait/name-card animation tables contain a single 60-tick still.
    animations = {}
    for name, alias in re.findall(r'unk_800792D8\s+(\w+)\[\]\s*=\s*'
                                  r'\{\s*\{\s*(\w+),\s*60\s*\},\s*\{\s*0\s*\}\s*\};', source):
        if alias not in aliases:
            raise ArtError('Unknown machine portrait animation texture')
        animations[name] = aliases[alias]
    for name, body in re.findall(r'unk_800792D8\*\s+(\w+)\[\]\s*=\s*\{(.*?)\};', source, re.S):
        names = re.findall(r'\bD_\w+\b', body)
        if not names or any(n not in animations for n in names):
            raise ArtError('Unsupported machine portrait animation table')
        tables[name] = [animations[n] for n in names]
    fonts = {}
    for number in (1, 2):
        advance = [int(v) for v in re.findall(r'\d+', _table(font_source, 'kFont%dCharWidths'%number))]
        if len(advance) != 48 or any(v < 1 or v > 128 for v in advance):
            raise ArtError('Original machine font widths changed')
        fonts[number] = dict(images=tables['sFont%dCompTexInfos'%number], advance=advance,
                             space=7 if number == 1 else 3, baseline=16 if number == 1 else 8)
    weight_widths = [int(v) for v in re.findall(r'\d+', _table(font_source, 'kMachineWeightDigitWidths'))]
    if len(weight_widths) != 10 or any(v < 1 or v > 16 for v in weight_widths):
        raise ArtError('Original weight glyph widths changed')
    return dict(specs=specs, aliases=aliases, tables=tables, fonts=fonts,
                weight_widths=weight_widths,
                source_types={name: kind for name, (_, kind) in declarations.items()})


def extract(data, rows, specs):
    """Decode original texels and pad each row to native eight-pixel alignment."""
    bank = bytearray()
    records = []
    for image, (symbol, w, h, fmt) in enumerate(specs):
        row = rows.get(symbol, {})
        kind = 'TEXTURE' if fmt == 'I4' else 'COMPRESSED_TEXTURE'
        if (row.get('type'), row.get('format'), row.get('width'), row.get('height')) != (kind, fmt, w, h):
            raise ArtError('Machine image declaration changed: '+symbol)
        if not 1 <= w <= 512 or not 1 <= h <= 255 or fmt not in ('RGBA16', 'I4') or w*h % 2:
            raise ArtError('Invalid machine image extent')
        start = row.get('offset')
        if not isinstance(start, int) or start < 0:
            raise ArtError('Invalid machine image offset')
        end = min((r['offset'] for r in rows.values() if r.get('offset', -1) > start), default=len(data))
        span = bounded(data, start, end-start)
        size = w*h//2 if fmt == 'I4' else w*h*2
        raw = bounded(span, 0, size) if fmt == 'I4' else mio0(span, size)
        if len(raw) != size:
            raise ArtError('Machine image extent changed')
        words = rgb_words(raw, fmt)
        stride = (w+7)//8*8
        words = [v for y in range(h) for v in words[y*w:(y+1)*w]+[0]*(stride-w)]
        palette = [0]+sorted(set(words)-{0})
        clut = len(palette) <= 16
        if clut:
            indices = {v: i for i, v in enumerate(palette)}
            pixels = bytes(indices[words[i]]*16+indices[words[i+1]] for i in range(0, len(words), 2))
            palette += [0]*(16-len(palette))
        else:
            pixels = struct.pack('>%dH'%len(words), *words)
            palette = []
        bank += bytes((-len(bank)) % 8)
        records.append(dict(image=image, symbol=symbol, offset=len(bank), bytes=len(pixels),
                            width=stride, height=h, source_width=w, source_height=h,
                            clut=clut, palette=palette, source_offset=BASE+start,
                            source_sha256=hashlib.sha256(raw).hexdigest()))
        bank += pixels
    bank += bytes((-len(bank)) % 8)
    return bytes(bank), records


def resident_banks(atlas, records, tables):
    """Common art and one original character's settings art fit together.

    All small portraits stay resident for selection. Full portrait background
    remains common; each character bank holds only its full portrait/name card.
    Three dormant stock alternate portraits are retained in separate banks.
    """
    portraits = tables.get('sPortraitCompTexInfos', [])
    names = tables.get('sNameCardCompTexInfos', [])
    if len(portraits) != 35 or len(names) != 31 or len(set(portraits[:30])) != 30 or len(set(names[:30])) != 30:
        raise ArtError('Original machine character-image mapping changed')
    full = set(portraits)
    dynamic = full | set(names)
    if len(full) != 33 or len(set(names)) != 30 or full & set(names):
        raise ArtError('Original machine portrait families changed')
    groups = [[r['image'] for r in records if r['image'] not in dynamic]]
    groups += [[portraits[i], names[i]] for i in range(30)]
    groups += [[image] for image in sorted(full-set(portraits[:30]))]
    if sorted(i for group in groups for i in group) != list(range(len(records))):
        raise ArtError('Machine atlas residency does not cover each image once')
    result = [dict(r) for r in records]
    banks = []
    for index, group in enumerate(groups):
        payload = bytearray()
        for image in group:
            record = records[image]
            payload += bytes((-len(payload)) % 8)
            result[image]['bank'] = index
            result[image]['offset'] = len(payload)
            payload += bounded(atlas, record['offset'], record['bytes'])
        payload += bytes((-len(payload)) % 8)
        banks.append(dict(bank=index, filename='MACHUI.PAK' if index == 0 else 'MCH%02d.PAK'%(index-1),
                          images=group, bytes=len(payload), fnv1a32=fnv(payload),
                          sha256=hashlib.sha256(payload).hexdigest(), payload=bytes(payload)))
    common = banks[0]['bytes']
    maximum = max(bank['bytes'] for bank in banks[1:])
    if common+maximum > 432*1024 or maximum > 128*1024:
        raise ArtError('Original machine UI exceeds bounded frontend residency')
    return banks, result


def identity_header(records, banks, catalog):
    header = '/* Generated original machine/settings artwork; regenerate, never edit. */\n'
    header += '#define FZX_MACHINE_MENU_BYTES %du\n#define FZX_MACHINE_MENU_HASH %du\n'%(banks[0]['bytes'], banks[0]['fnv1a32'])
    header += '#define FZX_MACHINE_MENU_MAX_BANK_BYTES %du\n'%max(b['bytes'] for b in banks[1:])
    header += '#define FZX_MACHINE_MENU_RESIDENT_BYTES %du\n'%(banks[0]['bytes']+max(b['bytes'] for b in banks[1:]))
    header += '#define FZX_MACHINE_MENU_ATLAS_IMAGES %du\n#define FZX_MACHINE_MENU_BANK_COUNT %du\n'%(len(records), len(banks))
    header += 'static const struct { unsigned int bytes,hash; } fzx_machine_menu_banks[FZX_MACHINE_MENU_BANK_COUNT]={\n'
    header += ',\n'.join(' {%du,%du}'%(b['bytes'], b['fnv1a32']) for b in banks)+'\n};\n'
    header += ('static const struct { unsigned int offset,bytes; unsigned short width,height,source_width; '
               'unsigned char clut,bank; unsigned short palette[16]; } '
               'fzx_machine_menu_images[FZX_MACHINE_MENU_ATLAS_IMAGES]={\n')
    header += ',\n'.join(' {%du,%du,%du,%du,%du,%du,%du,{%s}}'%(
        r['offset'], r['bytes'], r['width'], r['height'], r['source_width'], r['clut'], r['bank'],
        ','.join(str(v) for v in (r['palette'] or [0]*16))) for r in records)+'\n};\n'
    for font, table in catalog['fonts'].items():
        header += 'static const unsigned short fzx_machine_font%d[][2]={\n'%font
        header += ','.join('{%d,%d}'%pair for pair in zip(table['images'], table['advance']))+'\n};\n'
    return header.encode()


def export(rom, output):
    output_location(output)
    verify_source(DECOMP)
    _, provenance = extract_rom_course(rom)
    normalized, _ = normalize_rom(rom)
    rows = metadata(ASSET_YAML.read_text())
    catalog = census(rows=rows)
    atlas, records = extract(normalized[BASE:], rows, catalog['specs'])
    banks, records = resident_banks(atlas, records, catalog['tables'])
    report = dict(schema=1, rom_sha256=provenance['normalized_rom_sha256'], images=records,
                  banks=[{k: v for k, v in bank.items() if k != 'payload'} for bank in banks],
                  aliases=catalog['aliases'], tables=catalog['tables'], fonts=catalog['fonts'],
                  weight_widths=catalog['weight_widths'], source_types=catalog['source_types'],
                  texture_bytes=sum(b['bytes'] for b in banks),
                  common_bytes=banks[0]['bytes'],
                  max_character_bytes=max(b['bytes'] for b in banks[1:]),
                  resident_bytes=banks[0]['bytes']+max(b['bytes'] for b in banks[1:]),
                  source_viewport=[320, 240], native_crop=[0, 8, 320, 224],
                  adaptations=['RGBA16 retains all original RGB5 channels and binary alpha',
                               'I4 intensity retains all16 values as RGB5 grayscale with binary transparency',
                               'CLUT4 is lossless; images with more than15 opaque colors remain RGB1555',
                               'Native rows are padded to8 pixels with transparent pixels; source extent is retained',
                               'Common bank retains every small portrait; full portrait and namecard load on settings entry',
                               'Source background gradient and animated UI colors remain runtime draw state'])
    outputs = {b['filename']: b['payload'] for b in banks}
    outputs['machine_menu_identity.h'] = identity_header(records, banks, catalog)
    outputs['machine_menu.json'] = (json.dumps(report, indent=2)+'\n').encode()
    dependencies = [Path(__file__), ROOT/'tools/import_course.py', ROOT/'tools/import_source_art.py',
                    ROOT/'tools/menu_assets.py', ROOT/'tools/menu_source.py', ROOT/'tools/road_pack.py',
                    SOURCE, FONT, ASSET_YAML, DECOMP/'include/macros.h']
    receipt = ['# fzx generation receipt v1; regenerate, never edit']
    receipt += ['generator %s %s'%(hashlib.sha256(p.read_bytes()).hexdigest(),
                os.path.relpath(p, ROOT/'tools').replace('\\', '/')) for p in dependencies]
    receipt += ['input %s owner-rom'%hashlib.sha256(rom).hexdigest()]
    output.mkdir(parents=True, exist_ok=True)
    for name, data in outputs.items():
        (output/name).write_bytes(data)
        receipt.append('output %s %s'%(hashlib.sha256(data).hexdigest(), name))
    (output/'machine_menu.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    return {key: report[key] for key in ('texture_bytes', 'common_bytes', 'max_character_bytes', 'resident_bytes')} | dict(images=len(records), banks=len(banks))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'generated/machine_menu')
    args = parser.parse_args()
    path = os.environ.get('FZX_TEST_ROM')
    if not path:
        parser.error('Set FZX_TEST_ROM privately')
    print(export(Path(path).read_bytes(), args.output))


if __name__ == '__main__':
    main()
