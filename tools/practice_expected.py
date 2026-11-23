#!/usr/bin/env python3
"""The Practice fixture's expected rows, frozen from the HOST reference reports alone (C-169 item 1).

The gate used to take its expectation from the stage's generated course_practice.c: the same file the image links, so a bundle
and an expectation could agree on a wrong roster. This tool reads only the four HOST reports per course that
tools/modes_practice_regen.sh keeps (fzx-gp-ref --mode practice, classes 0..3, the shipped seed 1) and writes, per course and
class, the entrants by vehicle id (machine, grid place, energy), the vehicle count and the seed. The gate compares the live
SH-2 state at race_start with this file and archives it, with the BIN, in the build folder.

    tools/practice_expected.py REPORTS_DIR OUT.json [slug ...]     (REPORTS_DIR/<slug>/practice-d<class>-s1.json)
    tools/practice_expected.py --check OUT.json                    (re-verify a file's own hash; exit 1 if it fails)

The file is derived from the owner's ROM: keep it in the evidence folder, never in git. Output is canonical JSON (sorted keys);
its `sha256` is the hash of the JSON with that member removed.
"""
import hashlib, json, sys
from pathlib import Path

SLUGS = ['mute-city-1', 'silence-1', 'sand-ocean-1', 'devils-forest-1', 'big-blue-1', 'port-town-1']
SEED = 1
CARS = 30


class Bad(SystemExit):
    pass


def digest(doc):
    body = {k: v for k, v in doc.items() if k != 'sha256'}
    return hashlib.sha256(json.dumps(body, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def row_of(slug, d, rep):
    if rep.get('tier') != 'HOST' or int(rep.get('difficulty', -1)) != d or int(rep.get('seed', -1)) != SEED:
        raise Bad('%s class %d: not the HOST report of that class at seed %d' % (slug, d, SEED))
    racers = sorted(rep.get('racers', []), key=lambda x: x.get('id', -1))
    if [x.get('id') for x in racers] != list(range(CARS)):
        raise Bad('%s class %d: racers are not ids 0..%d once each' % (slug, d, CARS - 1))
    machine = [int(x['character']) for x in racers]
    grid = [int(x['grid']) for x in racers]
    energy = [int(round(float(x['max_energy']) * 256)) for x in racers]
    chase = [-1 if int(x['chase']) == 255 else int(x['chase']) for x in racers]   # the vehicle each paces against (255: none)
    if sorted(grid) != list(range(1, CARS + 1)):
        raise Bad('%s class %d: grid places are not 1..%d' % (slug, d, CARS))
    if len(set(machine[1:])) != CARS - 1 or machine[0] in machine[1:]:
        raise Bad('%s class %d: the opponents are not %d distinct machines apart from the player' % (slug, d, CARS - 1))
    if any(e <= 0 for e in energy):
        raise Bad('%s class %d: a vehicle with no energy' % (slug, d))
    return dict(count=len(racers), seed=SEED, machine=machine, grid=grid, energy=energy, chase=chase,
                course_length=round(float(rep['course_length']), 2))


def build(rdir, slugs):
    doc = dict(schema='fzx-practice-expected-1', tier='HOST', source='fzx-gp-ref --mode practice, seed %d, classes 0..3' % SEED, courses={}, reports={})
    for s in slugs:
        doc['courses'][s] = {}
        for d in range(4):
            p = Path(rdir) / s / ('practice-d%d-s1.json' % d)
            if not p.is_file():
                raise Bad('missing HOST report %s' % p)
            doc['reports']['%s/%d' % (s, d)] = hashlib.sha256(p.read_bytes()).hexdigest()
            doc['courses'][s][str(d)] = row_of(s, d, json.loads(p.read_text()))
    lens = [doc['courses'][s]['1']['course_length'] for s in slugs]
    if len(set(lens)) != len(lens):
        raise Bad('two courses share one length: a report is of another course')
    doc['sha256'] = digest(doc)
    return doc


def load(path):
    """The gate's reader: the file, its own hash verified, or SystemExit."""
    doc = json.loads(Path(path).read_text())
    if doc.get('schema') != 'fzx-practice-expected-1' or doc.get('sha256') != digest(doc):
        raise Bad('%s: not an untouched practice-expected file' % path)
    return doc


def main():
    a = sys.argv[1:]
    if a[:1] == ['--check']:
        d = load(a[1])
        print('ok %s: %d courses, sha256 %s' % (a[1], len(d['courses']), d['sha256']))
        return
    if len(a) < 2:
        sys.exit(__doc__)
    doc = build(a[0], a[2:] or SLUGS)
    Path(a[1]).write_text(json.dumps(doc, indent=1, sort_keys=True) + '\n')
    print('wrote %s: %d courses x 4 classes, sha256 %s' % (a[1], len(doc['courses']), doc['sha256']))


if __name__ == '__main__':
    main()
