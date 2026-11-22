#!/usr/bin/env python3
"""Self-tests of the outcome judge's scorer (C-161) on synthetic episodes: each check has an input that makes it fail.
Run: python3 tools/attacks_outcome_stats_test.py"""
import copy, random, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import attacks_outcome_stats as S  # noqa: E402

CELL = ['side', 'open', 'wall', 'middle']
N = 1024


def arm(rng, p_hit=0.97, p_ko=0.6):
    ko = rng.random() < p_ko
    return dict(hit=int(rng.random() < p_hit), ko=int(ko), ko_tick=rng.randint(20, 400) if ko else None, wall_ko=0,
                wall_ko_w={'60': int(ko and rng.random() < 0.2)}, retire=int(ko), nd=[1, 2], sd=[rng.uniform(0, .1), rng.uniform(0, .3)],
                sp=[rng.uniform(.3, 1), rng.uniform(.3, 1)], contacts=3, bad=[], side_starts=1, spin_starts=0)


def episodes(seed=1):
    rng = random.Random(seed)
    recs = []
    for i in range(N):
        a = arm(rng)
        recs.append(dict(cell=CELL, idx=i, ns='t', R=a, N=copy.deepcopy(a)))
    return recs


def only_family(res):
    return [r for r in res['rows']]


def check(name, ok):
    print('%-62s %s' % (name, 'ok' if ok else 'FAILED'))
    return ok


def main():
    S.CFG['coverage'] = dict(min_ref_contacts=100, min_ref_ko_events=50)
    allok = True
    # 1 identical arms: zero estimated uncertainty is inconclusive, never a pass
    recs = episodes()
    res = S.score(recs, recs, 'R', 'N', draws=2000)
    allok &= check('identical arms: no interval passes on zero uncertainty', all(r['status'] != 'pass' for r in res['rows'] if r['metric'] in ('hit',)))
    # 2 a few paired flips: hit and ko within the bands pass
    rng = random.Random(7)
    near = episodes()
    for r in near:
        if rng.random() < 0.004: r['N']['hit'] ^= 1
        if rng.random() < 0.004: r['N']['ko'] ^= 1; r['N']['ko_tick'] = 100 if r['N']['ko'] else None
    res = S.score(near, near, 'R', 'N', draws=2000)
    st = {r['metric']: r['status'] for r in res['rows']}
    allok &= check('0.4% paired flips: hit passes', st['hit'] == 'pass')
    allok &= check('0.4% paired flips: ko passes (band 2 points)', st['ko'] == 'pass')
    # 3 shifted probabilities must fail or be inconclusive, never pass
    for nm, key, shift in (('hit', 'hit', 0.06), ('ko', 'ko', 0.05)):
        bad = copy.deepcopy(near)
        flips = 0
        for r in bad:
            if r['N'][key] == 1 and flips < shift * N:
                r['N'][key] = 0; flips += 1
                if key == 'ko': r['N']['ko_tick'] = None
        st = {x['metric']: x['status'] for x in S.score(near, bad, 'R', 'N', draws=2000)['rows']}
        allok &= check('%d%% shift in %s fails' % (shift * 100, nm), st[nm] == 'fail')
    # 4 KO time shift beyond 12 ticks
    bad = copy.deepcopy(near)
    for r in bad:
        if r['N']['ko_tick']: r['N']['ko_tick'] += 40
    st = {x['metric']: x['status'] for x in S.score(near, bad, 'R', 'N', draws=2000)['rows']}
    allok &= check('KO time +40 ticks fails', st['ko_ticks'] == 'fail')
    # 5 damage shift beyond 0.03 of max energy
    bad = copy.deepcopy(near)
    for r in bad:
        r['N']['sd'][1] += 0.08
    st = {x['metric']: x['status'] for x in S.score(near, bad, 'R', 'N', draws=2000)['rows']}
    allok &= check('victim damage +0.08 fails', st['damage_victim'] == 'fail')
    # 6 coverage: too few reference KOs is inconclusive
    few = copy.deepcopy(near)
    for r in few[40:]:
        r['R']['ko'] = 0; r['R']['ko_tick'] = None; r['R']['wall_ko_w'] = {}
    res = S.score(few, few, 'R', 'N', draws=500)
    allok &= check('fewer than 50 reference KO events: missing coverage', any(m[1] == 'KO events' for m in res['missing_coverage']) and S.verdict(res) != 'pass')
    # 7 structural violation is reported
    badk = copy.deepcopy(near); badk[3]['N']['bad'] = ['ko without contact']
    res = S.score(near, badk, 'R', 'N', draws=500)
    allok &= check('structural violation in the test arm', S.verdict(res) == 'structural violation')
    # 8 pairing is enforced
    mis = copy.deepcopy(near); mis[0]['idx'] = 99999
    try:
        S.score(near, mis, 'R', 'N', draws=500); allok &= check('unpaired episode files are refused', False)
    except AssertionError:
        allok &= check('unpaired episode files are refused', True)
    print('ALL OK' if allok else 'FAILURES')
    return 0 if allok else 1


if __name__ == '__main__':
    sys.exit(main())
