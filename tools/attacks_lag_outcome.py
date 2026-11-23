#!/usr/bin/env python3
"""Outcome check of approximation P1 (ENG_ATK_LAG; lane claude/attacks-cost-3). Tier: HOST (tools/attacks_host fzx-atk).

P1: a second tick of a frame whose contact is not provably clear waits behind the queued field tick, and the player's share of that
contact (push, damage, regain, hit) reaches the player one tick later. Every rival's side is exact.

  run   --bins DIR --out DIR [--jobs 4] [--arms a,b,lag,l8,drop] [--seeds 200]   run the races (fzx-atk, one process a race)
  score --out DIR                                                                   the rule, the table, the controls

Fixed before the first lag run (freeze record: tools/attacks_lag_freeze.md). Races: 9,460 ticks (460 of nothing, 9,000 of race), the
engine's default field and attacks at the given seed (fzx-atk --seed S replaces the field's and the attacks' seeds).
Cells: course (Mute City 1, Sand Ocean 1, Big Blue 1) x driver (R01 closed loop with the course's matrix gains; the scripted hunter
= run-time driver 2) x difficulty (1 STANDARD, 3 MASTER) = 12. Arms, 200 seeded races per cell each:
  A    exact (--order canonical), seeds 1..200            B    exact, second seed set 1001..1200
  lag  --order lag (frame 2), seeds 1..200                l8   --order lag --lag 8, seeds 1..200 (control)
  drop --order canonical --lag-drop 1, seeds 1..200 (control: the player's contact share dropped entirely)
Metrics per cell (means over the races): rival KOs (rivals spun out), rivals retired, player KOs credited, player damage (fraction of
the player's max energy); and for each of the 30 slots the distribution of its final place (the field's rank at the last tick),
two-sample Kolmogorov-Smirnov statistic D.
Rule per cell and metric: |lag - A| <= |B - A| + tolerance (D(lag, A) <= D(B, A) + tolerance for the places).
Tolerances (the C-168 ones, counts as its retirements per episode, distributions as its probabilities): counts 0.02 a race,
damage 0.005 of max energy, D 0.005. The check passes when no cell-metric breaks the rule. A control must break the rule somewhere.
"""
import argparse, json, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

COURSES = ['mute-city-1', 'sand-ocean-1', 'big-blue-1']
DRIVERS = {'r01': ['--driver', '1', '--drive', '0.004,2.5'], 'hunter': ['--driver', '2']}
DIFFS = [1, 3]
TICKS = 9460
SEEDS_A = 0       # seeds 1..N
SEEDS_B = 1000    # seeds 1001..1000+N
ARMS = {
    'a':    (['--order', 'canonical'], 0),
    'b':    (['--order', 'canonical'], SEEDS_B),
    'lag':  (['--order', 'lag'], 0),
    'l8':   (['--order', 'lag', '--lag', '8'], 0),
    'drop': (['--order', 'canonical', '--lag-drop', '1'], 0),
}
TOL = {'rival_ko': 0.02, 'rival_retire': 0.02, 'player_ko': 0.02, 'player_damage': 0.005, 'places': 0.005}
SCALARS = ['rival_ko', 'rival_retire', 'player_ko', 'player_damage']


def cells():
    for c in COURSES:
        for d in DRIVERS:
            for f in DIFFS:
                yield c, d, f


def cell_name(c, d, f):
    return '%s.%s.d%d' % (c, d, f)


def run_one(job):
    binp, phys, riv, extra, seed, rep = job
    cmd = [binp, '--phys', phys, '--rivals', riv, '--ticks', str(TICKS), '--seed', str(seed), '--report', rep] + extra
    r = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    if r.returncode:
        raise SystemExit('fzx-atk failed: %s\n%s' % (' '.join(cmd), r.stderr.decode()))
    j = json.load(open(rep))
    os.remove(rep)
    return {'seed': seed, 'rival_ko': j['spun'], 'rival_retire': j['rivals_retired'], 'player_ko': j['kos'],
            'player_damage': j['player_damage'] / float(j['player_max_q8']), 'places': j['places'],
            'finished': j['player_finished'], 'crashed': j['player_crashed']}


def cmd_run(a):
    sys.path.insert(0, 'tools')
    import course_catalog as cc
    out = Path(a.out)
    (out / 'tmp').mkdir(parents=True, exist_ok=True)
    arms = a.arms.split(',')
    jobs, keys = [], []
    for c, d, f in cells():
        g = str(cc.gen_root(c))
        for arm in arms:
            extra, base = ARMS[arm]
            for s in range(1, a.seeds + 1):
                seed = base + s
                rep = str(out / 'tmp' / ('%s.%s.%d.json' % (cell_name(c, d, f), arm, seed)))
                jobs.append((str(Path(a.bins) / ('fzx-atk-' + c)), g + '/physics/PHYS.PAK', g + '/rivals/RIVALS.PAK',
                             DRIVERS[d] + ['--difficulty', str(f)] + extra, seed, rep))
                keys.append((cell_name(c, d, f), arm))
    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        res = list(ex.map(run_one, jobs))
    data = {}
    for (cn, arm), r in zip(keys, res):
        data.setdefault(cn, {}).setdefault(arm, []).append(r)
    path = out / ('races-%s.json' % a.tag)
    json.dump(data, open(path, 'w'))
    print('wrote', path, len(res), 'races')


def mean(xs):
    return sum(xs) / len(xs)


def ks(x, y):
    """Two-sample D over the integer places."""
    d = 0.0
    for p in range(1, 31):
        d = max(d, abs(sum(1 for v in x if v <= p) / len(x) - sum(1 for v in y if v <= p) / len(y)))
    return d


def load(out):
    data = {}
    for f in sorted(Path(out).glob('races-*.json')):
        for cn, arms in json.load(open(f)).items():
            for arm, rs in arms.items():
                data.setdefault(cn, {})[arm] = rs
    return data


def rows_for(data, arm):
    """Every cell-metric of an arm against A and B: (cell, metric, lag-A, B-A, tol, ok)."""
    rows = []
    for cn, arms in data.items():
        A, B, X = arms['a'], arms['b'], arms[arm]
        for m in SCALARS:
            ma, mb, mx = (mean([r[m] for r in z]) for z in (A, B, X))
            rows.append((cn, m, mx - ma, mb - ma, TOL[m], abs(mx - ma) <= abs(mb - ma) + TOL[m]))
        for s in range(30):
            pa, pb, px = ([r['places'][s] for r in z] for z in (A, B, X))
            dx, db = ks(px, pa), ks(pb, pa)
            rows.append((cn, 'place%02d' % s, dx, db, TOL['places'], dx <= db + TOL['places']))
    return rows


def cmd_score(a):
    data = load(a.out)
    lines = []
    need = ['a', 'b', 'lag']
    for cn, arms in data.items():
        for k in need:
            if k not in arms:
                raise SystemExit('cell %s lacks arm %s' % (cn, k))
    lines.append('# P1 outcome check (HOST), %d cells' % len(data))
    lines.append('cell | arm means: rival_ko rival_retire player_ko player_damage | places D(lag,A) max, D(B,A) max')
    for cn in sorted(data):
        z = data[cn]
        for arm in ['a', 'b', 'lag', 'l8', 'drop']:
            if arm in z:
                lines.append('%-28s %-4s n=%d  %s' % (cn, arm, len(z[arm]), '  '.join('%.4f' % mean([r[m] for r in z[arm]]) for m in SCALARS)))
        same = all(z['a'][i] == z['lag'][i] for i in range(len(z['a'])))
        lines.append('%-28s lag races identical to A: %s' % (cn, same))
    verdict = {}
    for arm in ['lag', 'l8', 'drop']:
        if not all(arm in z for z in data.values()):
            continue
        rows = rows_for(data, arm)
        bad = [r for r in rows if not r[5]]
        verdict[arm] = (len(rows), len(bad))
        lines.append('')
        lines.append('== arm %s: %d cell-metrics, %d break the rule' % (arm, len(rows), len(bad)))
        by = {}
        for r in bad:
            by.setdefault(r[1].rstrip('0123456789') if r[1].startswith('place') else r[1], []).append(r)
        for k, v in sorted(by.items()):
            lines.append('   %-14s %3d  e.g. %s' % (k, len(v), '; '.join('%s %s x-A %.4f B-A %.4f' % (r[0], r[1], r[2], r[3]) for r in v[:3])))
        if arm == 'lag':
            lines.append('   scalar rows (lag - A | B - A | tolerance):')
            for r in rows:
                if not r[1].startswith('place'):
                    lines.append('   %-28s %-14s %+.4f | %+.4f | %.3f %s' % (r[0], r[1], r[2], r[3], r[4], 'ok' if r[5] else 'BREAKS'))
    lines.append('')
    lines.append('VERDICT lag: %s' % ('PASS' if verdict.get('lag', (0, 1))[1] == 0 else 'FAIL'))
    for arm in ['l8', 'drop']:
        if arm in verdict:
            lines.append('CONTROL %s: %s (%d of %d cell-metrics break the rule)' % (arm, 'fails as it must' if verdict[arm][1] else 'DOES NOT FAIL (the check is blind to it)', verdict[arm][1], verdict[arm][0]))
    txt = '\n'.join(lines)
    print(txt)
    open(Path(a.out) / 'score.txt', 'w').write(txt + '\n')


def main():
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest='cmd', required=True)
    r = sp.add_parser('run')
    r.add_argument('--bins', required=True)
    r.add_argument('--out', required=True)
    r.add_argument('--jobs', type=int, default=4)
    r.add_argument('--arms', default='a,b,lag,l8,drop')
    r.add_argument('--seeds', type=int, default=200)
    r.add_argument('--tag', default='all')
    s = sp.add_parser('score')
    s.add_argument('--out', required=True)
    a = ap.parse_args()
    {'run': cmd_run, 'score': cmd_score}[a.cmd](a)


if __name__ == '__main__':
    main()
