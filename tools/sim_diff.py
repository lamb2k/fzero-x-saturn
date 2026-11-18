#!/usr/bin/env python3
"""Differential-harness analyser for the simulation lane (ref/sim_main.c records).

  sim_diff.py race REF.rec OTHER.rec [--json OUT]      mode 2: both arms evolved independently
  sim_diff.py onetick REF.rec ONETICK.rec [--json OUT] mode 1: OTHER ran one tick from REF's state

Mode 2 leads with the FIRST DIVERGENT GAMEPLAY DECISION (segment selection, state-flag
transitions such as wall contact or airborne, lap/checkpoint crossings, boost, RNG draws,
race time) and the first divergent camera decision (mode, race setting, look-back, FOV
flags, timer, angle indices). Continuous errors: racer along-track/lateral position,
speed, energy and heading; camera eye/at in units and Q20.12 LSB; and the camera's
screen displacement: probe points projected through each arm's camera (N64 look-at +
frustum, 320x240) and compared in pixels.

Mode 1 reports per-field errors against the reference in LSB of the fixed format plus one
ulp of the reference value (eye/at Q20.12, basis Q2.30) and exact discrete fields; angle
index differences at a rounding tie are listed as marginal.
Records hold no ROM bytes, but the race they describe is the owner's course: keep them
under the private evidence folder. Only summaries leave it."""
import argparse, json, math, struct, sys

REC_WORDS = 196
REC_SIZE = REC_WORDS * 4
MAGIC = 0x31534D46

STATE = ['intro'] + ['pos%d' % i for i in range(3)] + ['vel%d' % i for i in range(3)] + \
    ['speed', 'energy', 'lap_distance', 'seg_t', 'height', 'segment', 'lap', 'laps_completed',
     'race_time', 'state_flags', 'boost_timer']
STATE_INT = {'intro', 'segment', 'lap', 'laps_completed', 'race_time', 'state_flags', 'boost_timer'}
POSE = ['ppos%d' % i for i in range(3)] + ['pb%d' % i for i in range(9)] + \
    ['pspeed', 'penergy', 'pintro', 'plap', 'plaps', 'ptime', 'pflags']
POSE_INT = {'pintro', 'plap', 'plaps', 'ptime', 'pflags'}
CAM_INT = ['mode', 'setting', 'timer', 'look_back', 'fov_flags', 'state', 'type', 'pitch_index',
           'yaw_index', 'fixed']
CAM_FLOAT = ['eye0', 'eye1', 'eye2', 'at0', 'at1', 'at2'] + ['b%d' % i for i in range(9)] + \
    ['fov', 'fcx', 'fcy', 'xz0', 'xz1', 'peye0', 'peye1', 'peye2', 'pat0', 'pat1', 'pat2',
     'up0', 'up1', 'up2', 's_distance', 's_pitch', 's_yaw', 's_target_yaw', 'sa0', 'sa1', 'sa2',
     'near', 'far', 'fsx', 'fsy']


def f32(word):
    return struct.unpack('<f', struct.pack('<I', word & 0xFFFFFFFF))[0]


def load(path):
    data = open(path, 'rb').read()
    if len(data) % REC_SIZE:
        raise SystemExit('%s: not a whole number of %d-byte records' % (path, REC_SIZE))
    out = []
    for k in range(len(data) // REC_SIZE):
        w = struct.unpack_from('<%dI' % REC_WORDS, data, k * REC_SIZE)
        if w[0] != MAGIC:
            raise SystemExit('%s: bad record magic at %d' % (path, k))
        r = {'tick': w[1], 'button': w[2] & 0xFFFF}
        i = 3
        for name in STATE:
            r[name] = struct.unpack('<i', struct.pack('<I', w[i]))[0] if name in STATE_INT else f32(w[i])
            i += 1
        for name in POSE:
            r[name] = struct.unpack('<i', struct.pack('<I', w[i]))[0] if name in POSE_INT else f32(w[i])
            i += 1
        r['rng'] = list(w[i:i + 4]); i += 4
        r['camera_count'] = w[i]; r['camhash'] = w[i + 1]; r['fixed_ticks'] = w[i + 2]
        r['fallbacks'] = w[i + 3]; i += 4
        cams = []
        for c in range(2):
            cam = {}
            for name in CAM_INT:
                cam[name] = struct.unpack('<i', struct.pack('<I', w[i]))[0]; i += 1
            for name in CAM_FLOAT:
                cam[name] = f32(w[i]); i += 1
            cam['raw'] = [struct.unpack('<i', struct.pack('<I', x))[0] for x in w[i:i + 24]]; i += 24
            cams.append(cam)
        assert i == REC_WORDS, i
        r['cam'] = cams
        out.append(r)
    return out


def ulp(x):
    x = abs(x)
    if x == 0:
        return 2.0 ** -149
    m, e = math.frexp(x)  # x = m * 2^e, m in [0.5, 1)
    return 2.0 ** (e - 24)


def pct(values, p):
    if not values:
        return None
    v = sorted(values)
    k = min(len(v) - 1, max(0, int(math.ceil(p / 100.0 * len(v))) - 1))
    return v[k]


def stats(values):
    if not values:
        return None
    return {'n': len(values), 'mean': sum(values) / len(values), 'p50': pct(values, 50), 'p99': pct(values, 99),
            'max': max(values)}


# ---- camera projection (N64 look-at + frustum, see math.c Matrix_SetLookAt / Matrix_SetFrustrum) ----
def sub(a, b): return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def norm(a):
    n = math.sqrt(dot(a, a))
    return [x / n for x in a] if n > 0 else None


def camera_model(c):
    eye = [c['eye0'], c['eye1'], c['eye2']]
    at = [c['at0'], c['at1'], c['at2']]
    up = [c['b3'], c['b4'], c['b5']]
    look = norm(sub(eye, at))
    if look is None:
        return None
    xa = norm(cross(up, look))
    if xa is None:
        return None
    ya = norm(cross(look, xa))
    near, fsx, fsy = c['near'], c['fsx'], c['fsy']
    if near <= 0 or fsx == 0 or fsy == 0:
        return None
    half = math.floor(abs(c['fov'] * (4096 / 360.0) / 2) + 0.5) * (1 if c['fov'] >= 0 else -1)
    radius = math.tan(half * 2 * math.pi / 4096) * near
    cx = c['fcx'] * (2 * radius) / fsx
    right, left = cx + radius, cx - radius
    radius_y = radius * fsy / fsx
    cy = c['fcy'] * (2 * radius_y) / fsy
    top, bottom = cy + radius_y, cy - radius_y
    return {'eye': eye, 'x': xa, 'y': ya, 'z': look, 'm00': 2 * near / (right - left),
            'm20': (right + left) / (right - left), 'm11': 2 * near / (top - bottom),
            'm21': (top + bottom) / (top - bottom), 'near': near}


def project(m, p):
    d = sub(p, m['eye'])
    xv, yv, zv = dot(d, m['x']), dot(d, m['y']), dot(d, m['z'])
    w = -zv
    if w <= m['near']:
        return None
    nx = (m['m00'] * xv + m['m20'] * zv) / w
    ny = (m['m11'] * yv + m['m21'] * zv) / w
    return (160.0 + 160.0 * nx, 120.0 - 120.0 * ny)


def probes(ref_rec):
    c = ref_rec['cam'][0]
    eye = [c['eye0'], c['eye1'], c['eye2']]
    fwd = [c['b0'], c['b1'], c['b2']]
    side = [c['b6'], c['b7'], c['b8']]
    pts = {'at': [c['at0'], c['at1'], c['at2']], 'racer': [ref_rec['ppos0'], ref_rec['ppos1'], ref_rec['ppos2']]}
    for dist in (100, 500, 2000):
        pts['fwd%d' % dist] = [eye[i] + dist * fwd[i] for i in range(3)]
    for s in (-150, 150):
        pts['side%d' % s] = [eye[i] + 500 * fwd[i] + s * side[i] for i in range(3)]
    return pts


def px_error(ref_rec, other_rec):
    a = camera_model(ref_rec['cam'][0])
    b = camera_model(other_rec['cam'][0])
    if a is None or b is None:
        return None, {}
    worst, each = 0.0, {}
    for name, p in probes(ref_rec).items():
        pa, pb = project(a, p), project(b, p)
        if pa is None or pb is None:
            continue
        e = math.hypot(pa[0] - pb[0], pa[1] - pb[1])
        each[name] = e
        worst = max(worst, e)
    return worst, each


# ---- gameplay decisions -----------------------------------------------------------------------
FLAG_NAMES = {18: 'retired', 19: 'falling_off_track', 25: 'finished'}


def decisions(r):
    """Discrete gameplay state after a tick (the source's decisions)."""
    return {'segment': r['segment'], 'state_flags': r['state_flags'] & 0xFFFFFFFF, 'lap': r['lap'],
            'laps_completed': r['laps_completed'], 'race_time': r['race_time'],
            'boost_active': r['boost_timer'] > 0, 'rng': tuple(r['rng'])}


def camera_decisions(r):
    out = {}
    for i in range(min(2, r['camera_count'])):
        c = r['cam'][i]
        for k in ('mode', 'setting', 'look_back', 'fov_flags', 'state', 'timer', 'pitch_index', 'yaw_index'):
            out['cam%d.%s' % (i, k)] = c[k]
    return out


def describe_flags(a, b):
    diff = a ^ b
    names = [FLAG_NAMES.get(bit, 'bit%d' % bit) + ('+' if (b >> bit) & 1 else '-')
             for bit in range(32) if (diff >> bit) & 1]
    return ','.join(names)


def race(ref, other):
    n = min(len(ref), len(other))
    first_game, first_cam, events = None, None, []
    pos_along, pos_lat, speed_err, energy_err, heading_err = [], [], [], [], []
    eye_err, at_err, px, px_at, px_racer = [], [], [], [], []
    for t in range(n):
        a, b = ref[t], other[t]
        da, db = decisions(a), decisions(b)
        for k in da:
            if da[k] != db[k]:
                ev = {'tick': t, 'kind': 'gameplay', 'field': k, 'ref': str(da[k]), 'other': str(db[k])}
                if k == 'state_flags':
                    ev['flags'] = describe_flags(da[k], db[k])
                events.append(ev)
                if first_game is None:
                    first_game = ev
        ca, cb = camera_decisions(a), camera_decisions(b)
        for k in ca:
            if ca[k] != cb.get(k):
                ev = {'tick': t, 'kind': 'camera', 'field': k, 'ref': ca[k], 'other': cb.get(k)}
                if len(events) < 20000:
                    events.append(ev)
                if first_cam is None:
                    first_cam = ev
        # racer continuous errors, in the reference's along-track frame (its velocity)
        d = [b['pos%d' % i] - a['pos%d' % i] for i in range(3)]
        v = norm([a['vel%d' % i] for i in range(3)])
        if v is not None:
            along = dot(d, v)
            pos_along.append(abs(along))
            pos_lat.append(math.sqrt(max(0.0, dot(d, d) - along * along)))
        speed_err.append(abs(b['speed'] - a['speed']))
        energy_err.append(abs(b['energy'] - a['energy']))
        fa, fb = [a['pb0'], a['pb1'], a['pb2']], [b['pb0'], b['pb1'], b['pb2']]
        heading_err.append(math.degrees(math.atan2(math.sqrt(dot(cross(fa, fb), cross(fa, fb))), dot(fa, fb))))
        c0a, c0b = a['cam'][0], b['cam'][0]
        eye_err.append(math.sqrt(sum((c0b['eye%d' % i] - c0a['eye%d' % i]) ** 2 for i in range(3))))
        at_err.append(math.sqrt(sum((c0b['at%d' % i] - c0a['at%d' % i]) ** 2 for i in range(3))))
        w, each = px_error(a, b)
        if w is not None:
            px.append(w)
            if 'at' in each: px_at.append(each['at'])
            if 'racer' in each: px_racer.append(each['racer'])

    def finish(recs):
        for r in recs:
            if r['state_flags'] & (1 << 25):
                return {'tick': r['tick'], 'race_time': r['race_time']}
        return None
    return {
        'ticks_compared': n, 'ticks': [len(ref), len(other)],
        'first_divergent_gameplay_decision': first_game,
        'first_divergent_camera_decision': first_cam,
        'gameplay_divergences': sum(1 for e in events if e['kind'] == 'gameplay'),
        'camera_divergences': sum(1 for e in events if e['kind'] == 'camera'),
        'finish': {'ref': finish(ref), 'other': finish(other)},
        'racer': {'along_track_units': stats(pos_along), 'lateral_units': stats(pos_lat),
                  'speed': stats(speed_err), 'energy': stats(energy_err), 'heading_deg': stats(heading_err)},
        'camera0': {'eye_units': stats(eye_err), 'at_units': stats(at_err), 'eye_q12_lsb_max': max(eye_err) * 4096 if eye_err else None,
                    'screen_px_worst_probe': stats(px), 'screen_px_at': stats(px_at), 'screen_px_racer': stats(px_racer)},
        'first_camera_events': [e for e in events if e['kind'] == 'camera'][:10],
        'first_gameplay_events': [e for e in events if e['kind'] == 'gameplay'][:10],
    }


def onetick(ref, one):
    n = min(len(ref), len(one))
    worst = {'eye': 0.0, 'at': 0.0, 'basis': 0.0}
    over = {'eye': 0, 'at': 0, 'basis': 0}
    discrete, marginal, fixed_ticks, fallback_ticks = [], [], 0, 0
    errs = {'eye': [], 'at': [], 'basis': []}
    for t in range(n):
        a, b = ref[t], one[t]
        for ci in range(min(2, a['camera_count'])):
            ca, cb = a['cam'][ci], b['cam'][ci]
            if cb['raw'][23] != 1:  # 2: the source float tick ran (transition), 0: not fixed
                fallback_ticks += 1
                continue
            fixed_ticks += 1
            raw = cb['raw']
            checks = [('eye', [ca['eye%d' % i] for i in range(3)], raw[0:3], 4096.0, 4),
                      ('at', [ca['at%d' % i] for i in range(3)], raw[3:6], 4096.0, 4),
                      ('basis', [ca['b%d' % i] for i in range(9)], raw[6:15], 2.0 ** 30, 8)]
            for name, rv, fv, scale, k in checks:
                for x, q in zip(rv, fv):
                    e = abs(q - x * scale)
                    allowed = k + ulp(x) * scale
                    errs[name].append(e)
                    if e - allowed > worst[name]:
                        worst[name] = e - allowed
                    if e > allowed:
                        over[name] += 1
            # Conditioning of basis.x = normalize(at - eye): the float reference's own error
            # (against float64 of its own eye/at) and the input difference propagate as
            # |d(at - eye)| / |at - eye|. Row x is checked against 8 LSB plus both terms.
            q = 2.0 ** 30
            re_, ra = [ca['eye%d' % i] for i in range(3)], [ca['at%d' % i] for i in range(3)]
            fe, fa = [v / 4096.0 for v in raw[0:3]], [v / 4096.0 for v in raw[3:6]]
            dref, dfix = sub(ra, re_), sub(fa, fe)
            nref, nfix = norm(dref), norm(dfix)
            if nref and nfix:
                lref = math.sqrt(dot(dref, dref))
                ref_self = max(abs(ca['b%d' % i] - nref[i]) for i in range(3)) * q
                fix_self = max(abs(raw[6 + i] / q - nfix[i]) for i in range(3)) * q
                dinput = math.sqrt(dot(sub(dfix, dref), sub(dfix, dref)))
                bound = 8 + ref_self + q * dinput / lref
                ex = max(abs(raw[6 + i] - ca['b%d' % i] * q) for i in range(3))
                errs.setdefault('basis_x', []).append(ex)
                errs.setdefault('ref_self_x', []).append(ref_self)
                errs.setdefault('fix_self_x', []).append(fix_self)
                errs.setdefault('cond_bound_x', []).append(bound)
                if ex > bound:
                    over['basis_x_conditioned'] = over.get('basis_x_conditioned', 0) + 1
                # FIX orthonormality (rows x, y, z) in LSB
                rows = [raw[6:9], raw[9:12], raw[12:15]]
                ortho = max([abs(sum(a * b for a, b in zip(rows[i], rows[j])) / q) for i, j in ((0, 1), (0, 2), (1, 2))] +
                            [abs(math.sqrt(sum(a * a for a in r)) - q) for r in rows])
                errs.setdefault('fix_orthonormal', []).append(ortho)
            for k in ('mode', 'setting', 'look_back', 'fov_flags', 'state', 'timer', 'type'):
                if ca[k] != cb[k]:
                    discrete.append({'tick': t, 'camera': ci, 'field': k, 'ref': ca[k], 'fix': cb[k]})
            for k, deg in (('pitch_index', 's_pitch'), ('yaw_index', 's_target_yaw' if ca['type'] == 4 else 's_yaw')):
                if ca[k] != cb[k]:
                    exact = ca[deg] * 4096 / 360.0
                    frac = abs(exact) - math.floor(abs(exact))
                    entry = {'tick': t, 'camera': ci, 'field': k, 'ref': ca[k], 'fix': cb[k],
                             'ref_index_exact': exact, 'distance_from_tie': abs(frac - 0.5)}
                    (marginal if abs(frac - 0.5) < 1e-3 else discrete).append(entry)
    return {'ticks': n, 'camera_ticks_fixed': fixed_ticks, 'camera_ticks_fallback': fallback_ticks,
            'eye_q12_lsb': stats(errs['eye']), 'at_q12_lsb': stats(errs['at']), 'basis_u30_lsb': stats(errs['basis']),
            'basis_x_u30_lsb': stats(errs.get('basis_x', [])), 'ref_self_error_x_lsb': stats(errs.get('ref_self_x', [])),
            'fix_self_error_x_lsb': stats(errs.get('fix_self_x', [])), 'conditioned_bound_x_lsb': stats(errs.get('cond_bound_x', [])),
            'fix_orthonormality_lsb': stats(errs.get('fix_orthonormal', [])),
            'over_tolerance': over, 'worst_excess_lsb': worst,
            'tolerance': 'eye/at <= 4 LSB Q20.12 + ulp(ref); basis <= 8 LSB Q2.30 + ulp(ref); discrete exact',
            'discrete_mismatches': len(discrete), 'first_discrete': discrete[:10],
            'marginal_angle_ties': len(marginal), 'marginal_cap_per_10k': 1, 'first_marginal': marginal[:10],
            'pass_literal': not any(over.values()) and not discrete and len(marginal) <= max(1, n // 10000),
            'pass_conditioned': not over['eye'] and not over['at'] and not over.get('basis_x_conditioned') and not discrete
                                and len(marginal) <= max(1, n // 10000)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('mode', choices=['race', 'onetick'])
    ap.add_argument('ref')
    ap.add_argument('other')
    ap.add_argument('--json')
    args = ap.parse_args()
    ref, other = load(args.ref), load(args.other)
    result = race(ref, other) if args.mode == 'race' else onetick(ref, other)
    text = json.dumps(result, indent=1, default=str)
    if args.json:
        open(args.json, 'w').write(text + '\n')
    print(text)


if __name__ == '__main__':
    main()
