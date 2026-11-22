#!/usr/bin/env python3
"""First-divergence trace of attacks duel windows (C-145 item 3), HOST, diagnostic only.

Reads what a judge run already left in its directory (ref.tsv, ref.pairlog, w*/c092-native.tsv and
.pairlog) and, for each requested window, lists the first point where the native trajectory differs
from the reference in a discrete state writer or a pair predicate, with the continuous state error that
preceded it:
  event diff   the judge's own event matcher on the window (tools/attacks_contact_judge.py functions,
               called unchanged), the first group that fails to match
  writers      state flag bits (wall/recoil, spin-out, retired, crashed, damaged), attack state, spin-out
               timer, KO count, energy drop (damage) per racer
  predicates   the pair test: reach distance <= 46 (reference: from the pre-tick inputs it logs;
               native: the rule's own d2), the closing product dp.dv and its sign (push)
  margins      the reference's own distance of each flipped predicate from its boundary, beside the
               position and velocity error of the native trajectory at the previous tick, and the
               matched-state one-tick scan result (tools/attacks_onestep.py report) at the first tick:
               an empty entry means the native rule run one tick from the reference's own state decides
               as the reference did (drift origin); a listed entry is a model difference at that tick
Nothing here scores or excuses a window. Everything read and written is derived from the owner's ROM:
local only (the output directory is outside the repository).

Usage: attacks_trace.py --judge JUDGE_DIR --scenario SC --starts 960,973[:LENGTH] [--onestep onestep.json]
       [--native-tag c092-native] [--around 4] [--json OUT.json]"""
import argparse, json, math, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import attacks_duel as AD  # noqa: E402
import attacks_contact_judge as J  # noqa: E402
import attacks_onestep as OS  # noqa: E402  (flag masks, thresholds)

# racer state bits (decomp fzx_racer.h): 1<<13 COLLISION_RECOIL is the visual tilt the pair push and a wall hit set (it only feeds the model
# basis; the native body has no such bit and it is NOT counted as a writer here); 1<<17 RECEIVED_DAMAGE. attacks_onestep.py names these two the
# other way round ('damaged', 'recoil'); its masks are used unchanged by that tool, this one reads the decomp's names.
FLAG_NAMES = ((1 << 14, 'spin-out'), (1 << 17, 'received-damage'), (1 << 18, 'retired'), (1 << 27, 'crashed'))
FLAG_MASK = sum(b for b, _ in FLAG_NAMES)


def vec(rows, t, k, o):
    return [float(x) for x in rows[(t, k)][o:o + 3]]


def ref_pair(lg, t):
    """Reference pair-test inputs for tick t (pre-tick state) and its decisions."""
    P = lg['P'][t]
    touch = lg['T'].get(t)
    cl = lg['C'].get(t)
    return dict(dist=math.dist(P['p0'], P['p1']), trail=P['trail'], colliding=P['colliding'], touch=1 if touch else 0,
                closing=cl[0][2][0] if cl else None, push=1 if (cl and cl[0][2][2]) else 0)


def nat_pair(nlg, t):
    n = nlg['N'].get(t)
    if not n:
        return None
    return dict(dist=math.sqrt(n['d2']) / 65536.0, along_ok=n['along'], closing=n['closing'] / 2.0 ** 32, touch=n['touch'], push=n['push'])


def writers(rows_r, rows_n, t, k, base_kos=None):
    """Discrete writer differences for racer k at tick t."""
    out = []
    r, n = AD.state(rows_r, t, k), AD.state(rows_n, t, k)
    x = (r['flags'] ^ n['flags']) & FLAG_MASK
    for bit, name in FLAG_NAMES:
        if x & bit:
            out.append(dict(field=name, ref=int(bool(r['flags'] & bit)), native=int(bool(n['flags'] & bit))))
    for key in ('attack', 'spin'):
        if r[key] != n[key]:
            out.append(dict(field=key, ref=r[key], native=n[key]))
    # the KO counter is the reference's global (gRacersKOd); the native duel body starts it at 0, so it is compared as a change from the window start
    if base_kos is not None and (r['kos'] - base_kos) != n['kos']:
        out.append(dict(field='ko-count-change', ref=r['kos'] - base_kos, native=n['kos']))
    pr, pn = AD.state(rows_r, t - 1, k), AD.state(rows_n, t - 1, k)
    dr, dn = pr['energy'] - r['energy'], pn['energy'] - n['energy']
    if abs(dr - dn) > max(OS.E_EPS, 0.03 * abs(dr)):
        out.append(dict(field='energy-drop', ref=round(dr, 4), native=round(dn, 4)))
    return out


def trace_window(d, sc, start, length, rows, lg, tag, around, scan):
    w = d / ('w%05d' % start)
    nat = AD.load(w / (tag + '.tsv'))
    for k in (0, 1):
        nat[(start, k)] = rows[(start, k)]
    nlg = J.load_nat_log(w / (tag + '.pairlog'))
    end = start + length
    er, rr = J.ref_events(lg, rows, start, end + 1)
    en, rn = J.nat_events(nlg, nat, start, min(end + 1, max(t for t, _ in nat)))
    er, en = J.add_other_damage(er, rr, en, rn)
    ev_ok, why = J.match_groups(J.grouped(er), J.grouped(en), end)
    res = dict(scenario=sc, start=start, length=length, events_ok=ev_ok, event_diff=why)
    first, rows_out, grow = None, [], []
    for t in range(start + 1, end + 1):
        if (t, 0) not in nat:
            break
        rp, npd = ref_pair(lg, t), nat_pair(nlg, t)
        wr = {k: writers(rows, nat, t, k, AD.state(rows, start, 0)['kos']) for k in (0, 1)}
        pred = []
        if npd:
            if rp['touch'] != npd['touch']:
                pred.append('reach')
            elif rp['push'] != npd['push']:
                pred.append('closing-sign')
        if first is None and (pred or wr[0] or wr[1]):
            first = t
        err = {k: dict(pos=math.dist(vec(rows, t - 1, k, 2), vec(nat, t - 1, k, 2)), vel=math.dist(vec(rows, t - 1, k, 5), vec(nat, t - 1, k, 5)))
               for k in (0, 1)}
        evs = ''.join(kk for kk in 'TCHDK' if lg[kk].get(t)) + ('W' if any((AD.state(rows, t, k)['flags'] ^ AD.state(rows, t - 1, k)['flags']) & (1 << 17) for k in (0, 1)) else '')
        grow.append((t, evs, err[0]['pos'], err[0]['vel'], err[1]['pos'], err[1]['vel']))
        rows_out.append(dict(t=t, ref=rp, nat=npd, writers={str(k): v for k, v in wr.items() if v}, pred=pred, err_prev=err))
    res['first_tick'] = first
    # continuous state error (position, velocity; units, native vs reference at the end of tick t - 1) and the reference's logged pair events on tick t
    # (T touch, C closing test, H push, D damage call, K KO credit, W a received-damage bit set or cleared)
    res['growth'] = [dict(t=g[0], ref_events=g[1], err_r0=[g[2], g[3]], err_r1=[g[4], g[5]]) for g in grow]
    if first is not None:
        row = next(r for r in rows_out if r['t'] == first)
        rp, npd = row['ref'], row['nat']
        res['first'] = dict(tick=first, predicates=row['pred'], writers=row['writers'], ref_pair=rp, native_pair=npd, err_prev=row['err_prev'],
                            reach_margin_ref=rp['dist'] - 46.0, reach_margin_native=(npd['dist'] - 46.0) if npd else None,
                            onestep=scan.get(str(first)) if scan is not None else None)
        res['window_rows'] = [r for r in rows_out if first - around <= r['t'] <= first + 1]
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--judge', required=True, type=Path); ap.add_argument('--scenario', required=True)
    ap.add_argument('--starts', required=True)
    ap.add_argument('--native-tag', default='c092-native'); ap.add_argument('--around', type=int, default=4)
    ap.add_argument('--onestep', type=Path, help='attacks_onestep.py report (its scan section) for the same scenario')
    ap.add_argument('--json', type=Path)
    ap.add_argument('--growth', action='store_true', help='print the per-tick state error table of each window')
    a = ap.parse_args()
    d = a.judge / a.scenario
    rows = AD.load(d / 'ref.tsv')
    lg = J.load_ref_log(d / 'ref.pairlog')
    scan = json.loads(a.onestep.read_text())['scan'].get(a.scenario, {}) if a.onestep else None
    length = int(a.starts.split(':')[1]) if ':' in a.starts else 90
    out = [trace_window(d, a.scenario, int(s), length, rows, lg, a.native_tag, a.around, scan) for s in a.starts.split(':')[0].split(',')]
    for r in out:
        print('%s %d: events_ok=%s event_diff=%s' % (r['scenario'], r['start'], r['events_ok'], json.dumps(r['event_diff'])))
        f = r.get('first')
        if not f:
            print('   no discrete writer or pair predicate differs in the window')
            continue
        print('   first divergence tick %d: predicates %s writers %s' % (f['tick'], f['predicates'], json.dumps(f['writers'])))
        print('   ref pair %s' % json.dumps(f['ref_pair']))
        print('   nat pair %s' % json.dumps(f['native_pair']))
        if a.growth:
            for g in r['growth']:
                print('   g t=%d ev=%-6s r0 pos %.2e vel %.2e | r1 pos %.2e vel %.2e' % (g['t'], g['ref_events'], g['err_r0'][0], g['err_r0'][1], g['err_r1'][0], g['err_r1'][1]))
        print('   err at t-1 %s ; one-tick matched-state entry at t: %s' % (json.dumps(f['err_prev']), json.dumps(f['onestep'])))
    if a.json:
        a.json.write_text(json.dumps(out, indent=1))


if __name__ == '__main__':
    main()
