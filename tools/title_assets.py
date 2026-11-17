#!/usr/bin/env python3
"""Extract the original first title screen into a Saturn RGB1555 atlas.

The original compressed images stay local. Geometry retains the source's
320x240 coordinates; the native display applies its usual eight-line crop.
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
                               mio0, decode_texture, rgb1555, output_location,
                               verify_source)
from road_pack import fnv

BASE = 0x2B9EA0
SOURCE = 'src/overlays/ovl_i4/title.c'
ASSETS = (
    ('aTitleBackgroundMainTex', 304, 240, 8, 0),
    ('aTitleLogoTex', 160, 65, 85, 21),
    ('aTitlePushStartTex', 80, 40, 120, 150),
    ('aCopyrightTex', 128, 14, 94, 200),
    ('aTitleNoControllerTex', 160, 32, 80, 150),
)


def extract(data, rows):
    bank, records = bytearray(), []
    for symbol, width, height, left, top in ASSETS:
        row = rows.get(symbol, {})
        warning = symbol == 'aTitleNoControllerTex'
        fmt = 'I4' if warning else 'RGBA16'
        raw_bytes = width*height//2 if warning else width*height*2
        if (row.get('type'), row.get('format'), row.get('width'), row.get('height')) != ('COMPRESSED_TEXTURE', fmt, width, height):
            raise ArtError('Original title texture declaration changed')
        start = row['offset']
        end = min((r['offset'] for r in rows.values() if r.get('offset', -1) > start), default=len(data))
        compressed = bounded(data, start, end-start)
        raw = mio0(compressed, raw_bytes)
        if warning:
            # Original I4 intensity mask tinted red by Title_StartDraw. Native
            # binary transparency retains the authored glyph, at fixed full red.
            values = [n for b in raw for n in (b >> 4, b & 15)]
            pixels = b''.join(struct.pack('>H', 0x8000 | ((n*17) >> 3)) if n else b'\0\0' for n in values)
            stride = width
        else:
            pixels, stride = rgb1555(decode_texture(raw, fmt, width, height), width, height)
        if len(raw) != raw_bytes or len(bank) % 8:
            raise ArtError('Original title extent or alignment mismatch')
        records.append(dict(symbol=symbol, source_offset=BASE+start, source_sha256=hashlib.sha256(raw).hexdigest(),
                            offset=len(bank), bytes=len(pixels), width=width, height=height, stride=stride,
                            left=left, top=top))
        bank += pixels
    if len(bank) > 192*1024:
        raise ArtError('Title atlas exceeds temporary boot VRAM allocation')
    return bytes(bank), records


def verify_layout(source):
    # These source-owned placement and timing anchors must still be present.
    # The title variants are not silently substituted for the initial screen.
    anchors = ('Object_Init(OBJECT_TITLE_COPYRIGHT, 94, 200, 12);',
               'OBJECT_LEFT(backgroundObj) = 8;', 'OBJECT_LEFT(logoObj) = 0x55;',
               'OBJECT_TOP(logoObj) = 0x15;', 'OBJECT_LEFT(startObj) = 120;',
               'OBJECT_TOP(startObj) = 150;', 'OBJECT_LEFT(startObj) = 80;', 'if (D_i4_8011D790 < 80)',
               '(D_i4_8011D790 < 95) && (D_i4_8011D790 >= 0)')
    if any(a not in source for a in anchors):
        raise ArtError('Original title layout or timing changed')


def export(rom, output):
    output_location(output)
    _, provenance = extract_rom_course(rom)
    normalized, _ = normalize_rom(rom)
    verify_source(DECOMP)
    yaml = DECOMP/YAML/'common_assets_compressed.yaml'
    source = DECOMP/SOURCE
    verify_layout(source.read_text())
    rows = metadata(yaml.read_text())
    # Each image is independently MIO0-compressed in this raw ROM segment.
    # Do not decompress the whole segment as if it were one archive.
    bank, records = extract(normalized[BASE:], rows)
    report = dict(schema=1, rom_sha256=provenance['normalized_rom_sha256'], images=records,
                  texture_bytes=len(bank), source_viewport=[320,240], native_crop=[0,8,320,224],
                  input_unlock_tick=80, push_start_visible_tick=95,
                  adaptations=['Original first title variant; native 224-line display crops eight lines at each edge',
                               'No-controller authored mask uses fixed red and binary transparency; source pulsing backplate is not yet drawn'],
                  pending=['Other title variants', 'Original title audio', 'Selection screens'])
    header = '/* Generated original title atlas identity and layout; regenerate. */\n'
    header += '#define FZX_TITLE_BYTES %du\n#define FZX_TITLE_HASH %du\n' % (len(bank), fnv(bank))
    header += '#define FZX_TITLE_IMAGE_COUNT %du\n' % len(records)
    header += 'static const struct { unsigned int offset; unsigned short width,height; short left,top; } fzx_title_images[FZX_TITLE_IMAGE_COUNT]={\n'
    header += ',\n'.join(' {%du,%du,%du,%d,%d}' % (t['offset'], t['stride'], t['height'], t['left'], t['top']) for t in records)
    header += '\n};\n'
    output.mkdir(parents=True, exist_ok=True)
    outputs = {'TITLE.PAK': bank, 'title_identity.h': header.encode(), 'title.json': (json.dumps(report, indent=2)+'\n').encode()}
    dependencies = [Path(__file__), ROOT/'tools/import_source_art.py', ROOT/'tools/import_course.py', ROOT/'tools/road_pack.py', yaml, source]
    receipt = ['# fzx generation receipt v1; regenerate, never edit']
    receipt += ['generator %s %s' % (hashlib.sha256(p.read_bytes()).hexdigest(), os.path.relpath(p,ROOT/'tools').replace('\\','/')) for p in dependencies]
    receipt += ['input %s owner-rom' % hashlib.sha256(rom).hexdigest()]
    for name, data in outputs.items():
        (output/name).write_bytes(data)
        receipt.append('output %s %s' % (hashlib.sha256(data).hexdigest(),name))
    (output/'title.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    return dict(images=len(records), texture_bytes=len(bank))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'generated/title')
    args = parser.parse_args()
    path = os.environ.get('FZX_TEST_ROM')
    if not path:
        parser.error('Set FZX_TEST_ROM locally; keep its path out of logs')
    try:
        print(json.dumps(export(Path(path).read_bytes(),args.output)))
    except (OSError,ValueError,KeyError) as exc:
        print('title conversion failed: '+(str(exc) if isinstance(exc,ArtError) else type(exc).__name__),file=sys.stderr)
        raise SystemExit(1)
