#!/usr/bin/env python3
"""C-108 CPU-CPU contact-damage judge (HOST): native line field against the reference, per course.

  rivals_contact_judge.py --course SLUG:NATDIR:REFDIR [--course ...] --seeds 1,2,3,4,5,6 [--json OUT]
                          [--native-from-ref] [--transform zero-cpu-keep-pooled|double-cpu|player-x3] [--quiet]
  rivals_contact_judge.py --controls --course ... --seeds ...       (the C-108 item-3 controls; exit 0 when all behave)

Streams (both reduced to one incident list per race, in event order, before any split):
- native: NATDIR/<slug>-s<seed>.txt, one line per touching pair per contact pass, written by the HOST runner built
  with tools/line_field_contact_diag.py (fields: tick ia ib a_ext b_ext fresh tube half_width gap adl dl closing
  dlatvel norm dmg_a dmg_b a_attacks b_attacks; energy Q8). An incident is a line with fresh = 1: the library's own
  rule (a touch is an incident unless both vehicles are inside their 30-tick cool; every touching tick resets both
  cools). Its damage is the contact charge to each participant (dmg_a, dmg_b), so walls and other energy changes are
  excluded. Units: energy / 256.
- reference: REFDIR/diag-s<seed>.bin and REFDIR/ref-s<seed>.json (fzx-gp-ref --diag, 88-byte records). Touch: two
  ACTIVE racers within 46 (3D, the source's areColliding distance). The same rule as native, from race_start_tick
  (cool and touch state empty there, as the native field starts): a touch is an incident unless both are cooling;
  every touching tick resets both cools; pairs in index order. Damage at an incident: each participant's energy loss
  from the incident tick to the next (the source charges impact damage on the onset), once per recipient per tick.
  This is the contact damage attributable at matched onsets; the reference does not log the charge itself.
Split after the stream is built: CPU-CPU (two distinct CPU IDs, neither 0) and player (a pair with vehicle 0). Only
CPU-CPU incidents are judged; vehicle 0's own damage is never counted (its trace carries its own energy).
Verdict per course: sum of the native CPU-CPU totals over the seeds / the reference sum, inclusive 0.67 .. 1.50,
unrounded. A missing race or a zero reference sum is UNSCORABLE and fails. Exit 0 only when every course passes.
"""
import argparse, json, os, sys

BAND = (0.67, 1.50)
COOL = 30
TOUCH = 46.0


def native_stream(path):
    out = []
    for line in open(path):
        v = line.split()
        if len(v) != 18:
            raise ValueError('%s: bad line %r' % (path, line))
        t, ia, ib, fresh, da, db = int(v[0]), int(v[1]), int(v[2]), int(v[5]), int(v[14]), int(v[15])
        if fresh:
            out.append((t, ia, ib, da / 256.0, db / 256.0, int(v[6])))
    return out


def ref_stream(diag, report):
    import numpy as np
    start = json.load(open(report))['race_start_tick']
    a = np.frombuffer(open(diag, 'rb').read(), dtype=np.dtype([('i', '<i4', 10), ('f', '<f4', 12)]))
    T = a['i'][:, 0]; ID = a['i'][:, 1]; FL = a['i'][:, 2]; SH = a['i'][:, 3]
    nt = int(T.max()) + 1
    pos = np.full((nt, 30, 3), np.nan, np.float32); act = np.zeros((nt, 30), bool)
    en = np.full((nt, 30), np.nan, np.float32); ret = np.zeros((nt, 30), bool); shape = np.zeros((nt, 30), np.int32)
    pos[T, ID] = a['f'][:, 7:10]; act[T, ID] = (FL & (1 << 30)) != 0; en[T, ID] = a['f'][:, 3]
    ret[T, ID] = (FL & (1 << 18)) != 0; shape[T, ID] = SH
    cool = np.zeros(30, np.int32); out = []
    for t in range(start, nt - 1):
        p = pos[t]; d = np.sqrt(((p[:, None, :] - p[None, :, :]) ** 2).sum(-1))
        c = (d <= TOUCH) & act[t][:, None] & act[t][None, :]
        np.fill_diagonal(c, False); c = np.triu(c)
        cool = np.maximum(cool - 1, 0)
        charged = set()
        for i, j in zip(*np.nonzero(c)):
            i = int(i); j = int(j)
            fresh = not (cool[i] and cool[j]); cool[i] = cool[j] = COOL
            if not fresh:
                continue
            dmg = []
            for k in (i, j):
                if k in charged or ret[t, k]:
                    dmg.append(0.0); continue
                charged.add(k)
                l = float(en[t, k] - en[t + 1, k])
                dmg.append(l if l == l and l > 1e-4 else 0.0)
            out.append((t, i, j, dmg[0], dmg[1], int((shape[t, i] & 0x1c0) in (0x80, 0xc0))))
    return out


def transform(stream, how):
    if not how:
        return stream
    out = []
    # zero-cpu-keep-pooled: the CPU-CPU charges are zeroed and the player-pair charges scaled so the race's pooled
    # (unsplit) total is unchanged: a pooled judge sees the same total, the CPU judge sees zero.
    t0 = tally(stream); keep = t0['unsplit'] / t0['player'] if t0['player'] > 0 else 0.0
    for (t, a, b, da, db, tube) in stream:
        cpu = a != 0 and b != 0 and a != b
        if how == 'double-cpu' and cpu:
            da, db = 2 * da, 2 * db
        elif how == 'zero-cpu-keep-pooled':
            da, db = (0.0, 0.0) if cpu else (keep * da, keep * db)
        elif how == 'player-x3' and not cpu:
            da, db = 3 * da, 3 * db
        out.append((t, a, b, da, db, tube))
    return out


def tally(stream):
    r = dict(cpu=0.0, cpu_n=0, player=0.0, player_n=0, unsplit=0.0, unsplit_n=0, cpu_tube=0.0, cpu_road=0.0,
             cpu_tube_n=0, cpu_road_n=0)
    for (t, a, b, da, db, tube) in stream:
        cpu = a != 0 and b != 0 and a != b
        dm = (da if a else 0.0) + (db if b else 0.0)   # vehicle 0's own damage is never counted
        r['unsplit'] += dm; r['unsplit_n'] += 1
        if cpu:
            k = 'cpu_tube' if tube else 'cpu_road'
            r['cpu'] += dm; r['cpu_n'] += 1; r[k] += dm; r[k + '_n'] += 1
        else:
            r['player'] += dm; r['player_n'] += 1
    return r


def score(courses, seeds, native_from_ref=False, how='', quiet=False):
    """courses: [(slug, natdir, refdir)]. Returns (ok, result dict)."""
    res = {'band': BAND, 'courses': {}}
    ok_all = True
    for slug, nd, rd in courses:
        c = {'seeds': {}, 'unscorable': []}
        nat_sum = ref_sum = 0.0; pooled_n = pooled_r = 0.0
        for s in seeds:
            rp, rj = os.path.join(rd, 'diag-s%s.bin' % s), os.path.join(rd, 'ref-s%s.json' % s)
            npth = os.path.join(nd, '%s-s%s.txt' % (slug, s))
            if not (os.path.exists(rp) and os.path.exists(rj)) or (not native_from_ref and not os.path.exists(npth)):
                c['unscorable'].append('seed %s: missing race' % s); continue
            ref = ref_stream(rp, rj)
            nat = transform(ref if native_from_ref else native_stream(npth), how)
            tr, tn = tally(ref), tally(nat)
            for arm, x in (('ref', tr), ('nat', tn)):
                if abs(x['cpu'] + x['player'] - x['unsplit']) > 1e-6 * max(1.0, x['unsplit']) or x['cpu_n'] + x['player_n'] != x['unsplit_n']:
                    c['unscorable'].append('seed %s %s: buckets do not sum to the unsplit total' % (s, arm))
            c['seeds'][str(s)] = {'nat': tn, 'ref': tr}
            nat_sum += tn['cpu']; ref_sum += tr['cpu']; pooled_n += tn['unsplit']; pooled_r += tr['unsplit']
        if ref_sum <= 0:
            c['unscorable'].append('zero reference sum')
        ratio = nat_sum / ref_sum if ref_sum > 0 else None
        c.update(nat_cpu_sum=nat_sum, ref_cpu_sum=ref_sum, ratio=ratio,
                 pooled_ratio=(pooled_n / pooled_r if pooled_r > 0 else None))
        c['verdict'] = 'UNSCORABLE' if c['unscorable'] else ('PASS' if BAND[0] <= ratio <= BAND[1] else 'FAIL')
        ok_all = ok_all and c['verdict'] == 'PASS'
        res['courses'][slug] = c
        if not quiet:
            for s, x in c['seeds'].items():
                n, r = x['nat'], x['ref']
                print('%-15s s%-3s CPU-CPU native %8.1f / %4d = %5.2f | reference %8.1f / %4d = %5.2f | player native %7.1f (%d) ref %7.1f (%d)'
                      % (slug, s, n['cpu'], n['cpu_n'], n['cpu'] / max(n['cpu_n'], 1), r['cpu'], r['cpu_n'], r['cpu'] / max(r['cpu_n'], 1),
                         n['player'], n['player_n'], r['player'], r['player_n']))
            print('%-15s %s: native CPU-CPU %.1f / reference %.1f = %s (band %.2f-%.2f)%s'
                  % (slug, c['verdict'], nat_sum, ref_sum, '%.4f' % ratio if ratio is not None else '-', BAND[0], BAND[1],
                     ('; ' + '; '.join(c['unscorable'])) if c['unscorable'] else ''))
    res['verdict'] = 'PASS' if ok_all else 'FAIL'
    if not quiet:
        print('VERDICT-CPU-CONTACT', res['verdict'])
    return ok_all, res


def controls(courses, seeds):
    """C-108 item 3. Each control states what it must do; exit 0 when all of them do."""
    rows = []
    ok, _ = score(courses, seeds, native_from_ref=True, quiet=True)
    rows.append(('reference against itself passes', ok))
    ok0, r0 = score(courses, seeds, native_from_ref=True, how='zero-cpu-keep-pooled', quiet=True)
    pooled_pass = all(c['pooled_ratio'] is not None and BAND[0] <= c['pooled_ratio'] <= BAND[1] for c in r0['courses'].values())
    rows.append(('zeroed CPU damage, player damage kept (a pooled judge would pass: %s): CPU judge fails' % pooled_pass,
                 (not ok0) and pooled_pass and all(c['verdict'] == 'FAIL' for c in r0['courses'].values())))
    ok2, r2 = score(courses, seeds, native_from_ref=True, how='double-cpu', quiet=True)
    rows.append(('doubled CPU damage fails the upper bound',
                 (not ok2) and all(c['ratio'] is not None and c['ratio'] > BAND[1] for c in r2['courses'].values())))
    _, rb = score(courses, seeds, native_from_ref=True, quiet=True)
    _, r3 = score(courses, seeds, native_from_ref=True, how='player-x3', quiet=True)
    same = all(rb['courses'][k]['nat_cpu_sum'] == r3['courses'][k]['nat_cpu_sum'] for k in rb['courses'])
    moved = any(rb['courses'][k]['seeds'][s]['nat']['player'] != r3['courses'][k]['seeds'][s]['nat']['player']
                for k in rb['courses'] for s in rb['courses'][k]['seeds'])
    rows.append(('a player-only change leaves CPU totals unchanged (player totals moved: %s)' % moved, same and moved))
    _, rn = score(courses, seeds, quiet=True)
    sums = all(not [u for u in c['unscorable'] if 'buckets' in u] for c in rn['courses'].values())
    rows.append(('buckets sum to the unsplit total in each arm (native and reference)', sums))
    for name, good in rows:
        print('control %-4s %s' % ('OK' if good else 'BAD', name))
    return all(g for _, g in rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--course', action='append', required=True, help='SLUG:NATDIR:REFDIR')
    ap.add_argument('--seeds', required=True)
    ap.add_argument('--json', default='')
    ap.add_argument('--native-from-ref', action='store_true')
    ap.add_argument('--transform', default='', choices=['', 'zero-cpu-keep-pooled', 'double-cpu', 'player-x3'])
    ap.add_argument('--controls', action='store_true')
    ap.add_argument('--quiet', action='store_true')
    a = ap.parse_args()
    courses = [tuple(x.split(':', 2)) for x in a.course]
    seeds = a.seeds.split(',')
    if a.controls:
        sys.exit(0 if controls(courses, seeds) else 1)
    ok, res = score(courses, seeds, a.native_from_ref, a.transform, a.quiet)
    if a.json:
        json.dump(res, open(a.json, 'w'), indent=1)
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
