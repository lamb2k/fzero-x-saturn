#!/usr/bin/env python3
"""E2 rival checks: native field runs against the reference bands (HOST).
Usage: rivals_e2_check.py BANDS.json NATIVE.json... [--json OUT] [--mode legacy|revised]
The reference bands come from fzx-gp-ref runs (original code, 30 racers). A native metric passes when
every run lies inside the reference band widened by the tolerance below.
Modes (C-103). legacy (default; older reports): the exit status is the historical verdict (all non-external
off-track ticks). revised (behaviour-enabled runs): the exit status is the revised verdict (grounded off-track
ticks only, C-100), and every run must carry offtrack_split (eligible and out-of-bounds ticks for grounded,
airborne and falling/post-fall) and all three geometry counters; a run without them is unscorable. Both exact
verdicts and the per-class counts are printed in either mode.
Exit status: 0 the selected verdict passes, 1 it fails, 2 a run is unscorable in revised mode (missing or
inconsistent split or geometry)."""
import json, statistics as st, sys
TOL = {  # metric: (relative widening, absolute widening)
    'finished': (0.0, 2), 'retired': (0.0, 2), 'winner_tick': (0.03, 0), 'median_finish_tick': (0.05, 0), 'last_finish_tick': (0.10, 0),
    'lap1_min': (0.03, 0), 'lap1_median': (0.05, 0), 'lap1_max': (0.08, 0), 'rank_change_ticks': (0.5, 0), 'spread_mean': (0.35, 0),
    'spread_max': (0.35, 0), 'grid_finish_spearman': (0.0, 0.25), 'rival_mean_speed': (0.05, 0),
}
GEOMETRY = ('air_unexplained', 'air_missing', 'fall_suppressed')  # C-100 B2 counters, all required in revised mode
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
    out = sys.argv[sys.argv.index('--json') + 1] if '--json' in sys.argv else None
    mode = sys.argv[sys.argv.index('--mode') + 1] if '--mode' in sys.argv else 'legacy'
    if mode not in ('legacy', 'revised'): raise SystemExit('--mode must be legacy or revised')
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    for v in (out, mode):
        if v in args: args.remove(v)
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
    # C-100 / C-103: the revised off-track predicate (grounded ticks only; airborne and fallen ticks scored apart). It needs
    # the geometry trace clean: no AIR without an air node, no air node without AIR, no suppressed fall. A run without
    # the split or any of the three geometry counters is unscorable for it: the revised verdict is UNSCORABLE (never
    # read as zero), and in revised mode that is exit status 2.
    problems = []
    for path, d in zip(args[1:], runs):
        x, g = d.get('offtrack_split'), d.get('geometry')
        miss = [k for k in ('old', 'revised', 'eligible', 'oob') if not isinstance(x, dict) or k not in x]
        miss += ['geometry.' + k for k in GEOMETRY if not isinstance(g, dict) or not isinstance(g.get(k), int)]
        if not miss and (len(x['eligible']) != 3 or len(x['oob']) != 3):
            miss.append('offtrack_split classes (need grounded, airborne, falling)')
        if miss:
            problems.append('%s: missing %s' % (path, ', '.join(miss)))
        elif x['old'] != sum(x['oob']) or x['revised'] != x['oob'][0] or x['old'] != d.get('offtrack_ticks'):
            problems.append('%s: offtrack_split does not add up (old = sum of the three classes = offtrack_ticks, revised = grounded)' % path)
    if problems:
        res.update(exact_revised=None, exact_revised_verdict='UNSCORABLE', verdict_revised='UNSCORABLE', unscorable=problems)
    else:
        sp = [d['offtrack_split'] for d in runs]
        rev = dict(exact, offtrack_ticks=sum(x['revised'] for x in sp),
                   eligible=[sum(x['eligible'][k] for x in sp) for k in range(3)], oob=[sum(x['oob'][k] for x in sp) for k in range(3)],
                   geometry={k: sum(d['geometry'][k] for d in runs) for k in GEOMETRY})
        rev_ok = (rev['offtrack_ticks'] == 0 and not any(rev['geometry'].values()) and exact['stuck_events'] == 0 and exact['through_ticks'] == 0
                  and exact['rivals_pass_each_other'] and max(contacts) <= CONTACT_LIMIT)
        res.update(exact_revised=rev, exact_revised_verdict='PASS' if rev_ok else 'FAIL', verdict_revised='PASS' if ok and rev_ok else 'FAIL')
    res['mode'] = mode
    res['selected_verdict'] = res['verdict_revised'] if mode == 'revised' else res['verdict']
    for r in rows: print('%-22s ref %s  accept %s  native %s  %s' % (r['metric'], r['reference'], r['accepted'], r['native'], r['verdict']))
    print('exact', exact, res['exact_verdict'])
    for msg in res.get('unscorable', []): print('unscorable', msg)
    if res['exact_revised']:
        r = res['exact_revised']
        print('per-class eligible grounded/airborne/falling %s  out-of-bounds %s  geometry %s' % (r['eligible'], r['oob'], r['geometry']))
    print('exact-revised', res['exact_revised'], res['exact_revised_verdict'])
    print('VERDICT', res['verdict'])
    print('VERDICT-REVISED', res['verdict_revised'])
    print('SELECTED %s %s' % (mode, res['selected_verdict']))
    if out: json.dump(res, open(out, 'w'), indent=1)
    sys.exit({'PASS': 0, 'FAIL': 1}.get(res['selected_verdict'], 2))
main()
