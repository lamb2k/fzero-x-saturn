#!/usr/bin/env python3
"""Attacks outcome judge (C-168, attacks-5): per-cell metrics and the one scoring rule. Reads the episode files written by
attacks_outcome.py.

  attacks_outcome_stats.py coverage EPISODES.jsonl [--arm C]              reference-side coverage per cell (information only)
  attacks_outcome_stats.py score EPISODES.jsonl [--test-file B.jsonl] [--ref C] [--nudge T] [--test N] [--json OUT]

The rule (C-168, O-175): for each cell and metric, pass when |test - ref| <= |nudge - ref| + tolerance, on the same starts. ref is the
original from the converted start (C), nudge the same with the 1 q post-load start-velocity nudge (T), test the native pair (N).
No intervals, no bootstrap, no bands: the bar is the reference's own variation plus a fixed allowance.
The structural per-episode checks are reported for every arm (they are information, not part of the rule).
"""
import argparse, json
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
CFG = json.load(open(HERE / 'attacks_outcome_config.json'))
TICKS = CFG['episode_ticks']
TOL = CFG['scoring']['tolerance']


def load(path):
    return [json.loads(l) for l in open(path)]


def cell_key(r):
    return '/'.join(r['cell'])


def tol_of(metric):
    if metric in TOL:
        return TOL[metric]
    return TOL['damage'] if metric.startswith('damage') else TOL['pre_energy']


def applicable(metric, cell):
    man, env, plc, band = cell.split('/')
    ap = CFG['applicability']
    if metric in ('ko', 'ko_ticks'):
        return man in ap['ko']
    if metric == 'wall_ko':
        return man in ap['wall_ko']['maneuvers'] and plc in ap['wall_ko']['placements'] and env in ap['wall_ko']['environments']
    if metric.startswith(('damage_', 'pre_energy_')):
        return metric.split('_')[-1] in CFG['reading_damage_racers']
    return True


METRICS = ['hit', 'ko', 'wall_ko', 'ko_ticks', 'retire', 'damage_victim', 'damage_attacker', 'pre_energy_victim', 'pre_energy_attacker']


def wall_ko_of(x):
    W = CFG['wall_ko_window']
    return int((x['wall_ko'] if W == 0 else x['wall_ko_w'].get(str(W), 0)) if x['ko'] else 0)


def cell_metrics(outs):
    """one arm's metrics over the episodes of one cell"""
    n = len(outs)
    m = {}
    m['hit'] = sum(x['hit'] for x in outs) / n
    m['ko'] = sum(min(x['ko'], 1) for x in outs) / n
    m['wall_ko'] = sum(wall_ko_of(x) for x in outs) / n
    kt = [x['ko_tick'] for x in outs if x['ko'] and x['ko_tick']]
    m['ko_ticks'] = sum(kt) / len(kt) if kt else float('nan')
    m['retire'] = sum(x['retire'] for x in outs) / n
    for j, nm in ((1, 'victim'), (0, 'attacker')):
        den = sum(x['nd'][j] for x in outs)
        m['damage_' + nm] = sum(x['sd'][j] for x in outs) / den if den else float('nan')
        m['pre_energy_' + nm] = sum(x['sp'][j] for x in outs) / den if den else float('nan')
    return m


def structural(recs, arm):
    """per-episode checks ('bad' lists) and structurally-zero outcomes, counted by cell"""
    out = {}
    z = CFG['applicability']['structurally_zero']
    for r in recs:
        c = cell_key(r)
        d = out.setdefault(c, dict(bad=0, zero_violations=0, n=0))
        d['n'] += 1; o = r[arm]
        d['bad'] += bool(o['bad'])
        if r['cell'][0] in z['attack_starts'] and (o['side_starts'] or o['spin_starts']): d['zero_violations'] += 1
    return out


def coverage(recs, arm):
    res = {}
    for r in recs:
        c = cell_key(r); d = res.setdefault(c, dict(n=0, contacts=0, nd_victim=0, nd_attacker=0, hit=0, ko=0, wall_ko=0, retire=0))
        o = r[arm]; d['n'] += 1; d['contacts'] += o['contacts']; d['nd_victim'] += o['nd'][1]; d['nd_attacker'] += o['nd'][0]
        d['hit'] += o['hit']; d['ko'] += int(o['ko'] > 0); d['wall_ko'] += wall_ko_of(o); d['retire'] += o['retire']
    return res


def score(ref_recs, nudge_recs, test_recs, ref='C', nudge='T', test='N'):
    """the C-168 rule for every applicable cell and metric. The three record lists pair episode for episode."""
    by = {}
    for a, t, b in zip(ref_recs, nudge_recs, test_recs):
        assert a['cell'] == b['cell'] == t['cell'] and a['idx'] == b['idx'] == t['idx'] and a['ns'] == b['ns'] == t['ns'], 'episode files do not pair'
        by.setdefault(cell_key(a), []).append((a[ref], t[nudge], b[test]))
    rows = []
    for c in sorted(by):
        r_m = cell_metrics([x[0] for x in by[c]]); t_m = cell_metrics([x[1] for x in by[c]]); n_m = cell_metrics([x[2] for x in by[c]])
        for m in METRICS:
            if not applicable(m, c):
                continue
            ref_v, nud_v, tst_v = r_m[m], t_m[m], n_m[m]
            dn, dt = tst_v - ref_v, nud_v - ref_v
            allowed = abs(dt) + tol_of(m)
            nan = any(v != v for v in (ref_v, nud_v, tst_v))
            rows.append(dict(cell=c, metric=m, ref=ref_v, nudge=nud_v, test=tst_v, d_native=dn, d_nudge=dt, tol=tol_of(m), allowed=allowed,
                             n=len(by[c]), status='undefined' if nan else ('pass' if abs(dn) <= allowed else 'fail')))
    return dict(rows=rows)


def verdict(res):
    st = [r['status'] for r in res['rows']]
    return 'pass' if st and all(s == 'pass' for s in st) else ('fail' if any(s == 'fail' for s in st) else 'undefined')


def summarize(res):
    by = {}
    for r in res['rows']:
        d = by.setdefault(r['metric'], [0, 0, 0]); d[0] += 1; d[1] += r['status'] == 'pass'; d[2] += r['status'] != 'pass'
    return by


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('cmd', choices=['coverage', 'score'])
    ap.add_argument('file'); ap.add_argument('--arm', default='C'); ap.add_argument('--ref', default='C'); ap.add_argument('--nudge', default='T')
    ap.add_argument('--test', default='N'); ap.add_argument('--test-file'); ap.add_argument('--json', type=Path)
    ap.add_argument('--verbose', action='store_true', help='print every cell and metric, not only failures')
    a = ap.parse_args()
    recs = load(a.file)
    if a.cmd == 'coverage':
        cov = coverage(recs, a.arm)
        for c in sorted(cov):
            d = cov[c]; n = d['n']
            print('%-26s n %4d contacts %5d dmg-eps v %4d a %4d | hit %.3f ko %.3f wallko %.3f retire/ep %.2f' % (
                c, n, d['contacts'], d['nd_victim'], d['nd_attacker'], d['hit'] / n, d['ko'] / n, d['wall_ko'] / n, d['retire'] / n))
        print('structural', {c: v for c, v in structural(recs, a.arm).items() if v['bad'] or v['zero_violations']} or 'exact')
        return
    other = load(a.test_file) if a.test_file else recs
    res = score(recs, recs, other, a.ref, a.nudge, a.test)
    for r in res['rows']:
        if a.verbose or r['status'] != 'pass':
            print('%-26s %-18s ref %9.4f nudge %+9.4f native %+9.4f allowed %.4f  %s' % (
                r['cell'], r['metric'], r['ref'], r['d_nudge'], r['d_native'], r['allowed'], r['status']))
    print('per metric (cells, pass, not pass):', summarize(res))
    print('verdict:', verdict(res))
    if a.json:
        a.json.write_text(json.dumps(res, indent=1, default=float))


if __name__ == '__main__':
    main()
