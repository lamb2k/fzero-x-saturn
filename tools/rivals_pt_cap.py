#!/usr/bin/env python3
"""C-112 Port Town incident cap (HOST, replay-player fixture): per race, CPU-CPU incidents at or under a ceiling
taken from the reference. Frozen with its ceiling before any Port Town tuning; the same rule for every regression
and held-out race.

  rivals_pt_cap.py --native NATDIR --ref REFDIR --seeds 1,2,3,4,5,6 [--json OUT] [--quiet]
  rivals_pt_cap.py --calibrate --ref REFDIR            (prints the ceiling from reference seeds 1-6)
  rivals_pt_cap.py --controls --native NATDIR --ref REFDIR --seeds ...   (the four C-112 item-3 controls)

Counting (C-108's convention, tools/rivals_contact_judge.py, unchanged): fresh incidents in the C-108 event window
(reference from race_start_tick, native from its stream's first tick) with the 30-tick per-vehicle cooldown, in event
order, before splitting. Each incident counts once per unordered pair and tick; a second fresh line for the same
pair in the same tick is a stream error. Buckets: CPU-CPU (two distinct CPU IDs, neither 0) and player (a pair with
vehicle 0); they must sum to the unsplit count.
Ceiling: CEILING, the largest Port Town CPU-CPU count in the six reference calibration races (seeds 1-6, d1), checked
by --calibrate. Verdict per race: CPU-CPU <= CEILING (inclusive). A missing race, a duplicate or a bucket mismatch
fails. The historical E2 bound (all pairs, native only, <= 250 a race; tools/rivals_e2_check.py) is reported beside
it with its own verdict and is not changed by this rule. Exit 0 only when every race passes the CPU-CPU ceiling.
"""
import argparse, json, os, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rivals_contact_judge as J   # noqa: E402  (frozen C-108 judge: its streams and incident rule)

CEILING = 338          # Port Town, reference d1 seeds 1-6 CPU-CPU fresh incidents: 282 265 200 274 338 280
OLD_LIMIT = 250        # tools/rivals_e2_check.py CONTACT_LIMIT, all pairs, native only
SLUG = 'port-town-1'


def counts(stream):
    """stream: [(tick, a, b, dmg_a, dmg_b, tube)] -> dict(cpu, player, unsplit, dup)."""
    seen = set(); r = dict(cpu=0, player=0, unsplit=0, dup=0)
    for (t, a, b, _da, _db, _tube) in stream:
        k = (t, min(a, b), max(a, b))
        if k in seen:
            r['dup'] += 1; continue
        seen.add(k)
        r['unsplit'] += 1
        if a != 0 and b != 0 and a != b: r['cpu'] += 1
        else: r['player'] += 1
    return r


def race_rows(nat_dir, ref_dir, seeds, native_from_ref=False, extra_cpu=0, extra_player=0):
    rows = []
    for s in seeds:
        rp, rj = os.path.join(ref_dir, 'diag-s%s.bin' % s), os.path.join(ref_dir, 'ref-s%s.json' % s)
        npth = os.path.join(nat_dir, '%s-s%s.txt' % (SLUG, s)) if nat_dir else ''
        if not native_from_ref and not os.path.exists(npth):
            rows.append(dict(seed=s, missing=True)); continue
        st = J.ref_stream(rp, rj) if native_from_ref else J.native_stream(npth)
        c = counts(st)
        c['cpu'] += extra_cpu; c['unsplit'] += extra_cpu
        c['player'] += extra_player; c['unsplit'] += extra_player
        c['seed'] = s; c['missing'] = False
        rows.append(c)
    return rows


def judge(rows, quiet=False):
    ok = True
    for r in rows:
        if r['missing']:
            ok = False
            if not quiet: print('%s s%-3s MISSING race: FAIL' % (SLUG, r['seed']))
            continue
        bad = r['dup'] or r['cpu'] + r['player'] != r['unsplit']
        cpu_ok = not bad and r['cpu'] <= CEILING
        ok = ok and cpu_ok
        if not quiet:
            print('%s s%-3s CPU-CPU %4d player %4d sum %4d | ceiling %d: %s | historical E2 all pairs %4d <= %d: %s%s'
                  % (SLUG, r['seed'], r['cpu'], r['player'], r['unsplit'], CEILING, 'PASS' if cpu_ok else 'FAIL',
                     r['unsplit'], OLD_LIMIT, 'PASS' if r['unsplit'] <= OLD_LIMIT else 'FAIL',
                     ' (stream error: %d duplicate, buckets %s)' % (r['dup'], 'ok' if r['cpu'] + r['player'] == r['unsplit'] else 'mismatch') if bad else ''))
    if not quiet:
        print('VERDICT-PT-CAP', 'PASS' if ok else 'FAIL')
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--native', default='')
    ap.add_argument('--ref', required=True)
    ap.add_argument('--seeds', default='1,2,3,4,5,6')
    ap.add_argument('--json', default='')
    ap.add_argument('--calibrate', action='store_true')
    ap.add_argument('--controls', action='store_true')
    ap.add_argument('--quiet', action='store_true')
    ap.add_argument('--extra-cpu', type=int, default=0, help='controls only: CPU-CPU incidents added to every race')
    a = ap.parse_args()
    seeds = a.seeds.split(',')
    if a.calibrate:
        rows = race_rows('', a.ref, ['1', '2', '3', '4', '5', '6'], native_from_ref=True)
        cpu = [r['cpu'] for r in rows]
        print('reference CPU-CPU per seed 1-6: %s; ceiling %d; recorded CEILING %d: %s'
              % (cpu, max(cpu), CEILING, 'MATCH' if max(cpu) == CEILING else 'MISMATCH'))
        sys.exit(0 if max(cpu) == CEILING else 1)
    if a.controls:
        res = []
        res.append(('reference against itself passes', judge(race_rows('', a.ref, seeds, native_from_ref=True), quiet=True)))
        base = race_rows(a.native, a.ref, seeds)
        top = max(r['cpu'] for r in base)
        res.append(('a CPU-CPU count at the ceiling passes', judge(race_rows(a.native, a.ref, seeds, extra_cpu=CEILING - top), quiet=True)))
        def run(extra):
            return subprocess.run([sys.executable, os.path.abspath(__file__), '--native', a.native, '--ref', a.ref, '--seeds', a.seeds,
                                   '--quiet', '--extra-cpu', str(extra)]).returncode
        res.append(('the scorer at the ceiling exits 0', run(CEILING - top) == 0))
        res.append(('one above the ceiling fails with a nonzero exit', run(CEILING - top + 1) != 0))
        more_p = race_rows(a.native, a.ref, seeds, extra_player=500)
        res.append(('added player-only incidents leave the CPU verdict unchanged',
                    judge(more_p, quiet=True) == judge(base, quiet=True) and all(x['cpu'] == y['cpu'] for x, y in zip(more_p, base))))
        res.append(('a missing race fails', not judge(base + [dict(seed='x', missing=True)], quiet=True)))
        for name, good in res:
            print('control %-4s %s' % ('OK' if good else 'BAD', name))
        sys.exit(0 if all(g for _, g in res) else 1)
    rows = race_rows(a.native, a.ref, seeds, extra_cpu=a.extra_cpu)
    ok = judge(rows, a.quiet)
    if a.json:
        json.dump(dict(ceiling=CEILING, old_limit=OLD_LIMIT, rows=rows, verdict='PASS' if ok else 'FAIL'), open(a.json, 'w'), indent=1)
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
