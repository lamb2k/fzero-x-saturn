#!/usr/bin/env python3
"""Bake a course's gadget decals (dash plates; pit, dirt and ice strips) for the engine road: the decals of each road chunk
as VDP1 quads with their own small bitmaps, as generated/<course>/road_gadget/ROADGAD.PAK (+ identity header, receipt).

The original builds these gadgets in world space (course_gadgets.c: strips sampled at the road's chunk boundaries, plates
of two chevron halves) and draws them with wrapped 16-bit textures. VDP1 maps one bitmap onto one quad, so each decal quad
gets its own 4-bit bitmap, box-filtered from the original wrapped texture through the original's own texture coordinates
(exact phase), plus the 16-colour palette of its kind in the road's three fog variants (road_bake's quantiser and fog rule).
Everything here is derived from the owner's ROM: generated/ only, never committed.

    tools/gadget_bake.py --course SLUG [--output DIR] [--dump FILE]     (ROM at FZX_TEST_ROM; the HOST dumper is built by
                                                                        ref/gadget_build.sh from the original code)

ROADGAD.PAK (big-endian): "FZXGAD01", u32 version 1, u32 file bytes, u32 chunks, u32 quads, u32 image slots,
u32 GTEX bytes, u32 0, then (u32 offset, u32 bytes) for GFIRST, GPOS, GQUADS, GIMAGES, GTEX; tables 4-aligned (GTEX 32).
  GFIRST:  u16 x (chunks + 1): the first decal quad of each road chunk (quads are in chunk order; the last entry = quads).
  GPOS:    3 x i16 per corner, 4 corners per quad: the original's world coordinates, in VDP1 order A B C D
           (the bitmap's top-left, top-right, bottom-right, bottom-left).
  GQUADS:  u16 image slot, u16 flags (bit 0: dash plate) per quad.
  GIMAGES: 52-byte records as the road's image table (3 x (u32 pixels, u32 palette, u16 width, u16 height, u16 mode, u16 0),
           u8 mean[3], u8 0); pixels and palette are byte offsets into GTEX. Slot 0 and every fifth slot stay empty.
  GTEX:    CLUT4 bitmaps (8-aligned), then the fog palettes (32-aligned, RGB1555, opaque), 16 words each.
"""
import argparse, hashlib, json, math, os, struct, subprocess, sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import course_catalog                                    # noqa: E402
from import_source_art import ArtError, mio0, bounded     # noqa: E402
from import_course import normalize_rom                   # noqa: E402
from road_bake import quantize16, fog_palette, rgb, word, round_ratio  # noqa: E402
from road_pack import fnv                                 # noqa: E402

SEGMENT = (0x16C8A0, 0xE940)         # course_track_gfx (MIO0, stored size), the original's effect textures
# kind: (ROM offset in the decompressed segment, width, height); RGBA16, wrapped by the original's tile masks.
# s, t are s10.5 texture coordinates drawn at gSPTexture scale 0.5 (the US build's effect pass, course_gadgets.c): texel = coordinate / 64.
SCALE = {'dash': 1 / 64.0, 'pit': 1 / 64.0, 'dirt': 1 / 64.0, 'ice': 1 / 64.0}
TEXTURES = {'pit': (0x16220, 32, 32), 'dash': (0x16A20, 16, 16), 'dirt': (0x16C20, 32, 16), 'ice': (0x17020, 16, 16)}
EFFECT_KIND = {1: 'pit', 2: 'dirt', 3: 'dash', 4: 'ice'}
FADES = (0, 128, 255)


def read_dump(path):
    chunks, effects, cur = [], [], None
    for line in Path(path).read_text().splitlines():
        t = line.split()
        if t[0] == 'C':
            chunks.append((int(t[2]), float(t[3])))
        elif t[0] == 'E':
            cur = dict(index=int(t[1]), seg=int(t[2]), type=int(t[3]), t0=float(t[4]), verts=[])
            effects.append(cur)
        elif t[0] == 'V':
            cur['verts'].append(dict(pos=[int(float(x)) for x in t[3:6]], s=int(float(t[6])), t=int(float(t[7])), chunk=int(t[8])))
    return chunks, effects


def chunk_of(chunks, seg, t):
    """The chunk whose interval holds (segment, t): the last chunk starting at or before it (func_8006FD7C)."""
    best = 0
    for i, (cs, ct) in enumerate(chunks):
        if (cs, ct) <= (seg, t):
            best = i
    return best


def decode_textures(rom):
    seg = mio0(bounded(rom, SEGMENT[0], SEGMENT[1]))
    out = {}
    for kind, (off, w, h) in TEXTURES.items():
        px = [struct.unpack_from('>H', seg, off + 2 * i)[0] for i in range(w * h)]
        # RGBA5551 -> the road's RGB1555 (alpha bit is the high bit, always set here)
        words = [((p >> 11 & 31) | (p >> 6 & 31) << 5 | (p >> 1 & 31) << 10 | 0x8000) for p in px]
        if any(not p & 1 for p in px):
            raise ArtError('effect texture %s has transparent texels' % kind)
        out[kind] = (w, h, words)
    return out


def kind_palettes(textures, fog):
    pal = {}
    for kind, (w, h, words) in textures.items():
        indices, palette, quant = quantize16(words)
        pal[kind] = dict(palette=palette, fogs=[fog_palette(palette, fog, f) for f in FADES], quant=quant,
                         rgb=[rgb(p) for p in palette])
    return pal


def nearest(pal_rgb, c):
    return min(range(len(pal_rgb)), key=lambda i: (sum((c[k] - pal_rgb[i][k]) ** 2 for k in range(3)), i))


def bake_bitmap(kind, corners, uv, textures, pal, cache):
    """corners: UV (texels) of A B C D = bitmap TL TR BR BL. Returns (w, h, packed CLUT4 bytes, pixel colours)."""
    tw, th, words = textures[kind]
    ab = math.dist(uv[0], uv[1]); ad = math.dist(uv[0], uv[3])
    if kind == 'dash':      # a plate keeps its chevron detail: full resolution, at most 32 x 32
        w = min(32, max(8, (int(math.ceil(ab)) + 7) // 8 * 8)); h = min(32, max(2, int(math.ceil(ad))))
    else:                   # strips repeat a noisy texture several times per quad: half resolution, at most 32 x 16
        w = min(32, max(8, (int(math.ceil(ab / 2)) + 7) // 8 * 8)); h = min(16, max(2, int(math.ceil(ad / 4))))
    # box-filtered: each bitmap pixel averages the texels under it (at most 4 x 4 subsamples), then takes the nearest
    # palette entry, so a strip that repeats its texture several times per quad does not alias
    su = max(1, min(4, int(math.ceil(ab / w)))); sv = max(1, min(4, int(math.ceil(ad / h))))
    idx = []
    for j in range(h):
        for i in range(w):
            acc = [0, 0, 0]
            for b in range(sv):
                v = (j + (b + 0.5) / sv) / h
                for a in range(su):
                    u = (i + (a + 0.5) / su) / w
                    s = (uv[0][0] * (1 - u) * (1 - v) + uv[1][0] * u * (1 - v) + uv[2][0] * u * v + uv[3][0] * (1 - u) * v)
                    t = (uv[0][1] * (1 - u) * (1 - v) + uv[1][1] * u * (1 - v) + uv[2][1] * u * v + uv[3][1] * (1 - u) * v)
                    c = rgb(words[(math.floor(t) % th) * tw + (math.floor(s) % tw)])
                    for k in range(3):
                        acc[k] += c[k]
            c = tuple(x / (su * sv) for x in acc)
            key = (kind, tuple(round(x * 2) for x in c))
            if key not in cache:
                cache[key] = nearest(pal[kind]['rgb'], c)
            idx.append(cache[key])
    packed = bytes((idx[i] << 4) | idx[i + 1] for i in range(0, len(idx), 2))
    return w, h, packed, idx


def build(chunks, effects, textures, fog):
    pal = kind_palettes(textures, fog)
    cache = {}
    quads = []      # dict(chunk, order, kind, pos[4], uv[4], flags)
    for e in effects:
        kind = EFFECT_KIND[e['type']]
        v = e['verts']
        if e['type'] == 3:
            if len(v) != 6:
                raise ArtError('dash plate with %d vertices' % len(v))
            k = SCALE[kind]
            uvt = [(p['s'] * k, p['t'] * k) for p in v]
            c = chunk_of(chunks, e['seg'], e['t0'])
            # halves of the chevron (triangles (0,4,1) (0,3,4) and (0,2,3) (2,5,3) of the original): A B C D are the
            # parallelogram's corners with A->B the longer texture direction.
            for order in ((1, 4, 3, 0), (2, 0, 3, 5)):
                quads.append(dict(chunk=c, order=(e['index'], len(quads)), kind=kind, pos=[v[i]['pos'] for i in order],
                                  uv=[uvt[i] for i in order], flags=1))
        else:
            n = len(v) // 2
            k = SCALE[kind]
            step = (0, 0)
            for p in range(n - 1):
                r0, l0, r1, l1 = v[2 * p], v[2 * p + 1], v[2 * p + 2], v[2 * p + 3]
                tr0, tl0 = r0['t'], l0['t']
                if r1['t'] < tr0 or l1['t'] < tl0:
                    # The original patches the near pair's t to 0x8000 here (the strip's t counter wrapped), which smears
                    # the texture over hundreds of repeats; the bake continues the previous quad's step instead.
                    tr0, tl0 = r1['t'] - step[0], l1['t'] - step[1]
                else:
                    step = (r1['t'] - tr0, l1['t'] - tl0)
                uv = [(r0['s'] * k, tr0 * k), (l0['s'] * k, tl0 * k), (l1['s'] * k, l1['t'] * k), (r1['s'] * k, r1['t'] * k)]
                quads.append(dict(chunk=r0['chunk'], order=(e['index'], p), kind=kind,
                                  pos=[r0['pos'], l0['pos'], l1['pos'], r1['pos']], uv=uv, flags=0))
    # The road draws a quad only when its projected corners wind negative (area < 0 in fzx_road_stream_draw.c); the quads above
    # wind positive under the engine's view, so corners B and D swap (the bitmap is then baked in the swapped order).
    for q in quads:
        q['pos'] = [q['pos'][i] for i in (0, 3, 2, 1)]
        q['uv'] = [q['uv'][i] for i in (0, 3, 2, 1)]
    quads.sort(key=lambda q: (q['chunk'], q['order']))
    # texture pool: bitmaps 8-aligned, palettes 32-aligned, identical bytes stored once
    pixels = bytearray(); bitmaps = {}
    images = []; image_of = {}
    palettes = bytearray(); pal_off = {}
    for kind, p in pal.items():
        pal_off[kind] = []
    nq = len(quads); recs = []
    for q in quads:
        w, h, packed, idx = bake_bitmap(q['kind'], None, q['uv'], textures, pal, cache)
        key = (q['kind'], w, h, packed)
        if key not in image_of:
            at = bitmaps.get(packed)
            if at is None:
                at = bitmaps[packed] = len(pixels); pixels += packed; pixels += bytes(-len(pixels) % 8)
            image_of[key] = (len(images), at, w, h, q['kind'], idx)
            images.append(image_of[key])
        q['image'] = image_of[key][0]
    # palettes after the pixels
    base_pal = (len(pixels) + 31) // 32 * 32
    tex = bytearray(pixels) + bytes(base_pal - len(pixels))
    for kind, p in pal.items():
        for v in range(3):
            pal_off[kind].append(len(tex))
            tex += struct.pack('>16H', *p['fogs'][v])
    # Image records are numbered so that no decal image index is a multiple of five (the road's fog shortcut keys its
    # per-strip cache on index / 5 when the index is a multiple of five): index = 1 + k + k // 4, the gaps stay empty.
    slots = 1 + (len(images) - 1) + (len(images) - 1) // 4 + 1 if images else 0
    img_rec = bytearray(52 * slots)
    for (n, at, w, h, kind, idx) in images:
        sums = [0, 0, 0]
        for i in idx:
            col = pal[kind]['palette'][i]
            for c in range(3):
                sums[c] += (col >> (c * 5)) & 31
        mean = [(s + w * h // 2) // (w * h) for s in sums]
        rec = b''.join(struct.pack('>2I4H', at, pal_off[kind][v], w, h, 1, 0) for v in range(3)) + bytes(mean + [0])
        slot = 1 + n + n // 4
        img_rec[52 * slot:52 * slot + 52] = rec
    gfirst = []
    count = 0
    per = defaultdict(int)
    for q in quads:
        per[q['chunk']] += 1
    last = -1
    for q in quads:
        if q['chunk'] < last:
            raise ArtError('decal quads not in chunk order')
        last = q['chunk']
    for c in range(len(chunks) + 1):
        gfirst.append(count)
        count += per.get(c, 0)
    gpos = bytearray(); gq = bytearray()
    for q in quads:
        for p in q['pos']:
            for c in p:
                if not -32768 <= c <= 32767:
                    raise ArtError('decal vertex out of range')
            gpos += struct.pack('>3h', *p)
        gq += struct.pack('>2H', 1 + q['image'] + q['image'] // 4, q['flags'])
    return dict(quads=quads, images=images, slots=slots, gfirst=struct.pack('>%dH' % len(gfirst), *gfirst),
                gpos=bytes(gpos), gquads=bytes(gq), gimages=bytes(img_rec), gtex=bytes(tex))


TABLES = ('GFIRST', 'GPOS', 'GQUADS', 'GIMAGES', 'GTEX')


def pack(chunks, g):
    """ROADGAD.PAK: header, directory of five tables, the tables (4-aligned, GTEX 32-aligned)."""
    out = bytearray(b'FZXGAD01')
    out += struct.pack('>II', 1, 0)
    out += struct.pack('>5I', len(chunks), len(g['quads']), g['slots'], len(g['gtex']), 0)
    dir_at = len(out)
    out += bytes(8 * len(TABLES))
    tables = []
    for name, align in zip(TABLES, (4, 4, 4, 4, 32)):
        data = g[name.lower()]
        while len(out) % align:
            out += b'\x00'
        tables.append((len(out), (len(data) + 3) // 4 * 4))
        out += data + bytes(-len(data) % 4)
    for i, (at, ln) in enumerate(tables):
        struct.pack_into('>II', out, dir_at + 8 * i, at, ln)
    struct.pack_into('>I', out, 12, len(out))
    return bytes(out), tables


def identity(data, tables, g, chunks):
    h = ['/* Generated by tools/gadget_bake.py; derived from the owner\'s ROM: never commit. */',
         '#ifndef FZX_ROAD_GAD_IDENTITY_H', '#define FZX_ROAD_GAD_IDENTITY_H',
         '#define FZX_ROAD_GAD_BYTES %du' % len(data), '#define FZX_ROAD_GAD_FILE_HASH 0x%08Xu' % fnv(data),
         '#define FZX_ROAD_GAD_CHUNKS %du' % len(chunks), '#define FZX_ROAD_GAD_QUADS %du' % len(g['quads']),
         '#define FZX_ROAD_GAD_IMAGES %du' % g['slots'], '#define FZX_ROAD_GAD_TEX_BYTES %du' % len(g['gtex'])]
    for name, (at, ln) in zip(TABLES, tables):
        h += ['#define FZX_ROAD_GAD_%s_AT %du' % (name, at), '#define FZX_ROAD_GAD_%s_BYTES %du' % (name, ln),
              '#define FZX_ROAD_GAD_%s_HASH 0x%08Xu' % (name, fnv(data[at:at + ln]))]
    h += ['#endif', '']
    return ('\n'.join(h)).encode()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--course', default=course_catalog.DEFAULT, choices=tuple(course_catalog.COURSES))
    ap.add_argument('--output', type=Path)
    ap.add_argument('--dump', type=Path, help='an existing fzx-gadgets dump instead of running the dumper')
    ap.add_argument('--cc', default='gcc')
    a = ap.parse_args()
    root = course_catalog.gen_root(a.course)
    out = a.output or root / 'road_gadget'
    raw = Path(os.environ['FZX_TEST_ROM']).read_bytes()
    rom, _ = normalize_rom(raw)
    dump = a.dump
    if not dump:
        out.mkdir(parents=True, exist_ok=True)
        exe = ROOT / 'ref/build' / ('gadgets-%s-O2' % a.cc) / 'fzx-gadgets'
        if not exe.exists():
            subprocess.run(['bash', str(ROOT / 'ref/gadget_build.sh'), a.cc, '-O2'], check=True, cwd=ROOT)
        dump = out / 'dump.txt'
        c = course_catalog.course(a.course)
        rec = out / 'record.bin'
        rec.write_bytes(rom[c['rom_offset']:c['rom_offset'] + course_catalog.RECORD_SIZE])
        subprocess.run([str(exe), '--record', str(rec), '--rom', os.environ['FZX_TEST_ROM'], '--out', str(dump)], check=True)
        rec.unlink()
    manifest = json.loads((root / 'road_pack/manifest.json').read_text())
    fog = manifest['environment']['course_fog']
    chunks, effects = read_dump(dump)
    g = build(chunks, effects, decode_textures(rom), fog)
    data, tables = pack(chunks, g)
    out.mkdir(parents=True, exist_ok=True)
    (out / 'ROADGAD.PAK').write_bytes(data)
    ident = identity(data, tables, g, chunks)
    (out / 'road_gadget_identity.h').write_bytes(ident)
    per = defaultdict(int)
    for q in g['quads']:
        per[q['chunk']] += 1
    window = max((sum(per.get(c % len(chunks), 0) for c in range(i, i + 40)) for i in range(len(chunks))), default=0)
    report = dict(chunks=len(chunks), effects=len(effects), quads=len(g['quads']), images=len(g['images']),
                  texture_bytes=len(g['gtex']), file_bytes=len(data), fnv=fnv(data), chunks_with_decals=len(per),
                  max_quads_in_40_chunks=window)
    (out / 'road_gadget.json').write_text(json.dumps(report, indent=1) + '\n')
    digest = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
    deps = [Path(__file__), ROOT / 'ref/gadget_bake.c', ROOT / 'ref/gadget_main.c', ROOT / 'tools/road_bake.py', root / 'road_pack/manifest.json']
    rec = ['# fzx generation receipt v1; regenerate, never edit']
    rec += ['generator %s %s' % (digest(p), os.path.relpath(p, ROOT / 'tools').replace('\\', '/')) for p in deps]
    rec += ['output %s %s' % (digest(out / n), n) for n in ('ROADGAD.PAK', 'road_gadget_identity.h', 'road_gadget.json')]
    (out / 'road_gadget.receipt').write_bytes(('\n'.join(rec) + '\n').encode())
    print(json.dumps(report))


if __name__ == '__main__':
    main()
