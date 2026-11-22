#!/usr/bin/env python3
"""Self-tests of the outcome judge's scorer (C-168) on synthetic episodes: each check has an input that makes it fail.
Run: python3 tools/attacks_outcome_stats_test.py"""
import copy, random, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import attacks_outcome_stats as S  # noqa: E402

CELL = ['side', 'open', 'wall', 'middle']
N = 2400


def arm(rng, p_hit=0.97, p_ko=0.6):
    ko = rng.random() < p_ko
    return dict(hit=int(rng.random() < p_hit), ko=int(ko), ko_tick=rng.randint(100, 300) if ko else None, wall_ko=0,
                wall_ko_w={'60': int(ko and rng.random() < 0.2)}, retire=int(ko), nd=[1, 2], sd=[rng.uniform(0, .1), rng.uniform(0, .3)],
                sp=[rng.uniform(.3, 1), rng.uniform(.3, 1)], contacts=3, bad=[], side_starts=1, spin_starts=0)


def episodes(seed=1):
    rng = random.Random(seed)
    recs = []
    for i in range(N):
        a = arm(rng)
        recs.append(dict(cell=CELL, idx=i, ns='t', C=a, T=copy.deepcopy(a), N=copy.deepcopy(a)))
    return recs


def rows(recs):
    res = S.score(recs, recs, recs)
    return {r['metric']: r for r in res['rows']}, res


def check(name, ok):
    print('%-70s %s' % (name, 'ok' if ok else 'FAILED'))
    return ok


def main():
    ok = True
    # 1 identical arms pass every metric (zero allowance used)
    recs = episodes()
    r, res = rows(recs)
    ok &= check('identical arms: every metric passes', S.verdict(res) == 'pass')
    # 2 a native KO shift of 0.4 points with no reference variation passes (inside the 0.5 point allowance); 0.9 points fails
    rng = random.Random(3)
    for share, want in ((0.004, 'pass'), (0.009, 'fail')):
        rs = episodes()
        flip = rng.sample([i for i, x in enumerate(rs) if x['N']['ko']], int(share * N))
        for i in flip:
            rs[i]['N']['ko'] = 0; rs[i]['N']['ko_tick'] = None
        r, _ = rows(rs)
        ok &= check('native KO share %.1f points low: %s' % (share * 100, want), r['ko']['status'] == want)
    # 3 the reference's own variation widens the bar: the nudged arm moves KO by 1 point, native by 1.4 passes, by 1.6 fails
    for share, want in ((0.014, 'pass'), (0.016, 'fail')):
        rs = episodes()
        ids = [i for i, x in enumerate(rs) if x['C']['ko']]
        for i in ids[:24]:   # 24 / 2400 = 1 point
            rs[i]['T']['ko'] = 0; rs[i]['T']['ko_tick'] = None
        for i in ids[:round(share * N)]:
            rs[i]['N']['ko'] = 0; rs[i]['N']['ko_tick'] = None
        r, _ = rows(rs)
        ok &= check('nudge moves KO 1.0 point, native %.1f points: %s' % (share * 100, want), r['ko']['status'] == want)
    # 4 damage: 0.004 of max energy passes, 0.007 fails
    for dv, want in ((0.004, 'pass'), (0.007, 'fail')):
        rs = episodes()
        for x in rs:
            x['N']['sd'][1] += dv * x['N']['nd'][1]
        r, _ = rows(rs)
        ok &= check('native victim damage +%.3f of max energy: %s' % (dv, want), r['damage_victim']['status'] == want)
    # 5 time to KO: 1.5 ticks passes, 3 ticks fails
    for dv, want in ((1, 'pass'), (3, 'fail')):
        rs = episodes()
        for x in rs:
            if x['N']['ko_tick']: x['N']['ko_tick'] += dv
        r, _ = rows(rs)
        ok &= check('native time to KO +%d ticks: %s' % (dv, want), r['ko_ticks']['status'] == want)
    # 6 retirements: 0.015 per episode passes, 0.03 fails
    for share, want in ((0.015, 'pass'), (0.03, 'fail')):
        rs = episodes()
        for i in range(round(share * N)):
            rs[i]['N']['retire'] += 1
        r, _ = rows(rs)
        ok &= check('native retirements +%.3f per episode: %s' % (share, want), r['retire']['status'] == want)
    # 7 hit probability and wall-KO use the 0.5 point allowance
    rs = episodes()
    for i in range(round(0.009 * N)):
        rs[i]['N']['hit'] = 1 - rs[i]['N']['hit'] if rs[i]['N']['hit'] == 0 else rs[i]['N']['hit']
    rs2 = episodes()
    drop = [i for i, x in enumerate(rs2) if x['N']['hit']][:round(0.009 * N)]
    for i in drop: rs2[i]['N']['hit'] = 0
    r, _ = rows(rs2)
    ok &= check('native hit probability 0.9 points low: fail', r['hit']['status'] == 'fail')
    rs3 = episodes()
    ids = [i for i, x in enumerate(rs3) if x['N']['ko'] and x['N']['wall_ko_w']['60']][:round(0.009 * N)]
    for i in ids: rs3[i]['N']['wall_ko_w']['60'] = 0
    r, _ = rows(rs3)
    ok &= check('native wall-KO 0.9 points low: fail', r['wall_ko']['status'] == 'fail')
    # 8 unpaired episode files are refused
    bad = episodes(); bad[5]['idx'] = 999
    try:
        S.score(recs, recs, bad); refused = False
    except AssertionError:
        refused = True
    ok &= check('episode files that do not pair are refused', refused)
    # 9 not applicable metrics are skipped (bump has no wall-KO on interior cells)
    rs = episodes()
    for x in rs: x['cell'] = ['bump', 'open', 'interior', 'middle']
    r, _ = rows(rs)
    ok &= check('interior cell: wall-KO is not scored', 'wall_ko' not in r)
    print('ALL OK' if ok else 'SOME CHECK FAILED')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
