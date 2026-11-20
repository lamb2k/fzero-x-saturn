#!/usr/bin/env python3
"""Moving-endpoint windows of the attacks duel judge (C-096 item 4), HOST.

For each named window (both racers moving at the endpoint, positions and events in band, only the
endpoint speed out of the frozen C-092 band) this reports, from the judge's own reference and native
traces:
  - the first divergent tick and field: per racer and per state field, the first tick whose native
    value differs from the reference's by more than the field's quantum (QUANTA), and the first tick
    whose speed error passes the window's frozen band max(one speed quantum, 0.005 |reference speed|);
  - both endpoint speed errors against the band, the position errors (32-unit bound) and the event result,
    as the judge scored them;
  - the speed error's growth: the ticks with the largest one-tick change of each racer's speed error,
    with the reference's discrete state at that tick (flags, attack, contact);
  - matched-state replays: the native runner started from the reference's own state after tick s - 1
    (both racers, colliding bit, credit counters, spin draws, regain) and run to the window's end with
    the recorded pads, for s = the first divergence, the first band crossing and the largest growth tick;
    each reports its endpoint speed errors against the same band.
Nothing here changes the runtime arithmetic or a tolerance.

Usage:
  attacks_moving.py --ref FZX_DUEL --native N --native2 N2 --course C --course2 C2 --rom ROM
      --judge JUDGE_DIR --result c092-base.json --windows cpu:2542,cpu:2543,ko-m9:1949 --out DIR [--table FILE.json]
Everything written is derived from the owner's ROM: local only (DIR outside the repository)."""
import argparse, json, math, subprocess, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import attacks_duel as AD  # noqa: E402
import attacks_contact_judge as J  # noqa: E402

SPEED_Q = 1.0 / 65536.0      # the native speed's quantum (Q16 units a tick)
QUANTA = dict(pos=1.0 / 256.0, vel=1.0 / 65536.0, speed=SPEED_Q, energy=1.0 / 256.0)
POS_BOUND = 32.0


def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        raise SystemExit('failed: %s\n%s' % (' '.join(cmd), p.stderr[-2000:]))
    return p.stdout


def fields(rows, t, k):
    r = rows[(t, k)]
    s = AD.state(rows, t, k)
    return dict(pos=s['pos'], vel=[float(x) for x in r[5:8]], speed=s['speed'], energy=s['energy'])


def err(a, b):
    if isinstance(a, list):
        return math.dist(a, b)
    return abs(a - b)


def band(ref_speed):
    return max(SPEED_Q, 0.005 * abs(ref_speed))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ref', required=True); ap.add_argument('--native', required=True); ap.add_argument('--native2')
    ap.add_argument('--course', required=True); ap.add_argument('--course2'); ap.add_argument('--rom', required=True)
    ap.add_argument('--judge', required=True, type=Path); ap.add_argument('--result', required=True, type=Path)
    ap.add_argument('--windows', required=True, help='scenario:start,...')
    ap.add_argument('--tag', default='c092-native'); ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--table', type=Path)
    ap.add_argument('--native-arg', action='append', default=[], dest='native_args')
    a = ap.parse_args()
    table = {k: tuple(v) for k, v in json.loads(a.table.read_text()).items() if not k.startswith('_')} if a.table else AD.SCENARIOS
    res = json.loads(a.result.read_text())
    a.out.mkdir(parents=True, exist_ok=True)
    report = dict(result=str(a.result), quanta=QUANTA, windows=[])
    for spec in a.windows.split(','):
        sc, t0 = spec.split(':')
        t0 = int(t0)
        w = next(x for x in res['scenarios'][sc]['windows'] if x['start'] == t0)
        script, mref, mnat, ticks, cid, afrom = table[sc]
        base = ['--course', a.course if cid == 1 else a.course2, '--rom', a.rom, '--attack-from', str(afrom)]
        native = a.native if cid == 1 else (a.native2 or a.native)
        d = a.judge / sc
        rows = AD.load(d / 'ref.tsv')
        lg = J.load_ref_log(d / 'ref.pairlog')
        last = max(t for t, _ in rows)
        end = min(t0 + w['length'], last)
        wd = d / ('w%05d' % t0)
        nat = AD.load(wd / (a.tag + '.tsv'))
        for k in (0, 1):
            nat[(t0, k)] = rows[(t0, k)]
        # first divergence per racer and field; the speed error curve
        first = {}
        curve = {0: [], 1: []}
        cross = {}
        for t in range(t0 + 1, end + 1):
            for k in (0, 1):
                if (t, k) not in nat:
                    continue
                fr, fn = fields(rows, t, k), fields(nat, t, k)
                for f, q in QUANTA.items():
                    if (k, f) not in first and err(fr[f], fn[f]) > q:
                        first[(k, f)] = dict(tick=t, ref=fr[f], native=fn[f], err=err(fr[f], fn[f]))
                e = fn['speed'] - fr['speed']
                curve[k].append((t, e))
                if k not in cross and abs(e) > band(fr['speed']):
                    cross[k] = t
        t_first = min((v['tick'] for v in first.values()), default=None)
        f_first = sorted('r%d.%s' % kf for kf, v in first.items() if v['tick'] == t_first)
        growth = {}
        for k in (0, 1):
            steps = [(abs(curve[k][i][1] - curve[k][i - 1][1]), curve[k][i][0]) for i in range(1, len(curve[k]))]
            steps.sort(reverse=True)
            growth[k] = []
            for dv, t in steps[:5]:
                s = AD.state(rows, t, k)
                growth[k].append(dict(tick=t, d_speed_err=dv, speed_err=next(e for tt, e in curve[k] if tt == t),
                                      ref_flags=s['flags'], ref_attack=s['attack'], ref_contact=bool(lg['T'].get(t)),
                                      ref_push=bool(lg['C'].get(t) and lg['C'][t][0][2][2])))
        # matched-state replays from the reference state
        starts = sorted({x for x in [t_first, min(cross.values(), default=None), growth[0][0]['tick'] if growth[0] else None,
                                     growth[1][0]['tick'] if growth[1] else None] if x})
        replays = []
        o = a.out / ('%s-%05d' % (sc, t0))
        bk = o / 'bakes'
        bk.mkdir(parents=True, exist_ok=True)
        lo = min(starts) - 1 if starts else t0
        if starts and not (bk / ('p0-%05d.src' % lo)).exists():
            run([a.ref] + base + ['--scenario', script, '--machines', mref, '--ticks', str(end + 1), '--replay-pads', str(d / 'ref.pads'),
                                  '--bake-range', '%d:%d' % (lo, max(starts) - 1), '--bake-dir', str(bk)])
        for s0 in starts:
            sd = o / ('r%05d' % s0)
            sd.mkdir(exist_ok=True)
            tr, pl = sd / 'n.tsv', sd / 'n.pairlog'
            if not tr.exists():
                for k in (0, 1):
                    run([sys.executable, str(Path(AD.physics_pack.__file__)), str(bk / ('p%d-%05d.src' % (k, s0 - 1))), str(sd / ('p%d.pack' % k))])
                with open(sd / 'draws.txt', 'w') as df:
                    for t in range(s0, end + 1):
                        for (k, ctx, vv) in lg['D'].get(t, []):
                            if k in (0, 1) and not (int(vv[2]) & ((1 << 27) | (1 << 25))) and J.f32sub(vv[1], vv[0]) < 0.0:
                                st = float(rows[(t, k)][19])
                                if st != 0.0:
                                    df.write('%d %d %d\n' % (t, k, 1 if st > 0 else -1))
                r1 = AD.state(rows, s0 - 1, 1)
                run([native, '--pack0', str(sd / 'p0.pack'), '--pack1', str(sd / 'p1.pack'), '--pads', str(d / 'ref.pads'), '--from', str(s0 - 1),
                     '--ticks', str(end - s0 + 1), '--machines', mnat, '--since', str(r1['since']), '--toucher', str(r1['toucher']),
                     '--touching', str(lg['P'][s0]['colliding']), '--trace', str(tr), '--pairlog', str(pl), '--spin-draws', str(sd / 'draws.txt'),
                     '--regain-pending', '1' if any(att == 0 for (_, att, _) in lg['K'].get(s0 - 1, [])) else '0'] + a.native_args)
                for k in (0, 1):
                    (sd / ('p%d.pack' % k)).unlink()
            rn = AD.load(tr)
            ends = []
            for k in (0, 1):
                if (end, k) not in rn:
                    ends.append(dict(stopped=True))
                    continue
                fr, fn = fields(rows, end, k), fields(rn, end, k)
                ends.append(dict(speed_err=fn['speed'] - fr['speed'], band=band(fr['speed']),
                                 in_band=abs(fn['speed'] - fr['speed']) <= band(fr['speed']), pos_err=err(fr['pos'], fn['pos'])))
            replays.append(dict(start=s0, ticks=end - s0 + 1, endpoints=ends))
        report['windows'].append(dict(
            scenario=sc, course=cid, start=t0, length=w['length'], end=end,
            judge=dict(pos_ok=w['pos_ok'], events_ok=w['events_ok'], event_mismatch=w.get('event_mismatch'), speed_ok=w['speed_ok'],
                       endpoints=w['endpoints'], pos_bound=POS_BOUND),
            first_divergence=dict(tick=t_first, fields=f_first),
            first_by_field={'r%d.%s' % kf: v for kf, v in sorted(first.items())},
            band_crossing={'r%d' % k: t for k, t in cross.items()},
            speed_err_curve={'r%d' % k: [[t, e] for t, e in curve[k][::5]] for k in (0, 1)},
            growth={'r%d' % k: v for k, v in growth.items()},
            replays=replays))
    (a.out / 'moving.json').write_text(json.dumps(report, indent=1))
    for x in report['windows']:
        print('%s %d: first divergence %s %s; band crossing %s' % (x['scenario'], x['start'], x['first_divergence']['tick'],
                                                                  ','.join(x['first_divergence']['fields']), x['band_crossing']))
        for e in x['judge']['endpoints']:
            print('   judge endpoint speed err %.5f band %.5f pos err %.3f' % (e['speed_err'], e['speed_tol'], e['pos_err']))
        for r in x['replays']:
            print('   replay from %d (%d ticks): %s' % (r['start'], r['ticks'], '; '.join(
                'stopped' if e.get('stopped') else 'err %+.5f band %.5f %s' % (e['speed_err'], e['band'], 'in' if e['in_band'] else 'OUT')
                for e in r['endpoints'])))


if __name__ == '__main__':
    main()
