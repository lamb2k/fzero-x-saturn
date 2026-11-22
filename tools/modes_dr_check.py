#!/usr/bin/env python3
"""Death Race rival retirement judge: native field (HOST run of the SH2ZAM line field) against the original code's reference (HOST).

    modes_dr_check.py --seeds 31,32,33,34,35,36 --pairs "31=ref31.json,nat31.json" ... [--min-seeds 6]
    modes_dr_check.py --seeds 31,32 --manifest pairs.json        ({"31": {"ref": "...", "native": "..."}, ...})

Band (inclusive, fixed before the first native run, plan C-166): every one of the 29 opponents retires; the sorted retire ticks agree
within SORTED_BAND ticks rank by rank; per rival (by id) the retire tick is within ID_BAND ticks for at least ID_MIN of the 29.
Structure is checked before the band, and a structural failure is exit 2, never a silent pass:
  - the seed list given with --seeds is the exact manifest: one pair per expected seed, no missing, extra or duplicate seed,
    at least --min-seeds seeds, no empty candidate set;
  - every reference is a HOST report of that seed (report 'seed' equals the expected seed), every native report likewise;
  - reference and native carry the same course length (to 0.01 units) and the same lengths across seeds;
  - both rosters are complete and unique: ids 0..29, the player (id 0) excluded from the score, 29 opponents;
  - the reference and native opponent id sets are identical and each racer's grid slot matches (the native field was configured
    from this reference);
  - one valid in-window retirement per opponent: an integer retire tick in [race_start_tick, ticks], reference and native; the
    player never retires in the native report.
Exit 0 all seeds pass; 1 a band failure (structure sound); 2 a structural failure or bad usage."""
import argparse, json, sys

SORTED_BAND, ID_BAND, ID_MIN, N_OPP = 40, 60, 24, 29


class Structure(Exception):
    pass


def load(path, what):
    try:
        with open(path) as f:
            d = json.load(f)
    except (OSError, ValueError) as e:
        raise Structure('%s %s unreadable: %s' % (what, path, e))
    if not isinstance(d, dict) or not isinstance(d.get('racers'), list):
        raise Structure('%s %s has no racers list' % (what, path))
    return d


def roster(d, what):
    ids = [r.get('id') for r in d['racers']]
    if any(not isinstance(i, int) or isinstance(i, bool) for i in ids):
        raise Structure('%s: a racer id is not an integer' % what)
    if len(set(ids)) != len(ids):
        raise Structure('%s: duplicate racer ids %s' % (what, sorted(i for i in set(ids) if ids.count(i) > 1)))
    if sorted(ids) != list(range(N_OPP + 1)):
        raise Structure('%s: roster is not ids 0..%d (%d racers)' % (what, N_OPP, len(ids)))
    return {r['id']: r for r in d['racers']}


def retire_ticks(d, rost, what, window_end, allow_none=False):
    start = d.get('race_start_tick')
    if not isinstance(start, int) or not isinstance(d.get('ticks'), int):
        raise Structure('%s: race_start_tick or ticks missing' % what)
    out = {}
    for i, r in rost.items():
        t = r.get('retire_tick')
        if not isinstance(t, int) or isinstance(t, bool):
            raise Structure('%s: racer %d retire_tick is not an integer' % (what, i))
        if i == 0:
            continue
        if allow_none and t == -1:
            out[i] = -1
            continue
        if not (start <= t <= window_end):
            raise Structure('%s: opponent %d has no valid in-window retirement (retire_tick %d, window %d..%d)' % (what, i, t, start, window_end))
        out[i] = t
    return out


def score_seed(seed, ref_path, nat_path):
    ref, nat = load(ref_path, 'reference'), load(nat_path, 'native')
    tag = 'seed %d' % seed
    for what, d in (('reference', ref), ('native', nat)):
        if d.get('seed') != seed:
            raise Structure('%s: %s report is seed %r' % (tag, what, d.get('seed')))
        if d.get('tier') != 'HOST':
            raise Structure('%s: %s report tier is %r, not HOST' % (tag, what, d.get('tier')))
    cl_r, cl_n = ref.get('course_length'), nat.get('course_length')
    if not isinstance(cl_r, (int, float)) or not isinstance(cl_n, (int, float)) or abs(cl_r - cl_n) > 0.01:
        raise Structure('%s: course length differs (reference %r, native %r)' % (tag, cl_r, cl_n))
    rr, nr = roster(ref, tag + ' reference'), roster(nat, tag + ' native')
    if set(rr) != set(nr):
        raise Structure('%s: id sets differ' % tag)
    for i in rr:
        if rr[i].get('grid') != nr[i].get('grid'):
            raise Structure('%s: racer %d grid differs (reference %r, native %r): the native field was not configured from this reference' % (tag, i, rr[i].get('grid'), nr[i].get('grid')))
    rt = retire_ticks(ref, rr, tag + ' reference', ref['ticks'])
    nt = retire_ticks(nat, nr, tag + ' native', nat['ticks'], allow_none=True)
    if nr[0]['retire_tick'] != -1:
        raise Structure('%s: the player retired in the native report' % tag)
    rs = sorted(rt.values())
    n_ret = sum(1 for v in nt.values() if v >= 0)
    if n_ret < N_OPP:
        print('seed %d retired %d/%d (every opponent must retire) -> FAIL' % (seed, n_ret, N_OPP))
        return False, cl_r
    ns = sorted(nt.values())
    dev = max(abs(a - b) for a, b in zip(ns, rs))
    n_id = sum(1 for i in rt if abs(nt[i] - rt[i]) <= ID_BAND)
    ok = dev <= SORTED_BAND and n_id >= ID_MIN
    print('seed %d retired %d/%d sorted max dev %d (band %d) per-id within %d: %d/%d (min %d) first %d last %d (reference %d..%d) -> %s' % (
        seed, n_ret, N_OPP, dev, SORTED_BAND, ID_BAND, n_id, N_OPP, ID_MIN, ns[0], ns[-1], rs[0], rs[-1], 'PASS' if ok else 'FAIL'))
    return ok, cl_r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--seeds', required=True, help='the exact expected seed list, comma separated')
    ap.add_argument('--pairs', nargs='*', default=[], help='SEED=REF.json,NATIVE.json (repeat)')
    ap.add_argument('--manifest', help='json {"SEED": {"ref": path, "native": path}}')
    ap.add_argument('--min-seeds', type=int, default=6)
    a = ap.parse_args()
    try:
        try:
            expect = [int(s) for s in a.seeds.split(',') if s.strip() != '']
        except ValueError:
            raise Structure('--seeds is not a list of integers')
        if len(set(expect)) != len(expect):
            raise Structure('expected seed list has duplicates')
        if len(expect) < a.min_seeds:
            raise Structure('%d expected seeds, at least %d required' % (len(expect), a.min_seeds))
        pairs = []
        for p in a.pairs:
            try:
                s, rest = p.split('=', 1)
                ref, nat = rest.split(',', 1)
                pairs.append((int(s), ref, nat))
            except ValueError:
                raise Structure('bad --pairs entry %r' % p)
        if a.manifest:
            m = json.load(open(a.manifest))
            for s, v in m.items():
                pairs.append((int(s), v['ref'], v['native']))
        if not pairs:
            raise Structure('empty candidate set: no seed pairs given')
        got = [s for s, _, _ in pairs]
        if len(set(got)) != len(got):
            raise Structure('duplicate seed report(s): %s' % sorted(s for s in set(got) if got.count(s) > 1))
        if set(got) != set(expect):
            raise Structure('seed manifest mismatch: missing %s, unexpected %s' % (sorted(set(expect) - set(got)), sorted(set(got) - set(expect))))
        bad, lens = 0, set()
        for s, ref, nat in sorted(pairs):
            ok, cl = score_seed(s, ref, nat)
            lens.add(round(cl, 2))
            bad += 0 if ok else 1
        if len(lens) != 1:
            raise Structure('course length differs between seeds: %s' % sorted(lens))
    except Structure as e:
        print('STRUCTURE FAIL: %s' % e)
        sys.exit(2)
    print('%d/%d seeds pass' % (len(expect) - bad, len(expect)))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
