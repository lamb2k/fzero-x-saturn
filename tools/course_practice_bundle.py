#!/usr/bin/env python3
"""Practice row sets for the one-disc image (ENG_MODES with ENG_COURSE_SELECT; plan P3 item 3).

Reads each bundled course's rivals/rivals_field_practice.h (tools/rivals_field_header.py --classes over four reference
runs of the original code as Practice, fzx-gp-ref --mode practice, difficulties 0..3, the shipped field seed) and writes,
next to tools/course_bundle.py's outputs:
    generated/courses/select/course_practice.h    fzx_course_field_practice[course row][class][30][8]
    generated/courses/select/course_practice.c
    generated/courses/select/course_practice.receipt
The course rows follow the existing course_ids.c (checked). Practice races the same 30 machines as the Grand Prix but
deals the grid differently and paces the rivals by its own tables (cpu.c, D_i3_80141998), so it has its own rows.
Everything here is derived from the owner's ROM: generated/ only, never committed.

    tools/course_practice_bundle.py mute-city-1 silence-1 sand-ocean-1 devils-forest-1 big-blue-1 port-town-1
"""
import hashlib, json, re, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import course_catalog  # noqa: E402
from course_bundle import OUT, ROOT, fnv  # noqa: E402


class Bad(SystemExit):
    pass


def check_rows(slug, v):
    """One course's header values (4 classes x 30 vehicles x 8 columns, tools/rivals_field_header.py): refuse a table that is not a
    Practice field (grid places 1..30 in every class, the same 29 distinct machines in every class, the player a machine of its own)."""
    if len(v) != 4 * 30 * 8:
        raise Bad('%s: practice headers need 4 x 30 rows of 8 columns, found %d values' % (slug, len(v)))
    cls = [[v[(k * 30 + i) * 8:(k * 30 + i) * 8 + 8] for i in range(30)] for k in range(4)]
    machines = [tuple(r[5] for r in c) for c in cls]
    if len(set(machines)) != 1:
        raise Bad('%s: the four classes do not share one field of machines' % slug)
    if len(set(machines[0][1:])) != 29 or machines[0][0] in machines[0][1:]:
        raise Bad('%s: the 29 opponents are not 29 distinct machines (the player is not one of them)' % slug)
    for k, c in enumerate(cls):
        if sorted(r[0] for r in c) != list(range(1, 31)):
            raise Bad('%s: class %d grid places are not 1..30' % (slug, k))
        if any(r[3] <= 0 for r in c):
            raise Bad('%s: class %d has a vehicle with no energy' % (slug, k))
        if any(r[1] <= 0 for r in c[1:]):
            raise Bad('%s: class %d has an opponent with no pace' % (slug, k))


def check_reports(slug, reports):
    """The four reference reports a course's rows came from (fzx-gp-ref --mode practice, difficulties 0..3, seed 1): one course, one
    field, 30 unique racers. Returns the course length."""
    if len(reports) != 4:
        raise Bad('%s: need the four class reports, found %d' % (slug, len(reports)))
    lens = set()
    for d, r in enumerate(reports):
        if r.get('tier') != 'HOST' or int(r.get('difficulty', -1)) != d or int(r.get('seed', -1)) != 1:
            raise Bad('%s: report %d is not the HOST report of class %d, seed 1' % (slug, d, d))
        ids = [x.get('id') for x in r.get('racers', [])]
        if ids != list(range(30)):
            raise Bad('%s: report %d racers are not ids 0..29 once each' % (slug, d))
        lens.add(round(float(r.get('course_length', 0)), 2))
    if len(lens) != 1 or min(lens) <= 0:
        raise Bad('%s: the class reports are of different courses (lengths %s)' % (slug, sorted(lens)))
    return lens.pop()


def check_report_rows(slug, reports, v):
    """The header's rows are the reports' (grid, machine and energy per racer in id order, every class)."""
    for k, r in enumerate(reports):
        for i, x in enumerate(sorted(r['racers'], key=lambda x: x['id'])):
            row = v[(k * 30 + i) * 8:(k * 30 + i) * 8 + 8]
            chase = x['chase']
            lap_ms = x['target_lap_ms']
            if x['id'] and chase == 255 and x['lap_end_ticks'][0] > 0:   # tools/rivals_field_header.py --practice: the reference's own lap 1
                lap_ms = int(round(x['lap_end_ticks'][0] * 1000 / 60.0))
            want = (x['grid'], lap_ms, chase, int(round(x['max_energy'] * 256)), 38 if chase not in (0, 255) else 0, x['character'])
            if tuple(row[:6]) != want:
                raise Bad('%s: class %d racer %d: the header does not match its report (pace, chase, grid, energy or machine)' % (slug, k, i))


def main():
    args = sys.argv[1:]
    rdir = None
    if '--reports' in args:
        rdir = Path(args[args.index('--reports') + 1])
        del args[args.index('--reports'):args.index('--reports') + 2]
    slugs = args or list(course_catalog.COURSES)
    ids = [int(v) for v in re.findall(r'\.id = (\d+)u', (OUT / 'course_ids.c').read_text())]
    if ids != [course_catalog.course(s)['index'] for s in slugs]:
        raise SystemExit('give the courses in course_ids.c order (course indexes %s)' % ids)
    sets = []
    lengths = []
    for s in slugs:
        if s == 'death-race':
            # Death Race has no Practice (its course is the mode's own); the row keeps the table's shape: the Death Race rows, every class.
            p = course_catalog.gen_root(s) / 'rivals/rivals_field_class.h'
            m = re.search(r'fzx_rivals_field_class\[4\]\[30\]\[8\]\s*=\s*\{(.*?)\};', p.read_text(), re.S) if p.is_file() else None
            v = [int(x) for x in re.findall(r'-?\d+', m[1])] if m else []
            if len(v) != 4 * 30 * 8:
                raise Bad('death-race: no class header %s (run tools/modes_death_regen.sh)' % p)
            sets.append((p, v))
            continue
        p = course_catalog.gen_root(s) / 'rivals/rivals_field_practice.h'
        if not p.is_file():
            raise Bad('%s: no practice header %s (run tools/modes_practice_regen.sh)' % (s, p))
        m = re.search(r'fzx_rivals_field_class\[4\]\[30\]\[8\]\s*=\s*\{(.*?)\};', p.read_text(), re.S)
        v = [int(x) for x in re.findall(r'-?\d+', m[1])] if m else []
        check_rows(s, v)
        if rdir is not None:
            reps = []
            for d in range(4):
                rp = rdir / s / ('practice-d%d-s1.json' % d)
                if not rp.is_file():
                    raise Bad('%s: no report %s' % (s, rp))
                reps.append(json.load(open(rp)))
            lengths.append(check_reports(s, reps))
            check_report_rows(s, reps, v)
        sets.append((p, v))
    if rdir is not None and len(set(lengths)) != len(lengths):
        raise Bad('two courses share one course length: a report is of another course')
    # The rows are disc data, not image: 3,840 B a course (24 courses would be 92 KB of work RAM). Each course's rows are the file
    # PRnn.BIN (big-endian int32 [class][vehicle][8]), read at LOAD with the course's identity row (engine_courses.c) and checked against
    # the FNV-1a kept in the image's index (course_practice_index.c). course_practice.c stays as the readable record of the same values
    # (the gate's fixture compares it, and the files, with the HOST-frozen rows); it is not linked.
    names, blobs, hashes = [], [], []
    for (_, v), cid in zip(sets, ids):
        blob = b''.join(int(x).to_bytes(4, 'big', signed=True) for x in v)
        if len(blob) != 4 * 30 * 8 * 4:
            raise Bad('a practice row set is not 960 values')
        names.append('PR%02d.BIN' % cid)
        blobs.append(blob)
        hashes.append(fnv(blob))
        (OUT / names[-1]).write_bytes(blob)
    h = ['/* Generated by tools/course_practice_bundle.py; derived from the owner\'s ROM: never commit. */',
         '#ifndef FZX_COURSE_PRACTICE_H', '#define FZX_COURSE_PRACTICE_H', '#include <stdint.h>', '#include "course_ids.h"',
         '#define FZX_COURSE_PRACTICE_BYTES %du  /* one PRnn.BIN: [class: novice, standard, expert, master][vehicle][8] (rivals_field.h columns) */' % len(blobs[0]),
         '/* The files\' FNV-1a, by course row of fzx_course_index. */',
         'extern const uint32_t fzx_course_practice_hash[FZX_COURSE_IDS_COUNT];', '#endif', '']
    c = ['/* Generated by tools/course_practice_bundle.py; derived from the owner\'s ROM: never commit. */',
         '#include "course_practice.h"',
         'const uint32_t fzx_course_practice_hash[FZX_COURSE_IDS_COUNT] = { %s };' % ', '.join('%du' % x for x in hashes), '']
    rec_c = ['/* The Practice rows as values, the record the PRnn.BIN files were written from (not linked; tools/engine_gate.py reads it). */',
             '/* [course row of fzx_course_ids][class][vehicle][8] */', 'static const int32_t fzx_course_field_practice_record[%d][4][30][8] = {' % len(sets)]
    for _, v in sets:
        rec_c.append('  { %s },' % ', '.join(
            '{ %s }' % ', '.join('{ %s }' % ', '.join(map(str, v[(k * 30 + i) * 8:(k * 30 + i) * 8 + 8])) for i in range(30))
            for k in range(4)))
    rec_c += ['};', '']
    (OUT / 'course_practice.h').write_bytes(chr(10).join(h).encode())
    (OUT / 'course_practice_index.c').write_bytes(chr(10).join(c).encode())
    (OUT / 'course_practice.c').write_bytes(chr(10).join(rec_c).encode())
    (OUT / 'files_practice.txt').write_bytes(''.join('courses/select/%s %s' % (n, n) + chr(10) for n in names).encode())
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    rec = ['# fzx generation receipt v1; regenerate, never edit',
           'generator %s course_practice_bundle.py' % sha(Path(__file__)),
           'generator %s course_bundle.py' % sha(ROOT / 'tools/course_bundle.py'),
           'generator %s course_catalog.py' % sha(ROOT / 'tools/course_catalog.py')]
    rec += ['input %s %s' % (sha(p), p.relative_to(ROOT / 'generated')) for p, _ in sets]
    rec += ['output %s %s' % (sha(OUT / n), n) for n in ['course_practice.h', 'course_practice.c', 'course_practice_index.c', 'files_practice.txt'] + names]
    (OUT / 'course_practice.receipt').write_bytes(('\n'.join(rec) + '\n').encode())
    print('course practice bundle: %d courses x 4 classes' % len(sets))


if __name__ == '__main__':
    main()
