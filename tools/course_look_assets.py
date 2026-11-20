#!/usr/bin/env python3
"""Course-look assets: the spans the camera can be inside (pipes and tunnels), and the night sky.

Reads the inside-span list that `fzx-ref --inside` writes (course.c TRACK_FLAG_INSIDE spans with
their join t values and join scale) and writes look_inside.h, the table the engine's look
switches read (ENG_LOOK_FOG, ENG_LOOK_OUTER, ENG_LOOK_CLOSE in saturn/engine/Makefile).

With --night (a SKYBOX_STARRY course; FZX_TEST_ROM set) it also writes look_night.h for
ENG_LOOK_STARS and ENG_LOOK_WINDOWS (background.c):
- the star field (Background_InitStars: 100 stars, height 100,000..200,000, radius under
  800,000, ten brightness steps). Positions come from a fixed seed, since the source draws
  them from the game's running random number;
- the star texture (common assets D_F2207C8, IA8 8x8) baked into ten 8x8 RGB1555 sprites: the
  star's primitive colour times the texel intensity, blended over black by the texel alpha,
  transparent where that rounds to zero;
- the night window colours (sBackgroundFlashingColors).

Both outputs are course or ROM data: they go to the git-ignored generated/ folder only, and the
receipt records hashes, never the data.

    tools/course_look_assets.py --source INSIDE.txt --line LINE.txt [--output generated/look] [--night]

Q30 t values: a span without a previous join stores -1.0 (t never falls below it), one without
a next join stores the largest Q30 value (t never rises above it). The source's 0.49/0.51
overlap case is kept as written.
"""
import argparse
import hashlib
import math
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GENERATORS = ('tools/course_look_assets.py',)
SHAPES = {2: 'pipe', 5: 'tunnel'}  # TRACK_SHAPE_INDEX of the inside shapes
NO_PREV, NO_NEXT = -(1 << 30), 0x7FFFFFFF
STAR_COUNT, STAR_LEVELS = 100, 10


def q30(v):
    return int(round(v * (1 << 30)))


def parse(path):
    rows = []
    for line in Path(path).read_text().splitlines():
        f = line.split()
        if not f or f[0].startswith('#'):
            continue
        if f[0] != 'inside' or len(f) != 6:
            raise SystemExit('course look: unexpected line %r' % line[:40])
        seg, shape = int(f[1]), int(f[2])
        prev_end, next_start, scale = (float.fromhex(x) for x in f[3:])
        if shape not in SHAPES:
            raise SystemExit('course look: span %d has inside shape %d' % (seg, shape))
        has_prev, has_next = prev_end > -1.0, next_start < 2.0
        if (has_prev or has_next) and not 0.0 < scale <= 1.0:
            raise SystemExit('course look: span %d join scale %r' % (seg, scale))
        rows.append((seg, shape, q30(prev_end) if has_prev else NO_PREV,
                     q30(next_start) if has_next else NO_NEXT, q30(scale) if (has_prev or has_next) else 1 << 30))
    if [r[0] for r in rows] != sorted(set(r[0] for r in rows)):
        raise SystemExit('course look: spans out of order')
    return rows


def line_distance(stations, seg, t):
    """Lap distance (Q12) of span seg at spline t, interpolated between the course-line stations."""
    rows = [(st, d) for d, sg, st in stations if sg == seg]
    if not rows:
        raise SystemExit('course look: no course-line station on span %d' % seg)
    lo = max((r for r in rows if r[0] <= t), default=min(rows), key=lambda r: r[0])
    hi = min((r for r in rows if r[0] >= t), default=max(rows), key=lambda r: r[0])
    if hi[0] == lo[0]:
        return lo[1]
    return int(round(lo[1] + (hi[1] - lo[1]) * (t - lo[0]) / (hi[0] - lo[0])))


def openings(rows, line_path):
    """For each inside span, the lap distance (Q12) where the tube next opens ahead of it: the start
    of the next join of its run of consecutive inside spans (or the run's end); and the lap length."""
    text = Path(line_path).read_text().splitlines()
    head = text[0].split()
    if head[0] != 'course-line-1':
        raise SystemExit('course look: not a course line')
    length = int(head[1])
    stations = []
    for line in text[1:]:
        f = line.split()
        stations.append((int(f[0]), int(f[1]), float.fromhex(f[3])))
    by_seg = {r[0]: r for r in rows}
    nseg = max(sg for _, sg, _ in stations) + 1
    out = {}
    for seg in by_seg:
        s = seg
        for _ in range(nseg):
            r = by_seg[s]
            if r[3] != NO_NEXT:
                out[seg] = line_distance(stations, s, r[3] / float(1 << 30))
                break
            nxt = (s + 1) % nseg
            if nxt not in by_seg:
                out[seg] = line_distance(stations, nxt, 0.0)
                break
            s = nxt
        else:
            raise SystemExit('course look: an inside run without an opening')
    return out, length


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def stars(seed=0x5EED5):
    """Background_InitStars' distribution from a fixed-seed generator: x, y, z (source units), level 0..9."""
    state = seed

    def rand():
        nonlocal state
        state = (state * 1103515245 + 12345) & 0x7FFFFFFF
        return state >> 8

    out = []
    for _ in range(STAR_COUNT):
        y = rand() % 100000 + 100000
        angle = rand() % 0x1000
        scale = rand() % 800000
        a = angle * 2.0 * math.pi / 4096.0
        out.append((int(round(math.cos(a) * scale)), y, int(round(math.sin(a) * scale)), rand() % STAR_LEVELS))
    return out


def night(rom_bytes):
    """Star sprites (ten levels x 64 RGB1555 words) and the night window colours (5-bit RGB)."""
    sys.path.insert(0, str(ROOT / 'tools'))
    import backdrop_assets
    from import_course import normalize_rom
    from import_source_art import DECOMP, YAML
    rom, _ = normalize_rom(rom_bytes)
    rows = backdrop_assets.catalog((DECOMP / (YAML + 'common_assets_compressed.yaml')).read_text())
    record, texels = backdrop_assets.read_asset(rom, rows, 'D_F2207C8')
    if (record['format'], record['width'], record['height']) != ('IA8', 8, 8):
        raise SystemExit('course look: unexpected star texture')
    source = (DECOMP / 'src/overlays/ovl_i3/background.c').read_text()
    m = re.search(r'sBackgroundFlashingColors\[\]\[3\]\s*=\s*\{(.*?)\};', source, re.S)
    if not m:
        raise SystemExit('course look: missing the night window colours')
    flash = [tuple(int(v) for v in g.split(',')) for g in re.findall(r'\{\s*([0-9,\s]+?)\s*\}', m[1])]
    if not flash or any(len(c) != 3 or not all(0 <= v <= 31 for v in c) for c in flash):
        raise SystemExit('course look: unexpected night window colours')
    sprites = []
    for level in range(STAR_LEVELS):
        scale = (level + 1) * 0.1
        prim = (int(255.0 * scale), int(255.0 * scale), int(64.0 * scale))  # Background_InitStars' u8 stores
        for v in texels:
            i, a = (v >> 4) * 17, (v & 15) * 17
            c = [int(round(p * i * a / (255.0 * 255.0) * 31.0 / 255.0)) for p in prim]
            sprites.append(0 if not any(c) else 0x8000 | c[2] << 10 | c[1] << 5 | c[0])
    return sprites, flash


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--source', type=Path, required=True, help='inside-span list written by fzx-ref --inside')
    ap.add_argument('--output', type=Path, default=ROOT / 'generated/look')
    ap.add_argument('--line', type=Path, required=True, help='course-line stations written by fzx-ref --line')
    ap.add_argument('--night', action='store_true', help='also the star field, star sprites and window colours')
    args = ap.parse_args()
    rows = parse(args.source)
    opening, lap = openings(rows, args.line)
    out = args.output
    out.mkdir(parents=True, exist_ok=True)
    body = ['/* Generated by tools/course_look_assets.py; regenerate, never edit. */',
            '#ifndef FZX_LOOK_INSIDE_H', '#define FZX_LOOK_INSIDE_H',
            '/* Spans the camera can be inside (course.c TRACK_FLAG_INSIDE): span, shape index',
            ' * (2 pipe, 5 tunnel), Q30 t where the previous join ends and the next one starts,',
            ' * Q30 join scale (share of the span\'s arc length one join covers), and the lap',
            ' * distance (Q12) where the tube next opens ahead (its run\'s next join or end). */',
            '#define FZX_LOOK_INSIDE_COUNT %du' % len(rows),
            '#define FZX_LOOK_LAP_Q12 %d' % lap,
            '#define FZX_LOOK_INSIDE_TABLE { \\']
    body += ['    { %du, %du, %d, %d, %d, %d }, \\' % (r + (opening[r[0]],)) for r in rows]
    body += ['    { 0xffffu, 0u, 0, 0, 0, 0 } }', '#endif', '']
    (out / 'look_inside.h').write_text('\n'.join(body), newline='\n')
    lines = ['# fzx generation receipt v1; regenerate, never edit']
    for rel in GENERATORS:
        lines.append('generator %s %s' % (sha(ROOT / rel), os.path.relpath(ROOT / rel, ROOT / 'tools')))
    lines.append('input %s inside-spans' % sha(args.source))
    lines.append('input %s course-line' % sha(args.line))
    lines.append('output %s look_inside.h' % sha(out / 'look_inside.h'))
    if args.night:
        rom_path = os.environ.get('FZX_TEST_ROM')
        if not rom_path:
            raise SystemExit('course look: set FZX_TEST_ROM locally for --night; keep its path out of logs')
        rom_bytes = Path(rom_path).read_bytes()
        sprites, flash = night(rom_bytes)
        text = ['/* Generated by tools/course_look_assets.py --night; regenerate, never edit. */',
                '#ifndef FZX_LOOK_NIGHT_H', '#define FZX_LOOK_NIGHT_H',
                '#define FZX_LOOK_STAR_COUNT %du' % STAR_COUNT,
                '#define FZX_LOOK_STAR_LEVELS %du' % STAR_LEVELS,
                '/* x, y, z (source units), brightness level */',
                '#define FZX_LOOK_STARS { \\']
        text += ['    { %d, %d, %d, %d }, \\' % r for r in stars()]
        text += ['}', '/* STAR_LEVELS 8x8 RGB1555 sprites, row major; 0 transparent */',
                 '#define FZX_LOOK_STAR_SPRITES { \\']
        text += ['    ' + ', '.join('0x%04x' % w for w in sprites[i:i + 8]) + ', \\' for i in range(0, len(sprites), 8)]
        text += ['}', '#define FZX_LOOK_FLASH_COUNT %du' % len(flash),
                 '#define FZX_LOOK_FLASH { %s }' % ', '.join('{ %d, %d, %d }' % c for c in flash), '#endif', '']
        (out / 'look_night.h').write_text('\n'.join(text), newline='\n')
        lines.append('input %s owner-rom' % hashlib.sha256(rom_bytes).hexdigest())
        lines.append('output %s look_night.h' % sha(out / 'look_night.h'))
    (out / 'look.receipt').write_text('\n'.join(lines) + '\n', newline='\n')
    print('course look: %d inside spans (%s)%s' % (len(rows), ', '.join(
        '%d %s' % (sum(1 for r in rows if r[1] == s), n) for s, n in SHAPES.items()),
        '; night sky: %d stars, %d sprite levels' % (STAR_COUNT, STAR_LEVELS) if args.night else ''))


if __name__ == '__main__':
    main()
