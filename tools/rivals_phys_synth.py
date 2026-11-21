"""rivals_phys_synth.py IN OUT MODE (HOST, C-118 controls): a synthetic C-118 PHYS file for the fixture controls. Records up to and including the
reference finish tick (the first with the finished flag) are copied unchanged; after it, the pose is replaced:
  drive   the finished player drives clear: lapDistance +45 a tick, lateral and up held at the finish tick's
  freeze  the pose is frozen at the line: lapDistance, lateral and up held at the finish tick's
  weave   the player crawls (+12 a tick) and sweeps across the road (lateral -180..180, a 120-tick triangle), up -100
  axis    as weave with up 0: a tube pose on the line's axis when the sweep crosses 0 (ambiguous: unscorable)
  trunc   the records end 200 ticks after the finish (a missing pose: unscorable)
Segment, flags and lap columns are kept (a valid segment walk), so the runner's conversion path is the real one."""
import sys
import numpy as np
rt = np.dtype([('t', '<i4'), ('seg', '<i4'), ('flags', '<i4'), ('lap', '<i4'), ('ld', '<f4'), ('lat', '<f4'), ('up', '<f4'),
               ('speed', '<f4'), ('rd', '<f4'), ('x', '<f4'), ('y', '<f4'), ('z', '<f4')])
a = np.fromfile(sys.argv[1], dtype=rt); mode = sys.argv[3]
kf = int(np.nonzero(a['flags'] & (1 << 25))[0][0])
n = np.arange(1, len(a) - kf, dtype=np.float64)
post = a[kf + 1:]
if mode == 'drive':
    post['ld'] = (a['ld'][kf] + 45.0 * n).astype(np.float32); post['lat'] = a['lat'][kf]; post['up'] = a['up'][kf]
elif mode == 'freeze':
    post['ld'] = a['ld'][kf]; post['lat'] = a['lat'][kf]; post['up'] = a['up'][kf]
elif mode in ('weave', 'axis'):
    ph = (n % 120) / 120.0
    post['ld'] = (a['ld'][kf] + 12.0 * n).astype(np.float32)
    post['lat'] = (np.where(ph < 0.5, -180 + 720 * ph, 540 - 720 * ph)).astype(np.float32); post['up'] = -100.0 if mode == 'weave' else 0.0
elif mode == 'trunc':
    a = a[:kf + 201]; post = a[kf + 1:]
else:
    raise SystemExit('mode drive|freeze|weave|axis|trunc')
if mode != 'trunc': a[kf + 1:] = post
a.tofile(sys.argv[2])
print('%s: finish tick %d, %d records replaced' % (mode, kf, len(post)))
