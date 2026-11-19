#!/usr/bin/env python3
"""Offline rotation sprite set baker for race machines (RSP1, version 1).

Renders one machine (any character/skin by config) from a grid of viewing
directions around its own up axis and packs the images as an SH2ZAM rotation
sprite set (sh2zam/rotation_sprite_experimental.h, contract rotation-sprites-1):
4-bit indexed cells sharing one 16-entry CLUT (index 0 transparent).

Runs only on privately extracted source geometry/textures (FZX_TEST_ROM or
--config). Uses the Records preview rasterizer (records_models.raster): pixel-
centre coverage, perspective source-UV nearest texels, interpolated directional
lighting. Lighting is fixed relative to the viewer so neighbouring cells match.
No retail data is checked in. Interface: generated/SPRITE-RACERS-PLAN.md.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import os
import struct
import sys

from import_source_art import ROOT, DECOMP, ArtError, output_location, verify_source
from machine_menu_models import census
from records_models import raster
from road_pack import fnv

VERSION = 1
MAGIC = 0x52535031
MAX_YAW, MAX_PITCH = 32, 4
HEADER = 24 + MAX_YAW*4 + MAX_PITCH*4  # sizeof(sh2z_rspr_set_t) = 168
CELL = 16
DEFAULT_VIEWS = dict(
    character=0, skin=0, mirror='auto', yaw_bins=16, yaw_dense_deg=0, pitch_deg=[12.0],
    distance_radii=4.0, budget_bytes=18664, cell_limit=120,
    light=dict(ambient=[96, 96, 104], color=[176, 176, 168], view_dir=[-0.35, 0.75, 0.55]))


def sha(data):
    return hashlib.sha256(data).hexdigest()


def norm(v):
    length = math.sqrt(sum(c*c for c in v))
    if length <= 0:
        raise ArtError('Zero vector')
    return [c/length for c in v]


def cross3(a, b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def yaw_angles(views, mirror):
    n = views['yaw_bins']
    if not 1 <= n <= MAX_YAW:
        raise ArtError('yaw_bins out of range')
    if mirror:  # centres spread over 0..180 inclusive
        return [180.0*k/(n-1) if n > 1 else 0.0 for k in range(n)]
    return [-180.0+360.0*k/n for k in range(n)]


def capture_for(yaw, pitch, distance, projection, light):
    """Records-style capture: identity model, look-at view, viewer-fixed light."""
    a, b = math.radians(yaw), math.radians(pitch)
    eye = [distance*math.cos(b)*math.sin(a), distance*math.sin(b), distance*math.cos(b)*math.cos(a)]
    back = norm(eye)
    right = norm(cross3([0.0, 1.0, 0.0], back))
    up = cross3(back, right)
    view = [0.0]*16
    for j, axis in enumerate((right, up, back)):
        for k in range(3):
            view[k*4+j] = axis[k]
        view[12+j] = -sum(e*c for e, c in zip(eye, axis))
    view[15] = 1.0
    model = [1.0 if i in (0, 5, 10, 15) else 0.0 for i in range(16)]
    d = light['view_dir']
    world_dir = [right[i]*d[0]+up[i]*d[1]+back[i]*d[2] for i in range(3)]
    return dict(model=model, view=view, projection=[projection, projection],
                light=list(light['ambient'])+list(light['color'])+world_dir, viewport_scale=[160, 120])


def mirror_symmetric(vertices, tolerance=1.5):
    points = {(round(v['position'][0]), round(v['position'][1]), round(v['position'][2])) for v in vertices}
    hits = sum(1 for (x, y, z) in points if any(abs(-x-p[0]) <= tolerance and abs(y-p[1]) <= tolerance and abs(z-p[2]) <= tolerance for p in points))
    return hits >= 0.95*len(points)


def crop(sprite):
    w, h, px = sprite['width'], sprite['height'], sprite['pixels']
    occupied = [(i % w, i//w) for i, p in enumerate(px) if p]
    if not occupied:
        raise ArtError('Empty racer sprite cell')
    x0 = min(p[0] for p in occupied); x1 = max(p[0] for p in occupied)+1
    y0 = min(p[1] for p in occupied); y1 = max(p[1] for p in occupied)+1
    width = (x1-x0+7)//8*8; height = y1-y0
    pixels = [px[(y+y0)*w+x+x0] if x < x1-x0 else 0 for y in range(height) for x in range(width)]
    # origin: the model origin projects to viewport pixel (0, 0).
    return dict(width=width, height=height, origin_x=-(sprite['x']+x0), origin_y=-(sprite['y']+y0), pixels=pixels)


def rgb(v):
    return [(v >> (5*i)) & 31 for i in range(3)]


def quantize(cells, colors=15, rounds=12):
    """Shared palette: k-means over every opaque RGB555 pixel (deterministic)."""
    counts = {}
    for c in cells:
        for p in c['pixels']:
            if p:
                counts[p] = counts.get(p, 0)+1
    if not counts:
        raise ArtError('No opaque racer pixels')
    keys = sorted(counts, key=lambda k: (-counts[k], k))
    centres = [rgb(k) for k in keys[:colors]]
    while len(centres) < colors:
        centres.append(centres[-1])
    for _ in range(rounds):
        sums = [[0, 0, 0, 0] for _ in centres]
        for k, n in counts.items():
            c = rgb(k)
            best = min(range(len(centres)), key=lambda i: (sum((c[j]-centres[i][j])**2 for j in range(3)), i))
            for j in range(3):
                sums[best][j] += c[j]*n
            sums[best][3] += n
        centres = [[s[j]/s[3] for j in range(3)] if s[3] else centres[i] for i, s in enumerate(sums)]
    palette = [0x8000 | sum(int(round(c[j])) << (5*j) for j in range(3)) for c in centres]
    mapping = {}
    for k in counts:
        c = rgb(k)
        mapping[k] = 1+min(range(len(centres)), key=lambda i: (sum((c[j]-centres[i][j])**2 for j in range(3)), i))
    return [0]+palette, mapping


def pack(views, cells, palette, mapping, mirror, yaws, pitches, ppu):
    n_cells = len(cells)
    head = HEADER+n_cells*CELL
    clut_offset = (head+7)//8*8
    pixel_at = (clut_offset+32+7)//8*8
    blobs = []
    descriptors = b''
    for c in cells:
        data = bytes((mapping.get(c['pixels'][i], 0) << 4) | mapping.get(c['pixels'][i+1], 0)
                     for i in range(0, len(c['pixels']), 2))
        descriptors += struct.pack('>HHhhIHH', c['width'], c['height'], c['origin_x'], c['origin_y'], pixel_at, 0, 0)
        blobs.append(data)
        pixel_at += (len(data)+7)//8*8
    total = pixel_at
    yaw_words = []
    for k in range(MAX_YAW):
        if k < len(yaws):
            a = math.radians(yaws[k]); yaw_words += [round(16384*math.sin(a)), round(16384*math.cos(a))]
        else:
            yaw_words += [0, 0]
    pitch_words = []
    for k in range(MAX_PITCH):
        if k < len(pitches):
            b = math.radians(pitches[k]); pitch_words += [round(16384*math.sin(b)), round(16384*math.cos(b))]
        else:
            pitch_words += [0, 0]
    header = struct.pack('>IIHHHHIi', MAGIC, total, len(yaws), len(pitches), 1 if mirror else 0, 1,
                         clut_offset, int(round(ppu*65536)))
    header += struct.pack('>%dh' % (2*MAX_YAW), *yaw_words)+struct.pack('>%dh' % (2*MAX_PITCH), *pitch_words)
    if len(header) != HEADER:
        raise ArtError('RSP1 header layout')
    data = bytearray(header+descriptors)
    data += b'\0'*(clut_offset-len(data))
    data += struct.pack('>16H', *palette)
    for blob in blobs:
        data += b'\0'*((-len(data)) % 8)
        data += blob
    data += b'\0'*(total-len(data))
    if len(data) != total:
        raise ArtError('RSP1 extent')
    return bytes(data)


def bake(machine, textures, views):
    vertices = machine['geometry']['vertices']
    radius = max(math.sqrt(sum(c*c for c in v['position'])) for v in vertices)
    if not 0 < radius < 2000:
        raise ArtError('Racer model radius')
    mirror = views['mirror']
    if mirror == 'auto':
        mirror = mirror_symmetric(vertices)
    yaws = yaw_angles(views, mirror)
    pitches = list(views['pitch_deg'])
    if not 1 <= len(pitches) <= MAX_PITCH:
        raise ArtError('pitch_deg count')
    distance = views['distance_radii']*radius
    if not 16+radius < distance < 8192-radius:
        raise ArtError('Bake distance outside source near/far')

    def render(ppu):
        projection = ppu*distance/160.0
        out = []
        for p in pitches:
            for y in yaws:
                s = raster(machine, textures, capture_for(y, p, distance, projection, views['light']), views['skin'])
                out.append(crop(s))
        return out

    # Largest pixels-per-unit whose packed set fits the budget (bisection).
    lo, hi, best = 0.02, 2.0, None
    for _ in range(14):
        mid = (lo+hi)/2
        try:
            cells = render(mid)
            if max(max(c['width'], c['height']) for c in cells) > views['cell_limit']:
                raise ArtError('cell too large')
            size = HEADER+len(cells)*CELL+64+sum((c['width']*c['height']//2+7)//8*8 for c in cells)
            ok = size <= views['budget_bytes']
        except ArtError:
            ok = False
        if ok:
            best, lo = (mid, cells), mid
        else:
            hi = mid
    if best is None:
        raise ArtError('No pixels-per-unit fits the racer sprite budget')
    ppu, cells = best
    palette, mapping = quantize(cells)
    data = pack(views, cells, palette, mapping, mirror, yaws, pitches, ppu)
    if len(data) > views['budget_bytes']:
        raise ArtError('RSP1 over budget')
    manifest = dict(format='RSP1', version=VERSION, character=machine['character'], skin=views['skin'],
                    mirror=bool(mirror), yaw_deg=yaws, pitch_deg=pitches, distance_units=distance,
                    model_radius_units=radius, pixels_per_unit=ppu, bytes=len(data),
                    header_bytes=HEADER, cell_table_bytes=len(cells)*CELL, clut_bytes=32,
                    pixel_bytes=sum((c['width']*c['height']//2+7)//8*8 for c in cells),
                    cells=[dict(index=i, pitch=pitches[i//len(yaws)], yaw=yaws[i % len(yaws)], width=c['width'],
                                height=c['height'], origin_x=c['origin_x'], origin_y=c['origin_y']) for i, c in enumerate(cells)],
                    palette=['%04x' % v for v in palette])
    return data, manifest, cells, palette, mapping


def review(cells, palette, mapping, output):
    from PIL import Image
    cols = 8
    rows = (len(cells)+cols-1)//cols
    sheet = Image.new('RGB', (cols*128, rows*128), (40, 40, 48))
    for i, c in enumerate(cells):
        img = Image.new('RGB', (c['width'], c['height']), (40, 40, 48))
        img.putdata([tuple(v*255//31 for v in rgb(palette[mapping[p]])) if p else (40, 40, 48) for p in c['pixels']])
        sheet.paste(img.resize((c['width']*2, c['height']*2), Image.NEAREST), ((i % cols)*128, (i//cols)*128))
    path = output/'racer-sprites-review.png'
    sheet.save(path)
    return path


def export(rom, output, views):
    output = output.resolve(); output_location(output); verify_source(DECOMP); output.mkdir(parents=True, exist_ok=True)
    machines, textures, source = census(rom)
    matches = [m for m in machines if m['character'] == views['character']]
    if len(matches) != 1 or not 0 <= views['skin'] < len(matches[0]['skins']):
        raise ArtError('Unknown racer character/skin')
    data, manifest, cells, palette, mapping = bake(matches[0], textures, views)
    manifest.update(views=views, source=dict(rom_sha256=source['rom_sha256']), pack_sha256=sha(data), fnv=fnv(data))
    image = review(cells, palette, mapping, output)
    header = ('/* Generated by tools/racer_sprites.py; regenerate, never edit. */\n'
              '#ifndef FZX_RACER_SPRITES_IDENTITY_H\n#define FZX_RACER_SPRITES_IDENTITY_H\n'
              '#define FZX_RACER_SPRITE_BYTES %du\n#define FZX_RACER_SPRITE_HASH 0x%08Xu\n'
              '#define FZX_RACER_SPRITE_VERSION %du\n#define FZX_RACER_SPRITE_RADIUS %.6ff\n#endif\n'
              % (len(data), fnv(data), VERSION, manifest['model_radius_units'])).encode()
    outputs = {'RSPRITE.PAK': data, 'racer_sprites.json': (json.dumps(manifest, indent=1)+'\n').encode(),
               'racer_sprites_identity.h': header, image.name: image.read_bytes()}
    deps = [Path(__file__), ROOT/'tools/records_models.py', ROOT/'tools/machine_menu_models.py', ROOT/'tools/import_source_art.py',
            ROOT/'tools/road_pack.py']
    receipt = ['# fzx generation receipt v1; regenerate, never edit']
    receipt += ['generator %s %s' % (sha(p.read_bytes()), os.path.relpath(p, ROOT/'tools').replace('\\', '/')) for p in deps]
    receipt += ['input %s owner-rom' % sha(rom), 'input %s views' % sha(json.dumps(views, sort_keys=True).encode())]
    for name, content in outputs.items():
        (output/name).write_bytes(content)
        receipt.append('output %s %s' % (sha(content), name))
    (output/'racer_sprites.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--output', type=Path, default=ROOT/'generated/racer_sprites')
    parser.add_argument('--config', type=Path, help='JSON with {"rom": path}; else FZX_TEST_ROM')
    parser.add_argument('--views', type=Path, help='JSON view set; defaults to the player machine set')
    args = parser.parse_args()
    views = dict(DEFAULT_VIEWS)
    if args.views:
        views.update(json.loads(args.views.read_text()))
    rom_path = json.loads(args.config.read_text())['rom'] if args.config else os.environ.get('FZX_TEST_ROM')
    if not rom_path:
        parser.error('Set FZX_TEST_ROM privately or pass --config')
    if os.name != 'nt' and len(rom_path) > 2 and rom_path[1] == ':' and rom_path[2] in ('/', '\\'):
        rom_path = '/mnt/'+rom_path[0].lower()+'/'+rom_path[3:].replace('\\', '/')
    try:
        m = export(Path(rom_path).read_bytes(), args.output, views)
        print(json.dumps({k: m[k] for k in ('character', 'skin', 'mirror', 'bytes', 'pixels_per_unit', 'model_radius_units')}))
    except Exception as exc:
        print('Racer sprite bake failed: '+(str(exc) if isinstance(exc, ArtError) else type(exc).__name__+': '+str(exc)), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
