#!/usr/bin/env python3
"""E2 rival checks: native field runs against the reference bands (HOST).
Usage: rivals_e2_check.py BANDS.json NATIVE.json... [--json OUT]
The reference bands come from fzx-gp-ref runs (original code, 30 racers). A native metric passes when
every run lies inside the reference band widened by the tolerance below."""
import json, statistics as st, sys
TOL = {  # metric: (relative widening, absolute widening)
    'finished': (0.0, 2), 'retired': (0.0, 2), 'winner_tick': (0.03, 0), 'median_finish_tick': (0.05, 0), 'last_finish_tick': (0.10, 0),
    'lap1_min': (0.03, 0), 'lap1_median': (0.05, 0), 'lap1_max': (0.08, 0), 'rank_change_ticks': (0.5, 0), 'spread_mean': (0.35, 0),
    'spread_max': (0.35, 0), 'grid_finish_spearman': (0.0, 0.25), 'rival_mean_speed': (0.05, 0),
}
CONTACT_LIMIT = 250  # contact incidents per race; avoiding keeps the field well under it (native-only bound)
def metrics(d):
    R = d['racers']; fin = [r for r in R if r['finish_tick'] >= 0]; rivals = [r for r in R if r['id'] != 0]
    ft = sorted(r['finish_tick'] for r in fin); lap1 = sorted(r['lap_end_ticks'][0] for r in R if r['lap_end_ticks'][0] >= 0)
    def rank(v):
        o = sorted(range(len(v)), key=lambda i: v[i]); r = [0] * len(v)
        for k, i in enumerate(o): r[i] = k
        return r
    n = len(fin); rg, rf = rank([r['grid'] for r in fin]), rank([r['final_pos'] for r in fin])
    rho = 1 - 6 * sum((a - b) ** 2 for a, b in zip(rg, rf)) / (n * (n * n - 1)) if n > 2 else 0
    return dict(finished=n, retired=d['retired'], winner_tick=ft[0] if ft else -1, median_finish_tick=ft[n // 2] if ft else -1,
                last_finish_tick=ft[-1] if ft else -1, lap1_min=lap1[0], lap1_median=lap1[len(lap1) // 2], lap1_max=lap1[-1],
                rank_change_ticks=d['rank_change_ticks'], spread_mean=d['spread_mean'], spread_max=d['spread_max'],
                grid_finish_spearman=round(rho, 3), rival_mean_speed=round(st.mean(r['mean_speed'] for r in rivals), 3))
def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]; out = sys.argv[sys.argv.index('--json') + 1] if '--json' in sys.argv else None
    if out: args.remove(out)
    bands = json.load(open(args[0]))['bands']; runs = [json.load(open(p)) for p in args[1:]]; M = [metrics(d) for d in runs]
    rows, ok = [], True
    for k, (rel, ab) in TOL.items():
        lo, hi = bands[k]['min'], bands[k]['max']; lo2, hi2 = lo - abs(lo) * rel - ab, hi + abs(hi) * rel + ab
        v = [m[k] for m in M]; good = all(lo2 <= x <= hi2 for x in v); ok &= good
        rows.append(dict(metric=k, reference=[lo, hi], accepted=[round(lo2, 3), round(hi2, 3)], native=[min(v), max(v)], verdict='PASS' if good else 'FAIL'))
    contacts = [d.get('contacts', 0) for d in runs]
    exact = dict(offtrack_ticks=sum(d.get('offtrack_ticks', 0) for d in runs), stuck_events=sum(d.get('stuck_events', 0) for d in runs),
                 through_ticks=sum(d.get('through_ticks', 0) for d in runs), rivals_pass_each_other=min(d['rank_change_ticks'] for d in runs) > 0,
                 contact_incidents_max=max(contacts), contact_incidents_limit=CONTACT_LIMIT)
    exact_ok = exact['offtrack_ticks'] == 0 and exact['stuck_events'] == 0 and exact['through_ticks'] == 0 and exact['rivals_pass_each_other'] and max(contacts) <= CONTACT_LIMIT
    res = dict(verdict='PASS' if ok and exact_ok else 'FAIL', runs=len(runs), bands=rows, exact=exact, exact_verdict='PASS' if exact_ok else 'FAIL')
    # C-100: the revised off-track predicate (grounded ticks only; airborne and fallen ticks scored apart), side by side
    # with the accepted one above, which stays the verdict. It also needs the geometry trace clean: no AIR without an
    # air node, no air node without AIR, no suppressed fall. Runs without the split (older reports) leave it out.
    if all('offtrack_split' in d for d in runs):
        sp = [d['offtrack_split'] for d in runs]; geo = [d.get('geometry', {}) for d in runs]
        if any(x['old'] != sum(x['oob']) or x['revised'] != x['oob'][0] or x['old'] != d.get('offtrack_ticks') for x, d in zip(sp, runs)):
            raise SystemExit('offtrack_split does not add up (old = sum of the three classes, revised = grounded)')
        rev = dict(exact, offtrack_ticks=sum(x['revised'] for x in sp),
                   eligible=[sum(x['eligible'][k] for x in sp) for k in range(3)], oob=[sum(x['oob'][k] for x in sp) for k in range(3)],
                   geometry={k: sum(g.get(k, 0) for g in geo) for k in ('air_unexplained', 'air_missing', 'fall_suppressed')})
        rev_ok = (rev['offtrack_ticks'] == 0 and not any(rev['geometry'].values()) and exact['stuck_events'] == 0 and exact['through_ticks'] == 0
                  and exact['rivals_pass_each_other'] and max(contacts) <= CONTACT_LIMIT)
        res.update(exact_revised=rev, exact_revised_verdict='PASS' if rev_ok else 'FAIL', verdict_revised='PASS' if ok and rev_ok else 'FAIL')
    for r in rows: print('%-22s ref %s  accept %s  native %s  %s' % (r['metric'], r['reference'], r['accepted'], r['native'], r['verdict']))
    print('exact', exact, res['exact_verdict'])
    if 'exact_revised' in res: print('exact-revised', res['exact_revised'], res['exact_revised_verdict'])
    print('VERDICT', res['verdict'])
    if 'verdict_revised' in res: print('VERDICT-REVISED', res['verdict_revised'])
    if out: json.dump(res, open(out, 'w'), indent=1)
    sys.exit(0 if res['verdict'] == 'PASS' else 1)
main()
