#!/usr/bin/env python3
"""Bundle several baked courses into one engine image (ENG_COURSE_SELECT, run-time course choice).

For each course (tools/course_catalog.py, baked by the per-course tools) this reads the identity
headers the single-course image compiles in and writes them as data, one row per course:
    generated/courses/select/course_ids.c   const fzx_course_id_t fzx_course_ids[] (sizes, hashes,
                                             disc names, sky and skyline tables, rival field, record)
    generated/courses/select/course_ids.h   the row type, the count and the largest sizes (buffers)
    generated/courses/select/files.txt      "<generated path> <disc name>" for the disc staging
Each course's packs get their own disc names (a course digit), so one disc carries every course.
Everything here is derived from the owner's ROM: generated/ only, never committed.

    tools/course_bundle.py mute-city-1 silence-1 devils-forest-1 port-town-1
"""
import hashlib, re, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import course_catalog  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'generated/courses/select'
# pack -> (generated subpath, disc stem); FLOORMAP/FLOORKOF are the same for every course (checked).
PACKS = [('epath', 'engine_path/ENGPATH.PAK', 'EPATH'), ('road', 'road_pack/ROAD.PAK', 'ROAD'),
         ('rlod', 'road_bake/ROADLOD.PAK', 'RLOD'), ('rltx', 'road_bake/ROADLTX.PAK', 'RLTX'),
         ('rprep', 'road_prep/ROADPREP.PAK', 'RPREP'), ('skyline', 'skyline/SKYLINE.PAK', 'SKYL'),
         ('planes', 'planes_cloud/PLANES.PAK', 'PLANE'), ('fchr', 'floor_rbg/FLOORCHR.BIN', 'FCHR'),
         ('phys', 'physics/PHYS.PAK', 'PHYS'), ('rivals', 'rivals/RIVALS.PAK', 'RIV')]


def defines(path):
    return {m[1]: m[2].strip() for m in re.finditer(r'^#define[ \t]+(\w+)[ \t]+([^\n]+)$', path.read_text(), re.M)}


def num(v):
    return int(v.rstrip('uUlL'), 0)


def array(path, name):
    m = re.search(r'%s\[[^\]]*\](?:\[[^\]]*\])?\s*(?:__attribute__\(\(\w+\(\d+\)\)\))?\s*=\s*\{(.*?)\};' % re.escape(name),
                  path.read_text(), re.S)
    if not m:
        raise SystemExit('%s: no %s' % (path, name))
    return [int(v, 0) for v in re.findall(r'-?(?:0x[0-9a-fA-F]+|\d+)', m[1])]


def row(slug):
    c, g = course_catalog.course(slug), course_catalog.gen_root(slug)
    d = {}
    for h in ('road_pack/road_identity.h', 'road_bake/road_lod_identity.h', 'road_prep/road_prep_identity.h',
              'skyline/skyline_identity.h', 'skyline/skyline_ring.h', 'planes_cloud/planes_identity.h',
              'floor_rbg/floor_rbg_identity.h', 'engine_path/engine_path_identity.h',
              'engine_path_ranges/engine_path_ranges.h', 'physics/physics_identity.h', 'rivals/rivals_course.h',
              'rivals/rivals_field.h', 'rivals/rivals_zones.h'):
        d.update(defines(g / h))
    sky = array(g / 'sky/sky_colors.h', 'fzx_sky_colors')
    breaks = array(g / 'sky/sky_colors.h', 'fzx_sky_breaks')
    field = array(g / 'rivals/rivals_field.h', 'fzx_rivals_field')
    zones = array(g / 'rivals/rivals_zones.h', 'fzx_rivals_zones')[:5 * num(d['FZX_RIVALS_ZONE_COUNT'])]
    cols = 8 if '[8]' in (g / 'rivals/rivals_field.h').read_text() else 7
    rec = (g / 'race/course.bin' if slug != course_catalog.DEFAULT else g / 'road/course.bin').read_bytes()
    if len(rec) != 0x7E0:
        raise SystemExit(slug + ': course record')
    files = []
    for key, sub, stem in PACKS:
        files.append((g / sub, '%s%d.%s' % (stem, c['index'], sub.rsplit('.', 1)[1])))
    return dict(c=c, d=d, sky=sky, breaks=breaks, field=field, cols=cols, rec=rec, files=files, zones=zones,
                dash=int(d['FZX_RIVALS_DASH_FROM']),
                fog=[int(v) for v in re.findall(r'\d+', d['FZX_ROAD_COURSE_FOG'])],
                plane_fog=array(g / 'planes_cloud/planes_identity.h', 'fzx_plane_fog'),
                plane_mean=array(g / 'planes_cloud/planes_identity.h', 'fzx_plane_mean'),
                ring=[int(v) for v in re.findall(r'-?\d+', d['FZX_SKYLINE_RING'])],
                ids=[int(v) for v in re.findall(r'\d+', d['FZX_SKYLINE_IDS'])],
                tint=[int(v) for v in re.findall(r'\d+', d['FZX_SKYLINE_TINT'])],
                floor_shared=(num(d['FZX_FLOORMAP_HASH']), num(d['FZX_FLOORKOF_HASH'])))


PREP = ('NUNIT', 'NPOS', 'NQUADS', 'NSPANS', 'NCHUNKS', 'IMAGES')
U32 = [('path_bytes', 'FZX_ENGINE_PATH_BYTES'), ('path_hash', 'FZX_ENGINE_PATH_HASH'),
       ('path_file_hash', 'FZX_ENGINE_PATH_FILE_HASH'), ('path_ticks', 'FZX_ENGINE_PATH_TICKS'),
       ('head_bytes', 'FZX_EPATH_HEAD_BYTES'), ('head_hash', 'FZX_EPATH_HEAD_HASH'),
       ('lens_at', 'FZX_EPATH_LENS_AT'), ('lens_bytes', 'FZX_EPATH_LENS_BYTES'), ('lens_hash', 'FZX_EPATH_LENS_HASH'),
       ('road_bytes', 'FZX_ROAD_PACK_BYTES'), ('road_hash', 'FZX_ROAD_PACK_HASH'),
       ('lod_bytes', 'FZX_ROAD_LOD_BYTES'), ('lod_tex_bytes', 'FZX_ROAD_LOD_TEXTURE_BYTES'),
       ('lod_file_hash', 'FZX_ROAD_LOD_FILE_HASH'), ('lod_tex_file_hash', 'FZX_ROAD_LOD_TEXTURE_FILE_HASH'),
       ('prep_bytes', 'FZX_ROAD_PREP_BYTES'), ('prep_lod_hash', 'FZX_ROAD_PREP_LOD_HASH'),
       ('prep_ltx_hash', 'FZX_ROAD_PREP_LTX_HASH'), ('prep_npos_count', 'FZX_ROAD_PREP_NPOS_COUNT'),
       ('prep_quads', 'FZX_ROAD_PREP_QUADS'), ('prep_chunks', 'FZX_ROAD_PREP_CHUNKS'),
       ('prep_images', 'FZX_ROAD_PREP_IMAGES'), ('prep_strips', 'FZX_ROAD_PREP_STRIPS'),
       ('skyline_bytes', 'FZX_SKYLINE_PACK_BYTES'), ('skyline_hash', 'FZX_SKYLINE_PACK_HASH'),
       ('plane_hash', 'FZX_PLANE_HASH'), ('fchr_bytes', 'FZX_FLOORCHR_BYTES'), ('fchr_hash', 'FZX_FLOORCHR_HASH'),
       ('floor_fog', 'FZX_FLOOR_FOG'), ('phys_bytes', 'FZX_PHYSICS_PACK_BYTES'), ('phys_hash', 'FZX_PHYSICS_PACK_HASH'),
       ('rivals_bytes', 'FZX_RIVALS_PACK_BYTES'), ('rivals_hash', 'FZX_RIVALS_PACK_HASH'),
       ('rivals_nodes', 'FZX_RIVALS_NODE_COUNT'), ('rivals_length', 'FZX_RIVALS_LINE_LENGTH'),
       ('field_seed', 'FZX_RIVALS_FIELD_SEED')]
ZONE_MAX = 32  # = FZX_RIVALS_ZONE_MAX
MAXES = ('path_bytes', 'road_bytes', 'lod_bytes', 'lod_tex_bytes', 'prep_bytes', 'phys_bytes', 'rivals_bytes',
         'prep_npos_count', 'prep_strips')


def fnv(b):
    h = 2166136261
    for x in b:
        h = ((h ^ x) * 16777619) & 0xFFFFFFFF
    return h


def layout(maxsb):
    """The fzx_course_id_t fields in declaration order: (name, struct format, count, element size). The C
    compiler lays the struct out with natural alignment; course_ids.h asserts every offset computed here."""
    f = [('id', 'B', 1, 1), ('row', 'B', 1, 1), ('sky_linear', 'B', 1, 1), ('skyline_count', 'B', 1, 1),
         ('sky_breaks_count', 'B', 1, 1), ('pad', 'B', 3, 1), ('files', 's', len(PACKS) * 12, 1)]
    f += [(k, 'I', 1, 4) for k, _ in U32]
    f += [('prep', 'I', 18, 4), ('ring', 'i', 12, 4), ('ring_ids', 'B', 6, 1), ('tint', 'B', 3, 1), ('road_fog', 'B', 3, 1),
          ('plane_fog', 'H', 3, 2), ('plane_mean', 'H', 3, 2), ('sky_colors', 'H', 64, 2), ('sky_breaks', 'B', maxsb, 1),
          ('field', 'i', 240, 4), ('zone_count', 'i', 1, 4), ('dash_from', 'i', 1, 4), ('zones', 'i', ZONE_MAX * 5, 4),
          ('record', 'B', 0x7e0, 1)]
    out, at = [], 0
    for name, fmt, n, size in f:
        at = (at + size - 1) // size * size
        out.append((name, fmt, n, size, at))
        at += n * size
    return out, (at + 3) // 4 * 4


def row_bytes(r, index, maxsb):
    """One course's identity row as big-endian bytes in fzx_course_id_t's layout (the disc file CIDnn.BIN)."""
    import struct
    d = r['d']
    keys = [(v & 31) + ((v >> 5) & 31) + ((v >> 10) & 31) for v in r['sky']]
    monotonic = all(b >= a for a, b in zip(keys, keys[1:]))
    zones = list(r['zones']) + [0] * (ZONE_MAX * 5 - len(r['zones']))
    prep = []
    for t in PREP:
        prep += [num(d['FZX_ROAD_PREP_%s_%s' % (t, k)]) for k in ('AT', 'BYTES', 'HASH')]
    vals = dict(id=[r['c']['index']], row=[index], sky_linear=[0 if monotonic else 1],
                skyline_count=[num(d['FZX_SKYLINE_RING_COUNT'])], sky_breaks_count=[len(r['breaks'])], pad=[0, 0, 0],
                files=[b''.join(n.encode().ljust(12, b'\0') for _, n in r['files'])],
                prep=prep, ring=list(r['ring']), ring_ids=list(r['ids']), tint=list(r['tint']), road_fog=list(r['fog']),
                plane_fog=list(r['plane_fog']), plane_mean=list(r['plane_mean']), sky_colors=list(r['sky']),
                sky_breaks=list(r['breaks']) + [0] * (maxsb - len(r['breaks'])), field=list(r['field']),
                zone_count=[len(r['zones']) // 5], dash_from=[r['dash']], zones=zones, record=list(r['rec']))
    for k, m in U32:
        vals[k] = [num(d[m]) & 0xFFFFFFFF]
    spec, total = layout(maxsb)
    buf = bytearray(total)
    for name, fmt, n, size, at in spec:
        v = vals[name]
        if fmt == 's':
            buf[at:at + n] = v[0]
        else:
            if len(v) != n:
                raise SystemExit('%s: %s has %d values, layout %d' % (r['c']['slug'], name, len(v), n))
            struct.pack_into('>%d%s' % (n, fmt), buf, at, *v)
    return bytes(buf)


def main():
    slugs = sys.argv[1:] or list(course_catalog.SHIPPED)
    rows = [row(s) for s in slugs]
    if len({r['floor_shared'] for r in rows}) != 1:
        raise SystemExit('FLOORMAP/FLOORKOF differ between courses')
    if any(r['cols'] != 8 or len(r['field']) != 30 * 8 for r in rows):
        raise SystemExit('rival field headers need 30 rows of 8 columns (tools/rivals_field_header.py)')
    OUT.mkdir(parents=True, exist_ok=True)
    maxsb = max(len(r['breaks']) for r in rows)
    spec, total = layout(maxsb)
    blobs = [row_bytes(r, i, maxsb) for i, r in enumerate(rows)]
    names = ['CID%02d.BIN' % r['c']['index'] for r in rows]
    for n, b in zip(names, blobs):
        (OUT / n).write_bytes(b)
    h = ['/* Generated by tools/course_bundle.py; derived from the owner\'s ROM: never commit. */',
         '#ifndef FZX_COURSE_IDS_H', '#define FZX_COURSE_IDS_H', '#include <stdint.h>', '#include <stddef.h>',
         '#define FZX_COURSE_IDS_COUNT %du' % len(rows),
         '#define FZX_COURSE_BUNDLE_MASK 0x%x%s  /* bit c: course c is on this disc (64-bit when Death Race, course 54, is on it) */' % (
             sum(1 << r['c']['index'] for r in rows), 'ull' if any(r['c']['index'] >= 32 for r in rows) else 'u')]
    for k in MAXES:
        h.append('#define FZX_COURSE_MAX_%s %du' % (k.upper(), max(num(r['d'][dict(U32)[k]]) for r in rows)))
    h.append('#define FZX_COURSE_MAX_PREP_NUNIT_BYTES %du' % max(num(r['d']['FZX_ROAD_PREP_NUNIT_BYTES']) for r in rows))
    h.append('#define FZX_COURSE_MAX_SKY_BREAKS %du' % maxsb)
    h += ['#define FZX_COURSE_ROW_BYTES %du  /* one CIDnn.BIN: the selected course\'s row, read at LOAD */' % total,
          'typedef struct { uint32_t at, bytes, hash; } fzx_course_table_t;',
          'typedef struct {', '    uint8_t id, row, sky_linear, skyline_count, sky_breaks_count;',
          '    uint8_t pad[3];          /* row: this course\'s position in fzx_course_index (explicit, never pointer arithmetic) */',
          '    char files[%d][12];      /* disc names, in the order of fzx_course_file_e */' % len(PACKS)]
    h += ['    uint32_t %s;' % k for k, _ in U32]
    h += ['    fzx_course_table_t prep[6];      /* NUNIT, NPOS, NQUADS, NSPANS, NCHUNKS, IMAGES */',
          '    int32_t ring[6][2];', '    uint8_t ring_ids[6], tint[3], road_fog[3];',
          '    uint16_t plane_fog[3], plane_mean[3], sky_colors[64];', '    uint8_t sky_breaks[FZX_COURSE_MAX_SKY_BREAKS];',
          '    int32_t field[30][8];',
          '    int32_t zone_count, dash_from;   /* line-field-4: the rival surface (tools/rivals_zones_header.py) */',
          '    int32_t zones[%d][5];' % ZONE_MAX, '    uint8_t record[0x7e0];', '} fzx_course_id_t;']
    h += ['_Static_assert(offsetof(fzx_course_id_t, %s) == %du, "course row layout: %s");' % (n, at, n)
          for n, fmt, cnt, size, at in spec if n not in ('pad',)]
    h += ['_Static_assert(sizeof(fzx_course_id_t) == FZX_COURSE_ROW_BYTES, "course row size");',
          '/* The index stays in the image (8 B a course): the row file\'s size and FNV, checked as it is read. */',
          'typedef struct { uint8_t id, pad; uint16_t bytes; uint32_t hash; } fzx_course_index_t;',
          'enum { %s };' % ', '.join('FZX_CF_%s' % k.upper() for k, _, _ in PACKS),
          'extern const fzx_course_index_t fzx_course_index[FZX_COURSE_IDS_COUNT];', '#endif', '']
    (OUT / 'course_ids.h').write_bytes('\n'.join(h).encode())
    c = ['/* Generated by tools/course_bundle.py; derived from the owner\'s ROM: never commit. */',
         '#include "course_ids.h"', 'const fzx_course_index_t fzx_course_index[FZX_COURSE_IDS_COUNT] = {']
    for r, b in zip(rows, blobs):
        c.append('  { .id = %du, .bytes = %d, .hash = %du },' % (r['c']['index'], len(b), fnv(b)))
    c += ['};', '']
    (OUT / 'course_ids.c').write_bytes('\n'.join(c).encode())
    files = ''.join('%s %s\n' % (p.relative_to(ROOT / 'generated'), n) for r in rows for p, n in r['files'])
    files += ''.join('courses/select/%s %s\n' % (n, n) for n in names)
    (OUT / 'files.txt').write_bytes(files.encode())
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    rec = ['# fzx generation receipt v1; regenerate, never edit',
           'generator %s course_bundle.py' % sha(Path(__file__)), 'generator %s course_catalog.py' % sha(ROOT / 'tools/course_catalog.py')]
    for r in rows:
        for p, n in r['files']:
            rec.append('input %s %s' % (sha(p), n))
    rec += ['output %s %s' % (sha(OUT / n), n) for n in ['course_ids.h', 'course_ids.c', 'files.txt'] + names]
    (OUT / 'course_ids.receipt').write_bytes(('\n'.join(rec) + '\n').encode())
    print('course bundle: %d courses (%s), %d disc files, row %d B' % (len(rows), ', '.join(slugs), sum(len(r['files']) for r in rows) + len(rows), total))


if __name__ == '__main__':
    main()
