#!/usr/bin/env python3
"""Race effect art for the engine (lane claude/fx-boost, ENG_FX), baked from the owner's ROM.

Usage: FZX_TEST_ROM=... tools/fx_assets.py [--output generated/fx]

Reads the source race effects the engine draws without the attack code: the collision spark sheet
(aSupernovaTex, I4 32x256 = eight 32x32 frames, the wall sparks) and the booster glow
(aBoosterTrailTex, IA16 16x16, the boost flame head and tail). The source draws them through one
colour combiner (aSetupBoosterDL): colour = ENV + (PRIM - ENV) x I, alpha = A x ENV alpha. racer.c
sets PRIM and ENV per effect and per timer step; those steps become colour tables. VDP1 has no
per-pixel blend, so each frame keeps a pixel by its alpha at its steps through a 4x4 ordered dither
between two levels (ALPHA below): opaque at or above the upper level, none below the lower, a
dither ramp between. Nothing is redrawn: every pixel is a source texel and every colour a source
combiner output. (The explosion, smoke and flying-spark art of the attack effects is left to the
lane that draws them.)

Writes OUT/FX.PAK (one VDP1-ready block: 4-bit frames and colour tables), OUT/fx_identity.h (its
layout, size and FNV-1a), OUT/fx_tables.h (sBoosterInfos of the 30 stock machines, one 17-word
row each, from racer.c, and the guests' rows) and OUT/fx.receipt. The guests' rows come from the
optional OUT/guest_boosters.json ({"33": [17 ints], "34": [17 ints]}, GUEST-BOOSTERS draft 1: a
count, then four x, y, z, size_x16 tuples); a missing file or id is an empty row (nothing drawn).
ROM-derived: generated/ stays local, never committed."""
from pathlib import Path
import argparse
import hashlib
import os
import re
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from import_source_art import ROOT, DECOMP, ArtError, SourceArt, bounded, verify_source  # noqa: E402
from import_course import normalize_rom  # noqa: E402
import json  # noqa: E402

FRAMES = 8
CELL = 32
BOOST = 16
# Kept alpha per effect: (lower, upper) of the dither ramp.
ALPHA = dict(spark=(0.1, 0.5), boost=(0.1, 0.5))
BAYER4 = ((0, 8, 2, 10), (12, 4, 14, 6), (3, 11, 1, 9), (15, 7, 13, 5))


def sha(data):
    return hashlib.sha256(data).hexdigest()


def fnv32(data):
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def raw(src, symbol, fmt, width, height):
    row = src.rows[symbol]
    if row['type'] != 'TEXTURE' or row['format'] != fmt or row['width'] != width or row['height'] != height:
        raise ArtError('Effect texture %s changed in the source table' % symbol)
    size = {'I4': width * height // 2, 'IA8': width * height, 'IA16': width * height * 2}[fmt]
    return bounded(src.segments[row['family']], row['offset'], size)


def texels(data, fmt, width, height):
    """(intensity 0..1, alpha 0..1) per texel, row-major, as the RDP reads them."""
    out = []
    for i in range(width * height):
        if fmt == 'I4':
            v = (data[i // 2] >> (4 if i % 2 == 0 else 0)) & 15
            out.append((v / 15.0, v / 15.0))
        elif fmt == 'IA8':
            v = data[i]
            out.append(((v >> 4) / 15.0, (v & 15) / 15.0))
        else:
            out.append((data[2 * i] / 255.0, data[2 * i + 1] / 255.0))
    return out


def rgb555(c):
    r, g, b = (max(0, min(255, int(round(v)))) for v in c)
    return 0x8000 | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)


def clut(prim, env):
    """Index 0 transparent; index j (1..15) the combiner output at intensity j / 15."""
    words = [0]
    for j in range(1, 16):
        i = j / 15.0
        words.append(rgb555([e + (p - e) * i for p, e in zip(prim, env)]))
    return struct.pack('>16H', *words)


def frame4(tex, width, height, alpha_scale, levels, x_of=None):
    """4-bit indices: the intensity level (at least 1) where alpha x alpha_scale passes the dither."""
    lo, hi = levels
    out = bytearray(width * height // 2)
    kept = 0
    for y in range(height):
        for x in range(width):
            i, a = tex[y * width + x] if x_of is None else x_of(x, y)
            v = 0
            if a * alpha_scale >= lo + (hi - lo) * (BAYER4[y & 3][x & 3] + 0.5) / 16.0:
                v = max(1, int(round(i * 15)))
                kept += 1
            out[(y * width + x) // 2] |= v << (4 if x % 2 == 0 else 0)
    return bytes(out), kept


# Per-step source colour (racer.c, the effects draw): PRIM, ENV and ENV alpha.
def spark_step(t):              # gCollisionSparks, 16 steps
    return (255, 255, 255 - 8 * t), (255, 255 - 16 * t, 0), 255 - 4 * t


BOOST_ENV = ((0, 255, 255), (91, 255, 91), (255, 223, 0))   # idle, boost, dash plate


def machine_tables(decomp):
    """sBoosterInfos per boosters type and the stock machines' boosters type (racer.c)."""
    text = (decomp / 'src/game/racer.c').read_text()
    m = re.search(r'BoosterInfo\s+sBoosterInfos\[\]\s*=\s*\{(.*?)\n\};', text, re.S)
    if not m:
        raise ArtError('missing sBoosterInfos')
    num = r'(-?\d+(?:\.\d+)?)f'
    vec = r'\{\s*' + num + r'\s*,\s*' + num + r'\s*,\s*' + num + r'\s*\}'
    row = (r'\{\s*(\d)\s*,\s*\{\s*' + r'\s*,\s*'.join([vec] * 4) + r'\s*\}\s*,\s*\{\s*' +
           r'\s*,\s*'.join([num] * 4) + r'\s*\}\s*\}')
    boosters = []
    for r in re.finditer(row, m.group(1)):
        g = r.groups()
        boosters.append((int(g[0]), [tuple(float(v) for v in g[1 + 3 * k:4 + 3 * k]) for k in range(4)],
                         [float(v) for v in g[13:17]]))
    if len(boosters) < 30:
        raise ArtError('expected at least 30 booster types')
    m = re.search(r'Machine\s+sDefaultMachines\[\]\s*=\s*\{(.*?)\n\};', text, re.S)
    types = [int(v) for v in re.findall(r'\{\s*CUSTOM_MACHINE_\w+\s*,\s*\d+\s*,\s*(\d+)\s*,', m.group(1))]
    if len(types) < 30:
        raise ArtError('expected 30 stock machines')
    return boosters, types[:30]


def guest_rows(path):
    """The guests' 17-word rows (GUEST-BOOSTERS draft 1), validated; an empty row where none is given."""
    rows = {'33': [0] * 17, '34': [0] * 17}
    if path.exists():
        for k, v in json.loads(path.read_text()).items():
            if k not in rows:
                raise ArtError('guest booster row for id %s: only 33 and 34 are guests' % k)
            if len(v) != 17 or any(not isinstance(w, int) or not -32768 <= w <= 32767 for w in v):
                raise ArtError('guest booster row %s: 17 signed 16-bit words expected' % k)
            count = v[0]
            if not 0 <= count <= 4:
                raise ArtError('guest booster row %s: count %d outside 0..4' % (k, count))
            for i in range(4):
                t = v[1 + 4 * i:5 + 4 * i]
                if i < count and t[3] <= 0:
                    raise ArtError('guest booster row %s: live socket %d needs a positive size' % (k, i))
                if i >= count and any(t):
                    raise ArtError('guest booster row %s: spare socket %d must be zero' % (k, i))
            rows[k] = list(v)
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--output', type=Path, default=ROOT / 'generated/fx')
    ap.add_argument('--skin', type=int, default=1, help='the skin the rival sprite sets are baked with')
    args = ap.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to((ROOT / 'generated').resolve()):
        ap.error('output must stay in generated/')
    path = os.environ.get('FZX_TEST_ROM')
    if not path:
        ap.error('Set FZX_TEST_ROM locally; do not put its path in logs')
    rom = Path(path).read_bytes()
    normalized, _ = normalize_rom(rom)
    verify_source(DECOMP)
    src = SourceArt(normalized)

    blocks, layout, inputs, kept = [], {}, [], {}

    def put(name, data, align=8):
        at = sum(len(b) for b in blocks)
        pad = (-at) % align
        if pad:
            blocks.append(bytes(pad))
            at += pad
        layout[name] = at
        blocks.append(data)

    # Wall sparks: frame k serves steps 2k, 2k+1 (gCollisionSparks).
    for name, symbol, step, per in (('SPARK', 'aSupernovaTex', spark_step, 2),):
        data = raw(src, symbol, 'I4', CELL, CELL * FRAMES)
        inputs.append((symbol, data))
        tex = texels(data, 'I4', CELL, CELL * FRAMES)
        frames = b''
        for k in range(FRAMES):
            envA = sum(step(t)[2] for t in range(per * k, per * k + per)) / float(per)
            f, n = frame4(tex[k * CELL * CELL:(k + 1) * CELL * CELL], CELL, CELL, envA / 255.0, ALPHA[name.lower()])
            frames += f
            kept['%s%d' % (name.lower(), k)] = n
        put(name, frames)
    data = raw(src, 'aBoosterTrailTex', 'IA16', BOOST, BOOST)
    inputs.append(('aBoosterTrailTex', data))
    glow = texels(data, 'IA16', BOOST, BOOST)
    head, kept['boost_head'] = frame4(glow, BOOST, BOOST, 1.0, ALPHA['boost'])
    put('BOOST_HEAD', head)

    # The tail triangle (v1, v4, v3) samples texels (0,0), (16,0), (16,16) of the glow; drawn as a
    # distorted sprite with corners (v1, v4, v3, v3), cell pixel (x, y) lands where the source
    # samples s = 16 ((1 - v) u + v), t = 16 v (u, v the pixel centre over 16).
    def tail_of(x, y):
        u, v = (x + 0.5) / BOOST, (y + 0.5) / BOOST
        s, t = BOOST * ((1 - v) * u + v), BOOST * v
        return glow[min(BOOST - 1, int(t)) * BOOST + min(BOOST - 1, int(s))]
    tail, kept['boost_tail'] = frame4(glow, BOOST, BOOST, 1.0, ALPHA['boost'], tail_of)
    put('BOOST_TAIL', tail)

    put('CLUT_SPARK', b''.join(clut(*spark_step(t)[:2]) for t in range(16)), 32)
    put('CLUT_BOOST', b''.join(clut((255, 255, 255), env) for env in BOOST_ENV), 32)
    pak = b''.join(blocks)
    pak += bytes((-len(pak)) % 32)

    boosters, types = machine_tables(DECOMP)

    guest = guest_rows(out / 'guest_boosters.json')
    out.mkdir(parents=True, exist_ok=True)
    (out / 'FX.PAK').write_bytes(pak)
    with open(out / 'fx_identity.h', 'w', newline='\n') as o:
        o.write('/* Generated by tools/fx_assets.py from the owner\'s ROM; regenerate, never edit, never commit. */\n')
        o.write('#ifndef FZX_FX_IDENTITY_H\n#define FZX_FX_IDENTITY_H\n')
        o.write('#define FZX_FX_PACK_BYTES %uu\n#define FZX_FX_PACK_HASH 0x%08Xu\n' % (len(pak), fnv32(pak)))
        for name, at in layout.items():
            o.write('#define FZX_FX_%s %uu\n' % (name, at))
        o.write('#endif\n')
    with open(out / 'fx_tables.h', 'w', newline='\n') as o:
        o.write('/* Generated by tools/fx_assets.py from the decomp (racer.c sBoosterInfos, sDefaultMachines):\n'
                ' * game data, local only; regenerate, never edit, never commit. */\n')
        o.write('#ifndef FZX_FX_TABLES_H\n#define FZX_FX_TABLES_H\n#include <stdint.h>\n')
        o.write('/* Per stock machine: booster count, then per booster x, y, z (model units) and size x 16. */\n')
        o.write('static const int16_t fzx_fx_boosters[30][1 + 4 * 4] = {\n')
        for fam in range(30):
            count, pos, size = boosters[types[fam]]
            vals = [count]
            for k in range(4):
                vals += [int(round(pos[k][0])), int(round(pos[k][1])), int(round(pos[k][2])), int(round(size[k] * 16))]
            o.write('    {%s},\n' % ', '.join(str(v) for v in vals))
        o.write('};\n')
        o.write('/* Rows of the rendered machine ids 33 (the classic Eggmobile) and 34 (Metal Sonic): GUEST-BOOSTERS\n'
                ' * draft 1, validated here; an all-zero row (count 0) draws nothing. */\n')
        o.write('static const int16_t fzx_fx_guest_boosters[2][1 + 4 * 4] = {\n')
        for gid in ('33', '34'):
            o.write('    {%s},\n' % ', '.join(str(v) for v in guest[gid]))
        o.write('};\n#endif\n')
    rec = ['# fzx generation receipt v1; regenerate, never edit',
           'generator %s fx_assets.py' % sha(Path(__file__).read_bytes()),
           'generator %s import_source_art.py' % sha((Path(__file__).parent / 'import_source_art.py').read_bytes()),
           'input %s skin-%d' % (sha(str(args.skin).encode()), args.skin),
           'input %s racer.c' % sha((DECOMP / 'src/game/racer.c').read_bytes())]
    rec += ['input %s %s' % (sha(d), n) for n, d in inputs]
    if (out / 'guest_boosters.json').exists():
        rec.append('input %s guest_boosters.json' % sha((out / 'guest_boosters.json').read_bytes()))
    for name in ('FX.PAK', 'fx_identity.h', 'fx_tables.h'):
        rec.append('output %s %s' % (sha((out / name).read_bytes()), name))
    with open(out / 'fx.receipt', 'w', newline='\n') as o:
        o.write('\n'.join(rec) + '\n')
    print('fx assets: %d bytes, %d blocks; kept pixels %s' % (len(pak), len(layout),
          ' '.join('%s=%d' % kv for kv in sorted(kept.items()))))


if __name__ == '__main__':
    main()
