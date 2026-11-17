#!/usr/bin/env python3
"""Bake original road rows, repetition metadata and periodic Saturn textures.

Private source data only. No pre-split positions and no runtime integration.
"""
from collections import Counter
from fractions import Fraction
from pathlib import Path
import argparse
import hashlib
import json
import math
import os
import struct
import subprocess

from import_source_art import ArtError, ROOT, output_location
from road_lod_census import prepare, far_chains, pieces
from road_tiles import strip
from road_pack import fnv

PACK_LIMIT = 160000
TEXTURE_LIMIT = 150000
HEADER_BYTES = 160
STRIDES = (10, 16, 4, 8, 16, 32, 16, 16)


def round_ratio(n, d):
    """Nearest integer, ties to even, without host floating-point dependence."""
    q, r = divmod(n, d)
    return q + int(2*r > d or (2*r == d and q % 2))


def rgb(word):
    return tuple((word >> (5*c)) & 31 for c in range(3))


def word(color):
    if len(color) != 3 or any(not 0 <= c <= 31 for c in color):
        raise ArtError('Invalid RGB1555 colour')
    return 0x8000 | sum(c << (5*i) for i, c in enumerate(color))


def periodic_tier(base, width, height, repetitions):
    if len(base) != width*height or repetitions not in (1, 2, 4, 8) or height % repetitions:
        raise ArtError('Invalid periodic road tier')
    if repetitions == 1:
        return list(base)
    rows = [[rgb(base[y*width+x]) for x in range(width)] for y in range(height)]
    return [word(tuple(round_ratio(sum(rows[(y*repetitions+j) % height][x][c]
                                      for j in range(repetitions)), repetitions) for c in range(3)))
            for y in range(height) for x in range(width)]


def quantize16(pixels):
    """Deterministic weighted median cuts, followed by bounded Lloyd refinement."""
    if not pixels or any(not p & 0x8000 for p in pixels):
        raise ArtError('Road lookup images must contain opaque RGB1555 pixels')
    histogram = Counter(rgb(p) for p in pixels)
    boxes = [sorted(histogram)]
    def center(colors):
        count = sum(histogram[c] for c in colors)
        return tuple(round_ratio(sum(c[k]*histogram[c] for c in colors), count) for k in range(3))
    def variance(colors):
        count = sum(histogram[c] for c in colors)
        return [count*sum(c[k]*c[k]*histogram[c] for c in colors)-sum(c[k]*histogram[c] for c in colors)**2 for k in range(3)]
    while len(boxes) < min(16, len(histogram)):
        # Score is total squared error, expressed as an exact rational.
        candidates = [(i, b) for i, b in enumerate(boxes) if len(b) > 1]
        index, colors = max(candidates, key=lambda ib: (Fraction(sum(variance(ib[1])), sum(histogram[c] for c in ib[1])), -ib[0]))
        axis = max(range(3), key=lambda k: (variance(colors)[k], -k))
        colors = sorted(colors, key=lambda c: (c[axis], c))
        total = sum(histogram[c] for c in colors)
        cumulative = 0
        split = 1
        for i, color in enumerate(colors[:-1]):
            cumulative += histogram[color]
            split = i+1
            if cumulative*2 >= total:
                break
        boxes[index:index+1] = [colors[:split], colors[split:]]
    palette = [center(b) for b in boxes]
    def closest(c):
        return min(range(len(palette)), key=lambda i: (sum((c[k]-palette[i][k])**2 for k in range(3)), i))
    for _ in range(8):
        groups = [[] for _ in palette]
        for c in histogram:
            groups[closest(c)].append(c)
        next_palette = [center(g) if g else old for g, old in zip(groups, palette)]
        if next_palette == palette:
            break
        palette = next_palette
    palette = sorted(set(palette))
    mapping = {c: closest(c) for c in histogram}
    errors = Counter()
    squared = 0
    for c, count in histogram.items():
        delta = [abs(c[k]-palette[mapping[c]][k]) for k in range(3)]
        errors[max(delta)] += count
        squared += sum(d*d for d in delta)*count
    rank, cumulative, p99 = math.ceil(len(pixels)*.99), 0, 0
    for error, count in sorted(errors.items()):
        cumulative += count
        if cumulative >= rank:
            p99 = error
            break
    indices = [mapping[rgb(p)] for p in pixels]
    palette_words = [word(c) for c in palette]
    palette_words += [palette_words[-1]]*(16-len(palette_words))
    return indices, palette_words, dict(unique_colors=len(histogram), palette_colors=len(palette),
                                       max_channel_error=max(errors), p99_channel_error=p99,
                                       channel_mse=squared/(3*len(pixels)), error_histogram=dict(sorted(errors.items())))


def fog_palette(palette, fog, fade):
    return [word(tuple(round_ratio(c*255*(255-fade)+fog[k]*31*fade, 65025)
                       for k, c in enumerate(rgb(p)))) for p in palette]


class TexturePool:
    def __init__(self):
        self.data = bytearray(64)
        self.offsets = {}
        self.unshared_bytes = 64

    def add(self, data):
        data = bytes(data)+bytes(-len(data) % 8)
        self.unshared_bytes += len(data)
        if data not in self.offsets:
            self.offsets[data] = len(self.data)
            self.data.extend(data)
        return self.offsets[data]


def words_bytes(pixels):
    return struct.pack('>%dH' % len(pixels), *pixels)


def texture_images(mesh, strips, source_sha, fog):
    pool = TexturePool()
    records, strip_records, reports = bytearray(), bytearray(), []
    for number, s in enumerate(strips):
        data, _ = strip(mesh, s['material'], s['u0'], s['u1'], fog)
        original = struct.unpack('>%dH' % (len(data)//2), data)
        width, height = s['width'], s['period']
        count, stride = width*height, width*(height+1)
        banks = [original[v*stride:(v+1)*stride] for v in range(3)]
        strip_records += struct.pack('>4H2h2H', s['material'], width, height, 0,
                                     s['u0'], s['u1'], number*5, 5)
        for repetitions in (1, 2, 4, 8, 0):
            images = [periodic_tier(bank[:count], width, height, repetitions)
                      if repetitions else list(bank[count:]) for bank in banks]
            indices, palette, quant = quantize16(images[0])
            palettes = [fog_palette(palette, fog, fade) for fade in (0, 128, 255)]
            errors = [max(max(abs(a-b) for a, b in zip(rgb(p), rgb(pal[i])))
                          for p, i in zip(image, indices)) for image, pal in zip(images, palettes)]
            # This is a fixed quality bound, not a knob adjusted to fit memory.
            # More complex images remain source-derived RGB1555 in all fog banks.
            lookup = max(errors) <= 1
            if lookup:
                packed = bytes((indices[i] << 4) | indices[i+1] for i in range(0, len(indices), 2))
                offsets = [pool.add(packed)]*3
                palette_offsets = [pool.add(words_bytes(p)) for p in palettes]
            else:
                offsets = [pool.add(words_bytes(image)) for image in images]
                palette_offsets = [0, 0, 0]
            records += struct.pack('>4H6I', int(lookup), width, height if repetitions else 1,
                                   repetitions, *offsets, *palette_offsets)
            reports.append(dict(strip=number, repetitions=repetitions, mode='CLUT4' if lookup else 'RGB1555',
                                source_colors=quant['unique_colors'], quantizer=quant,
                                stored_max_channel_error=max(errors) if lookup else 0,
                                fog_variant_max_errors=errors if lookup else [0, 0, 0]))
    pool.data[:8] = b'FZXRTX01'
    struct.pack_into('>4I', pool.data, 8, 1, len(pool.data), 0, 1)
    pool.data[24:56] = source_sha
    struct.pack_into('>2I', pool.data, 56, len(reports), 64)
    struct.pack_into('>I', pool.data, 16, fnv(pool.data))
    return bytes(strip_records), bytes(records), bytes(pool.data), reports, pool.unshared_bytes


def repeat_record(q, vertices):
    if q['primitive']:
        return 0
    unit, lo, hi = q['unit'], q['lo'], q['hi']
    first, end, count = lo % unit, hi % unit, pieces(lo, hi, unit)
    if unit > 1024 or first > 1023 or end > 1023 or not 1 <= count <= 63:
        raise ArtError('Repetition record exceeds its 4-byte contract')
    a, b, c, d = q['edges']
    p = [vertices[i] for i in q['vertices']]
    uneven = p[a][4] != p[c][4] or p[b][4] != p[d][4]
    return first | (count << 10) | (end << 16) | (int(uneven) << 26)


def decode_repeat(value, unit):
    if value >> 27 or not 1 <= unit <= 1024:
        raise ArtError('Invalid repetition flags or period')
    first, count, end = value & 1023, (value >> 10) & 63, (value >> 16) & 1023
    if first >= unit or end >= unit or not count:
        raise ArtError('Invalid repetition endpoints/count')
    span = (count-1)*unit + (end or unit)-first
    if span <= 0:
        raise ArtError('Empty repetition span')
    return first, count, end, span, bool(value & (1 << 26))


def far_records(mesh, quads, length):
    vertices = mesh['vertices']
    result = bytearray()
    errors = []
    rejected = 0
    for chain in far_chains(mesh, quads, length):
        rows = []
        first = quads[chain[0]]
        a, b, c, d = first['edges']
        rows.append((first['vertices'][a], first['vertices'][c]))
        for qi in chain:
            q = quads[qi]
            a, b, c, d = q['edges']
            if any(vertices[old][:3] != vertices[new][:3]
                   for old, new in zip(rows[-1], (q['vertices'][a], q['vertices'][c]))):
                break
            rows.append((q['vertices'][b], q['vertices'][d]))
        if len(rows) != length+1:
            rejected += 1
            continue
        # Conservative L1 chord deviation, rounded upward in source units.
        # Stored for C3 admission; baking a candidate does not activate a merge.
        error = 0
        for j, row in enumerate(rows[1:-1], 1):
            for side in range(2):
                start, end, middle = (vertices[rows[0][side]], vertices[rows[-1][side]], vertices[row[side]])
                numerator = sum(abs(middle[k]*length-start[k]*(length-j)-end[k]*j) for k in range(3))
                error = max(error, (numerator+length-1)//length)
        if error > 65535:
            raise ArtError('Far-road deviation exceeds metadata range')
        indices = list(first['vertices'])
        a, b, c, d = first['edges']
        indices[b], indices[d] = rows[-1]
        result += struct.pack('>8H', *indices, first['strip'], chain[0], error, int(first['outer']))
        errors.append(error)
    return bytes(result), dict(records=len(errors), orientation_rejected=rejected,
                              maximum_chord_error=max(errors, default=0),
                              zero_chord_error=sum(x == 0 for x in errors))


def build(mesh, source_sha, fog):
    if len(source_sha) != 32 or len(fog) != 3 or any(not 0 <= c <= 255 for c in fog):
        raise ArtError('Invalid source identity or fog')
    if mesh.get('format') != 'fzx-source-road-v1':
        raise ArtError('Expected the original road export format')
    quads, strips = prepare(mesh)
    vertices = mesh['vertices']
    if len(vertices) > 65535 or len(quads)*2 > 65535:
        raise ArtError('Road identities exceed 16-bit metadata')
    if any(v[5:] != [0, 0, 0, 255] for v in vertices):
        raise ArtError('Compact road rows require the original constant shade')
    vertex_data = b''.join(struct.pack('>5h', *v[:5]) for v in vertices)
    quad_data = b''.join(struct.pack('>8H', *q['vertices'], q['strip'] if not q['primitive'] else 65535,
                                   int(q['outer']) | (int(q['primitive']) << 1), q['first'], q['chunk']) for q in quads)
    repeat_data = b''.join(struct.pack('>I', repeat_record(q, vertices)) for q in quads)
    chunk_data = bytearray()
    at = 0
    for chunk in range(len(mesh['chunks'])-1):
        end = at
        while end < len(quads) and quads[end]['chunk'] == chunk:
            end += 1
        chunk_data += struct.pack('>2I', at, end-at)
        at = end
    if at != len(quads):
        raise ArtError('Noncontiguous source chunk order')
    strip_data, image_data, textures, image_reports, unshared = texture_images(mesh, strips, source_sha, fog)
    far2, report2 = far_records(mesh, quads, 2)
    far4, report4 = far_records(mesh, quads, 4)
    sections = (vertex_data, quad_data, repeat_data, chunk_data, strip_data, image_data, far2, far4)
    # Repetition section is indexed by quads; it has no separate header count.
    counts = (len(vertices), len(quads), len(mesh['chunks'])-1, len(strips), len(strips)*5,
              len(far2)//16, len(far4)//16, len(textures))
    metadata = bytearray(HEADER_BYTES)
    offsets = []
    for data in sections:
        offsets.append(len(metadata))
        metadata.extend(data)
        metadata.extend(bytes(-len(metadata) % 8))
    metadata[:8] = b'FZXRLD01'
    struct.pack_into('>4I', metadata, 8, 1, len(metadata), 1, 0)
    metadata[24:56] = source_sha
    metadata[56:88] = hashlib.sha256(textures).digest()
    struct.pack_into('>16I', metadata, 88, *counts, *offsets)
    metadata[152:156] = bytes([*fog, 255])
    struct.pack_into('>I', metadata, 20, fnv(metadata))
    if len(metadata) > PACK_LIMIT or len(textures) > TEXTURE_LIMIT:
        raise ArtError('Road bake exceeds pack/texture budget: %d / %d' % (len(metadata), len(textures)))
    report = dict(format='FZXRLD01', vertices=len(vertices), quads=len(quads), chunks=counts[2], strips=len(strips),
                  metadata_bytes=len(metadata), metadata_limit=PACK_LIMIT, texture_bytes=len(textures), texture_limit=TEXTURE_LIMIT,
                  unshared_texture_bytes=unshared, source_sha256=source_sha.hex(), metadata_sha256=hashlib.sha256(metadata).hexdigest(),
                  texture_sha256=hashlib.sha256(textures).hexdigest(),
                  modes=dict(Counter(i['mode'] for i in image_reports)),
                  stored_max_channel_error=max(i['stored_max_channel_error'] for i in image_reports),
                  far={'2': report2, '4': report4}, images=image_reports,
                  adaptations=['constant original black/opaque vertex shade encoded once',
                               'periodic box-filtered texture tiers from original strips',
                               'CLUT4 only within one RGB5 channel step in every fog variant; otherwise RGB1555',
                               'exact shared texture/palette bytes stored once',
                               'far endpoint candidates retain chord error; runtime merge remains unqualified'])
    validate(bytes(metadata), textures)
    return bytes(metadata), textures, report


def checked_hash(data, offset):
    expected = struct.unpack_from('>I', data, offset)[0]
    copy = bytearray(data)
    copy[offset:offset+4] = bytes(4)
    if fnv(copy) != expected:
        raise ArtError('Road content checksum mismatch')


def validate(metadata, textures):
    """Validate bounds and reconstruct every repetition from compact source UVs."""
    if len(metadata) < HEADER_BYTES or metadata[:8] != b'FZXRLD01' or len(textures) < 64 or textures[:8] != b'FZXRTX01':
        raise ArtError('Invalid road bake header')
    if struct.unpack_from('>3I', metadata, 8) != (1, len(metadata), 1) or metadata[155] != 255 or any(metadata[156:160]):
        raise ArtError('Invalid road metadata version/size/flags')
    if struct.unpack_from('>2I', textures, 8) != (1, len(textures)) or struct.unpack_from('>I', textures, 20)[0] != 1:
        raise ArtError('Invalid road texture version/size/flags')
    checked_hash(metadata, 20)
    checked_hash(textures, 16)
    if metadata[24:56] != textures[24:56] or metadata[56:88] != hashlib.sha256(textures).digest():
        raise ArtError('Road texture identity mismatch')
    counts = struct.unpack_from('>8I', metadata, 88)
    offsets = struct.unpack_from('>8I', metadata, 120)
    nv, nq, nc, ns, ni, nf2, nf4, nt = counts
    if not all((nv, nq, nc, ns)) or ni != ns*5 or nt != len(textures):
        raise ArtError('Invalid road section counts')
    if len(metadata) > PACK_LIMIT or nt > TEXTURE_LIMIT or struct.unpack_from('>2I', textures, 56) != (ni, 64):
        raise ArtError('Invalid road budget/image header')
    section_counts = (nv, nq, nq, nc, ns, ni, nf2, nf4)
    cursor = HEADER_BYTES
    for at, count, stride in zip(offsets, section_counts, STRIDES):
        end = at+count*stride
        if at != cursor or end > len(metadata):
            raise ArtError('Invalid road section bounds')
        cursor = (end+7) & ~7
        if any(metadata[end:cursor]):
            raise ArtError('Nonzero road section padding')
    if cursor != len(metadata):
        raise ArtError('Trailing road metadata')
    vertices = [struct.unpack_from('>5h', metadata, offsets[0]+i*10) for i in range(nv)]
    strips = [struct.unpack_from('>4H2h2H', metadata, offsets[4]+i*16) for i in range(ns)]
    for i, s in enumerate(strips):
        _, width, height, flags, u0, u1, first, number = s
        if not width or width % 8 or width > 504 or height not in (8, 16, 32) or flags or u0 >= u1 or first != i*5 or number != 5:
            raise ArtError('Invalid road strip')
    quads, source_quads = [], []
    for i in range(nq):
        q = struct.unpack_from('>8H', metadata, offsets[1]+i*16)
        ids, strip_id, flags, first, chunk = q[:4], *q[4:]
        if len(set(ids)) != 4 or max(ids) >= nv or flags & ~3 or first != i*2 or chunk >= nc:
            raise ArtError('Invalid source quad identity')
        repeat = struct.unpack_from('>I', metadata, offsets[2]+i*4)[0]
        if flags & 2:
            if strip_id != 65535 or repeat:
                raise ArtError('Invalid primitive repetition')
            edges = (0, 1, 3, 2)
        else:
            if strip_id >= ns:
                raise ArtError('Invalid quad strip')
            p = [vertices[j] for j in ids]
            if p[0][3] != p[1][3] or p[2][3] != p[3][3] or tuple(sorted((p[0][3], p[2][3]))) != strips[strip_id][4:6]:
                raise ArtError('Source quad transverse UV differs from its strip')
            left, right = (0, 3) if p[0][3] < p[2][3] else (3, 0)
            le, re = (1 if left == 0 else 2), (1 if right == 0 else 2)
            if p[left][4] > p[le][4]:
                left, le = le, left
            if p[right][4] > p[re][4]:
                right, re = re, right
            lo, hi = min(p[left][4], p[right][4]), max(p[le][4], p[re][4])
            if p[left][4] >= p[le][4] or p[right][4] >= p[re][4]:
                raise ArtError('Invalid source UV edge')
            unit = strips[strip_id][2]*32
            first_phase, count, end_phase, span, uneven = decode_repeat(repeat, unit)
            if (first_phase, count, end_phase, span, uneven) != (lo % unit, pieces(lo, hi, unit), hi % unit, hi-lo,
                                                               p[left][4] != p[right][4] or p[le][4] != p[re][4]):
                raise ArtError('Repetition metadata differs from original UVs')
            edges = (left, le, right, re)
        quads.append(q)
        source_quads.append(dict(vertices=list(ids), strip=strip_id, material=strips[strip_id][0] if strip_id < ns else 65535,
                                 chunk=chunk, outer=bool(flags & 1), primitive=bool(flags & 2), edges=edges))
    cursor = 0
    for chunk in range(nc):
        first, count = struct.unpack_from('>2I', metadata, offsets[3]+chunk*8)
        if first != cursor or first+count > nq or any(q[7] != chunk for q in quads[first:first+count]):
            raise ArtError('Chunk coverage mismatch')
        cursor += count
    if cursor != nq:
        raise ArtError('Missing source quads')
    for i in range(ni):
        image = struct.unpack_from('>4H6I', metadata, offsets[5]+i*32)
        mode, width, height, repetitions, *addresses = image
        s = strips[i//5]
        if mode not in (0, 1) or width != s[1] or repetitions != (1, 2, 4, 8, 0)[i % 5] or height != (s[2] if repetitions else 1):
            raise ArtError('Invalid road tier image')
        pixel_bytes = width*height*2 if mode == 0 else width*height//2
        for at in addresses[:3]:
            if at < 64 or at % 8 or at+pixel_bytes > nt:
                raise ArtError('Invalid road image bounds')
        if mode:
            if len(set(addresses[:3])) != 1:
                raise ArtError('CLUT fog variants must share indices')
            for at in addresses[3:]:
                if at < 64 or at % 8 or at+32 > nt or any(not w & 0x8000 for w in struct.unpack_from('>16H', textures, at)):
                    raise ArtError('Invalid opaque RGB lookup palette')
        elif any(addresses[3:]):
            raise ArtError('RGB tier must not carry a lookup palette')
        elif any(not p & 0x8000 for at in addresses[:3] for p in struct.unpack_from('>%dH' % (width*height), textures, at)):
            raise ArtError('Road RGB image must be opaque')
    for section, count in ((6, nf2), (7, nf4)):
        for i in range(count):
            row = struct.unpack_from('>8H', metadata, offsets[section]+i*16)
            if max(row[:4]) >= nv or len(set(row[:4])) != 4 or row[4] >= ns or row[5] >= nq or row[7] & ~1:
                raise ArtError('Invalid far-road candidate')
            original = quads[row[5]]
            if original[4] != row[4] or original[5] != row[7]:
                raise ArtError('Far-road candidate provenance mismatch')
        reconstructed, _ = far_records(dict(vertices=vertices, chunks=[None]*(nc+1)), source_quads, 2 if section == 6 else 4)
        if metadata[offsets[section]:offsets[section]+count*16] != reconstructed:
            raise ArtError('Far-road rows/error bounds differ from the original chain')
    return counts, offsets


def generate(source, output):
    output_location(output)
    for receipt in (source/'road.receipt', ROOT/'generated/road_pack/road_pack.receipt'):
        subprocess.run(['bash', str(ROOT/'tools/check_receipt.sh'), str(receipt)], check=True)
    data = (source/'road.json').read_bytes()
    source_sha = hashlib.sha256(data).digest()
    manifest = json.loads((ROOT/'generated/road_pack/manifest.json').read_text())
    if manifest['source_sha256'] != source_sha.hex():
        raise ArtError('Road bake source differs from admitted course')
    metadata, textures, report = build(json.loads(data), source_sha, manifest['environment']['course_fog'])
    output.mkdir(parents=True, exist_ok=True)
    (output/'ROADLOD.PAK').write_bytes(metadata)
    (output/'ROADLTX.PAK').write_bytes(textures)
    (output/'road_bake.json').write_bytes((json.dumps(report, indent=2)+'\n').encode())
    identity = ('/* Generated road bake identity; regenerate, never edit. */\n'
                '#define FZX_ROAD_LOD_BYTES %du\n#define FZX_ROAD_LOD_TEXTURE_BYTES %du\n'
                '#define FZX_ROAD_LOD_HASH 0x%08Xu\n') % (len(metadata), len(textures), struct.unpack_from('>I', metadata, 20)[0])
    (output/'road_lod_identity.h').write_bytes(identity.encode())
    dependencies = [Path(__file__), ROOT/'tools/road_lod_census.py', ROOT/'tools/road_tiles.py', ROOT/'tools/road_pack.py',
                    ROOT/'tools/import_source_art.py', source/'road.receipt', source/'road.json',
                    ROOT/'generated/road_pack/road_pack.receipt', ROOT/'generated/road_pack/manifest.json']
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    receipt = ['# fzx generation receipt v1; regenerate, never edit']
    receipt += ['generator %s %s' % (digest(p), os.path.relpath(p, ROOT/'tools')) for p in dependencies]
    receipt += ['output %s %s' % (digest(output/n), n) for n in ('ROADLOD.PAK', 'ROADLTX.PAK', 'road_bake.json', 'road_lod_identity.h')]
    (output/'road_bake.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    print(json.dumps({k: v for k, v in report.items() if k != 'images'}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT/'generated/road')
    parser.add_argument('--output', type=Path, default=ROOT/'generated/road_bake')
    args = parser.parse_args()
    generate(args.source, args.output)
