#!/usr/bin/env python3
"""Controls for the Practice bundle's checks (tools/course_practice_bundle.py check_rows, check_reports, check_report_rows) on
synthetic data (no ROM data). Each malformed input must be refused and the good one accepted.

    python3 tools/course_practice_bundle_test.py"""
import copy, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import course_practice_bundle as b  # noqa: E402


def report(d, length=79027.953, mut=None):
    racers = [{'id': i, 'grid': 30 - i, 'character': i, 'max_energy': 160.0 + i, 'lap_end_ticks': [100 + i + int(length) % 7, 200, -1], 'chase': 255, 'target_lap_ms': 39000} for i in range(30)]
    r = {'tier': 'HOST', 'difficulty': d, 'seed': 1, 'course_length': length, 'racers': racers}
    if mut:
        mut(r)
    return r


def rows(reps):
    v = []
    for r in reps:
        for x in sorted(r['racers'], key=lambda x: x['id']):
            lap = int(round(x['lap_end_ticks'][0] * 1000 / 60.0)) if x['id'] else 39000
            v += [x['grid'], lap, 255, int(round(x['max_energy'] * 256)), 0, x['character'], 0, 0]
    return v


GOOD = [report(d) for d in range(4)]
bad = 0


def expect(name, ok, fn):
    global bad
    try:
        fn()
        got = True
    except b.Bad:
        got = False
    good = got == ok
    bad += 0 if good else 1
    print('%s %s: %s' % ('ok  ' if good else 'FAIL', 'accepted' if got else 'refused', name))


def with_(mut, which=0):
    reps = copy.deepcopy(GOOD)
    mut(reps[which])
    return reps


expect('good rows', True, lambda: b.check_rows('c', rows(GOOD)))
expect('good reports', True, lambda: b.check_reports('c', GOOD))
expect('rows match their reports', True, lambda: b.check_report_rows('c', GOOD, rows(GOOD)))
expect('missing rows (a class short)', False, lambda: b.check_rows('c', rows(GOOD)[:-8]))
expect('no rows at all (the header holds no table)', False, lambda: b.check_rows('c', []))
v = rows(GOOD); v[5 * 8 + 5] = v[6 * 8 + 5]
expect('corrupted rows (two opponents with one machine)', False, lambda: b.check_rows('c', v))
v = rows(GOOD); v[8 * 8 + 0] = 99
expect('corrupted rows (grid place out of 1..30)', False, lambda: b.check_rows('c', v))
v = rows(GOOD); v[2 * 30 * 8 + 4 * 8 + 5] = 77
expect('corrupted rows (a class with another machine)', False, lambda: b.check_rows('c', v))
v = rows(GOOD); v[3] = 0
expect('corrupted rows (a vehicle without energy)', False, lambda: b.check_rows('c', v))
reps = copy.deepcopy(GOOD); reps[2] = report(2, length=61000.0)
expect('mixed-course reports (one class of another course)', False, lambda: b.check_reports('c', reps))
other = [report(d, length=62484.97) for d in range(4)]
expect('mixed-course rows (the header of another course, the same field and other paces)', False, lambda: b.check_report_rows('c', GOOD, rows(other)))
expect('the other course against its own reports', True, lambda: b.check_report_rows('c', other, rows(other)))
expect('a class report of the wrong class', False, lambda: b.check_reports('c', [GOOD[0], GOOD[0], GOOD[2], GOOD[3]]))
expect('three reports only (a missing run)', False, lambda: b.check_reports('c', GOOD[:3]))
expect('a report with 29 racers', False, lambda: b.check_reports('c', with_(lambda r: r['racers'].pop())))
expect('a report of another seed', False, lambda: b.check_reports('c', with_(lambda r: r.update(seed=2))))
expect('a report that is not HOST', False, lambda: b.check_reports('c', with_(lambda r: r.update(tier='EMULATOR'))))
print('controls: %s' % ('all behave' if not bad else '%d wrong' % bad))
sys.exit(1 if bad else 0)
