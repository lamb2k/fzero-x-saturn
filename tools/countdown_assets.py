#!/usr/bin/env python3
"""Offline conversion of the stock race countdown's three fixed recipes.

Original geometry, UVs, normals, triangle order and material state are retained.
No display-list execution is added to the Saturn runtime. Outputs stay private.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import struct
import sys

from import_course import extract_rom_course, normalize_rom
from import_source_art import (ROOT, DECOMP, YAML, ArtError, SourceArt, bounded,
                               metadata, mio0, output_location, verify_source,
                               decode_texture, rgb1555)
from road_assets import resolve

LISTS = ('D_400A258', 'aCountdownSignDL', 'D_400A480')
DIGITS = ('aCountdown3Tex', 'aCountdown2Tex', 'aCountdown1Tex', 'aCountdownGoTex')
MOUTHS = ('aCountdownMrZeroMouthClosedTex', 'aCountdownMrZeroMouthOpenTex')
LOAD_OPS = [0xfd, 0xf5, 0xe6, 0xf3, 0xe7, 0xf5, 0xf2]
SHAPES = (
    [0xd9, 0xd9, 0xd7, 0xe7, 0xe3, 0xe2, 0xfc, 0xe3, 0xe2,
     0xe3, 0xe3, 0xe3, 0xe3, 0xe3, 0xe3, 0xdf],
    LOAD_OPS + [1, 6, 6, 6, 1, 6, 6, 6, 0xe7] + LOAD_OPS +
    [1, 6, 6, 1, 6, 6, 1, 6, 1, 6, 6, 6, 6, 6, 0xe7] + LOAD_OPS +
    [1, 6, 1, 6, 1, 6, 6, 0xdf],
    [1, 6, 1, 6, 0xdf],
)


def words(data, rows, name):
    row = rows.get(name, {})
    if row.get('type') != 'GFX':
        raise ArtError('Missing countdown recipe')
    start = row['offset']
    end = min((r['offset'] for r in rows.values() if r['offset'] > start),
              default=len(data))
    raw = bounded(data, start, end-start)
    if not raw or len(raw) % 8 or len(raw) > 512:
        raise ArtError('Invalid countdown recipe span')
    result = list(struct.iter_unpack('>II', raw))
    if result[-1] != (0xdf000000, 0):
        raise ArtError('Unterminated countdown recipe')
    return result


def geometry(lists, vertex_read, texture_name, digit_address):
    """Resolve only the pinned three lists; callbacks bind segment addresses.

    Dynamic textures use symbolic slots. The source's mouth/digit texture loads
    have the same 32x32 RGBA16 mirror-wrap setup as its final grate load.
    """
    for name, shape in zip(LISTS, SHAPES):
        if [a >> 24 for a, b in lists[name]] != shape or lists[name][-1] != (0xdf000000, 0):
            raise ArtError('Unsupported countdown recipe shape')
    vertices, commands, provenance = [], [], []
    sign = lists[LISTS[1]]
    last_load = max(i for i, (a, b) in enumerate(sign) if a >> 24 == 0xfd)
    texture_load = sign[last_load:last_load+7]

    def append(sequence, dynamic=None):
        for a, b in sequence:
            op, name = a >> 24, '-'
            if op == 0xdf:
                if (a, b) != (0xdf000000, 0):
                    raise ArtError('Malformed countdown end')
                continue
            if op == 1:
                count, end = (a >> 12) & 255, (a >> 1) & 127
                if end != count or count not in (4, 8, 12, 20):
                    raise ArtError('Unsupported countdown vertex load')
                rows, symbol = vertex_read(b, count)
                if len(rows) != count or any(len(v) != 9 for v in rows):
                    raise ArtError('Incomplete countdown vertices')
                provenance.append(dict(symbol=symbol, address=b, first=len(vertices), count=count))
                b, name = len(vertices), 'vertices'
                vertices.extend(rows)
            elif op == 0xfd:
                name, b = dynamic or texture_name(b), 0
            commands.append((a, b, name))

    append(lists[LISTS[0]])
    append(sign)
    append(texture_load, 'countdown.mouth')
    append(lists[LISTS[2]])
    append(texture_load, 'countdown.digit')
    # Stock Racer_Draw's final panel quad; geometry comes from its named asset.
    append([(0x01004008, digit_address), (0x06000204, 0x00000406)])
    model = resolve(vertices, commands, {})
    if len(model['triangles']) != 46 or len(vertices) != 92:
        raise ArtError('Countdown source coverage changed')
    # Pair only the adjacent source triangles with the authored 0,1,2 / 0,2,3 topology.
    quads = []
    for first, second in zip(model['triangles'][::2], model['triangles'][1::2]):
        if first[0] != second[0] or first[2] != second[1] or first[3] != second[3]:
            raise ArtError('Countdown quad topology changed')
        quads.append(first[:3] + [second[2], first[3]])
    model.update(quads=quads, source_vertices=provenance,
                 sections=dict(frame=[0, 20], mouth=[20, 2], digit=[22, 1]))
    return model


def export(rom, out):
    output_location(out)
    _, provenance = extract_rom_course(rom)
    normalized, _ = normalize_rom(rom)
    verify_source(DECOMP)
    source = SourceArt(normalized)
    track_path = DECOMP / YAML / 'course_track_gfx.yaml'
    track_rows = metadata(track_path.read_text())
    track = mio0(normalized[0x16c8a0:0x17b1e0])
    hud = source.segments['hud_gfx']
    hud_rows = {n: r for n, r in source.rows.items() if r['family'] == 'hud_gfx'}
    lists = {n: words(hud, hud_rows, n) for n in LISTS}
    segments = {4: (hud, hud_rows), 8: (track, track_rows)}

    def binding(address, kind):
        data, rows = segments.get(address >> 24, (None, {}))
        found = [(n, r) for n, r in rows.items()
                 if r['offset'] == address & 0xffffff and r['type'] == kind]
        if len(found) != 1 or data is None:
            raise ArtError('Unresolved countdown asset address')
        return data, *found[0]

    def vertex_read(address, count):
        data, name, row = binding(address, 'VTX')
        if row['count'] != count:
            raise ArtError('Countdown vertex span mismatch')
        raw = bounded(data, row['offset'], count*16)
        values = list(struct.iter_unpack('>3hH2h4B', raw))
        if any(v[3] for v in values):
            raise ArtError('Unexpected countdown vertex flag')
        return [list(v[:3]+v[4:]) for v in values], name

    def texture_name(address):
        return binding(address, 'TEXTURE')[1]

    model = geometry(lists, vertex_read, texture_name,
                     0x04000000 | hud_rows['D_400AA28']['offset'])
    textures, outputs = [], {}
    # Read the load's dimensions, not just the first metadata row: the frame
    # uses a 64x32 load spanning two adjacent authored 32x32 source images.
    sign = lists[LISTS[1]]
    for i, (a, address) in enumerate(sign):
        if a >> 24 != 0xfd:
            continue
        data, name, row = binding(address, 'TEXTURE')
        load = sign[i:i+7]
        if a != 0xfd100000 or [x >> 24 for x, y in load] != LOAD_OPS:
            raise ArtError('Unsupported countdown texture load')
        w = (((load[6][1] >> 12) & 4095) >> 2)+1
        h = ((load[6][1] & 4095) >> 2)+1
        count = ((load[3][1] >> 12) & 4095)+1
        if (w, h) not in ((64, 32), (32, 32)) or count != w*h:
            raise ArtError('Countdown texture dimensions disagree')
        covered, symbols = 0, []
        while covered < count:
            _, symbol, part = binding((address & 0xff000000) | (row['offset']+covered*2), 'TEXTURE')
            if part.get('format') != 'RGBA16' or part['width'] != 32 or part['height'] != 32:
                raise ArtError('Unsupported countdown adjacent texture')
            covered += part['width']*part['height']; symbols.append(symbol)
        raw = bounded(data, row['offset'], count*2)
        native, stride = rgb1555(decode_texture(raw, 'RGBA16', w, h), w, h)
        outputs[name+'.rgb1555'] = native
        textures.append(dict(symbol=name, source_symbols=symbols, width=w, height=h,
                             stride=stride, source_sha256=hashlib.sha256(raw).hexdigest(),
                             file=name+'.rgb1555'))
    for name in MOUTHS+DIGITS:
        tex = source.texture(name)
        if (tex['format'], tex['width'], tex['height']) != ('RGBA16', 32, 32):
            raise ArtError('Unsupported countdown panel format')
        native, stride = rgb1555(tex['rgba'], 32, 32)
        outputs[name+'.rgb1555'] = native
        textures.append(dict(symbol=name, source_symbols=[name], width=32, height=32,
                             stride=stride, source_sha256=tex['source_sha256'], file=name+'.rgb1555'))
    manifest = dict(schema=1, rom_sha256=provenance['normalized_rom_sha256'], model=model,
                    textures=textures, mouth_textures=MOUTHS, digit_textures=DIGITS,
                    adaptations=['Native RGB1555 texture channels; binary alpha retained'],
                    pending=['Native chart baking and renderer', 'Live source matrix and timer integration',
                             'Lighting and local image comparison', 'Countdown audio'])
    outputs['countdown.json'] = (json.dumps(manifest, indent=2)+'\n').encode()
    receipt = ['# fzx generation receipt v1']
    deps = [Path(__file__), ROOT/'tools/import_source_art.py', ROOT/'tools/import_course.py',
            ROOT/'tools/road_assets.py', ROOT/'tools/hud_assets.py', ROOT/'tools/model_assets.py', track_path]
    deps += [DECOMP/p for p in source.inputs]
    for path in deps:
        receipt.append('generator %s %s' % (hashlib.sha256(path.read_bytes()).hexdigest(),
                                            os.path.relpath(path, ROOT/'tools').replace('\\', '/')))
    receipt.append('input %s owner-rom' % hashlib.sha256(rom).hexdigest())
    out.mkdir(parents=True, exist_ok=True)
    for name, data in outputs.items():
        (out/name).write_bytes(data)
        receipt.append('output %s %s' % (hashlib.sha256(data).hexdigest(), name))
    (out/'countdown.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    return dict(quads=len(model['quads']), triangles=len(model['triangles']),
                textures=len(textures), texture_bytes=sum(len(v) for k,v in outputs.items() if k.endswith('.rgb1555')))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'generated/countdown')
    args = parser.parse_args()
    path = os.environ.get('FZX_TEST_ROM')
    if not path:
        parser.error('Set FZX_TEST_ROM locally; keep its path out of logs')
    try:
        print(json.dumps(export(Path(path).read_bytes(), args.output), sort_keys=True))
    except (OSError, ValueError, KeyError) as exc:
        print('countdown conversion failed: '+(str(exc) if isinstance(exc, ArtError) else type(exc).__name__), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
