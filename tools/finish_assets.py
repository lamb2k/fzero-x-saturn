#!/usr/bin/env python3
"""Extract only the original finish sign's two pinned offline display lists.

All 256 loaded vertex rows and 222 source-order triangles remain private.
The Saturn runtime consumes a native pack, never display-list instructions.
"""
from pathlib import Path
import argparse
import collections
import hashlib
import json
import os
import struct
import sys

from import_course import extract_rom_course, normalize_rom
from import_source_art import (ROOT, DECOMP, ArtError, SourceArt, bounded,
                               output_location, rgb1555, verify_source)
from countdown_assets import SHAPES
from road_assets import resolve, signed16

LISTS = ('D_400A258', 'aFinishDL')
TEXTURES = ('aFinishSideTex', 'aFinishFaceTex')
VERTEX_COUNTS = (20,10,8,4,20,10,8,4,32,20,22,24,12,12,22,4,10,4,10)
COUNTS = {0xd9:3,0xd7:6,0xe7:36,0xfd:18,0xf5:36,0xe6:18,
          0xf3:18,0xf2:18,1:19,6:110,5:2,0xdf:1}
GEOMETRY = 0x800404  # clipping/shading, back cull; no Z, lighting or generated UV.
OPA_SURF = 0x0f0a4000  # gbi.h RM_OPA_SURF(1)|RM_OPA_SURF(2).


def recipe(data, rows, name):
    if name not in LISTS or rows.get(name, {}).get('type') != 'GFX':
        raise ArtError('Unsupported finish recipe')
    start = rows[name]['offset']
    end = min((r['offset'] for r in rows.values() if r['offset'] > start), default=len(data))
    size = 128 if name == LISTS[0] else 2280
    if end-start != size:
        raise ArtError('Finish recipe extent changed')
    result = list(struct.iter_unpack('>II', bounded(data, start, size)))
    ops = [a >> 24 for a, _ in result]
    if result[-1] != (0xdf000000, 0) or 0xdf in ops[:-1]:
        raise ArtError('Malformed finish termination')
    if name == LISTS[0]:
        if ops != SHAPES[0]:
            raise ArtError('Finish setup recipe changed')
    elif collections.Counter(ops) != COUNTS:
        raise ArtError('Finish model recipe changed')
    return result


def geometry(lists, vertex_read, texture_name):
    """Keep each load row; independently resolve source material/cache state."""
    vertices, provenance, commands, source_triangles = [], [], [], []
    cache = [None]*32
    for name in LISTS:
        if name == LISTS[1]:
            # Exact Menus_DrawFinish overrides between setup and aFinishDL.
            commands.extend(((0xd9fffbfe, 0, '-'), (0xe200001c, OPA_SURF, '-')))
        sequence = lists[name]
        if not sequence or sequence[-1] != (0xdf000000, 0):
            raise ArtError('Missing finish end')
        for ci, (a, b) in enumerate(sequence):
            op, label = a >> 24, '-'
            if op == 0xdf:
                if ci != len(sequence)-1 or b:
                    raise ArtError('Early finish end')
                continue
            if op == 1:
                count, end = (a >> 12) & 255, (a >> 1) & 127
                if count <= 0 or count > 32 or end != count:
                    raise ArtError('Unsupported finish vertex load')
                values, symbol = vertex_read(b, count)
                if len(values) != count or any(len(v) != 9 for v in values):
                    raise ArtError('Incomplete finish vertex load')
                first = len(vertices)
                provenance.append(dict(symbol=symbol,address=b,first=first,count=count))
                vertices.extend(values)
                cache[:count] = range(first,first+count)
                b, label = first, 'vertices'
            elif op == 0xfd:
                label, b = texture_name(b), 0
                if label not in TEXTURES:
                    raise ArtError('Unknown finish texture')
            elif op in (5, 6):
                if op == 5 and b:
                    raise ArtError('Malformed finish single triangle')
                for word in ((a & 0xffffff, b) if op == 6 else (a & 0xffffff,)):
                    slots = [(word >> s) & 255 for s in (16,8,0)]
                    if any(v & 1 or v >= 64 or cache[v//2] is None for v in slots):
                        raise ArtError('Unloaded finish vertex')
                    source_triangles.append([cache[v//2] for v in slots])
            commands.append((a,b,label))
    model = resolve(vertices,commands,{})
    scaled = [v[:3]+[signed16(q*65535 >> 16) for q in v[3:5]]+v[5:] for v in vertices]
    if len(source_triangles) != len(model['triangles']):
        raise ArtError('Finish triangle accounting mismatch')
    triangles = []
    for ids, resolved in zip(source_triangles,model['triangles']):
        if [scaled[k] for k in ids] != [model['vertices'][k] for k in resolved[:3]]:
            raise ArtError('Finish independent vertex/cache comparison failed')
        material = model['materials'][resolved[3]]
        triangles.append(ids+[TEXTURES.index(material['texture']['name'])])
    if (len(vertices),len(triangles),tuple(p['count'] for p in provenance)) != (256,222,VERTEX_COUNTS):
        raise ArtError('Finish geometry coverage changed')
    if collections.Counter(t[3] for t in triangles) != {0:124,1:98} or model['accounting']['degenerates']:
        raise ArtError('Finish material coverage changed')
    materials = []
    for name in TEXTURES:
        found = [m for m in model['materials'] if m['texture']['name']==name]
        if len(found)!=1:
            raise ArtError('Unexpected finish material state')
        m=found[0];side=name==TEXTURES[0]
        if (m['geometry']!=GEOMETRY or m['combine']!=[0xfcffffff,0xfffcf279] or
            m['scale']!=[65535,65535] or m['tile']!=0 or not m['enabled'] or
            m['other_l']!=OPA_SURF or m['other_h']!=0x82c00 or
            m['texture']['image']!=0xfd100000 or
            m['tiles'].get('0')!=([0xf5100400,0x4c130] if side else [0xf5100800,0x90240])):
            raise ArtError('Unsupported finish shading or sampler')
        materials.append(m)
    return dict(vertices=scaled,triangles=triangles,materials=materials,
                source_vertices=provenance,source_order=True,
                source_loaded_rows=256,independent_resolved_rows=len(model['vertices']))


def export(rom, out):
    output_location(out)
    _, provenance = extract_rom_course(rom)
    normalized,_ = normalize_rom(rom)
    verify_source(DECOMP)
    art=SourceArt(normalized)
    data=art.segments['hud_gfx']
    rows={n:r for n,r in art.rows.items() if r['family']=='hud_gfx'}
    lists={n:recipe(data,rows,n) for n in LISTS}
    def binding(address,kind):
        found=[(n,r) for n,r in rows.items() if r['offset']==address&0xffffff and r['type']==kind]
        if address>>24!=4 or len(found)!=1:
            raise ArtError('Unresolved finish asset address')
        return found[0]
    def vertex_read(address,count):
        name,row=binding(address,'VTX')
        if row['count']!=count:
            raise ArtError('Finish source vertex span mismatch')
        values=list(struct.iter_unpack('>3hH2h4B',bounded(data,row['offset'],count*16)))
        if any(v[3] for v in values):
            raise ArtError('Nonzero finish vertex flag')
        return [list(v[:3]+v[4:]) for v in values],name
    model=geometry(lists,vertex_read,lambda a:binding(a,'TEXTURE')[0])
    outputs,textures={},[]
    for name,extent in zip(TEXTURES,(8,16)):
        tex=art.texture(name)
        if (tex['format'],tex['width'],tex['height'])!=('RGBA16',extent,extent):
            raise ArtError('Finish texture format changed')
        if set(tex['rgba'][3::4])!={255}:
            raise ArtError('Finish texture must be fully opaque')
        pixels,stride=rgb1555(tex['rgba'],extent,extent)
        colors=set(struct.unpack('>'+'H'*(extent*extent),pixels))
        if len(colors)!=(1 if extent==8 else 13):
            raise ArtError('Finish palette coverage changed')
        filename=name+'.rgb1555';outputs[filename]=pixels
        textures.append(dict(symbol=name,width=extent,height=extent,stride=stride,
                             source_sha256=tex['source_sha256'],file=filename,colors=len(colors)))
    projection=dict(source='src/overlays/ovl_i3/C2160.c:func_i3_8012EE90',
                    fov_degrees=45,near=16,far=4096,source_width=320,source_height=240,
                    frustum_center=[0,0],eye=[0,0,0],at=[0,0,1000],up=[0,1,0],
                    model_scale=[0.07,0.07,0.07],single_player_only=True,
                    note='Use source matrices/viewport; separate screen-space sign camera, not the race camera')
    manifest=dict(schema=1,rom_sha256=provenance['normalized_rom_sha256'],model=model,
                  textures=textures,projection=projection,
                  adaptations=['Native RGB1555 channel order; all opaque source RGB5 colors retained'],
                  pending=['Bounded native triangle charts','Native projection and local image review'])
    outputs['finish.json']=(json.dumps(manifest,indent=2)+'\n').encode()
    deps=[Path(__file__),ROOT/'tools/countdown_assets.py',ROOT/'tools/road_assets.py',
          ROOT/'tools/import_source_art.py',ROOT/'tools/import_course.py']
    deps += [DECOMP/p for p in art.inputs]
    deps += [DECOMP/p for p in ('src/overlays/ovl_i3/menus.c','src/overlays/ovl_i3/C2160.c',
                               'src/sys/math.c','include/PR/gbi.h')]
    receipt=['# fzx generation receipt v1; regenerate, never edit']
    receipt += ['generator %s %s'%(hashlib.sha256(p.read_bytes()).hexdigest(),os.path.relpath(p,ROOT/'tools').replace('\\','/')) for p in dict.fromkeys(deps)]
    receipt.append('input %s owner-rom'%hashlib.sha256(rom).hexdigest())
    out.mkdir(parents=True,exist_ok=True)
    for name,value in outputs.items():
        (out/name).write_bytes(value)
        receipt.append('output %s %s'%(hashlib.sha256(value).hexdigest(),name))
    (out/'finish.receipt').write_text('\n'.join(receipt)+'\n',newline='\n')
    return dict(vertices=256,triangles=222,side_triangles=124,face_triangles=98,
                materials=2,source_texture_bytes=640,source_order=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'generated/finish')
    args=parser.parse_args();path=os.environ.get('FZX_TEST_ROM')
    if not path:parser.error('Set FZX_TEST_ROM privately; never put its path in logs')
    try:print(json.dumps(export(Path(path).read_bytes(),args.output),sort_keys=True))
    except (OSError,ValueError,KeyError) as exc:
        print('finish extraction failed: '+(str(exc) if isinstance(exc,ArtError) else type(exc).__name__),file=sys.stderr)
        raise SystemExit(1)
