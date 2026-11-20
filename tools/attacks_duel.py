#!/usr/bin/env python3
"""Attacks plays-right judge (lane claude/attacks), HOST: the C-076 window judge on the duel reference.

The reference is the original race code with two pad-driven racers (ref/duel_main.c, fzx-duel). For each
scenario the reference runs once with its pads recorded; windows start at reference events (chosen from
the reference alone) and on a 300-tick grid from the start. Each window bakes both racers' state after
tick T (fzx-duel --bake-at T), starts the native pair there (tools/attacks_host/duel_native.c: SH2ZAM
vehicle-dynamics-3 bodies in contact through body-contact-1) and replays the recorded pads from T + 1.

Scored per window, as C-076: both racers' endpoint 3D position within 32 units and speed within
0.5% of the reference (plus one Q16 quantum); events one-to-one by class, in order, at most one tick
apart, none dropped or added. Where the original draws a random number (the side a spin-out turns to)
the native window takes the reference's draw. Event classes: contact (a pair coming within reach, 46, along gap 92),
side and spin attack starts, damage (an energy drop of 0.01 or more), spin-out start, retirement, KO.
Damage amounts are scored within max(3% of the reference amount, 0.1 energy), E1's W01 wall-damage band. --mutate runs every window with one planted
rule change; the class it targets must fail some window.

Usage: attacks_duel.py --ref FZX_DUEL --native FZX_DUEL_NATIVE --course COURSE --rom ROM --out DIR
                        [--mutate NAME] [--scenarios a,b] [--jobs N]
Everything written is derived from the owner's ROM: local only (DIR outside the repository)."""
import argparse, json, math, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import physics_pack  # noqa: E402

SCENARIOS = {  # name: (machines for the reference c0,e0,c1,e1; native c0,c1; ticks)
    'bump': ('0,0,9,0', '0,9', 4000),
    'side': ('0,0,0,0', '0,0', 4000),
    'spin': ('0,0,0,0', '0,0', 4000),
    'cpu': ('0,0,0,0', '0,0', 4000),
    'ko': ('0,0,0,0', '0,0', 4000),
}
START, GRID, LEAD, LEN, POS_TOL, SPEED_TOL, QUANTUM = 460, 300, 30, 90, 32.0, 0.005, 1.0 / 65536
LONG = {'spin_out': 200, 'retire': 60}
ST_RETIRED = 1 << 18
COURSE_LENGTH = 79028.0  # the first course's lap length, for wrapping the along gap only
CLASSES = ['contact', 'side', 'spin', 'damage', 'spin_out', 'retire', 'ko']
MUTATIONS = {'impact1.2': 'damage', 'share-swap': 'damage', 'heavy9': 'damage', 'push2.5': 'side', 'reach45': 'contact',
             'spin140': 'retire', 'credit-off': 'ko', 'heavy5': 'spin_out'}   # mutation: the class whose windows must fail


def load(path):
    rows = {}
    for line in open(path):
        r = line.rstrip('\n').split('\t')
        rows[(int(r[0]), int(r[1]))] = r
    return rows


def state(rows, t, k):
    r = rows[(t, k)]
    return dict(pos=[float(x) for x in r[2:5]], speed=float(r[8]), energy=float(r[9]), lapd=float(r[12]),
                flags=int(r[13]), attack=int(r[14]), spin=int(r[15]), kos=int(r[16]), since=int(r[17]), toucher=int(r[18]))


def events(rows, t0, t1, length):
    """Events in ticks (t0, t1] from a trace (reference or native)."""
    ev = []
    for t in range(t0 + 1, t1 + 1):
        if (t, 0) not in rows or (t - 1, 0) not in rows:
            continue
        a, b = state(rows, t, 0), state(rows, t, 1)
        pa, pb = state(rows, t - 1, 0), state(rows, t - 1, 1)
        gap = abs(((b['lapd'] - a['lapd']) + length / 2) % length - length / 2)
        gp = abs(((pb['lapd'] - pa['lapd']) + length / 2) % length - length / 2)
        if math.dist(a['pos'], b['pos']) <= 46 and gap <= 92 and not (math.dist(pa['pos'], pb['pos']) <= 46 and gp <= 92):
            ev.append(('contact', t, 2, 0.0))
        for k, s, p in ((0, a, pa), (1, b, pb)):
            if s['attack'] != p['attack'] and s['attack'] in (1, 2):
                ev.append(('side' if s['attack'] == 1 else 'spin', t, k, 0.0))
            if s['energy'] < p['energy'] - 0.01 and not (p['flags'] & ST_RETIRED):
                ev.append(('damage', t, k, p['energy'] - s['energy']))
            if s['spin'] and not p['spin']:
                ev.append(('spin_out', t, k, 0.0))
            if (s['flags'] & ST_RETIRED) and not (p['flags'] & ST_RETIRED):
                ev.append(('retire', t, k, 0.0))
        if a['kos'] > pa['kos']:
            ev.append(('ko', t, 0, 0.0))
    return ev


def match(ref, nat):
    """One-to-one by class and racer, in order, within one tick. Returns (ok, details)."""
    out = {}
    for c in CLASSES:
        for k in (0, 1, 2):
            r = [e for e in ref if e[0] == c and e[2] == k]
            n = [e for e in nat if e[0] == c and e[2] == k]
            if not r and not n:
                continue
            ok = len(r) == len(n) and all(abs(x[1] - y[1]) <= 1 for x, y in zip(r, n))
            if c == 'damage':   # and the amount, within max(3% of the reference, 0.1 energy) as E1's W01 band
                ok = ok and all(abs(y[3] - x[3]) <= max(0.03 * x[3], 0.1) for x, y in zip(r, n))
            rel = [abs(y[3] - x[3]) / max(x[3], 0.01) for x, y in zip(r, n)] if c == 'damage' else []
            out.setdefault(c, []).append(dict(racer=k, ok=ok, ref=[e[1] for e in r], native=[e[1] for e in n],
                                              damage_rel=max(rel) if rel else None))
    return out


def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        raise SystemExit('failed: %s\n%s' % (' '.join(cmd), p.stderr[-2000:]))
    return p.stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ref', required=True); ap.add_argument('--native', required=True)
    ap.add_argument('--course', required=True); ap.add_argument('--rom', required=True)
    ap.add_argument('--out', required=True, type=Path); ap.add_argument('--mutate', default='')
    ap.add_argument('--scenarios', default=','.join(SCENARIOS)); ap.add_argument('--jobs', type=int, default=8)
    a = ap.parse_args()
    out = a.out; out.mkdir(parents=True, exist_ok=True)
    base = ['--course', a.course, '--rom', a.rom]
    summary = {'mutation': a.mutate, 'windows': 0, 'passed': 0, 'endpoints': 0, 'classes': {}, 'scenarios': {}}
    for sc in a.scenarios.split(','):
        mref, mnat, ticks = SCENARIOS[sc]
        d = out / sc; d.mkdir(exist_ok=True)
        tr, pads = d / 'ref.tsv', d / 'ref.pads'
        if not tr.exists():
            run([a.ref] + base + ['--scenario', sc, '--machines', mref, '--ticks', str(ticks), '--trace', str(tr), '--pads', str(pads)])
        rows = load(tr)
        length = COURSE_LENGTH
        ref_ev = events(rows, 0, ticks - 1, length)
        starts = {}
        last_event = max((e[1] for e in ref_ev), default=ticks)
        for t in range(START, min(ticks - LEN, last_event), GRID):   # the scenario is over after its last event
            starts.setdefault(t, LEN)
        for e in ref_ev:
            if e[1] - LEAD >= START:
                t = e[1] - LEAD
                starts[t] = max(starts.get(t, 0), LEAD + LONG.get(e[0], LEN - LEAD))
        def window(item):
            t, n = item
            n = min(n, ticks - 1 - t)
            w = d / ('w%05d' % t); w.mkdir(exist_ok=True)
            s0, s1 = w / 'p0.src', w / 'p1.src'
            if not (w / 'p1.pack').exists():
                run([a.ref] + base + ['--scenario', sc, '--machines', mref, '--ticks', str(t + 1), '--bake-at', str(t),
                                      '--bake0', str(s0), '--bake1', str(s1)])
                for k in (0, 1):
                    src = w / ('p%d.src' % k)
                    physics_pack.build_pack(src, w / ('p%d.pack' % k)) if hasattr(physics_pack, 'build_pack') else \
                        run([sys.executable, str(Path(physics_pack.__file__)), str(src), str(w / ('p%d.pack' % k))])
            r1 = state(rows, t, 1); r0 = state(rows, t, 0)
            gap = abs(((r1['lapd'] - r0['lapd']) + length / 2) % length - length / 2)
            touching = int(math.dist(r0['pos'], r1['pos']) <= 46 and gap <= 92)
            nt = w / ('native%s.tsv' % ('-' + a.mutate if a.mutate else ''))
            cmd = [a.native, '--pack0', str(w / 'p0.pack'), '--pack1', str(w / 'p1.pack'), '--pads', str(pads), '--from', str(t),
                   '--ticks', str(n), '--machines', mnat, '--since', str(r1['since']), '--toucher', str(r1['toucher']),
                   '--touching', str(touching), '--trace', str(nt)]
            for k in (0, 1):   # the spin-out side the reference drew, for a spin-out inside the window
                sp = [e[1] for e in ref_ev if e[0] == 'spin_out' and e[2] == k and t < e[1] <= t + n]
                if sp:
                    cmd += ['--spin-side%d' % k, '1' if float(rows[(sp[0], k)][19]) >= 0 else '-1']
            if a.mutate:
                cmd += ['--mutate', a.mutate]
            run(cmd)
            nat = load(nt)
            for k in (0, 1):   # the window's start state is the reference's (tick T): events at T + 1 compare against it
                nat[(t, k)] = rows[(t, k)]
            end = t + n
            if (end, 0) not in nat:
                return dict(start=t, length=n, ok=False, reason='native stopped early')
            ep = []
            for k in (0, 1):
                rs, ns = state(rows, end, k), state(nat, end, k)
                ep.append(dict(racer=k, pos_err=math.dist(rs['pos'], ns['pos']), speed_err=abs(rs['speed'] - ns['speed']),
                               speed_tol=SPEED_TOL * abs(rs['speed']) + QUANTUM))
            ep_ok = all(e['pos_err'] <= POS_TOL and e['speed_err'] <= e['speed_tol'] for e in ep)
            sens = None
            if not ep_ok and not a.mutate:
                # Sensitivity probe: the same native window with racer 0 moved 64 Q16 LSB (0.001 unit)
                # sideways, judged against the unmoved native run with the same tolerances.
                nn = w / 'native-nudge.tsv'
                run([x if x != str(nt) else str(nn) for x in cmd] + ['--nudge', '64'])
                nud = load(nn)
                sens = (end, 0) in nud and not all(
                    math.dist(state(nat, end, k)['pos'], state(nud, end, k)['pos']) <= POS_TOL and
                    abs(state(nat, end, k)['speed'] - state(nud, end, k)['speed']) <= SPEED_TOL * abs(state(nat, end, k)['speed']) + QUANTUM
                    for k in (0, 1))
            m = match(events(rows, t, end, length), events(nat, t, end, length))
            ev_ok = all(x['ok'] for v in m.values() for x in v)
            return dict(start=t, length=n, endpoint_ok=ep_ok, events_ok=ev_ok, ok=ep_ok and ev_ok, endpoints=ep, events=m,
                        chaotic=sens)
        with ThreadPoolExecutor(a.jobs) as ex:
            results = list(ex.map(window, sorted(starts.items())))
        sres = dict(reference_events={c: sum(1 for e in ref_ev if e[0] == c) for c in CLASSES}, windows=results)
        summary['scenarios'][sc] = sres
        for r in results:
            summary['windows'] += 1; summary['passed'] += r.get('ok', False); summary['endpoints'] += r.get('endpoint_ok', False)
            for c, v in r.get('events', {}).items():
                cl = summary['classes'].setdefault(c, {'windows': 0, 'passing': 0, 'damage_rel_max': 0.0})
                cl['windows'] += 1; cl['passing'] += all(x['ok'] for x in v)
                for x in v:
                    if x['damage_rel'] is not None:
                        cl['damage_rel_max'] = max(cl['damage_rel_max'], x['damage_rel'])
    tag = a.mutate or 'base'
    (out / ('judge-%s.json' % tag)).write_text(json.dumps(summary, indent=1))
    chaotic = sum(1 for v in summary['scenarios'].values() for r in v['windows'] if r.get('chaotic'))
    summary['chaotic'] = chaotic
    (out / ('judge-%s.json' % tag)).write_text(json.dumps(summary, indent=1))
    print('%s: windows %d pass %d endpoints %d; failing endpoints that a 0.001-unit nudge also breaks: %d' % (
        tag, summary['windows'], summary['passed'], summary['endpoints'], chaotic))
    for c in CLASSES:
        if c in summary['classes']:
            cl = summary['classes'][c]
            print('  %-8s %d/%d%s' % (c, cl['passing'], cl['windows'], ('  damage rel max %.3f' % cl['damage_rel_max']) if c == 'damage' else ''))


if __name__ == '__main__':
    main()
