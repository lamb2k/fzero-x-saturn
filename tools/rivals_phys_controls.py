"""rivals_phys_controls.py: the C-118 item 3 fixture controls (finished-player replay), read from native arms.
HOST only. Arms: reports and .p0 dumps under $FZX_ARM_ROOT/<arm>/, contact streams (tools/line_field_contact_diag.py)
under $FZX_CONTACT_ROOT/<arm>/.
  rivals_phys_controls.py ident OLD NEW            the runner with C-118 code but no --player-phys equals the old runner (reports
                                       timing aside, hash files, contact streams)
  rivals_phys_controls.py prefix OLD NEW PHYSROOT  old replay vs C-118 replay: vehicle 0's input identical through the reference
                                       finish tick, the contact stream identical through that tick's contact pass,
                                       vehicle 0's finish record unchanged; the first tick and pair where they part
  rivals_phys_controls.py synth ARM...             per arm: stuck events, and contact incidents with vehicle 0 after the switch"""
import sys, json, glob, os
import numpy as np
CON = os.environ.get('FZX_CONTACT_ROOT', '/root/ptp2/contact'); ARM = os.environ.get('FZX_ARM_ROOT', '/dev/shm/ptp2/arm')
rt = np.dtype([('t', '<i4'), ('seg', '<i4'), ('flags', '<i4'), ('lap', '<i4'), ('ld', '<f4'), ('lat', '<f4'), ('up', '<f4'),
               ('speed', '<f4'), ('rd', '<f4'), ('x', '<f4'), ('y', '<f4'), ('z', '<f4')])


def rep(arm, run):
    r = json.load(open('%s/%s/%s.json' % (ARM, arm, run))); r.pop('host_ns_per_tick', None); return r


def runs(arm):
    return sorted(os.path.basename(f)[:-5] for f in glob.glob('%s/%s/*-s[0-9].json' % (ARM, arm)))


def lines(arm, run):
    return open('%s/%s/%s.txt' % (CON, arm, run)).read().splitlines()


mode = sys.argv[1]
if mode == 'ident':
    a, b = sys.argv[2], sys.argv[3]; bad = 0
    for r in runs(a):
        same = rep(a, r) == rep(b, r) and open('%s/%s/%s.json.hash' % (ARM, a, r), 'rb').read() == open('%s/%s/%s.json.hash' % (ARM, b, r), 'rb').read() \
            and lines(a, r) == lines(b, r)
        bad += not same
    print('IDENT %s vs %s: %d runs, %d differ -> %s' % (a, b, len(runs(a)), bad, 'PASS' if bad == 0 else 'FAIL'))
elif mode == 'prefix':
    a, b, P = sys.argv[2], sys.argv[3], sys.argv[4]; fails = 0
    for r in runs(a):
        c, s = r.rsplit('-s', 1)
        ph = np.fromfile('%s/%s/phys-s%s.bin' % (P, c, s), dtype=rt); kf = int(np.nonzero(ph['flags'] & (1 << 25))[0][0])
        pa = np.fromfile('%s/%s/%s.p0' % (ARM, a, r), dtype='<i4').reshape(-1, 5); pb = np.fromfile('%s/%s/%s.p0' % (ARM, b, r), dtype='<i4').reshape(-1, 5)
        pre_same = np.array_equal(pa[:kf + 1], pb[:kf + 1]); post_diff = not np.array_equal(pa[kf + 1:kf + 2], pb[kf + 1:kf + 2])
        ra, rb = rep(a, r), rep(b, r)
        fa = {k: ra['racers'][0][k] for k in ('lap_end_ticks', 'finish_tick', 'final_pos')}; fb = {k: rb['racers'][0][k] for k in ('lap_end_ticks', 'finish_tick', 'final_pos')}
        la = lines(a, r); lb = lines(b, r)
        # the stream's tick is the field tick (runner tick + 1): the finish tick's contact pass is field tick kf + 1
        sa = [l for l in la if int(l.split()[0]) <= kf + 1]; sb = [l for l in lb if int(l.split()[0]) <= kf + 1]
        first = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), None)
        where = ('field tick %s pair %s-%s (old) / %s-%s (new)' % (la[first].split()[0], la[first].split()[1], la[first].split()[2], lb[first].split()[1], lb[first].split()[2])
                 if first is not None else 'none')
        ok = pre_same and post_diff and fa == fb and sa == sb and rb.get('player_phys', {}).get('scorable') == 1
        fails += not ok
        print('%-22s finish tick %d: inputs equal through it %s, differ after %s; finish record equal %s; stream equal through its contact pass %s (%d lines); first stream difference %s; scorable %s'
              % (r, kf, pre_same, post_diff, fa == fb, sa == sb, len(sa), where, rb.get('player_phys')))
    print('PREFIX %s vs %s: %s' % (a, b, 'PASS' if fails == 0 else 'FAIL (%d)' % fails))
elif mode == 'synth':
    for arm in sys.argv[2:]:
        for r in runs(arm):
            rp = rep(arm, r); sw = rp.get('player_phys', {}).get('switch_race_tick', -1) + rp['race_start_tick']
            v0 = [l.split() for l in lines(arm, r)]
            post = [l for l in v0 if int(l[0]) > sw + 1 and (l[1] == '0' or l[2] == '0')]
            fresh = sum(1 for l in post if l[5] == '1')
            print('%-8s %-22s stuck_events %d, through %d; vehicle-0 contact lines after the switch %d (fresh incidents %d); player_phys %s'
                  % (arm, r, rp['stuck_events'], rp['through_ticks'], len(post), fresh, rp.get('player_phys')))
