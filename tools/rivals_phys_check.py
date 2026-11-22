"""rivals_phys_check.py PHYS_ROOT BREF_ROOT (HOST): C-118 projection checks per course and seed, from fzx-gp-ref --trace-phys.
 - the PHYS records' raceDistance column equals the --trace file's (same ticks, same values);
 - before the finish, raceDistance - lapDistance is a whole number of laps (the segment walk is the official
   distance's own source), worst deviation in units;
 - after the finish: the unwrapped lapDistance step per tick against the reference speed (median ratio), steps over a
   quarter lap (ambiguous), ticks without a segment or retired (missing), the distance driven, and how far the
   records reach past the reference race's end."""
import sys, json, struct, glob, os
import numpy as np
P, R = sys.argv[1], sys.argv[2]
rt = np.dtype([('t', '<i4'), ('seg', '<i4'), ('flags', '<i4'), ('lap', '<i4'), ('ld', '<f4'), ('lat', '<f4'), ('up', '<f4'),
               ('speed', '<f4'), ('rd', '<f4'), ('x', '<f4'), ('y', '<f4'), ('z', '<f4')])
bad_all = 0
for c in sorted(os.listdir(P)):
    for s in range(1, 7):
        f = '%s/%s/phys-s%d.bin' % (P, c, s)
        if not os.path.exists(f): continue
        a = np.fromfile(f, dtype=rt)
        rep = json.load(open('%s/%s/d1/ref-s%d.json' % (R, c, s))); L = rep['course_length']; st = rep['race_start_tick']
        tr = np.fromfile('%s/%s/d1/tr-s%d.bin' % (R, c, s), dtype='<f4').reshape(-1, 3)
        n = len(tr)
        same_rd = bool(np.array_equal(a['rd'][:n], tr[:, 0])) and bool(np.array_equal(a['t'][:len(a)], np.arange(len(a))))
        fin = np.nonzero(a['flags'][:n] & (1 << 25))[0]; kf = int(fin[0]) if len(fin) else -1
        pre = slice(st, kf + 1 if kf >= 0 else n)
        q = (a['rd'][pre].astype(np.float64) - a['ld'][pre]) / L
        dev = float(np.max(np.abs(q - np.round(q))) * L) if q.size else 0.0
        post = a[kf + 1:] if kf >= 0 else a[:0]
        d = np.diff(np.concatenate([[a['ld'][kf]], post['ld']]).astype(np.float64)) if kf >= 0 else np.zeros(0)
        d[d < -L / 2] += L; d[d > L / 2] -= L
        amb = int(np.sum(np.abs(d) > L / 4)); miss = int(np.sum((post['seg'] < 0) | ((post['flags'] & (1 << 18)) != 0)))
        ratio = float(np.median(d[post['speed'] > 1] / post['speed'][post['speed'] > 1])) if len(post) else 0.0
        bad_all += (not same_rd) + amb + miss
        print('%-16s s%d: trace distance column equal %s; finish race tick %d; pre-finish |raceDistance - lapDistance - laps| max %.4f;'
              ' post-finish %d records (%d past the race end), driven %.0f, step/speed median %.3f, max step %.1f, ambiguous %d, missing %d'
              % (c, s, same_rd, kf - st, dev, len(post), len(a) - n, float(d.sum()), ratio, float(np.abs(d).max()) if len(d) else 0, amb, miss))
print('PHYS CHECK', 'PASS' if bad_all == 0 else 'FAIL (%d)' % bad_all)
