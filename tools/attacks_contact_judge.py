#!/usr/bin/env python3
"""Contact-window judge for the attacks lane (C-092), HOST. Runs alongside tools/attacks_duel.py (kept
unchanged, the strict round-2 scorer) on the same windows, chosen from the reference alone.

What it reads (the C-092 logging): the duel reference built with the hooked racer.c (ref/duel_build.sh)
and run with --pairlog, which records the original's own pair decisions (first contact, closing
comparison, push), its damage calls with their source and its KO credit with victim and attacker; and
the native runner's --pairlog (tools/attacks_host/duel_native.c), the body-contact rule's decisions.
The native window starts with the reference's own colliding bit after tick T (the round-2 scorer
guessed it from overlap at T).

Every window must pass:
  - both racers' endpoint position within 32 units;
  - events: type, count and causal order, each within one tick; events of one tick form a group that
    must stay one group. Events: contact (the first-contact branch, carrying both racers' contact
    damage, each within max(0.1, 3%) of the reference), side and spin attack starts, other damage
    (energy drop beyond contact damage, 0.01 or more, within the same band), spin-out start,
    retirement, KO (victim and attacker exact);
  - speed within max(one Q16 quantum, 0.005 x |reference speed|) for both racers, unless the window is
    speed-diagnostic.
Revised score (C-116, O-123): for a racer that is an uninterrupted retired coast (tools/attacks_q16_floor.py)
the speed tolerance is max(one Q16 quantum, 0.005 x |reference speed|, F), F the endpoint discrepancy of the
reference-only ideal Q16 trajectory; every other endpoint keeps the band. The strict and C-092 scores are
computed exactly as before; the revised score is position, events and (speed or speed-diagnostic) with the
enlarged tolerance.

Speed-diagnostic (reference only, frozen in ambiguity.json before any native window runs): some pair
test in the window whose closing product dp.dv, over the reference inputs rounded to Q16 (each input
within half a Q16 step of the reference's value) and the reference's float32 rounding, has an interval
containing zero; and, for that tick, both a same-state branch replay (the reference with that one
comparison flipped, recorded pads) and a reference-only perturbation that flips it (velocities moved
by half a Q16 step toward the other sign) move an endpoint speed beyond the band. Reach-46 ambiguity
and later wall damage are labels only.

Usage:
  attacks_contact_judge.py --ref FZX_DUEL --native N --native2 N2 --course C --course2 C2 --rom ROM --out DIR
      [--reuse OLD_DIR] [--table FILE.json] [--start 460] [--lead 30] [--stage all|ref|ambiguity|score] [--mutate NAME]
      [--seed actual|overlap] [--jobs N]
Everything written is derived from the owner's ROM: local only (DIR outside the repository)."""
import argparse, collections, hashlib, json, math, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import attacks_duel as AD  # noqa: E402  (window selection, trace columns and the scenario table)
import attacks_q16_floor as QF  # noqa: E402  (C-116: the Q16 retired-coast floor and its eligibility)

Q = 1.0 / 65536
U32 = 2.0 ** -24
POS_TOL, SPEED_REL = 32.0, 0.005
EV_MIN = 0.01


def f32sub(a, b):
    """a - b in binary32, as the reference computes energy - damage."""
    import struct
    return struct.unpack('f', struct.pack('f', a - b))[0]


def band(ref):
    return max(0.1, 0.03 * abs(ref))


def speed_tol(ref_speed):
    return max(Q, SPEED_REL * abs(ref_speed))


def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        raise SystemExit('failed: %s\n%s' % (' '.join(cmd), p.stderr[-2000:]))
    return p.stdout


# ---------------------------------------------------------------- logs
def load_ref_log(path):
    """Reference pair log: P (pre-tick inputs), T, C, H, D, K records by tick."""
    lg = {'P': {}, 'T': {}, 'C': {}, 'H': {}, 'D': {}, 'K': {}}
    for line in open(path):
        f = line.split()
        if f[0] == 'P':
            t = int(f[1])
            v = f[2:]
            r = dict(trail=float.fromhex(v[0]), colliding=int(v[1]))
            for k, o in ((0, 2), (1, 10)):
                r['p%d' % k] = [float.fromhex(x) for x in v[o:o + 3]]
                r['v%d' % k] = [float.fromhex(x) for x in v[o + 3:o + 6]]
                r['att%d' % k] = int(v[o + 6]); r['flags%d' % k] = int(v[o + 7])
            lg['P'][t] = r
        else:
            t, a, b = int(f[1]), int(f[2]), int(f[3])
            v = [float.fromhex(x) for x in f[4:14]]
            lg[f[0]].setdefault(t, []).append((a, b, v))
    return lg


def load_nat_log(path):
    lg = {'N': {}, 'K': {}}
    for line in open(path):
        f = line.split()
        if f[0] == 'N':
            t = int(f[1])
            lg['N'][t] = dict(along=int(f[2]), d2=int(f[3]), closing=int(f[4]), touch=int(f[5]), first=int(f[6]), push=int(f[7]),
                              hit=(int(f[8]), int(f[9])), dmg=(int(f[10]) * Q, int(f[11]) * Q))
        elif f[0] == 'K':
            lg['K'].setdefault(int(f[1]), []).append((int(f[2]), int(f[3])))
    return lg


# ---------------------------------------------------------------- events
def residual_drops(rows, t0, t1, cdmg):
    """Energy drop per racer and tick beyond the contact damage of that tick (walls and the rest)."""
    out = {}
    for t in range(t0 + 1, t1 + 1):
        if (t, 0) not in rows or (t - 1, 0) not in rows:
            continue
        for k in (0, 1):
            s, p = AD.state(rows, t, k), AD.state(rows, t - 1, k)
            if p['flags'] & AD.ST_RETIRED:
                continue
            out[(t, k)] = (p['energy'] - s['energy']) - cdmg.get((t, k), 0.0)
    return out


def trace_events(rows, t0, t1):
    ev = []
    for t in range(t0 + 1, t1 + 1):
        if (t, 0) not in rows or (t - 1, 0) not in rows:
            continue
        for k in (0, 1):
            s, p = AD.state(rows, t, k), AD.state(rows, t - 1, k)
            if s['attack'] != p['attack'] and s['attack'] in (1, 2):
                ev.append((t, ('side' if s['attack'] == 1 else 'spin', k), None))
            if s['spin'] and not p['spin']:
                ev.append((t, ('spin_out', k), None))
            if (s['flags'] & AD.ST_RETIRED) and not (p['flags'] & AD.ST_RETIRED):
                ev.append((t, ('retire', k), None))
    return ev


def ref_events(lg, rows, t0, t1):
    ev, cdmg = trace_events(rows, t0, t1), {}
    for t in range(t0 + 1, t1 + 1):
        for (a, b, v) in lg['D'].get(t, []):
            if b == 1:
                cdmg[(t, a)] = cdmg.get((t, a), 0.0) + v[0]
        for (a, b, v) in lg['T'].get(t, []):
            if v[0] == 0:
                ev.append((t, ('contact',), (cdmg.get((t, 0), 0.0), cdmg.get((t, 1), 0.0))))
        for (victim, attacker, v) in lg['K'].get(t, []):
            ev.append((t, ('ko', victim, attacker), None))
    return ev, residual_drops(rows, t0, t1, cdmg)


def nat_events(lg, rows, t0, t1):
    ev, cdmg = trace_events(rows, t0, t1), {}
    for t in range(t0 + 1, t1 + 1):
        n = lg['N'].get(t)
        if n and n['touch']:
            cdmg[(t, 0)], cdmg[(t, 1)] = n['dmg']
            if n['first']:
                ev.append((t, ('contact',), n['dmg']))
        for (victim, attacker) in lg['K'].get(t, []):
            ev.append((t, ('ko', victim, attacker), None))
    return ev, residual_drops(rows, t0, t1, cdmg)


def add_other_damage(ev_r, res_r, ev_n, res_n):
    """Other-damage events (residual energy drop >= 0.01). A drop on one side whose counterpart at
    t-1..t+1 is below the event threshold but within the damage band is the same event on both
    sides (a threshold crossing of the judge's 0.01, not a difference): the counterpart is added."""
    def sig(res):
        return {key: d for key, d in res.items() if d >= EV_MIN}
    sr, sn = sig(res_r), sig(res_n)
    add_r, add_n = dict(sr), dict(sn)
    for src, dst_sig, dst_res, add in ((sr, sn, res_n, add_n), (sn, sr, res_r, add_r)):
        for (t, k), d in src.items():
            if any((t + dt, k) in dst_sig for dt in (-1, 0, 1)):
                continue
            for dt in (0, -1, 1):
                r = dst_res.get((t + dt, k))
                if r is not None and abs(r - d) <= band(d):
                    add[(t + dt, k)] = r
                    break
    ev_r = ev_r + [(t, ('damage', k), (d,)) for (t, k), d in add_r.items()]
    ev_n = ev_n + [(t, ('damage', k), (d,)) for (t, k), d in add_n.items()]
    return ev_r, ev_n


def grouped(ev):
    g = {}
    for t, key, amt in ev:
        g.setdefault(t, []).append((key, amt))
    return [(t, sorted(g[t], key=lambda x: repr(x[0]))) for t in sorted(g)]


def match_groups(gr, gn, end):
    """Groups in order, one-to-one, each within one tick, same multiset of keys, amounts in band.
    Groups only at tick end + 1 (past the window, kept for one-tick slack) may stay unmatched."""
    i = j = 0
    while i < len(gr) and j < len(gn):
        (tr, er), (tn, en) = gr[i], gn[j]
        if abs(tr - tn) > 1 or [k for k, _ in er] != [k for k, _ in en]:
            break
        for (k, ar), (_, an) in zip(er, en):
            if ar is not None:
                for x, y in zip(ar, an):
                    if abs(y - x) > band(x):
                        return False, dict(at=tr, why='amount', key=list(k), ref=list(ar), native=list(an))
        i += 1; j += 1
    rest_r = [g for g in gr[i:] if g[0] <= end]
    rest_n = [g for g in gn[j:] if g[0] <= end]
    if not rest_r and not rest_n:
        return True, None
    fr = gr[i] if i < len(gr) else None
    fn = gn[j] if j < len(gn) else None
    return False, dict(at=min(x[0] for x in (fr, fn) if x), why='group',
                       ref=[fr[0], [list(k) for k, _ in fr[1]]] if fr else None,
                       native=[fn[0], [list(k) for k, _ in fn[1]]] if fn else None)


# ---------------------------------------------------------------- ambiguity (reference only)
def iv_mul(a, b):
    c = (a[0] * b[0], a[0] * b[1], a[1] * b[0], a[1] * b[1])
    return min(c), max(c)


def closing_interval(P):
    """dp.dv over the box: each reference input within half a Q16 step (so the box holds the reference
    values and their native rounding), widened by the reference's float32 rounding of the product."""
    lo = hi = 0.0
    absum = 0.0
    for i in range(3):
        dx = P['p1'][i] - P['p0'][i]
        dv = P['v1'][i] - P['v0'][i]
        a, b = iv_mul((dx - Q, dx + Q), (dv - Q, dv + Q))
        lo += a; hi += b
        absum += abs(dx * dv) + abs(dx) * Q + abs(dv) * Q
    e = 6 * U32 * absum   # fl(dx), fl(dv), three products, two sums
    return lo - e, hi + e


def reach_interval(P):
    lo = hi = 0.0
    for i in range(3):
        dx = P['p1'][i] - P['p0'][i]
        a, b = dx - Q, dx + Q
        sq = (0.0 if a <= 0 <= b else min(a * a, b * b), max(a * a, b * b))
        lo += sq[0]; hi += sq[1]
    return lo * (1 - 4 * U32), hi * (1 + 4 * U32)


def speeds_at(trace, t):
    rows = AD.load(trace)
    return [AD.state(rows, t, k)['speed'] for k in (0, 1)]


# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ref', required=True); ap.add_argument('--native', required=True); ap.add_argument('--native2')
    ap.add_argument('--course', required=True); ap.add_argument('--course2'); ap.add_argument('--rom', required=True)
    ap.add_argument('--out', required=True, type=Path); ap.add_argument('--reuse', type=Path)
    ap.add_argument('--table', type=Path, help='scenario table (JSON, the SCENARIOS layout); default the round-2 table')
    ap.add_argument('--start', type=int, default=AD.START, help='first grid window start')
    ap.add_argument('--lead', type=int, default=AD.LEAD, help='event windows start this many ticks before the event')
    ap.add_argument('--stage', default='all', choices=['all', 'ref', 'ambiguity', 'score'])
    ap.add_argument('--mutate', default=''); ap.add_argument('--seed', default='actual', choices=['actual', 'overlap'])
    ap.add_argument('--jobs', type=int, default=8); ap.add_argument('--scenarios', default='')
    a = ap.parse_args()
    table = {k: tuple(v) for k, v in json.loads(a.table.read_text()).items() if not k.startswith('_')} if a.table else AD.SCENARIOS
    names = [n for n in (a.scenarios.split(',') if a.scenarios else table) if table[n][4] == 1 or a.course2]
    out = a.out; out.mkdir(parents=True, exist_ok=True)

    def scen(sc):
        script, mref, mnat, ticks, cid, afrom = table[sc]
        base = ['--course', a.course if cid == 1 else a.course2, '--rom', a.rom, '--attack-from', str(afrom)]
        return script, mref, mnat, ticks, cid, base

    # ---- stage ref: reference run with its pair log, windows, bakes
    plan = {}
    for sc in names:
        script, mref, mnat, ticks, cid, base = scen(sc)
        d = out / sc; d.mkdir(exist_ok=True)
        if a.reuse and (a.reuse / sc / 'ref.tsv').exists() and not (d / 'ref.tsv').exists():
            for f in ('ref.tsv', 'ref.pads', 'length.txt'):
                os.link(a.reuse / sc / f, d / f)
        if not (d / 'ref.pairlog').exists():
            tmp = d / 'ref-check.tsv'
            run([a.ref] + base + ['--scenario', script, '--machines', mref, '--ticks', str(ticks), '--trace', str(tmp),
                                  '--pads', str(d / 'ref-check.pads'), '--pairlog', str(d / 'ref.pairlog'), '--length-out', str(d / 'length.txt.new')])
            if (d / 'ref.tsv').exists():   # the hooked reference must reproduce the trace it reuses
                if tmp.read_bytes() != (d / 'ref.tsv').read_bytes() or (d / 'ref-check.pads').read_bytes() != (d / 'ref.pads').read_bytes():
                    raise SystemExit('%s: the hooked reference does not reproduce the reused trace' % sc)
                tmp.unlink(); (d / 'ref-check.pads').unlink(); (d / 'length.txt.new').unlink()
            else:
                tmp.rename(d / 'ref.tsv'); (d / 'ref-check.pads').rename(d / 'ref.pads'); (d / 'length.txt.new').rename(d / 'length.txt')
        rows = AD.load(d / 'ref.tsv')
        length = float((d / 'length.txt').read_text())
        ref_ev = AD.events(rows, 0, ticks - 1, length)   # round-2 window selection, unchanged
        starts = {}
        last_event = max((e[1] for e in ref_ev), default=ticks)
        for t in range(a.start, min(ticks - AD.LEN, last_event), AD.GRID):
            starts.setdefault(t, AD.LEN)
        for e in ref_ev:
            if e[1] - a.lead >= a.start:
                t = e[1] - a.lead
                starts[t] = max(starts.get(t, 0), a.lead + AD.LONG.get(e[0], AD.LEN - AD.LEAD))
        plan[sc] = dict(rows=rows, length=length, ref_ev=ref_ev, starts=sorted((t, min(n, ticks - 2 - t)) for t, n in starts.items()))

        def bake(item, d=d, script=script, mref=mref, base=base, sc=sc):
            t, n = item
            w = d / ('w%05d' % t); w.mkdir(exist_ok=True)
            if (w / 'p1.pack').exists():
                return
            old = a.reuse / sc / ('w%05d' % t) if a.reuse else None
            if old and (old / 'p1.pack').exists():
                for f in ('p0.pack', 'p1.pack'):
                    os.link(old / f, w / f)
                return
            run([a.ref] + base + ['--scenario', script, '--machines', mref, '--ticks', str(t + 1), '--bake-at', str(t),
                                  '--bake0', str(w / 'p0.src'), '--bake1', str(w / 'p1.src')])
            for k in (0, 1):
                run([sys.executable, str(Path(AD.physics_pack.__file__)), str(w / ('p%d.src' % k)), str(w / ('p%d.pack' % k))])
        with ThreadPoolExecutor(a.jobs) as ex:
            list(ex.map(bake, plan[sc]['starts']))
    if a.stage == 'ref':
        return

    # ---- stage ambiguity: reference only, frozen before any native window runs
    amb_path = out / 'ambiguity.json'
    if a.stage in ('all', 'ambiguity') and not amb_path.exists():
        if any(out.glob('*/w*/c092-native*.tsv')):
            raise SystemExit('native windows already ran in %s: the ambiguity set must be frozen before them' % out)
        amb = {}
        for sc in names:
            script, mref, mnat, ticks, cid, base = scen(sc)
            d = out / sc
            lg = load_ref_log(d / 'ref.pairlog')
            rows = plan[sc]['rows']
            tests = {}
            for t, recs in lg['C'].items():
                P = lg['P'][t]
                lo, hi = closing_interval(P)
                tests[t] = dict(closing=recs[0][2][0], natural=int(recs[0][2][1]), lo=lo, hi=hi, ambiguous=lo <= 0.0 <= hi)
            reach_amb = sorted(t for t, P in lg['P'].items()
                               if P['trail'] <= 92.0 and (lambda r: r[0] <= 2116.0 <= r[1])(reach_interval(P)))
            sres = {'windows': {}, 'sign_ambiguous_ticks': sorted(t for t, x in tests.items() if x['ambiguous']),
                    'reach_ambiguous_ticks': reach_amb, 'pair_tests': len(tests)}
            cache = {}

            def endpoint(force=None, perturb=(), until=None):
                key = (force, perturb, until)
                if key not in cache:
                    tag = 'f%s-p%s-u%d' % (force and '%d_%d' % force, '_'.join('%d.%d.%d.%d.%g' % p for p in perturb), until)
                    tr = d / 'replay' / ('%s.tsv' % hashlib.sha1(tag.encode()).hexdigest()[:16])
                    pl = tr.with_suffix('.pairlog')
                    tr.parent.mkdir(exist_ok=True)
                    if not tr.exists():
                        cmd = [a.ref] + base + ['--scenario', script, '--machines', mref, '--ticks', str(until + 1),
                                                '--replay-pads', str(d / 'ref.pads'), '--trace', str(tr), '--pairlog', str(pl)]
                        if force:
                            cmd += ['--force', '%d:%d' % force]
                        for p in perturb:
                            cmd += ['--perturb', '%d:%d:1:%d:%s' % (p[0], p[1], p[2], float(p[3]).hex())]
                        run(cmd)
                    cache[key] = (AD.load(tr), load_ref_log(pl))
                return cache[key]

            def win(item):
                t0, n = item
                end = t0 + n
                amb_ticks = [t for t in range(t0 + 1, end + 1) if t in tests and tests[t]['ambiguous']]
                r = dict(start=t0, length=n, sign_ambiguous=amb_ticks,
                         reach_ambiguous=[t for t in reach_amb if t0 < t <= end], proofs=[])
                ref_sp = [AD.state(rows, end, k)['speed'] for k in (0, 1)]
                tol = [speed_tol(s) for s in ref_sp]
                for ta in amb_ticks[:6]:
                    nat = tests[ta]['natural']
                    rows_f, _ = endpoint(force=(ta, 1 - nat), until=end)
                    dsp_f = [abs(AD.state(rows_f, end, k)['speed'] - ref_sp[k]) for k in (0, 1)]
                    replay_moves = any(dsp_f[k] > tol[k] for k in (0, 1))
                    # perturbation: both racers' velocities moved by half a Q16 step, toward the other sign
                    P = lg['P'][ta]
                    sgn = 1.0 if nat else -1.0   # natural 1: closing <= 0, push C up; else down
                    pert = []
                    for i in range(3):
                        dx = P['p1'][i] - P['p0'][i]
                        s = sgn * (1.0 if dx >= 0 else -1.0)
                        pert.append((ta, 1, i, s * Q / 2)); pert.append((ta, 0, i, -s * Q / 2))
                    rows_p, lg_p = endpoint(perturb=tuple(pert), until=end)
                    cp = lg_p['C'].get(ta)
                    flipped = bool(cp) and int(cp[0][2][1]) != nat
                    dsp_p = [abs(AD.state(rows_p, end, k)['speed'] - ref_sp[k]) for k in (0, 1)]
                    pert_moves = flipped and any(dsp_p[k] > tol[k] for k in (0, 1))
                    r['proofs'].append(dict(tick=ta, closing=tests[ta]['closing'], interval=[tests[ta]['lo'], tests[ta]['hi']],
                                            replay_dspeed=dsp_f, replay_moves=replay_moves, perturb_flips=flipped,
                                            perturb_dspeed=dsp_p, perturb_moves=pert_moves))
                    if replay_moves and pert_moves:
                        break
                r['speed_diagnostic'] = any(p['replay_moves'] and p['perturb_moves'] for p in r['proofs'])
                return r
            # control: the replayed reference (recorded pads, nothing forced) reproduces the trace
            rows_c, _ = endpoint(until=ticks - 2)
            same = all(rows_c[(t, k)] == rows[(t, k)] for t in range(0, ticks - 1) for k in (0, 1))
            if not same:
                raise SystemExit('%s: the pad replay does not reproduce the reference' % sc)
            with ThreadPoolExecutor(a.jobs) as ex:
                res = list(ex.map(win, plan[sc]['starts']))
            sres['windows'] = {str(r['start']): r for r in res}
            sres['tests'] = {str(t): x for t, x in tests.items() if x['ambiguous']}
            amb[sc] = sres
        txt = json.dumps(amb, indent=1, sort_keys=True)
        amb_path.write_text(txt)
        (out / 'ambiguity.sha256').write_text(hashlib.sha256(txt.encode()).hexdigest() + '  ambiguity.json\n')
        print('ambiguity frozen: %s' % hashlib.sha256(txt.encode()).hexdigest())
    if a.stage == 'ambiguity':
        return

    # ---- stage score
    amb = json.loads(amb_path.read_text())
    amb_sha = hashlib.sha256(amb_path.read_bytes()).hexdigest()
    summary = dict(mutation=a.mutate, seed=a.seed, ambiguity_sha256=amb_sha, windows=0, strict=0, c092=0, c116=0, pos_ok=0, events_ok=0,
                   speed_checked=0, speed_checked_ok=0, speed_diagnostic=0, scenarios={})
    for sc in names:
        script, mref, mnat, ticks, cid, base = scen(sc)
        d = out / sc
        native = a.native if cid == 1 else (a.native2 or a.native)
        rows, length = plan[sc]['rows'], plan[sc]['length']
        lg = load_ref_log(d / 'ref.pairlog')
        # C-116: the reference-only facts the retired-coast eligibility reads (pair touches, KOs, the colliding
        # bit, ticks where the pair could be in reach, damage calls per racer)
        ref_pair_ticks = sorted(set(t for t in lg['T'] if lg['T'][t]) | set(t for t in lg['K'] if lg['K'][t]) |
                                set(t for t, P in lg['P'].items() if P['colliding']) |
                                set(t for t, P in lg['P'].items()
                                    if P['trail'] <= QF.REACH_ALONG and reach_interval(P)[0] <= QF.REACH2))
        ref_damage_ticks = {k: sorted(t for t, recs in lg['D'].items() if any(a == k for a, _, _ in recs)) for k in (0, 1)}

        def window(item):
            t, n = item
            w = d / ('w%05d' % t)
            tag = 'c092-native%s%s' % ('-' + a.mutate if a.mutate else '', '-overlap' if a.seed == 'overlap' else '')
            nt, nl = w / (tag + '.tsv'), w / (tag + '.pairlog')
            r0, r1 = AD.state(rows, t, 0), AD.state(rows, t, 1)
            if a.seed == 'actual':
                touching = lg['P'][t + 1]['colliding']   # the bit the reference's tick T + 1 reads
            else:
                gap = abs(((r1['lapd'] - r0['lapd']) + length / 2) % length - length / 2)
                touching = int(math.dist(r0['pos'], r1['pos']) <= 46 and gap <= 92)
            cmd = [native, '--pack0', str(w / 'p0.pack'), '--pack1', str(w / 'p1.pack'), '--pads', str(d / 'ref.pads'), '--from', str(t),
                   '--ticks', str(n + 1), '--machines', mnat, '--since', str(r1['since']), '--toucher', str(r1['toucher']),
                   '--touching', str(touching), '--trace', str(nt), '--pairlog', str(nl),
                   # a KO the reference credited to racer 0 on tick T leaves its energy regain pending (not baked)
                   '--regain-pending', '1' if any(att == 0 for (_, att, _) in lg['K'].get(t, [])) else '0']
            # every spin-side draw of the reference in the window, on its own tick (Racer_ReceiveDamage
            # with the energy going below zero on a racer neither crashed nor finished)
            with open(w / 'c092-draws.txt', 'w') as df:
                for tt in range(t + 1, t + n + 2):
                    for (k, ctx, v) in lg['D'].get(tt, []):
                        flags = int(v[2])
                        if k in (0, 1) and not (flags & ((1 << 27) | (1 << 25))) and f32sub(v[1], v[0]) < 0.0 and (tt, k) in rows:
                            steer = float(rows[(tt, k)][19])
                            if steer != 0.0:
                                df.write('%d %d %d\n' % (tt, k, 1 if steer > 0 else -1))
            cmd += ['--spin-draws', str(w / 'c092-draws.txt')]
            if a.mutate:
                cmd += ['--mutate', a.mutate]
            run(cmd)
            nat = AD.load(nt)
            for k in (0, 1):
                nat[(t, k)] = rows[(t, k)]
            end = t + n
            res = dict(start=t, length=n, touching_seed=touching)
            if (end, 0) not in nat:
                res.update(pos_ok=False, events_ok=False, speed_ok=False, strict_ok=False, c092_ok=False, c116_ok=False,
                           reason='native stopped early')
                return res
            nlg = load_nat_log(nl)
            er, rr = ref_events(lg, rows, t, end + 1)
            en, rn = nat_events(nlg, nat, t, min(end + 1, max(tt for tt, _ in nat)))
            er, en = add_other_damage(er, rr, en, rn)
            ev_ok, why = match_groups(grouped(er), grouped(en), end)
            ep = []
            for k in (0, 1):
                rs, ns = AD.state(rows, end, k), AD.state(nat, end, k)
                ep.append(dict(pos_err=math.dist(rs['pos'], ns['pos']), speed=rs['speed'], speed_err=abs(rs['speed'] - ns['speed']),
                               speed_tol=speed_tol(rs['speed'])))
            pos_ok = all(e['pos_err'] <= POS_TOL for e in ep)
            speed_ok = all(e['speed_err'] <= e['speed_tol'] for e in ep)
            # C-116: the floor for each racer that is a retired coast; the other endpoints keep the band
            ref_ctx = dict(rows=rows, pair_ticks=ref_pair_ticks, damage_ticks=ref_damage_ticks, events=er)
            nat_ctx = dict(rows=nat, events=en, touch_ticks=[tt for tt, x in nlg['N'].items() if x['touch'] or x['push']])
            for k in (0, 1):
                why, F = QF.racer_floor(k, t, end, ref_ctx, nat_ctx)
                terms = QF.speed_terms(ep[k]['speed'], AD.state(nat, end, k)['speed'], F)
                ep[k].update(eligible=not why, ineligible_why=why, tol_q=terms['tol_q'], tol_rel=terms['tol_rel'], tol_floor=F,
                             tol_revised=terms['tol'], speed_ok_revised=terms['ok'])
            speed_ok_rev = all(e['speed_ok_revised'] for e in ep)
            aw = amb[sc]['windows'][str(t)]
            diag = aw['speed_diagnostic']
            # labels (diagnostic only): reach ambiguity at 46, other damage after the first contact
            first_contact = min((tt for tt, key, _ in er if key == ('contact',)), default=None)
            wall_after = first_contact is not None and any(key[0] == 'damage' and tt > first_contact for tt, key, _ in er)
            # the first differing decision between the logs (diagnostic)
            div = None
            for tt in range(t + 1, end + 1):
                rt = 1 if lg['T'].get(tt) else 0
                rp = 1 if (lg['C'].get(tt) and lg['C'][tt][0][2][2]) else 0
                nn = nlg['N'].get(tt, dict(touch=0, push=0))
                if rt != nn['touch'] or rp != nn['push']:
                    div = dict(tick=tt, ref_touch=rt, nat_touch=nn['touch'], ref_push=rp, nat_push=nn['push'],
                               ref_closing=lg['C'][tt][0][2][0] if lg['C'].get(tt) else None,
                               nat_closing=nn.get('closing', 0) / 2.0 ** 32 if 'closing' in nn else None,
                               state_err=[math.dist(AD.state(rows, tt - 1, k)['pos'], AD.state(nat, tt - 1, k)['pos']) for k in (0, 1)])
                    break
            res.update(pos_ok=pos_ok, events_ok=ev_ok, event_mismatch=why, speed_ok=speed_ok, speed_diagnostic=diag,
                       strict_ok=pos_ok and ev_ok and speed_ok, c092_ok=pos_ok and ev_ok and (speed_ok or diag),
                       speed_ok_revised=speed_ok_rev, c116_ok=QF.revised_ok(pos_ok, ev_ok, speed_ok_rev, diag), endpoints=ep,
                       labels=dict(sign_ambiguous=aw['sign_ambiguous'], reach_ambiguous=aw['reach_ambiguous'], wall_after_contact=wall_after),
                       first_divergence=div)
            return res
        with ThreadPoolExecutor(a.jobs) as ex:
            results = list(ex.map(window, plan[sc]['starts']))
        summary['scenarios'][sc] = dict(course=cid, windows=results)
        for r in results:
            summary['windows'] += 1
            summary['strict'] += r['strict_ok']; summary['c092'] += r['c092_ok']; summary['c116'] += r['c116_ok']
            summary['pos_ok'] += r['pos_ok']; summary['events_ok'] += r['events_ok']
            if r.get('speed_diagnostic'):
                summary['speed_diagnostic'] += 1
            else:
                summary['speed_checked'] += 1; summary['speed_checked_ok'] += r['speed_ok']
    # distinct trajectories: overlapping windows of one scenario form one cluster
    clusters = []
    for sc, v in summary['scenarios'].items():
        cur = None
        for r in sorted(v['windows'], key=lambda r: r['start']):
            if cur and r['start'] <= cur['end']:
                cur['end'] = max(cur['end'], r['start'] + r['length']); cur['windows'] += 1; cur['c092_ok'] &= r['c092_ok']
                cur['strict_ok'] &= r['strict_ok']; cur['c116_ok'] &= r['c116_ok']
                cur['eligible'] |= any(e.get('eligible') for e in r.get('endpoints', []))
            else:
                cur = dict(scenario=sc, start=r['start'], end=r['start'] + r['length'], windows=1, c092_ok=r['c092_ok'], strict_ok=r['strict_ok'],
                           c116_ok=r['c116_ok'], eligible=any(e.get('eligible') for e in r.get('endpoints', [])))
                clusters.append(cur)
    summary['trajectories'] = dict(count=len(clusters), c092_all_pass=sum(c['c092_ok'] for c in clusters),
                                   strict_all_pass=sum(c['strict_ok'] for c in clusters),
                                   c116_all_pass=sum(c['c116_ok'] for c in clusters), eligible=sum(c['eligible'] for c in clusters))
    # C-116 eligibility counts: racer-windows, windows with an eligible racer, and what the floor decided
    allw = [r for v in summary['scenarios'].values() for r in v['windows']]
    elig = [(r, e) for r in allw for e in r.get('endpoints', []) if e.get('eligible')]
    summary['c116'] = dict(
        score=summary.pop('c116'), eligible_racer_windows=len(elig), eligible_windows=len({id(r) for r, _ in elig}),
        racer_windows=2 * len(allw), floor_rescued_windows=sum(1 for r in allw if r['c116_ok'] and not r['c092_ok']),
        eligible_speed_pass_by_band=sum(1 for _, e in elig if e['speed_err'] <= e['speed_tol']),
        eligible_speed_pass_by_floor=sum(1 for _, e in elig if e['speed_ok_revised']),
        ineligible_reasons=dict(sorted(collections.Counter(
            w for r in allw for e in r.get('endpoints', []) if not e.get('eligible') for w in e.get('ineligible_why', [])).items())))
    tag = (a.mutate or 'base') + ('-overlap' if a.seed == 'overlap' else '')
    (out / ('c092-%s.json' % tag)).write_text(json.dumps(summary, indent=1))
    c = summary['c116']
    print('%s: windows %d  strict %d  C-092 %d  revised (C-116) %d  (position %d, events %d, speed checked %d/%d, speed-diagnostic %d)  '
          'trajectories %d (strict %d, C-092 %d, revised %d all-pass)' % (
              tag, summary['windows'], summary['strict'], summary['c092'], c['score'], summary['pos_ok'], summary['events_ok'],
              summary['speed_checked_ok'], summary['speed_checked'], summary['speed_diagnostic'], len(clusters),
              summary['trajectories']['strict_all_pass'], summary['trajectories']['c092_all_pass'], summary['trajectories']['c116_all_pass']))
    print('  eligibility: racer-windows %d of %d, windows with an eligible racer %d, trajectories with one %d; speed passes of eligible '
          'endpoints: by the band %d, with the floor %d; windows the floor rescued %d; ineligible reasons %s' % (
              c['eligible_racer_windows'], c['racer_windows'], c['eligible_windows'], summary['trajectories']['eligible'],
              c['eligible_speed_pass_by_band'], c['eligible_speed_pass_by_floor'], c['floor_rescued_windows'], c['ineligible_reasons']))
    for cid in (1, 2):
        ws = [r for v in summary['scenarios'].values() if v['course'] == cid for r in v['windows']]
        if ws:
            print('  course %d: windows %d strict %d C-092 %d revised %d' % (cid, len(ws), sum(r['strict_ok'] for r in ws),
                                                                      sum(r['c092_ok'] for r in ws), sum(r['c116_ok'] for r in ws)))


if __name__ == '__main__':
    main()
