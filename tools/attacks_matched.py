#!/usr/bin/env python3
"""Matched-start diagnostic and additional score for the attacks duel windows (C-142 item 1), HOST.

The native window starts from a pack (p0.pack, p1.pack): the reference's state after tick T converted to the
native fixed-point formats. This tool replays the SAME reference code from that converted state (the reference's
--load-state: just before tick T + 1 the baked state fields take the values of the converted state, as float32;
every other field, the pads and the random state are the original run's), then

  paired     scores the converted-start reference against the original reference with the ordinary judge functions
             (tools/attacks_contact_judge.py, called unchanged) and keeps signed end errors, both event sequences,
             and the first divergent predicate or discrete writer (tools/attacks_trace.py functions): a deterministic
             paired failure is a window the reference itself fails from the exactly representable start.
             --perturb-q N adds seeded +-N q start-velocity variants (optional; reported apart, never an allowance).
  identity   loads the UNCONVERTED state and must reproduce the original trace and pair log exactly (the load is the
             exact inverse of the bake).
  score      scores a native run (c092-native<tag> files of the window) against the converted-start reference with
             the ordinary C-092 requirements (events, position, speed, exact KO attacker and victim, damage); this
             is the prospective additional score. It changes neither C-092 nor C-116.

Serialized entry state and conversion (published by `table`): every key of the bake source that the pack holds,
the format of each, signed rounding (nearest, ties away from zero: tools/physics_pack.py fx), then the nearest
float32 (what the reference stores). Simulation phase: the state after tick T, loaded before tick T + 1. RNG: the
reference has none in these scenarios (pads are replayed from ref.pads); the native draws are the judge's own.

Everything read and written is derived from the owner's ROM: local only.
"""
import argparse, hashlib, json, math, os, random, struct, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import attacks_duel as AD  # noqa: E402
import attacks_contact_judge as J  # noqa: E402
import attacks_trace as TR  # noqa: E402
import physics_pack as PP  # noqa: E402

Q = J.Q
# (key, slice of the key's floats, fraction bits; 'F' is the engine force scale of the pack)
FORMATS = (('pos', None, 16), ('vel', None, 16), ('body', None, 30), ('road', None, 30), ('gravity_up', None, 30),
           ('tilt_up', None, 30), ('ground_up', None, 30), ('fwd', None, 16), ('spos', None, 16), ('disp', None, 16),
           ('ground', None, 16), ('bvel', None, 16),
           ('scalars', (0, 2), 30), ('scalars', (2, 6), 16), ('scalars', (6, 9), 12), ('scalars', (9, 12), 16),
           ('scalars', (12, 13), 'F'), ('scalars', (13, 14), 16),
           ('dynamics', (15, 20), 16), ('dynamics', (20, 21), 30))


def f32(x):
    return struct.unpack('f', struct.pack('f', x))[0]


def convert_state(text, force_frac=16, stats=None):
    """The state source rounded as the native pack holds it. Lines that are not state keys are dropped (the reference loads
    state keys only). stats: dict (key, slice) -> [values, changed, max abs change]."""
    out = []
    for line in text.splitlines():
        f = line.split()
        if not f or f[0].startswith('#') or f[0] not in {k for k, _, _ in FORMATS}:
            continue
        key, a, b, vals = f[0], f[1], f[2], [float.fromhex(x) for x in f[3:]]
        for fk, sl, bits in FORMATS:
            if fk != key:
                continue
            lo, hi = sl if sl else (0, len(vals))
            nb = force_frac if bits == 'F' else bits
            for i in range(lo, hi):
                v = vals[i]
                w = f32(PP.fx(v, nb) / float(1 << nb))
                if stats is not None:
                    s = stats.setdefault((key, '%d:%d' % (lo, hi), nb), [0, 0, 0.0])
                    s[0] += 1
                    s[1] += w != v
                    s[2] = max(s[2], abs(w - v))
                vals[i] = w
        out.append('%s %s %s %s' % (key, a, b, ' '.join(float.hex(x) for x in vals)))
    return '\n'.join(out) + '\n'


def identity_state(text):
    return '\n'.join(l for l in text.splitlines() if l.split() and l.split()[0] in {k for k, _, _ in FORMATS}) + '\n'


class Ctx:
    def __init__(self, a):
        self.a = a
        self.out = Path(a.judge)
        self.table = {k: tuple(v) for k, v in json.load(open(a.table)).items() if not k.startswith('_')} if a.table else AD.SCENARIOS
        self.res = json.load(open(self.out / a.result))
        self.ff = a.force_frac

    def wins(self):
        for sc, v in self.res['scenarios'].items():
            for w in v['windows']:
                yield sc, w['start'], w['length']

    def ref_args(self, sc, until):
        script, mref, mnat, ticks, cid, afrom = self.table[sc]
        a = self.a
        return [a.ref, '--course', a.course if cid == 1 else a.course2, '--rom', a.rom, '--attack-from', str(afrom), '--scenario', script,
                '--machines', mref, '--ticks', str(min(until, ticks - 2) + 1), '--replay-pads', str(self.out / sc / 'ref.pads')]

    def mref(self, sc, t0, n, mode):
        """The reference replayed from window start t0 with the state converted (mode 'q'), unconverted (mode 'id'), or perturbed."""
        w = self.out / sc / ('w%05d' % t0)
        tag = 'mref-%s' % mode
        tr, pl = w / (tag + '.tsv'), w / (tag + '.pairlog')
        if tr.exists() and pl.exists():
            return tr, pl
        srcs = []
        for k in (0, 1):
            raw = (w / ('p%d.src' % k)).read_text()
            p = w / ('%s-p%d.src' % (tag, k))
            p.write_text(convert_state(raw, self.ff) if mode == 'q' else identity_state(raw))
            srcs.append(p)
        cmd = self.ref_args(sc, t0 + n + 2) + ['--trace', str(tr), '--pairlog', str(pl), '--load-state', '%d:%s:%s' % (t0 + 1, srcs[0], srcs[1])]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            raise SystemExit('failed %s\n%s' % (' '.join(cmd), r.stderr[-600:]))
        return tr, pl


def ref_pair_state(rows, lg, t):
    return TR.ref_pair(lg, t)


def paired(ctx, sc, t0, n):
    d = ctx.out / sc
    rows, lg = AD.load(d / 'ref.tsv'), J.load_ref_log(d / 'ref.pairlog')
    tr, pl = ctx.mref(sc, t0, n, 'q')
    qrows, qlg = AD.load(tr), J.load_ref_log(pl)
    end = t0 + n
    er, rr = J.ref_events(lg, rows, t0, end + 1)
    eq, rq = J.ref_events(qlg, qrows, t0, end + 1)
    er, eq = J.add_other_damage(er, rr, eq, rq)
    ev_ok, why = J.match_groups(J.grouped(er), J.grouped(eq), end)
    ep = []
    for k in (0, 1):
        rs, qs = AD.state(rows, end, k), AD.state(qrows, end, k)
        ep.append(dict(pos_err=math.dist(rs['pos'], qs['pos']), speed_signed=qs['speed'] - rs['speed'], speed_tol=J.speed_tol(rs['speed'])))
    pos_ok = all(e['pos_err'] <= J.POS_TOL for e in ep)
    speed_ok = all(abs(e['speed_signed']) <= e['speed_tol'] for e in ep)
    first = None
    for t in range(t0 + 1, end + 1):
        if (t, 0) not in qrows:
            break
        ra, rb = TR.ref_pair(lg, t), TR.ref_pair(qlg, t)
        pred = [nm for nm, x, y in (('touch', ra['touch'], rb['touch']), ('closing-sign', ra['push'], rb['push'])) if x != y]
        wr = {k: TR.writers(rows, qrows, t, k, AD.state(rows, t0, 0)['kos']) for k in (0, 1)}
        wr = {str(k): v for k, v in wr.items() if v}
        if pred or wr:
            first = dict(tick=t, predicates=pred, writers=wr, orig=ra, matched=rb)
            break
    return dict(sc=sc, start=t0, length=n, events_ok=ev_ok, event_diff=why, pos_ok=pos_ok, speed_ok=speed_ok, ok=ev_ok and pos_ok and speed_ok,
                end=ep, events_orig=[[t, list(map(str, key))] for t, key, _ in er][:40], events_matched=[[t, list(map(str, key))] for t, key, _ in eq][:40],
                first_divergence=first)


def identity(ctx, sc, t0, n):
    d = ctx.out / sc
    tr, pl = ctx.mref(sc, t0, n, 'id')
    end = t0 + n
    a = AD.load(d / 'ref.tsv'); b = AD.load(tr)
    same = all(a[(t, k)] == b[(t, k)] for t in range(t0 + 1, end + 1) for k in (0, 1) if (t, k) in b) and (end, 0) in b
    la, lb = J.load_ref_log(d / 'ref.pairlog'), J.load_ref_log(pl)
    same_log = all(la[x].get(t) == lb[x].get(t) for x in ('T', 'C', 'H', 'D', 'K') for t in range(t0 + 1, end + 1))
    return dict(sc=sc, start=t0, same_trace=same, same_log=same_log)


def score(ctx, sc, t0, n, tag, mode='q'):
    """The native window of the judge's own run (files <tag>.tsv/.pairlog) against the converted-start reference."""
    d = ctx.out / sc
    w = d / ('w%05d' % t0)
    tr, pl = ctx.mref(sc, t0, n, mode)
    rows, lg = AD.load(tr), J.load_ref_log(pl)
    orows = AD.load(d / 'ref.tsv')
    nat = AD.load(w / (tag + '.tsv'))
    for k in (0, 1):
        nat[(t0, k)] = orows[(t0, k)]
    end = t0 + n
    if (end, 0) not in nat or (end, 0) not in rows:
        return dict(sc=sc, start=t0, length=n, ok=False, events_ok=False, pos_ok=False, speed_ok=False, reason='run stopped early')
    nlg = J.load_nat_log(w / (tag + '.pairlog'))
    er, rr = J.ref_events(lg, rows, t0, end + 1)
    en, rn = J.nat_events(nlg, nat, t0, min(end + 1, max(t for t, _ in nat)))
    er, en = J.add_other_damage(er, rr, en, rn)
    ev_ok, why = J.match_groups(J.grouped(er), J.grouped(en), end)
    ep = [dict(pos_err=math.dist(AD.state(rows, end, k)['pos'], AD.state(nat, end, k)['pos']),
               speed_err=abs(AD.state(rows, end, k)['speed'] - AD.state(nat, end, k)['speed']), speed_tol=J.speed_tol(AD.state(rows, end, k)['speed']))
          for k in (0, 1)]
    pos_ok = all(e['pos_err'] <= J.POS_TOL for e in ep)
    speed_ok = all(e['speed_err'] <= e['speed_tol'] for e in ep)
    return dict(sc=sc, start=t0, length=n, ok=ev_ok and pos_ok and speed_ok, events_ok=ev_ok, pos_ok=pos_ok, speed_ok=speed_ok, event_diff=why, end=ep)


def sensitivity(ctx, sc, t0, n, nvar, mag):
    """Optional: the converted-start reference with seeded +-mag q start-velocity moves, scored against the converted-start reference."""
    d = ctx.out / sc
    base_tr, base_pl = ctx.mref(sc, t0, n, 'q')
    rows, lg = AD.load(base_tr), J.load_ref_log(base_pl)
    rnd = random.Random(1000003 * t0 + sum(map(ord, sc)))
    res = []
    for v in range(nvar):
        perts = [(k, i, (1 if rnd.random() < 0.5 else -1) * mag * Q) for k in (0, 1) for i in range(3)]
        w = d / ('w%05d' % t0)
        tr, pl = w / ('mref-s%d.tsv' % v), w / ('mref-s%d.pairlog' % v)
        if not pl.exists():
            srcs = [w / ('mref-q-p%d.src' % k) for k in (0, 1)]
            cmd = ctx.ref_args(sc, t0 + n + 2) + ['--trace', str(tr), '--pairlog', str(pl), '--load-state', '%d:%s:%s' % (t0 + 1, srcs[0], srcs[1])]
            for k, i, dv in perts:
                cmd += ['--perturb', '%d:%d:1:%d:%s' % (t0 + 1, k, i, float(dv).hex())]
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode:
                raise SystemExit(r.stderr[-400:])
        vr, vl = AD.load(tr), J.load_ref_log(pl)
        end = t0 + n
        e1, r1 = J.ref_events(lg, rows, t0, end + 1); e2, r2 = J.ref_events(vl, vr, t0, end + 1)
        e1, e2 = J.add_other_damage(e1, r1, e2, r2)
        ok_ev, _ = J.match_groups(J.grouped(e1), J.grouped(e2), end)
        okp = all(math.dist(AD.state(rows, end, k)['pos'], AD.state(vr, end, k)['pos']) <= J.POS_TOL for k in (0, 1))
        oks = all(abs(AD.state(rows, end, k)['speed'] - AD.state(vr, end, k)['speed']) <= J.speed_tol(AD.state(rows, end, k)['speed']) for k in (0, 1))
        res.append(ok_ev and okp and oks)
    return dict(sc=sc, start=t0, length=n, variants=nvar, passed=sum(res))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('cmd', choices=['table', 'paired', 'identity', 'score', 'sensitivity'])
    ap.add_argument('--judge', required=True, help='a judge output directory (reference files, p*.src, native results)')
    ap.add_argument('--result', default='c092-base.json', help='the judge result file that lists the windows')
    ap.add_argument('--table'); ap.add_argument('--ref'); ap.add_argument('--course'); ap.add_argument('--course2'); ap.add_argument('--rom')
    ap.add_argument('--force-frac', type=int, default=16, help="the pack's engine force scale (the state's accel_force is converted at it)")
    ap.add_argument('--tag', default='c092-native'); ap.add_argument('--jobs', type=int, default=8)
    ap.add_argument('--variants', type=int, default=4); ap.add_argument('--mag', type=float, default=1.0)
    ap.add_argument('--only-failing', help='a judge result file: run only the windows that file fails')
    ap.add_argument('--json', type=Path)
    a = ap.parse_args()
    ctx = Ctx(a)
    if a.cmd == 'table':
        stats = {}
        for sc, t0, n in ctx.wins():
            for k in (0, 1):
                convert_state((ctx.out / sc / ('w%05d' % t0) / ('p%d.src' % k)).read_text(), a.force_frac, stats)
        for (key, sl, nb), (cnt, ch, mx) in sorted(stats.items()):
            print('%-11s %-6s Q%-2d values %9d changed %9d max change %.3e' % (key, sl, nb, cnt, ch, mx))
        return
    items = list(ctx.wins())
    if a.only_failing:
        bad = json.load(open(a.only_failing))
        keep = {(sc, w['start']) for sc, v in bad['scenarios'].items() for w in v['windows'] if not w.get('c092_ok', w.get('ok', True))}
        items = [x for x in items if (x[0], x[1]) in keep]
    fn = {'paired': lambda i: paired(ctx, *i), 'identity': lambda i: identity(ctx, *i),
          'score': lambda i: score(ctx, *i, a.tag), 'sensitivity': lambda i: sensitivity(ctx, *i, a.variants, a.mag)}[a.cmd]
    with ThreadPoolExecutor(a.jobs) as ex:
        res = list(ex.map(fn, items))
    if a.cmd == 'identity':
        print('identity: windows %d, trace equal %d, pair log equal %d' % (len(res), sum(r['same_trace'] for r in res), sum(r['same_log'] for r in res)))
    elif a.cmd == 'sensitivity':
        print('sensitivity: windows %d variants %d passed %d' % (len(res), sum(r['variants'] for r in res), sum(r['passed'] for r in res)))
    else:
        ok = sum(r['ok'] for r in res)
        print('%s: windows %d ok %d (events %d, position %d, speed %d)' % (a.cmd, len(res), ok, sum(r['events_ok'] for r in res),
              sum(r['pos_ok'] for r in res), sum(r['speed_ok'] for r in res)))
        traj = {}
        for r in res:
            traj[r['sc']] = traj.get(r['sc'], True) and r['ok']
        print('trajectories %d all-pass %d' % (len(traj), sum(traj.values())))
    if a.json:
        a.json.write_text(json.dumps(res, indent=1))


if __name__ == '__main__':
    main()
