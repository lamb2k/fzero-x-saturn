#!/usr/bin/env python3
"""Bake the admitted road LOD pack into culling clusters (ROADCL.PAK).

Private source data only: the input and output are generated from the owner's
ROM and are never committed. View-independent: everything the per-frame track
build reads is precomputed here, in Saturn (big-endian) order, so the runtime
uses it in place with no boot-time layout pass.

Each chunk's quads are sorted into a few facing classes (clusters): a quad
joins the first class whose normal cone stays within MAX_SPREAD degrees with
it, else it opens a new one. Each cluster gets bounds for SH2ZAM segmesh-2
(sh2z_segmesh_bound_t, 20 bytes): a sphere holding every vertex of its quads
and a cone holding both triangle normals of each quad (the runtime's winding
test sums both triangles). Each quad gets its cluster's index in its chunk.
The runtime classifies a visible chunk's clusters in one call and skips the
quads of culled ones before fetching them; such quads would all be culled,
so the commands drawn are unchanged. Quads keep their order and numbering.

Layout (big-endian, sections 8-byte aligned):
  header   64 bytes: 'FZXRCL01', version 1, bytes, embedded FNV (computed
           with that field zero), FNV of the source ROADLOD.PAK, counts
           (chunks, clusters, quads), section offsets
  chunks   (chunks + 1) x cluster_first u16
  bounds   clusters x sh2z_segmesh_bound_t
  tags     quads x u8: the quad's cluster index within its chunk
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import os
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MAGIC = b'FZXRCL01'
HEADER = 64
MAX_CLASSES = 32        # bounds per chunk (one sh2z_segmesh_cull call)
MAX_SPREAD = float(os.environ.get('ROAD_CLUSTER_SPREAD', '45'))   # degrees
CONE_MARGIN = 0.5       # degrees added for axis quantisation
CONE_LIMIT = 80.0       # segmesh-2 cones must stay below this half angle
ONE = 16384             # Q1.14
NO_CONE = 1             # bound flag: frustum test only


def fnv(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xffffffff
    return h


def open_pack(data):
    if data[:8] != b'FZXRLD01' or struct.unpack_from('>I', data, 12)[0] != len(data):
        raise SystemExit('road_cluster_bake: not an admitted ROADLOD pack')
    return struct.unpack_from('>8I', data, 88), struct.unpack_from('>8I', data, 120)


def normal(a, b, c):
    u = [b[i] - a[i] for i in range(3)]
    v = [c[i] - a[i] for i in range(3)]
    n = (u[1]*v[2] - u[2]*v[1], u[2]*v[0] - u[0]*v[2], u[0]*v[1] - u[1]*v[0])
    m = math.sqrt(sum(x*x for x in n))
    return None if m == 0 else tuple(x/m for x in n)


def spread(axis, normals):
    return max(math.degrees(math.acos(max(-1.0, min(1.0, sum(a*b for a, b in zip(axis, n)))))) for n in normals)


def cone(normals):
    """Unit axis and half angle in degrees, or None when no axis exists."""
    if not normals:
        return None
    s = [sum(n[i] for n in normals) for i in range(3)]
    m = math.sqrt(sum(x*x for x in s))
    if m < 1e-9:
        return None
    axis = tuple(x/m for x in s)
    return axis, spread(axis, normals)


def quantised_cone(normals):
    """Q1.14 axis, cos and sin of the conservative half angle, or None."""
    c = cone(normals)
    if not c:
        return None
    axis = tuple(int(round(x*ONE)) for x in c[0])
    m = math.sqrt(sum(x*x for x in axis))
    if m == 0:
        return None
    worst = spread(tuple(x/m for x in axis), normals) + CONE_MARGIN
    if worst >= CONE_LIMIT:
        return None
    # cos rounded down, sin rounded up.
    return axis, int(math.floor(math.cos(math.radians(worst))*ONE)), int(math.ceil(math.sin(math.radians(worst))*ONE))


def bake(pack):
    counts, offsets = open_pack(pack)
    nq, nc = counts[1], counts[2]
    def vtx(i): return struct.unpack_from('>5h', pack, offsets[0] + 10*i)
    def quad(i): return struct.unpack_from('>8H', pack, offsets[1] + 16*i)
    def chunk(i): return struct.unpack_from('>2I', pack, offsets[3] + 8*i)
    chunk_records, bounds, clusters = [], [], []
    tags = [None]*nq
    stats = dict(chunks=nc, quads=nq, clusters=0, cone_clusters=0, max_classes=0, spread_degrees=MAX_SPREAD)
    expected = 0
    for c in range(nc):
        first, number = chunk(c)
        if first != expected or first + number > nq:
            raise SystemExit('road_cluster_bake: chunks are not contiguous quad ranges')
        expected = first + number
        chunk_records.append(len(clusters))
        classes = []   # [ids, normals, members]
        owner = {}
        for i in range(first, first + number):
            q = quad(i)
            corners = [vtx(v)[:3] for v in q[:4]]
            t = [n for n in (normal(corners[0], corners[1], corners[2]),
                             normal(corners[0], corners[2], corners[3])) if n]
            chosen = None
            for k, (ids, tri, members) in enumerate(classes):
                if t and tri:
                    trial = cone(tri + t)
                    if trial is None or trial[1] > MAX_SPREAD:
                        continue
                chosen = k
                break
            if chosen is None:
                if len(classes) >= MAX_CLASSES:
                    raise SystemExit('road_cluster_bake: a chunk needs more than %d classes' % MAX_CLASSES)
                classes.append(([], [], []))
                chosen = len(classes) - 1
            ids, tri, members = classes[chosen]
            ids += [v for v in dict.fromkeys(q[:4]) if v not in ids]
            tri += t
            members.append(i)
            owner[i] = chosen
        stats['max_classes'] = max(stats['max_classes'], len(classes))
        for ids, tri, members in classes:
            pts = [vtx(v)[:3] for v in ids]
            lo = [min(p[k] for p in pts) for k in range(3)]
            hi = [max(p[k] for p in pts) for k in range(3)]
            centre = [(lo[k] + hi[k]) // 2 for k in range(3)]
            radius = int(math.ceil(max(math.sqrt(sum((p[k]-centre[k])**2 for k in range(3))) for p in pts))) + 1
            if radius > 65535:
                raise SystemExit('road_cluster_bake: cluster too large')
            qc = quantised_cone(tri)
            if qc:
                (axis, cos_q, sin_q), flags = qc, 0
                stats['cone_clusters'] += 1
            else:
                axis, cos_q, sin_q, flags = (0, 0, 0), 0, ONE, NO_CONE
            bounds.append(struct.pack('>3hH3h2hH', *centre, radius, *axis, cos_q, sin_q, flags))
            clusters.append(len(members))
        for i in range(first, first + number):
            tags[i] = owner[i]
    if expected != nq:
        raise SystemExit('road_cluster_bake: chunks do not cover the quad table')
    chunk_records.append(len(clusters))
    stats['clusters'] = len(clusters)
    if len(clusters) > 65535 or nq > 65535:
        raise SystemExit('road_cluster_bake: table limits')
    sections = [b''.join(struct.pack('>H', r) for r in chunk_records), b''.join(bounds), bytes(tags)]
    out = bytearray(HEADER)
    offs = []
    for s in sections:
        out.extend(bytes(-len(out) % 8))
        offs.append(len(out))
        out.extend(s)
    out.extend(bytes(-len(out) % 8))
    out[:8] = MAGIC
    struct.pack_into('>2I', out, 8, 1, len(out))
    struct.pack_into('>I', out, 20, fnv(pack))
    struct.pack_into('>3H', out, 24, nc, len(clusters), nq)
    struct.pack_into('>3I', out, 32, *offs)
    struct.pack_into('>I', out, 16, fnv(out))   # embedded checksum, hashed with this field zero
    stats['bytes'] = len(out)
    return bytes(out), stats


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description='Bake ROADLOD.PAK into culling clusters.')
    p.add_argument('--source', type=Path, default=ROOT/'generated/road_bake')
    p.add_argument('--output', type=Path, default=ROOT/'generated/road_cluster')
    a = p.parse_args()
    subprocess.run(['sh', str(ROOT/'tools/check_receipt.sh'), str(a.source/'road_bake.receipt')],
                   check=True, stdout=subprocess.DEVNULL)
    pack = (a.source/'ROADLOD.PAK').read_bytes()
    data, stats = bake(pack)
    a.output.mkdir(parents=True, exist_ok=True)
    (a.output/'ROADCL.PAK').write_bytes(data)
    (a.output/'road_cluster.json').write_text(json.dumps(stats, indent=2) + '\n')
    (a.output/'road_cluster_identity.h').write_text(
        '/* Generated by tools/road_cluster_bake.py; regenerate, never edit. */\n'
        '#define FZX_ROAD_CLUSTER_BYTES %du\n#define FZX_ROAD_CLUSTER_FILE_HASH 0x%08xu\n'
        '#define FZX_ROAD_CLUSTER_SOURCE_HASH 0x%08xu\n' % (len(data), fnv(data), fnv(pack)))
    deps = [Path(__file__), a.source/'road_bake.receipt', a.source/'ROADLOD.PAK']
    receipt = ['# fzx generation receipt v1; regenerate, never edit']
    receipt += ['generator %s %s' % (digest(d), os.path.relpath(d, ROOT/'tools')) for d in deps]
    receipt += ['output %s %s' % (digest(a.output/n), n)
                for n in ('ROADCL.PAK', 'road_cluster.json', 'road_cluster_identity.h')]
    (a.output/'road_cluster.receipt').write_text('\n'.join(receipt) + '\n')
    print('road clusters: %(clusters)d clusters (%(cone_clusters)d with cones, at most %(max_classes)d per chunk) '
          'over %(chunks)d chunks, %(quads)d quads, %(bytes)d bytes' % stats)


if __name__ == '__main__':
    main()
