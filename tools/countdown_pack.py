#!/usr/bin/env python3
"""Bake the original countdown's planar quads into resident Saturn charts.

This bounded offline converter retains both authored triangles and their UV
mapping. It is not a display-list interpreter or a runtime rasterizer.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import os
import struct
import subprocess

from import_source_art import ROOT, ArtError, output_location
from road_pack import fnv

HEADER = 64
VERTEX = 16
QUAD = 16
IMAGE = 16
MAX_TEXTURE_BYTES = 96 * 1024


def cross(a, b, c):
    return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])


def chart_geometry(vertices):
    """Choose an exact planar basis and reject non-convex/degenerate quads."""
    a, b, c, d = [v[:3] for v in vertices]
    ab = [b[i]-a[i] for i in range(3)]
    ac = [c[i]-a[i] for i in range(3)]
    n = [ab[(i+1)%3]*ac[(i+2)%3]-ab[(i+2)%3]*ac[(i+1)%3] for i in range(3)]
    if not any(n) or sum(n[i]*(d[i]-a[i]) for i in range(3)):
        raise ArtError('Countdown quad is degenerate or non-planar')
    drop = max(range(3), key=lambda i: abs(n[i]))
    points = [[v[i] for i in range(3) if i != drop] for v in vertices]
    turns = [cross(points[i], points[(i+1)%4], points[(i+2)%4]) for i in range(4)]
    if not (all(t > 0 for t in turns) or all(t < 0 for t in turns)):
        raise ArtError('Countdown quad is not strictly convex')
    return points


def chart_size(vertices):
    # One source texel per chart step, rounded to VDP1's width multiple of 8.
    # Integer ceil(sqrt()) keeps Windows/Linux generation byte-identical.
    def length(a, b):
        d = sum((vertices[a][i]-vertices[b][i])**2 for i in (3, 4))
        root = math.isqrt(d)
        return root + (root*root != d)
    def extent(n, multiple):
        count = (n+31)//32+1
        return max(multiple, (count+multiple-1)//multiple*multiple)
    width = extent(max(length(0, 1), length(3, 2)), 8)
    height = extent(max(length(0, 3), length(1, 2)), 2)
    if width > 128 or height > 128:
        raise ArtError('Countdown chart exceeds bounded dimensions')
    return width, height


def uv_at(vertices, points, x, y, width, height):
    """Original piecewise-affine UV at the quad's bilinear chart position.

    All coordinates are rational integers. The returned UV numerators share
    their positive denominator, in source Q5 texel units.
    """
    w, h = width-1, height-1
    weights = [(w-x)*(h-y), x*(h-y), x*y, (w-x)*y]
    denominator = w*h
    point = [sum(weights[k]*points[k][i] for k in range(4)) for i in range(2)]
    for ids in ((0, 1, 2), (0, 2, 3)):
        a, b, c = [[p*denominator for p in points[k]] for k in ids]
        area = cross(a, b, c)
        bary = [cross(point, b, c), cross(a, point, c), cross(a, b, point)]
        if area < 0:
            area, bary = -area, [-v for v in bary]
        if area and min(bary) >= 0:
            return [sum(bary[j]*vertices[k][i] for j, k in enumerate(ids)) for i in (3, 4)], area
    raise ArtError('Countdown chart point is outside its original triangles')


def mirror(index, extent):
    index %= 2*extent
    return index if index < extent else 2*extent-1-index


def bake_chart(vertices, texture, pixels):
    points = chart_geometry(vertices)
    width, height = chart_size(vertices)
    tw, th = texture['width'], texture['height']
    if (tw, th) not in ((32, 32), (64, 32)) or len(pixels) != tw*th*2:
        raise ArtError('Invalid countdown source texture extent')
    result = bytearray()
    for y in range(height):
        for x in range(width):
            uv, den = uv_at(vertices, points, x, y, width, height)
            # Source nearest texel with mirror-wrap in both axes. Point
            # sampling replaces source three-point filtering explicitly.
            u, v = [mirror((n+16*den)//(32*den), extent) for n, extent in zip(uv, (tw, th))]
            at = (v*tw+u)*2
            result += pixels[at:at+2]
    return width, height, bytes(result)


def build(manifest, files):
    model = manifest['model']
    vertices, quads, materials = model['vertices'], model['quads'], model['materials']
    textures = manifest['textures']
    if manifest['schema'] != 1 or len(vertices) != 92 or len(quads) != 23 or len(materials) != 5 or len(textures) != 9:
        raise ArtError('Countdown source coverage changed')
    if model['sections'] != dict(frame=[0, 20], mouth=[20, 2], digit=[22, 1]):
        raise ArtError('Countdown source sections changed')
    names = {t['symbol']: i for i, t in enumerate(textures)}
    if len(names) != 9:
        raise ArtError('Duplicate countdown texture')
    for material in materials:
        # The original setup clears all geometry flags, then selects back-face
        # culling and direct texture color/alpha. It does NOT enable lighting.
        if material['geometry'] != 0x800405 or material['combine'] != [0xfcffffff, 0xfffcf279]:
            raise ArtError('Unsupported countdown material or lighting')
        if material['tile'] != 0 or material['scale'] != [65535, 65535]:
            raise ArtError('Unsupported countdown texture scale')
        a, b = material['tiles']['0']
        if a >> 24 != 0xf5 or (b & 0xffffff) not in (0x54150, 0x54160):
            raise ArtError('Unsupported countdown mirror sampler')
    data = bytearray(HEADER)
    data[:8] = b'FZXCDN01'
    va = len(data)
    for v in vertices:
        if len(v) != 9 or any(not -32768 <= q <= 32767 for q in v[:5]) or any(not 0 <= q <= 255 for q in v[5:]):
            raise ArtError('Invalid countdown source vertex')
        data += struct.pack('>5h4B2x', *v)
    qa = len(data)
    data += bytes(len(quads)*QUAD)
    ia = len(data)
    images, bank, records = [], bytearray(), []
    for qi, q in enumerate(quads):
        kind = 0 if qi < 20 else 1 if qi < 22 else 2
        if len(q) != 5 or len(set(q[:4])) != 4 or any(not 0 <= k < len(vertices) for k in q[:4]) or not 0 <= q[4] < 5:
            raise ArtError('Invalid countdown quad reference')
        if kind == 0 and q[4] >= 3:
            raise ArtError('Countdown frame uses a dynamic panel')
        if kind and q[4] != kind+2:
            raise ArtError('Countdown dynamic material mismatch')
        tris = model['triangles'][qi*2:qi*2+2]
        if tris != [[q[0], q[1], q[2], q[4]], [q[0], q[2], q[3], q[4]]]:
            raise ArtError('Countdown source triangle coverage changed')
        symbols = ([materials[q[4]]['texture']['name']] if kind == 0 else
                   manifest['mouth_textures'] if kind == 1 else manifest['digit_textures'])
        if len(symbols) != (1, 2, 4)[kind] or any(s not in names for s in symbols):
            raise ArtError('Invalid countdown panel states')
        first = len(images)
        for state, symbol in enumerate(symbols):
            ti = names[symbol]
            texture = textures[ti]
            w, h, pixels = bake_chart([vertices[k] for k in q[:4]], texture, files[texture['file']])
            offset = len(bank)
            bank += pixels
            images.append(struct.pack('>2HI4H', w, h, offset, qi, state, ti, 0))
            records.append(dict(quad=qi, state=state, texture=ti, width=w, height=h, offset=offset))
        struct.pack_into('>8H', data, qa+qi*QUAD, *q[:4], first, len(symbols), kind, qi)
    data += b''.join(images)
    if len(bank) > MAX_TEXTURE_BYTES:
        raise ArtError('Countdown texture budget exceeded')
    struct.pack_into('>10I', data, 8, len(data), len(bank), fnv(bank), len(vertices), len(quads), len(images), va, qa, ia, 0)
    struct.pack_into('>I', data, 44, fnv(data))
    report = dict(metadata_bytes=len(data), texture_bytes=len(bank), vertices=len(vertices),
                  quads=len(quads), triangles=len(model['triangles']), images=records,
                  lighting=False, source_material='direct texture color and alpha; back-face culling',
                  adaptations=['Original triangle UVs baked into planar quad charts offline',
                               'Nearest texel sampling replaces source three-point filtering',
                               'Original RGB5 channels and binary transparency retained'],
                  native_residency='UNRUN', native_rendering='UNRUN')
    return bytes(data), bytes(bank), report


def generate(source, output):
    output_location(output)
    subprocess.run(['bash', str(ROOT/'tools/check_receipt.sh'), str(source/'countdown.receipt')], check=True)
    manifest = json.loads((source/'countdown.json').read_text())
    inputs = [source/t['file'] for t in manifest['textures']]
    metadata, textures, report = build(manifest, {p.name: p.read_bytes() for p in inputs})
    output.mkdir(parents=True, exist_ok=True)
    outputs = {'COUNTDN.PAK': metadata, 'COUNTDTX.PAK': textures,
               'countdown_pack.json': (json.dumps(report, indent=2)+'\n').encode()}
    header = '/* Generated countdown pack identity; regenerate. */\n'
    for name, value in (('BYTES', len(metadata)), ('TEXTURE_BYTES', len(textures)),
                        ('HASH', fnv(metadata)), ('TEXTURE_HASH', fnv(textures))):
        header += '#define FZX_COUNTDOWN_%s %du\n' % (name, value)
    outputs['countdown_identity.h'] = header.encode()
    dependencies = [Path(__file__), ROOT/'tools/import_source_art.py', ROOT/'tools/road_pack.py',
                    source/'countdown.receipt', source/'countdown.json', *inputs]
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    receipt = ['# fzx generation receipt v1; regenerate, never edit']
    receipt += ['generator %s %s' % (digest(p), os.path.relpath(p, ROOT/'tools').replace('\\', '/')) for p in dependencies]
    for name, value in outputs.items():
        (output/name).write_bytes(value)
        receipt.append('output %s %s' % (hashlib.sha256(value).hexdigest(), name))
    (output/'countdown_pack.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    print(json.dumps({k: v for k, v in report.items() if k != 'images'}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT/'generated/countdown')
    parser.add_argument('--output', type=Path, default=ROOT/'generated/countdown_pack')
    args = parser.parse_args()
    generate(args.source, args.output)
