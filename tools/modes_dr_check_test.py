#!/usr/bin/env python3
"""Controls for tools/modes_dr_check.py on synthetic reports (no ROM data): the judge must pass a matching set and fail, with the
named exit code, every malformed or out-of-band set, including the two controls Codex ran on the first checker (an empty candidate
set and one expected id replaced by 9001).

    python3 tools/modes_dr_check_test.py [--real SEEDS REFPAT NATPAT]     REFPAT/NATPAT contain {s}: also run the same
                                                                          controls on real HOST reports and print each exit code
Exit 0 only when every control behaves."""
import copy, json, os, subprocess, sys, tempfile

CHECK = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'modes_dr_check.py')
SEEDS = [31, 32, 33, 34, 35, 36]


def make(seed, shift=0, nat=False):
    racers = [{'id': 0, 'grid': 30, 'retire_tick': -1}]
    for i in range(1, 30):
        t = 740 + 3 * i + (shift if nat else 0)
        racers.append({'id': i, 'grid': 30 - i, 'retire_tick': t})
    return {'tier': 'HOST', 'seed': seed, 'course_length': 24353.863, 'race_start_tick': 420, 'ticks': 1256, 'racers': racers}


def run(sets, seeds=SEEDS, extra=()):
    with tempfile.TemporaryDirectory() as d:
        args = [sys.executable, CHECK, '--seeds', ','.join(map(str, seeds))]
        pairs = []
        for s, (ref, nat) in sets:
            rp, np_ = os.path.join(d, 'r%d_%d.json' % (s, len(pairs))), os.path.join(d, 'n%d_%d.json' % (s, len(pairs)))
            json.dump(ref, open(rp, 'w')); json.dump(nat, open(np_, 'w'))
            pairs.append('%d=%s,%s' % (s, rp, np_))
        if pairs:
            args += ['--pairs'] + pairs
        args += list(extra)
        r = subprocess.run(args, capture_output=True, text=True)
        return r.returncode, r.stdout.strip().splitlines()[-1] if r.stdout.strip() else ''


def good():
    return [(s, (make(s), make(s, 5, True))) for s in SEEDS]


def mutated(fn, which=0):
    sets = good()
    s, (r, n) = sets[which]
    r, n = copy.deepcopy(r), copy.deepcopy(n)
    fn(r, n)
    sets[which] = (s, (r, n))
    return sets


def set_native(i, **kw):
    def f(r, n):
        for x in n['racers']:
            if x['id'] == i:
                x.update(kw)
    return f


CASES = []


def case(name, want, sets, **kw):
    CASES.append((name, want, sets, kw))


case('matching set passes', 0, good())
case('--ref-seed: one reference of seed 1 against native seeds 31..36', 0, [(sd, (make(1), make(sd, 5, True))) for sd in SEEDS], extra=['--ref-seed', '1'])
case('--ref-seed: a reference of another seed than the one named', 2, [(sd, (make(2), make(sd, 5, True))) for sd in SEEDS], extra=['--ref-seed', '1'])
case('empty candidate set (Codex control 1)', 2, [])
case('one expected id replaced by 9001 in the native roster (Codex control 2)', 2, mutated(set_native(7, id=9001)))
case('one expected id replaced by 9001 in the reference roster', 2, mutated(lambda r, n: [x.update(id=9001) for x in r['racers'] if x['id'] == 7]))
case('missing seed (5 of 6 reports)', 2, good()[:5])
case('duplicate seed report', 2, good()[:5] + [good()[4]])
case('unexpected extra seed report', 2, good() + [(99, (make(99), make(99, 5, True)))])
case('seed list too short (3 seeds)', 2, good()[:3], seeds=SEEDS[:3])
case('native report of the wrong seed', 2, mutated(lambda r, n: n.update(seed=99)))
case('reference report of the wrong seed', 2, mutated(lambda r, n: r.update(seed=99)))
case('native tier not HOST', 2, mutated(lambda r, n: n.update(tier='EMULATOR')))
case('course length differs (wrong course)', 2, mutated(lambda r, n: n.update(course_length=20000.0)))
case('native grid slot differs from the reference', 2, mutated(set_native(3, grid=1)))
case('native roster has 28 opponents', 2, mutated(lambda r, n: n['racers'].pop()))
case('native duplicate racer id', 2, mutated(lambda r, n: n['racers'].append(copy.deepcopy(n['racers'][5]))))
case('reference roster has 28 opponents', 2, mutated(lambda r, n: r['racers'].pop()))
case('native retire tick before the race start', 2, mutated(set_native(4, retire_tick=10)))
case('native retire tick after the window', 2, mutated(set_native(4, retire_tick=5000)))
case('native retire tick not an integer', 2, mutated(set_native(4, retire_tick=800.5)))
case('the player (id 0) is not scored: it may retire in the native report', 0, mutated(set_native(0, retire_tick=800)))
case('the player (id 0) is not scored: it may finish in the native report', 0, mutated(set_native(0, retire_tick=-1, finish_tick=1473)))
case('reference opponent never retired', 2, mutated(lambda r, n: [x.update(retire_tick=-1) for x in r['racers'] if x['id'] == 9]))
case('one opponent never retires (28 of 29)', 1, mutated(set_native(12, retire_tick=-1)))
case('no opponent retires (rule off)', 1, mutated(lambda r, n: [x.update(retire_tick=-1) for x in n['racers'] if x['id']]))
case('sorted error 41 ticks (band 40, inclusive)', 1, mutated(lambda r, n: [x.update(retire_tick=x['retire_tick'] + 36) for x in n['racers'] if x['id'] == 29]))
case('sorted error exactly 40 ticks passes', 0, mutated(lambda r, n: [x.update(retire_tick=x['retire_tick'] + 35) for x in n['racers'] if x['id'] == 29]))
case('only 23 of 29 within 60 ticks', 1, mutated(lambda r, n: [x.update(retire_tick=x['retire_tick'] + 56) for x in n['racers'] if 0 < x['id'] <= 6]))


def main():
    bad = 0
    for name, want, sets, kw in CASES:
        got, last = run(sets, **kw)
        ok = got == want
        bad += 0 if ok else 1
        print('%s exit %d (want %d) %s | %s' % ('ok  ' if ok else 'FAIL', got, want, name, last))
    if '--real' in sys.argv:
        i = sys.argv.index('--real')
        seeds, refpat, natpat = [int(x) for x in sys.argv[i + 1].split(',')], sys.argv[i + 2], sys.argv[i + 3]
        base = [(s, (json.load(open(refpat.format(s=s))), json.load(open(natpat.format(s=s))))) for s in seeds]

        def real(fn=None, which=0, keep=None):
            sets = copy.deepcopy(base if keep is None else [base[j] for j in keep])
            if fn:
                fn(*sets[which][1])
            return sets
        for name, want, sets, kw in [
            ('real: reports as produced pass the structure', None, real(), dict(seeds=seeds)),
            ('real: empty candidate set', 2, [], dict(seeds=seeds)),
            ('real: one native id replaced by 9001', 2, real(lambda r, n: [x.update(id=9001) for x in n['racers'] if x['id'] == 7]), dict(seeds=seeds)),
            ('real: one seed missing', 2, real(keep=list(range(len(seeds) - 1))), dict(seeds=seeds)),
            ('real: duplicate seed', 2, real(keep=list(range(len(seeds) - 1))) + [base[len(seeds) - 2]], dict(seeds=seeds)),
        ]:
            got, last = run(sets, **kw)
            ok = want is None or got == want
            bad += 0 if ok else 1
            print('%s exit %d (want %s) %s | %s' % ('ok  ' if ok else 'FAIL', got, 'any' if want is None else want, name, last))
    print('controls: %s' % ('all behave' if not bad else '%d wrong' % bad))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
