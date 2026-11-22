#!/usr/bin/env python3
"""Attacks outcome judge (C-161): per-cell metrics, coverage, structural checks and the frozen paired,
cell-stratified max-t bootstrap. Reads the episode files written by attacks_outcome.py.

  attacks_outcome_stats.py coverage EPISODES.jsonl [--arm R]        reference-side coverage per cell (no comparison)
  attacks_outcome_stats.py score EPISODES.jsonl --ref R --test N     the judge: every interval strictly inside its band
  attacks_outcome_stats.py score A.jsonl --ref R --test N --test-file B.jsonl   (the test arm from another run of the same episodes)

Statuses: pass (interval strictly inside the band), fail (interval entirely outside the band), inconclusive (crosses a band,
missing coverage, or zero estimated uncertainty). The overall verdict is pass only if every applicable interval passes,
coverage holds in every cell, and the structural checks are exact in both arms.
"""
import argparse, json, sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
CFG = json.load(open(HERE / 'attacks_outcome_config.json'))
TICKS = CFG['episode_ticks']


def load(path):
    return [json.loads(l) for l in open(path)]


def cell_key(r):
    return '/'.join(r['cell'])


def vectors(recs, arm):
    """per-episode arrays for one arm: (mean-type metrics: x), (ratio-type: num, den)"""
    o = [r[arm] for r in recs]
    m = {}
    m['hit'] = ('mean', np.array([x['hit'] for x in o], float))
    m['ko'] = ('mean', np.array([min(x['ko'], 1) for x in o], float))
    W = CFG['wall_ko_window']
    m['wall_ko'] = ('mean', np.array([(x['wall_ko'] if W == 0 else x['wall_ko_w'].get(str(W), 0)) if x['ko'] else 0 for x in o], float))
    m['ko_ticks'] = ('mean', np.array([x['ko_tick'] if x['ko_tick'] else TICKS for x in o], float))
    m['retire'] = ('mean', np.array([x['retire'] for x in o], float))
    for j, nm in ((1, 'victim'), (0, 'attacker')):
        den = np.array([x['nd'][j] for x in o], float)
        m['damage_' + nm] = ('ratio', np.array([x['sd'][j] for x in o], float), den)
        m['pre_energy_' + nm] = ('ratio', np.array([x['sp'][j] for x in o], float), den)
    return m


def band_of(metric):
    b = CFG['bands']
    for k in ('hit', 'wall_ko', 'ko_ticks', 'retire'):
        if metric == k:
            return b[k]
    if metric == 'ko':
        return b['ko']
    return b['damage'] if metric.startswith('damage') else b['pre_energy']


def applicable(metric, cell):
    man, env, plc, band = cell.split('/')
    ap = CFG['applicability']
    if metric in ('ko', 'ko_ticks'):
        return man in ap['ko']
    if metric == 'wall_ko':
        return man in ap['wall_ko']['maneuvers'] and plc in ap['wall_ko']['placements'] and env in ap['wall_ko']['environments']
    if metric.startswith(('damage_', 'pre_energy_')):
        return metric.split('_')[-1] in [x for x in CFG['reading_damage_racers']]
    return True


def structural(recs, arm):
    """exact checks: per-episode 'bad' lists empty; structurally-zero outcomes zero; counts by cell"""
    out = {}
    z = CFG['applicability']['structurally_zero']
    for r in recs:
        c = cell_key(r); man = r['cell'][0]
        d = out.setdefault(c, dict(bad=0, zero_violations=0, n=0))
        d['n'] += 1; o = r[arm]
        d['bad'] += bool(o['bad'])
        if man in z['attack_starts'] and (o['side_starts'] or o['spin_starts']): d['zero_violations'] += 1
    return out


def coverage(recs, arm):
    res = {}
    for r in recs:
        c = cell_key(r); d = res.setdefault(c, dict(n=0, contacts=0, nd_victim=0, nd_attacker=0, hit=0, ko=0, wall_ko=0, retire=0))
        o = r[arm]; d['n'] += 1; d['contacts'] += o['contacts']; d['nd_victim'] += o['nd'][1]; d['nd_attacker'] += o['nd'][0]
        d['hit'] += o['hit']; d['ko'] += int(o['ko'] > 0); d['wall_ko'] += int(o['ko'] > 0 and (o['wall_ko'] if CFG['wall_ko_window'] == 0 else o['wall_ko_w'].get(str(CFG['wall_ko_window']), 0))); d['retire'] += o['retire']
    miss = []
    mc, mk = CFG['coverage']['min_ref_contacts'], CFG['coverage']['min_ref_ko_events']
    for c, d in res.items():
        for nm in CFG['reading_damage_racers']:
            if d['nd_' + nm] < mc: miss.append((c, 'contact episodes with damage (%s)' % nm, d['nd_' + nm], mc))
        if applicable('ko', c) and d['ko'] < mk: miss.append((c, 'KO events', d['ko'], mk))
        if applicable('wall_ko', c) and d['wall_ko'] < mk: miss.append((c, 'wall-KO events', d['wall_ko'], mk))
    return res, miss


def score(ref_recs, test_recs, ref, test, scale=1, seed=None, draws=None, nmax=None):
    cfg_b = CFG['bootstrap']
    B = draws or cfg_b['draws']
    rng = np.random.default_rng(cfg_b['seed'] if seed is None else seed)
    by = {}
    for a, b in zip(ref_recs, test_recs):
        assert a['cell'] == b['cell'] and a['idx'] == b['idx'] and a['ns'] == b['ns'], 'episode files do not pair'
        by.setdefault(cell_key(a), []).append((a, b))
    keys = []   # (cell, metric)
    est, ses, thetas, cover = {}, {}, [], {}
    cov, miss = coverage([a for v in by.values() for a, _ in v], ref)
    for c in sorted(by):
        rr = [a for a, _ in by[c]]; tt = [b for _, b in by[c]]
        vr, vt = vectors(rr, ref), vectors(tt, test)
        names = [m for m in vr if applicable(m, c)]
        cols, spec = [], []
        for m in names:   # columns: for mean: x_t - x_r ; for ratio: num_t, den_t, num_r, den_r
            if vr[m][0] == 'mean':
                cols.append(vt[m][1] - vr[m][1]); spec.append((m, 'mean', len(cols) - 1))
            else:
                cols += [vt[m][1], vt[m][2], vr[m][1], vr[m][2]]; spec.append((m, 'ratio', len(cols) - 4))
        X = np.stack(cols, 1); n = X.shape[0]
        hat = stat_n(X.sum(0), spec, n)
        th = np.empty((B, len(spec)))
        step = 2000
        for s in range(0, B, step):
            w = min(step, B - s)
            cnt = rng.multinomial(n, np.full(n, 1.0 / n), size=w).astype(np.float64)
            S = cnt @ X
            th[s:s + w] = stat_n(S, spec, n)
        for j, (m, kind, i) in enumerate(spec):
            keys.append((c, m)); est[(c, m)] = hat[j]
            ses[(c, m)] = np.nanstd(th[:, j]); cover[(c, m)] = None
        thetas.append((c, th, hat))
    K = len(keys)
    T = np.empty((B, K)); col = 0
    for c, th, hat in thetas:
        for j in range(th.shape[1]):
            se = ses[keys[col]]
            T[:, col] = np.abs(th[:, j] - hat[j]) / se if se > 0 and np.isfinite(se) else 0.0
            col += 1
    cmax = float(np.quantile(np.nanmax(T, 1), cfg_b['confidence']))
    rows = []
    for (c, m) in keys:
        h, se, bd = float(est[(c, m)]), float(ses[(c, m)]), band_of(m)
        if not np.isfinite(h) or not np.isfinite(se) or se <= 0:
            st = 'inconclusive (zero or undefined uncertainty)'
        else:
            lo, hi = h - cmax * se, h + cmax * se
            st = 'pass' if (lo > -bd and hi < bd) else ('fail' if (lo >= bd or hi <= -bd) else 'inconclusive (crosses band)')
        rows.append(dict(cell=c, metric=m, diff=h, se=se, lo=h - cmax * se, hi=h + cmax * se, band=bd, status=st))
    return dict(critical=cmax, rows=rows, coverage=cov, missing_coverage=miss, structural_ref=structural([a for v in by.values() for a, _ in v], ref),
                structural_test=structural([b for v in by.values() for _, b in v], test))


def stat_n(S, spec, n):
    out = []
    for m, kind, i in spec:
        if kind == 'mean':
            out.append(S[..., i] / n)
        else:
            with np.errstate(divide='ignore', invalid='ignore'):
                out.append(S[..., i] / S[..., i + 1] - S[..., i + 2] / S[..., i + 3])
    return np.stack(out, -1)


def verdict(res):
    st = [r['status'] for r in res['rows']]
    bad = sum(v['bad'] + v['zero_violations'] for s in ('structural_ref', 'structural_test') for v in res[s].values())
    if res['missing_coverage'] or bad:
        return 'inconclusive' if not bad else 'structural violation'
    if all(s == 'pass' for s in st):
        return 'pass'
    return 'fail' if any(s == 'fail' for s in st) else 'inconclusive'


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('cmd', choices=['coverage', 'score'])
    ap.add_argument('file'); ap.add_argument('--arm', default='R'); ap.add_argument('--ref', default='R'); ap.add_argument('--test', default='N')
    ap.add_argument('--test-file'); ap.add_argument('--draws', type=int); ap.add_argument('--json', type=Path)
    a = ap.parse_args()
    recs = load(a.file)
    if a.cmd == 'coverage':
        cov, miss = coverage(recs, a.arm)
        for c in sorted(cov):
            d = cov[c]; n = d['n']
            print('%-26s n %4d contacts %5d dmg-eps v %4d a %4d | hit %.3f ko %.3f wallko %.3f retire/ep %.2f' % (
                c, n, d['contacts'], d['nd_victim'], d['nd_attacker'], d['hit'] / n, d['ko'] / n, d['wall_ko'] / n, d['retire'] / n))
        print('structural', {c: v for c, v in structural(recs, a.arm).items() if v['bad'] or v['zero_violations']} or 'exact')
        print('missing coverage (n=%d episodes/cell here; the ruling needs 100 / 50 at 1024):' % (len(recs) // max(1, len(cov))))
        for m in miss: print('  ', m)
        return
    other = load(a.test_file) if a.test_file else recs
    res = score(recs, other, a.ref, a.test, draws=a.draws)
    print('critical value %.3f over %d intervals' % (res['critical'], len(res['rows'])))
    for r in res['rows']:
        if r['status'] != 'pass':
            print('%-26s %-18s diff %+.4f [%+.4f, %+.4f] band %.2f  %s' % (r['cell'], r['metric'], r['diff'], r['lo'], r['hi'], r['band'], r['status']))
    print('verdict:', verdict(res))
    if a.json:
        a.json.write_text(json.dumps(res, indent=1, default=float))


if __name__ == '__main__':
    main()
