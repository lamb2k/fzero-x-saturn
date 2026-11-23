#!/usr/bin/env python3
"""Outcome check of approximation P1 (ENG_ATK_LAG; lane claude/attacks-cost-4), version 2: the check with power. Tier: HOST (tools/attacks_host fzx-atk).

Replaces tools/attacks_lag_outcome.py (frozen at 2c5e9ee), which could not pass for an exact arm (exact on a third seed set broke 161 of 408
cell-metrics: 200 races a cell is below the 0.02 tolerance's noise). Same rule and tolerances; the power is fixed, not the tolerances.

  run       --bins DIR --out DIR [--jobs 4] --arms a,b,c,d --blocks 0-1     run blocks of BLOCK races a cell and arm (resumable, one file a block)
  calibrate --out DIR --blocks 0-1                                          exact arms only (a,b,c,d): the rule on every ordered triple
  score     --out DIR --blocks 0-3                                          the rule for lag, with the controls l8 and drop (arms lag,l8,drop)

Cells (6): R01 closed loop (matrix gains 0.004,2.5) x course (Mute City 1, Sand Ocean 1, Big Blue 1) x difficulty (1 STANDARD, 3 MASTER).
The hunter cells are dropped (the hunter never lags: those cells compared exact against exact).
Arms, every arm on its own seed set (no arm shares a seed with another, so every comparison has the same noise):
  a, b, c, d  exact (--order canonical), seeds 0, 1,000,000, 2,000,000, 3,000,000 + 1..N   (c, d: calibration only)
  lag         --order lag                                      seeds 4,000,000 + 1..N
  l8          --order lag --lag 8 (control)                    seeds 5,000,000 + 1..N
  drop        canonical --lag-drop 1 (control)                 seeds 6,000,000 + 1..N
Races: 9,460 ticks (460 of nothing, 9,000 of race), the engine's default field and attacks at the seed (fzx-atk --seed S).
Per cell and arm, over the races: rival KOs (spun out), rivals retired, player KOs credited, player damage (fraction of max energy), and the place
statistic P: the mean over the 30 slots of the two-sample Kolmogorov-Smirnov D of the slot's final place (the field's rank at the last tick).
Rule per cell and metric (the same as before): |X - A| <= |B - A| + tolerance, for P: P(X,A) <= P(B,A) + tolerance.
Tolerances (C-168's): counts 0.02 a race, damage 0.005 of max energy, P 0.005. 6 cells x 5 metrics = 30 rows.
The check passes when no row breaks the rule. Calibration (exact arms only): X, A, B ranging over every ordered triple of a, b, c, d (24): no row may break,
and the worst slack, (|X-A| - |B-A|) / tolerance, must be <= SLACK_MAX. Controls l8 and drop must each break a row.
"""
import argparse, itertools, json, os, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

COURSES = ['mute-city-1', 'sand-ocean-1', 'big-blue-1']
DRIVE = ['--driver', '1', '--drive', '0.004,2.5']
DIFFS = [1, 3]
TICKS = 9460
BLOCK = 5000
ARMS = {
    'a':    (['--order', 'canonical'], 0),
    'b':    (['--order', 'canonical'], 1000000),
    'c':    (['--order', 'canonical'], 2000000),
    'd':    (['--order', 'canonical'], 3000000),
    'lag':  (['--order', 'lag'], 4000000),
    'l8':   (['--order', 'lag', '--lag', '8'], 5000000),
    'drop': (['--order', 'canonical', '--lag-drop', '1'], 6000000),
}
TOL = {'rival_ko': 0.02, 'rival_retire': 0.02, 'player_ko': 0.02, 'player_damage': 0.005, 'P': 0.005}
METRICS = ['rival_ko', 'rival_retire', 'player_ko', 'player_damage', 'P']
SLACK_MAX = 0.75


def cells():
    for c in COURSES:
        for f in DIFFS:
            yield c, f


def cell_name(c, f):
    return '%s.r01.d%d' % (c, f)


def parse_blocks(s):
    a, _, b = s.partition('-')
    return list(range(int(a), int(b or a) + 1))


def run_one(job):
    binp, phys, riv, extra, seed, rep = job
    cmd = [binp, '--phys', phys, '--rivals', riv, '--ticks', str(TICKS), '--seed', str(seed), '--report', rep] + extra
    r = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    if r.returncode:
        raise SystemExit('fzx-atk failed: %s\n%s' % (' '.join(cmd), r.stderr.decode()))
    j = json.load(open(rep))
    os.remove(rep)
    return (j['spun'], j['rivals_retired'], j['kos'], j['player_damage'] / float(j['player_max_q8']), j['places'])


def cmd_run(a):
    sys.path.insert(0, 'tools')
    import course_catalog as cc
    out = Path(a.out)
    (out / 'tmp').mkdir(parents=True, exist_ok=True)
    t0 = time.time()
    for blk in parse_blocks(a.blocks):
        for arm in a.arms.split(','):
            extra, base = ARMS[arm]
            for c, f in cells():
                fn = out / ('agg-%s-%s-b%d.json' % (cell_name(c, f), arm, blk))
                if fn.exists():
                    continue
                g = str(cc.gen_root(c))
                jobs = []
                for i in range(BLOCK):
                    seed = base + blk * BLOCK + i + 1
                    rep = str(out / 'tmp' / ('%s.%s.%d.json' % (cell_name(c, f), arm, seed)))
                    jobs.append((str(Path(a.bins) / ('fzx-atk-' + c)), g + '/physics/PHYS.PAK', g + '/rivals/RIVALS.PAK',
                                 DRIVE + ['--difficulty', str(f)] + extra, seed, rep))
                with ThreadPoolExecutor(max_workers=a.jobs) as ex:
                    res = list(ex.map(run_one, jobs))
                agg = {'n': len(res), 'sums': [sum(r[k] for r in res) for k in range(4)],
                       'sq': [sum(r[k] * r[k] for r in res) for k in range(4)],
                       'hist': [[0] * 31 for _ in range(30)]}
                for r in res:
                    for s, p in enumerate(r[4]):
                        agg['hist'][s][p] += 1
                json.dump(agg, open(str(fn) + '.part', 'w'))
                os.replace(str(fn) + '.part', fn)
                print('%7.0fs block %d %s %s: %d races' % (time.time() - t0, blk, cell_name(c, f), arm, len(res)), flush=True)


def load(out, arm, blocks):
    """cell -> aggregate summed over the blocks."""
    data = {}
    for c, f in cells():
        n, sums, hist = 0, [0.0] * 4, [[0] * 31 for _ in range(30)]
        for blk in blocks:
            fn = Path(out) / ('agg-%s-%s-b%d.json' % (cell_name(c, f), arm, blk))
            if not fn.exists():
                return None
            g = json.load(open(fn))
            n += g['n']
            sums = [x + y for x, y in zip(sums, g['sums'])]
            for s in range(30):
                for p in range(31):
                    hist[s][p] += g['hist'][s][p]
        data[cell_name(c, f)] = {'n': n, 'mean': [x / n for x in sums], 'hist': hist}
    return data


def pstat(x, y):
    """Mean over the slots of the two-sample KS D of the final place."""
    tot = 0.0
    for s in range(30):
        cx = cy = 0
        d = 0.0
        for p in range(31):
            cx += x['hist'][s][p]
            cy += y['hist'][s][p]
            d = max(d, abs(cx / x['n'] - cy / y['n']))
        tot += d
    return tot / 30


def rows(X, A, B):
    """(cell, metric, lhs, rhs_base, tol, slack) for one triple; slack = (lhs - rhs_base) / tol, the rule breaks when it exceeds 1."""
    out = []
    for cn in A:
        for k, m in enumerate(METRICS):
            if m == 'P':
                lhs, base = pstat(X[cn], A[cn]), pstat(B[cn], A[cn])
            else:
                lhs, base = abs(X[cn]['mean'][k] - A[cn]['mean'][k]), abs(B[cn]['mean'][k] - A[cn]['mean'][k])
            out.append((cn, m, lhs, base, TOL[m], (lhs - base) / TOL[m]))
    return out


def cmd_calibrate(a):
    blocks = parse_blocks(a.blocks)
    ex = {arm: load(a.out, arm, blocks) for arm in 'abcd'}
    if any(v is None for v in ex.values()):
        raise SystemExit('missing blocks for an exact arm')
    n = next(iter(ex['a'].values()))['n']
    lines = ['# Calibration on exact arms only (a,b,c,d), %d races a cell and arm, blocks %s' % (n, a.blocks)]
    worst, nbreak = -9, 0
    perrow = {}
    for X, A, B in itertools.permutations('abcd', 3):
        for r in rows(ex[X], ex[A], ex[B]):
            worst = max(worst, r[5])
            if r[5] > 1:
                nbreak += 1
            perrow[(r[0], r[1])] = max(perrow.get((r[0], r[1]), -9), r[5])
    lines.append('24 ordered triples x 30 rows = 720 tests: %d break the rule; worst slack %.3f (rule breaks above 1.0, required <= %.2f)' % (nbreak, worst, SLACK_MAX))
    lines.append('worst slack per row over the 24 triples:')
    for cn in sorted({k[0] for k in perrow}):
        lines.append('  %-24s %s' % (cn, '  '.join('%s %.2f' % (m, perrow[(cn, m)]) for m in METRICS)))
    lines.append('per-cell per-arm means (rival_ko rival_retire player_ko player_damage):')
    for cn in sorted(ex['a']):
        for arm in 'abcd':
            lines.append('  %-24s %s %s' % (cn, arm, ' '.join('%.4f' % v for v in ex[arm][cn]['mean'])))
    ok = nbreak == 0 and worst <= SLACK_MAX
    lines.append('CALIBRATION %s at N=%d' % ('PASS' if ok else 'FAIL', n))
    txt = '\n'.join(lines)
    print(txt)
    open(Path(a.out) / ('calibration-n%d.txt' % n), 'w').write(txt + '\n')


def cmd_score(a):
    blocks = parse_blocks(a.blocks)
    A, B = load(a.out, 'a', blocks), load(a.out, 'b', blocks)
    n = next(iter(A.values()))['n']
    lines = ['# P1 outcome check v2 (HOST), %d races a cell and arm, 6 cells' % n]
    verdict = {}
    for arm in ['lag', 'l8', 'drop']:
        X = load(a.out, arm, blocks)
        if X is None:
            continue
        rs = rows(X, A, B)
        bad = [r for r in rs if r[5] > 1]
        verdict[arm] = (len(rs), len(bad))
        lines.append('')
        lines.append('== arm %s: %d rows, %d break the rule' % (arm, len(rs), len(bad)))
        lines.append('   cell | metric | |X-A| (P: P(X,A)) | |B-A| (P: P(B,A)) | tol | slack')
        for r in rs:
            lines.append('   %-22s %-14s %.4f  %.4f  %.3f  %+.2f %s' % (r[0], r[1], r[2], r[3], r[4], r[5], 'BREAKS' if r[5] > 1 else ''))
    lines.append('')
    lines.append('VERDICT lag: %s' % ('PASS' if verdict.get('lag', (0, 1))[1] == 0 else 'FAIL'))
    for arm in ['l8', 'drop']:
        if arm in verdict:
            lines.append('CONTROL %s: %s (%d of %d rows break the rule)' % (arm, 'fails as it must' if verdict[arm][1] else 'DOES NOT FAIL (the check is blind to it: stop and report)', verdict[arm][1], verdict[arm][0]))
    txt = '\n'.join(lines)
    print(txt)
    open(Path(a.out) / ('score-n%d.txt' % n), 'w').write(txt + '\n')


def main():
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest='cmd', required=True)
    r = sp.add_parser('run')
    r.add_argument('--bins', required=True)
    r.add_argument('--out', required=True)
    r.add_argument('--jobs', type=int, default=4)
    r.add_argument('--arms', default='a,b,c,d')
    r.add_argument('--blocks', default='0')
    c = sp.add_parser('calibrate')
    c.add_argument('--out', required=True)
    c.add_argument('--blocks', default='0')
    s = sp.add_parser('score')
    s.add_argument('--out', required=True)
    s.add_argument('--blocks', default='0')
    a = ap.parse_args()
    {'run': cmd_run, 'calibrate': cmd_calibrate, 'score': cmd_score}[a.cmd](a)


if __name__ == '__main__':
    main()
