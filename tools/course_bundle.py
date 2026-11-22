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


def main():
    slugs = sys.argv[1:] or list(course_catalog.SHIPPED)
    rows = [row(s) for s in slugs]
    if len({r['floor_shared'] for r in rows}) != 1:
        raise SystemExit('FLOORMAP/FLOORKOF differ between courses')
    if any(r['cols'] != 8 or len(r['field']) != 30 * 8 for r in rows):
        raise SystemExit('rival field headers need 30 rows of 8 columns (tools/rivals_field_header.py)')
    OUT.mkdir(parents=True, exist_ok=True)
    h = ['/* Generated by tools/course_bundle.py; derived from the owner\'s ROM: never commit. */',
         '#ifndef FZX_COURSE_IDS_H', '#define FZX_COURSE_IDS_H', '#include <stdint.h>',
         '#define FZX_COURSE_IDS_COUNT %du' % len(rows),
         '#define FZX_COURSE_BUNDLE_MASK 0x%xu  /* bit c: course c is on this disc */' % sum(1 << r['c']['index'] for r in rows)]
    for k in MAXES:
        h.append('#define FZX_COURSE_MAX_%s %du' % (k.upper(), max(num(r['d'][dict(U32)[k]]) for r in rows)))
    h.append('#define FZX_COURSE_MAX_PREP_NUNIT_BYTES %du' % max(num(r['d']['FZX_ROAD_PREP_NUNIT_BYTES']) for r in rows))
    h.append('#define FZX_COURSE_MAX_SKY_BREAKS %du' % max(len(r['breaks']) for r in rows))
    h += ['typedef struct { uint32_t at, bytes, hash; } fzx_course_table_t;',
          'typedef struct {', '    uint8_t id, sky_linear, skyline_count, sky_breaks_count;',
          '    char files[%d][12];      /* disc names, in the order of fzx_course_file_e */' % len(PACKS)]
    h += ['    uint32_t %s;' % k for k, _ in U32]
    h += ['    fzx_course_table_t prep[6];      /* NUNIT, NPOS, NQUADS, NSPANS, NCHUNKS, IMAGES */',
          '    int32_t ring[6][2];', '    uint8_t ring_ids[6], tint[3], road_fog[3];',
          '    uint16_t plane_fog[3], plane_mean[3], sky_colors[64];', '    uint8_t sky_breaks[FZX_COURSE_MAX_SKY_BREAKS];',
          '    int32_t field[30][8];',
          '    int32_t zone_count, dash_from;   /* line-field-4: the rival surface (tools/rivals_zones_header.py) */',
          '    int32_t zones[%d][5];' % ZONE_MAX, '    uint8_t record[0x7e0];', '} fzx_course_id_t;',
          'enum { %s };' % ', '.join('FZX_CF_%s' % k.upper() for k, _, _ in PACKS),
          'extern const fzx_course_id_t fzx_course_ids[FZX_COURSE_IDS_COUNT];', '#endif', '']
    (OUT / 'course_ids.h').write_bytes('\n'.join(h).encode())
    c = ['/* Generated by tools/course_bundle.py; derived from the owner\'s ROM: never commit. */',
         '#include "course_ids.h"', 'const fzx_course_id_t fzx_course_ids[FZX_COURSE_IDS_COUNT] = {']
    for r in rows:
        d = r['d']
        # A brightness-monotonic gradient keeps the first course's bisection; others pick by distance.
        keys = [(v & 31) + ((v >> 5) & 31) + ((v >> 10) & 31) for v in r['sky']]
        monotonic = all(b >= a for a, b in zip(keys, keys[1:]))
        c.append('  { .id = %du, .sky_linear = %du, .skyline_count = %du, .sky_breaks_count = %du,' % (
            r['c']['index'], 0 if monotonic else 1, num(d['FZX_SKYLINE_RING_COUNT']), len(r['breaks'])))
        c.append('    .files = { %s },' % ', '.join('"%s"' % n for _, n in r['files']))
        c += ['    .%s = %du,' % (k, num(d[m]) & 0xFFFFFFFF) for k, m in U32]
        c.append('    .prep = { %s },' % ', '.join('{ %du, %du, %du }' % (
            num(d['FZX_ROAD_PREP_%s_AT' % t]), num(d['FZX_ROAD_PREP_%s_BYTES' % t]), num(d['FZX_ROAD_PREP_%s_HASH' % t]))
            for t in PREP))
        c.append('    .ring = { %s },' % ', '.join('{ %d, %d }' % (r['ring'][2 * i], r['ring'][2 * i + 1]) for i in range(6)))
        c.append('    .ring_ids = { %s }, .tint = { %s }, .road_fog = { %s },' % (
            ', '.join(map(str, r['ids'])), ', '.join(map(str, r['tint'])), ', '.join(map(str, r['fog']))))
        c.append('    .plane_fog = { %s }, .plane_mean = { %s },' % (', '.join(map(str, r['plane_fog'])),
                                                                       ', '.join(map(str, r['plane_mean']))))
        c.append('    .sky_colors = { %s },' % ', '.join('0x%04x' % v for v in r['sky']))
        c.append('    .sky_breaks = { %s },' % ', '.join(map(str, r['breaks'])))
        c.append('    .field = { %s },' % ', '.join('{ %s }' % ', '.join(map(str, r['field'][8 * i:8 * i + 8]))
                                                      for i in range(30)))
        c.append('    .zone_count = %d, .dash_from = %d,' % (len(r['zones']) // 5, r['dash']))
        c.append('    .zones = { %s },' % (', '.join('{ %s }' % ', '.join(map(str, r['zones'][5 * i:5 * i + 5]))
                                                      for i in range(len(r['zones']) // 5)) or '{ 0 }'))
        c.append('    .record = { %s } },' % ', '.join('%d' % b for b in r['rec']))
    c += ['};', '']
    (OUT / 'course_ids.c').write_bytes('\n'.join(c).encode())
    (OUT / 'files.txt').write_bytes(''.join('%s %s\n' % (p.relative_to(ROOT / 'generated'), n)
                                            for r in rows for p, n in r['files']).encode())
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    rec = ['# fzx generation receipt v1; regenerate, never edit',
           'generator %s course_bundle.py' % sha(Path(__file__)), 'generator %s course_catalog.py' % sha(ROOT / 'tools/course_catalog.py')]
    for r in rows:
        for p, n in r['files']:
            rec.append('input %s %s' % (sha(p), n))
    rec += ['output %s %s' % (sha(OUT / n), n) for n in ('course_ids.h', 'course_ids.c', 'files.txt')]
    (OUT / 'course_ids.receipt').write_bytes(('\n'.join(rec) + '\n').encode())
    print('course bundle: %d courses (%s), %d disc files' % (len(rows), ', '.join(slugs), sum(len(r['files']) for r in rows)))


if __name__ == '__main__':
    main()
