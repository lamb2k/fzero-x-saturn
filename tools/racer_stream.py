#!/usr/bin/env python3
"""Racer sprite streaming: RSP1 v1.2 compressed roster pack, codec measurement and a
synthetic 30-racer cache trace.

Reads RSP1 interface 1.1 sets (a roster pack with the 32-byte directory, as written by the
roster assembler) and writes:

  pack    RSZ1 pack (RSP1 interface 1.2 option): every set's header, cell table and CLUT
          unchanged, and each cell's pixels coded as a cell4z stream (SH2ZAM sprite-cache-1)
          with 16 context code-length tables shared by the whole pack. Raw sets stay valid:
          1.2 adds a pack format, it changes no set.
  measure codec ratio and modelled decode cost per profile, cell size histogram.
  trace   a synthetic race: per frame, the cells 30 racers need, for tools/racer_stream_replay.c.

RSZ1 layout (big-endian, offsets from the pack start):
  0     'RSZ1', u16 version 0x0102, u16 set count
  8     16 x 30 code lengths (context k, symbol s at 8 + 30k + s), for sh2z_cell4z_build
  488   directory, 32 bytes per set: key[8], profile[4], u32 header offset, u32 header bytes,
        u32 stream table offset, u32 stream table + streams bytes, u32 FNV-1a of the raw set
  then per set (8-aligned): the raw set's bytes up to its first pixel (header, cell table,
        CLUTs; unchanged, so pixel_offset still names where each cell goes when inflated),
        the stream table (u32 offset from the stream base, u32 bytes, per cell), the alias
        map (u16 per cell: 0x8000 | j = draw cell j flipped, no stream; else the cell itself),
        then the streams (stream base = table + 10 x cells), each 2-aligned
  end   1,024 zero bytes of decoder slack
Inflating every cell to its pixel_offset (and mirroring aliased cells) gives back the RSP1 1.1
set byte for byte, which the FNV checks.

Everything read from or written next to the roster comes from the owner's ROM: keep it out
of git. Only the tools are committed.
"""
import argparse, collections, hashlib, heapq, json, math, struct, sys
from pathlib import Path

RUNS = [2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 24, 32, 48, 64]   # sh2z_cell4z_runs
SYMS = 30
CTX = 16
LMAX = 15
ENTRY = struct.Struct('>8s4sIIIHHI')
CELL = struct.Struct('>HHhhIHH')
RSZ_MAGIC = b'RSZ1'
RSZ_VERSION = 0x0102   # RSP1 interface 1.2

# ---------------------------------------------------------------- RSP1 reading

def read_roster(path):
    d = Path(path).read_bytes()
    sets, i = [], 0
    while (i + 1) * 32 <= len(d):
        k, p, o, n, h, v, r, z = ENTRY.unpack_from(d, i * 32)
        if v != 0x101:
            break
        sets.append(dict(key=k.rstrip(b'\0').decode(), profile=p.rstrip(b'\0').decode(), framing=r, blob=d[o:o + n]))
        i += 1
    if not sets:
        raise SystemExit('racer_stream: no RSP1 1.1 directory entries in ' + str(path))
    return sets


def set_cells(blob):
    magic, total = struct.unpack_from('>II', blob, 0)
    if magic != 0x52535031:
        raise SystemExit('racer_stream: not an RSP1 set')
    yc, pc, flags, clut_count = struct.unpack_from('>HHHH', blob, 8)
    yaw = [struct.unpack_from('>hh', blob, 24 + 4 * j) for j in range(yc)]
    pitch = [struct.unpack_from('>hh', blob, 24 + 4 * 32 + 4 * j) for j in range(pc)]
    cells = []
    for c in range(yc * pc):
        w, h, ox, oy, off, clut, res = CELL.unpack_from(blob, 168 + 16 * c)
        px = bytes(n for v in blob[off:off + w * h // 2] for n in (v >> 4, v & 15))
        cells.append(dict(w=w, h=h, off=off, px=px))
    return dict(yaw=yaw, pitch=pitch, cells=cells, header_bytes=min(c['off'] for c in cells))

# ---------------------------------------------------------------- cell4z coding

def tokens(w, h, px):
    for y in range(h):
        row = px[y * w:(y + 1) * w]
        x, left = 0, 0
        while x < w:
            if row[x] == 0:
                z = 0
                while x + z < w and row[x + z] == 0:
                    z += 1
                if z >= 2:
                    r = max(i for i, v in enumerate(RUNS) if v <= z)
                    yield left, 16 + r
                    x += RUNS[r]
                    left = 0
                    continue
            yield left, row[x]
            left = row[x]
            x += 1


def vlc_lengths(freq, lmax=LMAX):
    """SH2ZAM vlc-1 sh2z_vlc8_build lengths (VLC.md): Huffman, clamp, lengthen, shorten."""
    syms = sorted(s for s in range(len(freq)) if freq[s])
    m = len(syms)
    if m == 1:
        return {syms[0]: 1}
    rank = {s: i for i, s in enumerate(syms)}
    heap = [(freq[s], rank[s], [s]) for s in syms]
    heapq.heapify(heap)
    depth = collections.Counter()
    nid = m
    while len(heap) > 1:
        f1, i1, a = heapq.heappop(heap)
        f2, i2, b = heapq.heappop(heap)
        for s in a + b:
            depth[s] += 1
        heapq.heappush(heap, (f1 + f2, nid, a + b))
        nid += 1
    L = {s: min(depth[s], lmax) for s in syms}
    K = sum(2 ** (lmax - L[s]) for s in syms)
    while K > 2 ** lmax:
        s = max((s for s in syms if L[s] < lmax), key=lambda s: (L[s], -freq[s], -s))
        K -= 2 ** (lmax - L[s]) - 2 ** (lmax - L[s] - 1)
        L[s] += 1
    while True:
        cand = [s for s in syms if L[s] > 1 and K + 2 ** (lmax - L[s]) <= 2 ** lmax]
        if not cand:
            break
        s = max(cand, key=lambda s: (freq[s], -s))
        K += 2 ** (lmax - L[s])
        L[s] -= 1
    assert K == 2 ** lmax
    return L


def canonical_codes(lens):
    """Canonical (length, symbol) codes, bit-reversed so the first stream bit is bit 0."""
    out = {}
    code = 0
    prev = 0
    for l, s in sorted((l, s) for s, l in lens.items()):
        code <<= (l - prev)
        prev = l
        out[s] = (int(format(code, '0%db' % l)[::-1], 2), l)
        code += 1
    return out


class Bits:
    def __init__(self):
        self.units, self.n = [], 0

    def put(self, code, l):
        for i in range(l):
            if self.n % 16 == 0:
                self.units.append(0)
            self.units[-1] |= ((code >> i) & 1) << (self.n % 16)
            self.n += 1

    def bytes(self):
        return b''.join(struct.pack('<H', u) for u in self.units)


def train(sets_cells):
    freq = [[1] * SYMS for _ in range(CTX)]   # every symbol codable in every context
    for sc in sets_cells:
        for c in sc['cells']:
            for k, s in tokens(c['w'], c['h'], c['px']):
                freq[k][s] += 1
    lens = [vlc_lengths(f) for f in freq]
    return lens, [canonical_codes(l) for l in lens]


def encode(cell, codes):
    b = Bits()
    for k, s in tokens(cell['w'], cell['h'], cell['px']):
        code, l = codes[k][s]
        b.put(code, l)
    return b.bytes(), b.n


def decode_ref(stream, w, h, lens):
    """Python reference decoder (independent of the C one; used for self-checks)."""
    rev = [{} for _ in range(CTX)]
    for k in range(CTX):
        for s, (code, l) in canonical_codes(lens[k]).items():
            rev[k][(code, l)] = s
    bitpos = 0
    def bit(i):
        return (stream[i // 8] >> (i % 8)) & 1
    px = []
    for y in range(h):
        x = left = 0
        while x < w:
            code = l = 0
            while (code, l) not in rev[left]:
                code |= bit(bitpos) << l
                bitpos += 1
                l += 1
            s = rev[left][(code, l)]
            if s < 16:
                px.append(s); left = s; x += 1
            else:
                n = RUNS[s - 16]
                assert x + n <= w
                px.extend([0] * n); x += n; left = 0
    return bytes(px), bitpos

# ---------------------------------------------------------------- cost model

def cycles_raw_copy(nbytes):
    # CPU word copy cartridge -> VDP1 VRAM: about 4 cycles per 16-bit word with flat memory
    # (load, store, two increments in a 2-word unrolled loop); cartridge and B-bus wait states
    # are not modelled (unknown until a console run).
    return 4 * (nbytes // 2) + 40


def cycles_decode(cell, nbits, syms):
    # SH-2 code of sh2z_cell4z_decode at -O2 (sh2eb-elf-gcc), counted by instruction: about 30
    # instructions per pixel symbol including the 12-instruction vlc-2 step, about 34 per run
    # symbol plus 3 per pixel of the run, 11 per 16-bit refill, 25 per row, 60 per call.
    # Instruction counts with flat memory are a model, not a timing.
    runs = [s for s in syms if s >= 16]
    px_syms = len(syms) - len(runs)
    run_px = sum(RUNS[s - 16] for s in runs)
    return 30 * px_syms + 34 * len(runs) + 3 * run_px + 11 * (nbits // 16) + 25 * cell['h'] + 60

# ---------------------------------------------------------------- pack (RSZ1, interface 1.2)

def mirror_aliases(cells):
    """Per-cell flip permission: map[i] = 0x8000 | j when cell i is exactly the horizontal mirror
    of an earlier, unaliased cell j of the same size (drawn from j's slot with the VDP1 flip bit);
    otherwise map[i] = i. Exact match only: a lit or decalled cell never qualifies."""
    out = []
    for i, c in enumerate(cells):
        w = c['w']
        rows = [c['px'][y * w:(y + 1) * w][::-1] for y in range(c['h'])]
        flipped = b''.join(bytes(r) for r in rows)
        src = next((j for j in range(i) if out[j] == j and cells[j]['w'] == w and cells[j]['h'] == c['h']
                    and cells[j]['px'] == flipped), None)
        out.append(0x8000 | src if src is not None else i)
    return out


def write_pack(sets, out):
    parsed = [set_cells(s['blob']) for s in sets]
    lens, codes = train(parsed)
    blob = bytearray()
    # header: magic, version, set count, code-length table (16 x 30 bytes), padding to 8
    hdr = bytearray(RSZ_MAGIC + struct.pack('>HH', RSZ_VERSION, len(sets)))
    for k in range(CTX):
        hdr += bytes(lens[k].get(s, 0) for s in range(SYMS))
    while len(hdr) % 8:
        hdr.append(0)
    dir_at = len(hdr)
    entry = struct.Struct('>8s4sIIIII')   # key, profile, set header offset, header bytes, stream table offset, stream bytes, FNV of raw set
    body = bytearray()
    entries = []
    base = dir_at + entry.size * len(sets)
    for s, sc in zip(sets, parsed):
        raw = s['blob']
        hb = sc['header_bytes']
        h_at = base + len(body)
        body += raw[:hb]
        while len(body) % 2:
            body.append(0)
        # stream table: per cell (offset from stream base, bytes); then the alias map
        st_at = base + len(body)
        cells = sc['cells']
        table_size = 10 * len(cells)
        alias = mirror_aliases(cells)
        streams = bytearray()
        offs = []
        for i, c in enumerate(cells):
            if alias[i] & 0x8000:
                offs.append((0, 0))
                continue
            data, nbits = encode(c, codes)
            offs.append((len(streams), len(data)))
            streams += data
        tbl = b''.join(struct.pack('>II', o, n) for o, n in offs) + b''.join(struct.pack('>H', a) for a in alias)
        body += tbl + streams
        while len(body) % 8:
            body.append(0)
        fnv = 2166136261
        for x in raw:
            fnv = ((fnv ^ x) * 16777619) & 0xFFFFFFFF
        entries.append(entry.pack(s['key'].encode(), s['profile'].encode(), h_at, hb, st_at, table_size + len(streams), fnv))
    blob = hdr + b''.join(entries) + body
    blob += bytes(1024)   # SH2Z_CELL4Z_SLACK for the widest cell at lmax 15 is 946 bytes
    Path(out).write_bytes(blob)
    return lens, len(blob)


def measure(sets, sample_check=200):
    parsed = [set_cells(s['blob']) for s in sets]
    lens, codes = train(parsed)
    per = collections.defaultdict(lambda: dict(raw_px_bytes=0, z_bytes=0, cells=0, px=0, cyc_dec=0, cyc_copy=0,
                                              sizes=collections.Counter(), sets=0, set_bytes=0))
    checked = 0
    for s, sc in zip(sets, parsed):
        p = per[s['profile']]
        p['sets'] += 1
        p['set_bytes'] += len(s['blob'])
        for c in sc['cells']:
            data, nbits = encode(c, codes)
            syms = [sy for _, sy in tokens(c['w'], c['h'], c['px'])]
            raw = c['w'] * c['h'] // 2
            p['raw_px_bytes'] += raw
            p['z_bytes'] += len(data)
            p['cells'] += 1
            p['px'] += c['w'] * c['h']
            p['cyc_dec'] += cycles_decode(c, nbits, syms)
            p['cyc_copy'] += cycles_raw_copy(raw)
            p['sizes'][(raw + 255) // 256 * 256] += 1
            if checked < sample_check:
                back, used = decode_ref(data, c['w'], c['h'], lens)
                assert back == c['px'] and used == nbits, 'reference decoder mismatch'
                checked += 1
    res = {}
    for prof, p in sorted(per.items()):
        res[prof] = dict(sets=p['sets'], cells=p['cells'], raw_set_bytes=p['set_bytes'], raw_pixel_bytes=p['raw_px_bytes'],
                         cell4z_bytes=p['z_bytes'], ratio=round(p['z_bytes'] / p['raw_px_bytes'], 4),
                         decode_cycles_per_cell=round(p['cyc_dec'] / p['cells']), copy_cycles_per_cell=round(p['cyc_copy'] / p['cells']),
                         decode_cycles_per_px=round(p['cyc_dec'] / p['px'], 2),
                         cell_bytes_hist={str(k): v for k, v in sorted(p['sizes'].items())})
    res['reference_decoder_checked_cells'] = checked
    return res, lens, codes, parsed

# ---------------------------------------------------------------- synthetic race trace

def track_point(s, L):
    """A closed synthetic course of length L: straights, sweeping and tight turns, two climbs."""
    t = (s % L) / L * 2 * math.pi
    r = L / (2 * math.pi)
    x = r * math.cos(t) + 0.35 * r * math.cos(3 * t + 0.7)
    y = r * 0.6 * math.sin(t) + 0.25 * r * math.sin(2 * t)
    z = 0.04 * r * math.sin(2 * t + 1.1) + 0.02 * r * math.sin(5 * t)
    return x, y, z


def unit(v):
    n = math.sqrt(sum(a * a for a in v)) or 1.0
    return tuple(a / n for a in v)


def frame_of(s, L):
    p = track_point(s, L)
    q = track_point(s + 40.0, L)
    f = unit(tuple(b - a for a, b in zip(p, q)))
    up = (0.0, 0.0, 1.0)
    side = unit((f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0]))
    return p, f, side


def nearest(angle, table):
    return min(range(len(table)), key=lambda i: abs((angle - table[i] + 180) % 360 - 180))


def trace(sets, out, frames, seed, near_dist, view_dist, families=30):
    import random
    rnd = random.Random(seed)
    by = {(s['key'], s['profile']): set_cells(s['blob']) for s in sets}
    keys = sorted({s['key'] for s in sets if s['key'].startswith('c')})[:families]
    def ang(t):
        return [math.degrees(math.atan2(a[0], a[1])) for a in t]
    meta = {}
    for fam_i, k in enumerate(keys):
        for prof in ('play', 'near', 'far'):
            sc = by[(k, prof)]
            meta[(fam_i, prof)] = dict(yaw=ang(sc['yaw']), pitch=ang(sc['pitch']), yc=len(sc['yaw']),
                                       bytes=[c['w'] * c['h'] // 2 for c in sc['cells']])
    L, laps = 79028.0, 3
    # grid: player in the middle of the pack; per-racer pace 36..42 units/tick with drift
    pos = [-(i * 220.0) for i in range(30)]
    rnd.shuffle(pos)
    lat = [rnd.uniform(-500, 500) for _ in range(30)]
    pace = [rnd.uniform(36.0, 42.0) for _ in range(30)]
    cam_back, cam_pitch = 1153.6, math.radians(32.0)
    lines = []
    for f in range(frames):
        for _ in range(2):   # two 60 Hz ticks per 30 fps frame
            for i in range(30):
                pace[i] = min(44.0, max(33.0, pace[i] + rnd.uniform(-0.15, 0.15)))
                pos[i] += pace[i]
                lat[i] = max(-650.0, min(650.0, lat[i] + rnd.uniform(-6, 6)))
        p0, f0, s0 = frame_of(pos[0], L)
        c0 = tuple(p + s * lat[0] for p, s in zip(p0, s0))
        eye = tuple(c - fo * cam_back * math.cos(cam_pitch) + u * cam_back * math.sin(cam_pitch)
                    for c, fo, u in zip(c0, f0, (0.0, 0.0, 1.0)))
        lines.append('F')
        for i in range(30):
            if i == 0:
                prof = 'play'
                d = c0
                dist = cam_back
            else:
                pi, fi, si = frame_of(pos[i], L)
                d = tuple(p + s * lat[i] for p, s in zip(pi, si))
                rel = tuple(a - b for a, b in zip(d, eye))
                dist = math.sqrt(sum(a * a for a in rel))
                depth = sum(a * b for a, b in zip(rel, f0))
                if depth < 200 or dist > view_dist:
                    continue
                lateral = sum(a * b for a, b in zip(rel, s0))
                if abs(lateral) > depth * 1.25:   # outside a ~100 degree field of view
                    continue
                prof = 'near' if dist < near_dist else 'far'
            pr, fr, sr = frame_of(pos[i], L)
            to_cam = unit(tuple(e - a for e, a in zip(eye, d)))
            yaw = math.degrees(math.atan2(sum(a * b for a, b in zip(to_cam, sr)), sum(a * b for a, b in zip(to_cam, fr))))
            pitch = math.degrees(math.asin(max(-1.0, min(1.0, to_cam[2]))))
            m = meta[(i, prof)]
            cell = nearest(pitch, m['pitch']) * m['yc'] + nearest(yaw, m['yaw'])
            key = i * 1024 + {'play': 0, 'near': 1, 'far': 2}[prof] * 256 + cell
            lines.append('R %d %d %s %d' % (key, m['bytes'][cell], prof, i))
    Path(out).write_text('\n'.join(lines) + '\n')
    return len(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('roster', help='RSP1 1.1 roster pack (local, ROM-derived)')
    sub = ap.add_subparsers(dest='cmd', required=True)
    m = sub.add_parser('measure'); m.add_argument('--out', required=True)
    p = sub.add_parser('pack'); p.add_argument('--out', required=True)
    t = sub.add_parser('trace'); t.add_argument('--out', required=True); t.add_argument('--frames', type=int, default=3600)
    t.add_argument('--seed', type=int, default=1); t.add_argument('--near-dist', type=float, default=4500.0)
    t.add_argument('--view-dist', type=float, default=16000.0)
    a = ap.parse_args()
    sets = [s for s in read_roster(a.roster) if s['framing'] == 2]
    if a.cmd == 'measure':
        res, lens, codes, parsed = measure(sets)
        Path(a.out).write_text(json.dumps(res, indent=1))
        print(json.dumps({k: (v if not isinstance(v, dict) else {kk: vv for kk, vv in v.items() if kk != 'cell_bytes_hist'}) for k, v in res.items()}, indent=1))
    elif a.cmd == 'pack':
        lens, n = write_pack(sets, a.out)
        print('RSZ1 pack', n, 'bytes,', len(sets), 'sets, sha256', hashlib.sha256(Path(a.out).read_bytes()).hexdigest())
    else:
        n = trace(sets, a.out, a.frames, a.seed, a.near_dist, a.view_dist)
        print('trace lines', n)


if __name__ == '__main__':
    main()
