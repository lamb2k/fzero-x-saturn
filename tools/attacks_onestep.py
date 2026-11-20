#!/usr/bin/env python3
"""Matched-state one-tick scan and first-divergence classes for the attacks duel windows (C-092), HOST.

For every tick t of the failing windows of a C-092 judge result, the native duel runner is started from
the reference's own state after tick t - 1 (both racers, the reference's colliding bit, credit counters
and spin draws) and run one tick with the recorded pads; its state after t is compared with the
reference's. A one-tick difference beyond rounding is a model difference at that tick; a window whose
native trajectory diverges at a tick where the one-tick step from the reference state agrees is
drift-triggered (the native trajectory's own accumulated drift crossed a threshold there).

Per failing window it reports the first discrete divergence of the native trajectory (wall entry,
contact decision, damage, spin-out, retirement, KO; else the endpoint only) and its class:
  model:<what>   a one-tick step from the reference state already differs at or before that tick (a
                 discrete difference, or model:continuous-r<k>: racer k's one-tick velocity differs by
                 more than VEL_EPS with no discrete difference)
  drift:<what>   the one-tick steps agree up to that tick; the window's own drift triggered it
  endpoint-only  no model difference and no discrete divergence; only the endpoint is out of band

Usage:
  attacks_onestep.py --ref FZX_DUEL --native N --native2 N2 --course C --course2 C2 --rom ROM
      --judge JUDGE_DIR --result c092-base.json --out DIR [--jobs N] [--scenarios a,b] [--table FILE.json]
Everything written is derived from the owner's ROM: local only (DIR outside the repository)."""
import argparse, json, math, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import attacks_duel as AD  # noqa: E402
import attacks_contact_judge as J  # noqa: E402

# state flags compared tick by tick (the native body mirrors the original's bits): damaged, spinning,
# recoil (wall), retired, crashed
F_DAMAGED, F_SPIN, F_RECOIL, F_RETIRED, F_CRASHED = 1 << 13, 1 << 14, 1 << 17, 1 << 18, 1 << 27
FLAG_MASK = F_DAMAGED | F_SPIN | F_RECOIL | F_RETIRED | F_CRASHED
VEL_EPS = 1e-3     # a one-tick velocity difference beyond rounding (Q16 rounding is ~1e-5 a tick)
E_EPS = 0.01       # the judge's energy event threshold


def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        raise SystemExit('failed: %s\n%s' % (' '.join(cmd), p.stderr[-2000:]))
    return p.stdout


def vel(rows, t, k):
    return [float(x) for x in rows[(t, k)][5:8]]


def discrete(ref_rows, nat_rows, rlog, nlog, t):
    """Discrete differences at tick t between a native and the reference: list of (what, racer)."""
    out = []
    for k in (0, 1):
        if (t, k) not in nat_rows:
            return [('stopped', k)]
        r, n = AD.state(ref_rows, t, k), AD.state(nat_rows, t, k)
        x = (r['flags'] ^ n['flags']) & FLAG_MASK
        if x & F_RECOIL:
            out.append(('wall', k))
        if x & (F_SPIN | F_RETIRED | F_CRASHED):
            out.append(('spin/retire', k))
        if r['attack'] != n['attack']:
            out.append(('attack', k))
        pr, pn = AD.state(ref_rows, t - 1, k), AD.state(nat_rows, t - 1, k) if (t - 1, k) in nat_rows else None
        if pn is not None:
            dr, dn = pr['energy'] - r['energy'], pn['energy'] - n['energy']
            if abs(dr - dn) > max(E_EPS, 0.03 * abs(dr)):
                out.append(('damage', k))
    rt = 1 if rlog['T'].get(t) else 0
    rp = 1 if (rlog['C'].get(t) and rlog['C'][t][0][2][2]) else 0
    nn = nlog['N'].get(t, dict(touch=0, push=0))
    if rt != nn['touch']:
        out.append(('contact-reach', 2))
    elif rp != nn['push']:
        out.append(('contact-closing', 2))
    rk = sorted((v, a) for (v, a, _) in rlog['K'].get(t, []))
    nk = sorted(nlog['K'].get(t, []))
    if rk != nk:
        out.append(('ko', 2))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ref', required=True); ap.add_argument('--native', required=True); ap.add_argument('--native2')
    ap.add_argument('--course', required=True); ap.add_argument('--course2'); ap.add_argument('--rom', required=True)
    ap.add_argument('--judge', required=True, type=Path, help='the judge directory (ref.tsv, ref.pads, ref.pairlog, w*/)')
    ap.add_argument('--result', required=True, type=Path, help='the judge result (c092-*.json)')
    ap.add_argument('--tag', default='c092-native', help='native trace name in each window directory')
    ap.add_argument('--out', required=True, type=Path); ap.add_argument('--jobs', type=int, default=8)
    ap.add_argument('--scenarios', default=''); ap.add_argument('--table', type=Path)
    ap.add_argument('--all', action='store_true', help='scan every window, not only the failing ones')
    ap.add_argument('--native-arg', action='append', default=[], dest='native_args', help='extra runner argument (repeatable)')
    a = ap.parse_args()
    table = {k: tuple(v) for k, v in json.loads(a.table.read_text()).items() if not k.startswith('_')} if a.table else AD.SCENARIOS
    res = json.loads(a.result.read_text())
    want = set(a.scenarios.split(',')) if a.scenarios else None
    a.out.mkdir(parents=True, exist_ok=True)
    report = dict(result=str(a.result), windows=[], scan={})
    for sc, v in res['scenarios'].items():
        if want and sc not in want:
            continue
        wins = [w for w in v['windows'] if a.all or not w['c092_ok']]
        if not wins:
            continue
        script, mref, mnat, ticks, cid, afrom = table[sc]
        base = ['--course', a.course if cid == 1 else a.course2, '--rom', a.rom, '--attack-from', str(afrom)]
        native = a.native if cid == 1 else (a.native2 or a.native)
        d = a.judge / sc
        rows = AD.load(d / 'ref.tsv')
        lg = J.load_ref_log(d / 'ref.pairlog')
        last = max(t for t, _ in rows)
        span = set()
        for w in wins:
            span.update(range(w['start'] + 1, min(w['start'] + w['length'], last) + 1))
        lo, hi = min(span), max(span)
        o = a.out / sc
        bk = o / 'bakes'
        bk.mkdir(parents=True, exist_ok=True)
        if not (bk / ('p1-%05d.src' % hi)).exists() or not (bk / ('p0-%05d.src' % (lo - 1))).exists():
            run([a.ref] + base + ['--scenario', script, '--machines', mref, '--ticks', str(hi + 1), '--replay-pads', str(d / 'ref.pads'),
                                  '--bake-range', '%d:%d' % (lo - 1, hi), '--bake-dir', str(bk)])

        def step(t):
            """Native one tick from the reference state after t - 1."""
            s = o / ('s%05d' % t)
            s.mkdir(exist_ok=True)
            tr, pl = s / 'n.tsv', s / 'n.pairlog'
            if not tr.exists():
                for k in (0, 1):
                    run([sys.executable, str(Path(AD.physics_pack.__file__)), str(bk / ('p%d-%05d.src' % (k, t - 1))), str(s / ('p%d.pack' % k))])
                with open(s / 'draws.txt', 'w') as df:
                    for (k, ctx, vv) in lg['D'].get(t, []):
                        if k in (0, 1) and not (int(vv[2]) & ((1 << 27) | (1 << 25))) and J.f32sub(vv[1], vv[0]) < 0.0:
                            st = float(rows[(t, k)][19])
                            if st != 0.0:
                                df.write('%d %d %d\n' % (t, k, 1 if st > 0 else -1))
                r1 = AD.state(rows, t - 1, 1)
                run([native, '--pack0', str(s / 'p0.pack'), '--pack1', str(s / 'p1.pack'), '--pads', str(d / 'ref.pads'), '--from', str(t - 1),
                     '--ticks', '1', '--machines', mnat, '--since', str(r1['since']), '--toucher', str(r1['toucher']),
                     '--touching', str(lg['P'][t]['colliding']), '--trace', str(tr), '--pairlog', str(pl), '--spin-draws', str(s / 'draws.txt'),
                     '--regain-pending', '1' if any(att == 0 for (_, att, _) in lg['K'].get(t - 1, [])) else '0'] + a.native_args)
                for k in (0, 1):
                    (s / ('p%d.pack' % k)).unlink()
            nat = AD.load(tr)
            for k in (0, 1):
                nat[(t - 1, k)] = rows[(t - 1, k)]
            nlog = J.load_nat_log(pl)
            dv = [math.dist(vel(rows, t, k), vel(nat, t, k)) for k in (0, 1)]
            dp = [math.dist(AD.state(rows, t, k)['pos'], AD.state(nat, t, k)['pos']) for k in (0, 1)]
            return t, dict(dv=dv, dp=dp, discrete=discrete(rows, nat, lg, nlog, t))
        with ThreadPoolExecutor(a.jobs) as ex:
            scan = dict(ex.map(step, sorted(span)))
        report['scan'][sc] = {str(t): x for t, x in scan.items() if x['discrete'] or max(x['dv']) > VEL_EPS}

        for w in wins:
            t0, n = w['start'], w['length']
            end = min(t0 + n, last)
            wd = d / ('w%05d' % t0)
            nat = AD.load(wd / (a.tag + '.tsv'))
            for k in (0, 1):
                nat[(t0, k)] = rows[(t0, k)]
            nlog = J.load_nat_log(wd / (a.tag + '.pairlog'))
            first = None
            for t in range(t0 + 1, end + 1):
                dd = discrete(rows, nat, lg, nlog, t)
                if dd:
                    first = (t, dd)
                    break
            upto = first[0] if first else end
            model = next(((t, scan[t]) for t in range(t0 + 1, upto + 1) if scan[t]['discrete']), None)
            cont = max(((max(scan[t]['dv']), t) for t in range(t0 + 1, upto + 1)), default=(0.0, None))
            if model:
                cls = 'model:' + '+'.join(sorted({x[0] for x in model[1]['discrete']}))
            elif cont[0] > VEL_EPS:   # no discrete difference, but a one-tick velocity difference beyond rounding
                k = 0 if scan[cont[1]]['dv'][0] >= scan[cont[1]]['dv'][1] else 1
                cls = 'model:continuous-r%d' % k
            elif first:
                cls = 'drift:' + '+'.join(sorted({x[0] for x in first[1]}))
            else:
                cls = 'endpoint-only'
            report['windows'].append(dict(scenario=sc, course=cid, start=t0, length=n, pos_ok=w['pos_ok'], events_ok=w['events_ok'],
                                          speed_ok=w['speed_ok'], first=first and [first[0], first[1]], model=model and [model[0], model[1]],
                                          max_onetick_dv=cont[0], max_onetick_dv_tick=cont[1], cls=cls,
                                          state_err_at_first=[math.dist(AD.state(rows, first[0] - 1, k)['pos'], AD.state(nat, first[0] - 1, k)['pos'])
                                                              for k in (0, 1)] if first else None))
    (a.out / 'onestep.json').write_text(json.dumps(report, indent=1))
    from collections import Counter
    c = Counter(w['cls'] for w in report['windows'])
    print('failing windows %d' % len(report['windows']))
    for k, x in c.most_common():
        print('  %-40s %d' % (k, x))


if __name__ == '__main__':
    main()
