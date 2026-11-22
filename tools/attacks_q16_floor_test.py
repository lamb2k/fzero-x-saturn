#!/usr/bin/env python3
"""Fixtures and controls for the C-116 Q16 retired-coast floor (tools/attacks_q16_floor.py), HOST, no ROM.
Frozen with the rule before any rescore. Every "must fail" case is asserted to fail.

Usage: python3 tools/attacks_q16_floor_test.py        (prints PASS/FAIL per check; exit 1 on any FAIL)

Positives: the rounding (exact Q16 inputs, half-quantum ties both ways, negative ties), the recurrence against an
independent integer oracle on 300 seeded series, the floor equal to the ideal trajectory's own discrepancy, an
endpoint exactly on the enlarged bound (passes, inclusive).
Must-fail: the next representable error above the bound; a biased damp (truncation toward zero, and a constant
offset) accumulating beyond the bound; a later touch, a native-only interaction or any other reason making the
racer ineligible (the allowance must disappear and the old band apply); an event changed by a reach/closing flip
(the revised verdict must fail even where the floor applies); a position miss.
The existing band must be unchanged for an ineligible racer (equal to the judge's speed_tol)."""
import math
import random
import sys
from fractions import Fraction
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import attacks_q16_floor as QF  # noqa: E402
import attacks_contact_judge as CJ  # noqa: E402

FAILS = []
Q = QF.Q_FRAC


def check(name, cond, detail=''):
    print('%-4s %s %s' % ('PASS' if cond else 'FAIL', name, detail))
    if not cond:
        FAILS.append(name)


# ---------------------------------------------------------------- an independent integer oracle
def oracle(a, kint=98305196):
    """a: reference velocities as integers in units of 1e-6, ticks T..end, 3 components. Returns u in units of q.
    x = r1 + k*(u - r0), k = kint/1e8; all exact in integers, rounded half to even by divmod."""
    def rnd(num, den):
        q, rem = divmod(num, den)
        if 2 * rem > den or (2 * rem == den and q & 1):
            q += 1
        return q
    u = [tuple(rnd(c * 65536, 10 ** 6) for c in a[0])]
    for t in range(len(a) - 1):
        # x in q units = r1*65536 + k*(u*1e6 - r0*65536)*... over the common denominator 1e14
        u.append(tuple(rnd(a[t + 1][i] * 65536 * 10 ** 8 + kint * (u[t][i] * 10 ** 6 - a[t][i] * 65536), 10 ** 14) for i in range(3)))
    return u


def series(rng, ticks=91, scale=3000, forcing=40):
    """A decaying velocity series with a little forcing, in 1e-6 units."""
    v = [rng.randint(-scale, scale) for _ in range(3)]
    out = [tuple(v)]
    for _ in range(ticks - 1):
        v = [round(0.98305196 * c) + rng.randint(-forcing, forcing) for c in v]
        out.append(tuple(v))
    return out


def as_str(a):
    return [tuple('%d.%06d' % (c // 10 ** 6, c % 10 ** 6) if c >= 0 else '-%d.%06d' % ((-c) // 10 ** 6, (-c) % 10 ** 6) for c in v) for v in a]


# ---------------------------------------------------------------- rounding: exact inputs and ties
for n in (0, 1, -1, 7, -300, 65536):
    check('R exact multiple %d q' % n, QF.rq(n * Q) == n * Q)
for num, want in ((1, 0), (3, 2), (5, 2), (7, 4), (-1, 0), (-3, -2), (-5, -2), (-7, -4)):
    check('R tie %d/2 q -> %d q (ties to even)' % (num, want), QF.rq(Fraction(num, 2) * Q) == want * Q)
check('R just above a tie rounds up', QF.rq((Fraction(1, 2) + Fraction(1, 10 ** 9)) * Q) == Q)
check('R just below a tie rounds down', QF.rq((Fraction(1, 2) - Fraction(1, 10 ** 9)) * Q) == 0)

# exact Q16 inputs: reference velocities that are exact multiples of q give u == r and F == 0
exact = [(Fraction(n, 65536), Fraction(-n, 65536), 0) for n in (900, 886, 871, 856, 842)]
rf, u = QF.coast_model(exact)
check('exact Q16 inputs: u equals r', all(x == y for x, y in zip(rf, u)))
check('exact Q16 inputs: F is 0', QF.floor_term(exact) == 0.0)

# a tie inside the recurrence: r[1] chosen so that r[1] + k*(u[0] - r[0]) is exactly (n + 1/2) q
r0 = Fraction(1, 1000)                                   # u[0] = R(r0), not a multiple of q
u0 = QF.rq(r0)
for n in (10, 11, -10, -11):
    r1 = (n + Fraction(1, 2)) * Q - QF.K_FRAC * (u0 - r0)
    rr, uu = QF.coast_model([(r0, 0, 0), (r1, 0, 0)])
    want = n * Q if n % 2 == 0 else (n + 1) * Q          # ties to even in q units (n and n + 1: the even one)
    check('recurrence tie at (%d + 1/2) q -> %d q' % (n, round(want / Q)), uu[1][0] == want, '(got %d q)' % round(uu[1][0] / Q))
check('u[0] ties: R(3/2 q) = 2 q, R(5/2 q) = 2 q',
      QF.coast_model([(Fraction(3, 2) * Q,) * 3])[1][0][0] == 2 * Q and QF.coast_model([(Fraction(5, 2) * Q,) * 3])[1][0][0] == 2 * Q)

# the recurrence against the independent integer oracle (seeded random series, both signs, small and large)
rng = random.Random(116)
bad = 0
for i in range(300):
    a = series(rng, ticks=rng.choice((2, 5, 31, 91)), scale=rng.choice((3000, 100000, 20000000)), forcing=rng.choice((0, 40, 5000)))
    _, u = QF.coast_model(as_str(a))
    if [tuple(int(c / Q) for c in x) for x in u] != oracle(a):
        bad += 1
check('recurrence equals the integer oracle on 300 series', bad == 0, '(%d differ)' % bad)

# ---------------------------------------------------------------- the floor and the tolerance
def pick_floor_series():
    """A seeded series whose floor F is the largest of the three tolerance terms."""
    for seed in range(1000, 2000):
        a = series(random.Random(seed), scale=1800, forcing=30)
        r = as_str(a)
        F = QF.floor_term(r)
        speed = QF.norm([Fraction(c) for c in r[-1]])
        if F > 1.5 * max(QF.Q, 0.005 * speed):
            return seed, r, F, speed
    raise SystemExit('no fixture series found')


seed, R, F, SPEED = pick_floor_series()
check('fixture series has F above the band', F > QF.tolerance(SPEED), '(seed %d, F %.3g q, band %.3g q)' % (seed, F / QF.Q, QF.tolerance(SPEED) / QF.Q))
check('tolerance is max(q, 0.005 |ref|, F)', QF.tolerance(SPEED, F) == max(QF.Q, 0.005 * SPEED, F) == F)
check('tolerance without a floor is the judge\'s existing band',
      all(QF.tolerance(s) == CJ.speed_tol(s) for s in (0.0, 1e-4, 0.3, 17.5, 140.0, -2.0)))
check('ineligible terms equal the existing band', QF.speed_terms(SPEED, SPEED + 1e-3, None)['tol'] == CJ.speed_tol(SPEED))

# ideal native: its endpoint speed is norm(u[end]); the error is F and the inclusive comparison passes
rf, u = QF.coast_model(R)
ideal = QF.norm(u[-1])
terms = QF.speed_terms(SPEED, ideal, F)
check('the ideal Q16 trajectory passes (error equals F)', terms['ok'] and abs(terms['speed_err'] - F) < 1e-15,
      '(err %.9g, tol %.9g)' % (terms['speed_err'], terms['tol']))
terms_old = QF.speed_terms(SPEED, ideal, None)
check('the same endpoint fails the old band (the floor is what passes it)', not terms_old['ok'])

# an endpoint exactly on the enlarged bound passes; the next representable error above it fails
def native_with_error(target):
    """The nearest native speed whose error against SPEED is exactly target (scanning a few ulps)."""
    for sign in (1, -1):
        x = SPEED + sign * target
        for _ in range(16):
            if abs(x - SPEED) == target:
                return x
            x = math.nextafter(x, SPEED + sign * 10)
        x = SPEED + sign * target
        for _ in range(16):
            if abs(x - SPEED) == target:
                return x
            x = math.nextafter(x, SPEED - sign * 10)
    return None


tol = QF.tolerance(SPEED, F)
x_on = native_with_error(tol)
check('an endpoint exactly on the bound exists in the fixture', x_on is not None)
check('exactly on the enlarged bound: passes (inclusive)', QF.speed_terms(SPEED, x_on, F)['ok'])
above = math.nextafter(tol, math.inf)
x_above = native_with_error(above)
if x_above is None:   # the next representable error may not be reachable from SPEED: step the native speed until the error exceeds
    x_above = x_on
    for _ in range(64):
        x_above = math.nextafter(x_above, math.inf if x_on > SPEED else -math.inf)
        if abs(x_above - SPEED) > tol:
            break
check('the next representable error above the bound: FAILS', not QF.speed_terms(SPEED, x_above, F)['ok'],
      '(error %.17g, bound %.17g)' % (abs(x_above - SPEED), tol))

# a biased damp accumulating beyond the bound must fail: same forcing, same recurrence, other rounding
def biased(r, rounder):
    rf = [tuple(Fraction(c) for c in v) for v in r]
    u = [tuple(rounder(c) for c in rf[0])]
    for t in range(len(rf) - 1):
        u.append(tuple(rounder(rf[t + 1][i] + QF.K_FRAC * (u[t][i] - rf[t][i])) for i in range(3)))
    return u


def trunc_q(x):
    return math.trunc(x / Q) * Q


def offset_q(x):   # nearest, plus a constant quarter quantum
    return QF.rq(x) + Q / 4


quiet = as_str([tuple(round(c * 0.98305196 ** t) for c in (6000, 4000, -6000)) for t in range(91)])   # a pure coast, speed 0.0094 -> 0.0019
sp_q = QF.norm([Fraction(c) for c in quiet[-1]])
Fq = QF.floor_term(quiet)
for name, rnd in (('truncation toward zero', trunc_q), ('a constant quarter-quantum offset', offset_q)):
    nb = QF.norm(biased(quiet, rnd)[-1])
    t = QF.speed_terms(sp_q, nb, Fq)
    check('biased damp (%s) accumulates beyond the bound and FAILS' % name, not t['ok'],
          '(error %.3g q, bound %.3g q: floor %.3g q)' % (t['speed_err'] / QF.Q, t['tol'] / QF.Q, Fq / QF.Q))
nb = QF.norm(biased(quiet, QF.rq)[-1])
check('the unbiased ideal rounding on the same series passes', QF.speed_terms(sp_q, nb, Fq)['ok'])


# ---------------------------------------------------------------- eligibility
T0, END = 100, 190
RET = 1 << 18
BASEFLAGS = 0x48000000 | RET


def row(t, k, flags=BASEFLAGS, attack=0, spin=0, vel=None):
    v = vel or ('0.01%04d' % (t - T0), '0.000000', '0.000000')
    return [str(t), str(k), '0', '0', '0', v[0], v[1], v[2], v[0], '0.000000', '0.0', '0.0', '0.0', str(flags), str(attack), str(spin), '0', '0', '-1', '0.0']


def make(vel_rows=None):
    rr, nr = {}, {}
    for t in range(T0, END + 3):
        for k in (0, 1):
            vel = (vel_rows[t - T0] if vel_rows and t - T0 < len(vel_rows) else None)
            rr[(t, k)] = row(t, k, vel=vel)
            nr[(t, k)] = row(t, k, vel=vel)
    ref = dict(rows=rr, pair_ticks=[], damage_ticks={0: [], 1: []}, events=[])
    nat = dict(rows=nr, touch_ticks=[], events=[])
    return ref, nat


def variants():
    """name -> (mutator, the reason it must produce). Each makes racer 1 (and, for pair-wide ones, racer 0) ineligible."""
    def f(c):
        return c
    out = {}
    out['not retired at the start'] = (lambda ref, nat: ref['rows'].update({(T0, 1): row(T0, 1, flags=0x48000000)}), 'retired-at-start', (1,))
    out['reference flags change mid-window'] = (lambda ref, nat: ref['rows'].update({(150, 1): row(150, 1, flags=BASEFLAGS | 1)}), 'ref-flags', (1,))
    out['reference attack field'] = (lambda ref, nat: ref['rows'].update({(150, 1): row(150, 1, attack=1)}), 'ref-attack-spin', (1,))
    out['reference spin-out field'] = (lambda ref, nat: ref['rows'].update({(150, 1): row(150, 1, spin=5)}), 'ref-attack-spin', (1,))
    out['later reference pair touch'] = (lambda ref, nat: ref['pair_ticks'].append(185), 'ref-pair', (0, 1))
    out['pair touch on the last scored tick + 1'] = (lambda ref, nat: ref['pair_ticks'].append(END + 1), 'ref-pair', (0, 1))
    out['reference damage call on the racer (a wall)'] = (lambda ref, nat: ref['damage_ticks'][1].append(120), 'ref-damage', (1,))
    out['reference event of the racer'] = (lambda ref, nat: ref['events'].append((130, ('damage', 1), (0.5,))), 'ref-events', (1,))
    out['reference KO of the racer'] = (lambda ref, nat: ref['events'].append((130, ('ko', 1, 0), None)), 'ref-events', (0, 1))
    out['native loses the retired bit'] = (lambda ref, nat: nat['rows'].update({(160, 1): row(160, 1, flags=0x48000000)}), 'native-retired', (1,))
    out['native flags change'] = (lambda ref, nat: nat['rows'].update({(160, 1): row(160, 1, flags=BASEFLAGS | 2)}), 'native-retired', (1,))
    out['native attack'] = (lambda ref, nat: nat['rows'].update({(160, 1): row(160, 1, attack=2)}), 'native-attack-spin', (1,))
    out['native-only touch (a later touch injected)'] = (lambda ref, nat: nat['touch_ticks'].append(170), 'native-pair', (0, 1))
    out['native-only contact event'] = (lambda ref, nat: nat['events'].append((170, ('contact',), (0.0, 0.0))), 'native-events', (0, 1))
    out['native-only damage event on the racer'] = (lambda ref, nat: nat['events'].append((170, ('damage', 1), (0.4,))), 'native-events', (1,))
    out['missing reference tick'] = (lambda ref, nat: ref['rows'].pop((150, 1)), 'missing-tick', (1,))
    out['missing native tick'] = (lambda ref, nat: nat['rows'].pop((END, 0)), 'missing-tick', (0,))
    return out


ref, nat = make()
check('eligibility: a clean retired coast qualifies (both racers)',
      QF.eligibility_reasons(0, T0, END, ref, nat) == [] and QF.eligibility_reasons(1, T0, END, ref, nat) == [])
for name, (mut, reason, racers) in variants().items():
    ref, nat = make()
    mut(ref, nat)
    for k in racers:
        why = QF.eligibility_reasons(k, T0, END, ref, nat)
        check('must fail eligibility: %s (racer %d)' % (name, k), reason in why, '(%s)' % why)
# things that must not disqualify
for name, mut in (('damage call on the other racer only', lambda ref, nat: ref['damage_ticks'][0].append(120)),
                  ('an event at the window start tick', lambda ref, nat: ref['events'].append((T0, ('contact',), (0.0, 0.0)))),
                  ('an event two ticks past the window', lambda ref, nat: ref['events'].append((END + 2, ('contact',), (0.0, 0.0)))),
                  ('a native touch at the window start tick', lambda ref, nat: nat['touch_ticks'].append(T0)),
                  ('the other racer\'s own damage event', lambda ref, nat: ref['events'].append((130, ('damage', 0), (0.5,))))):
    ref, nat = make()
    mut(ref, nat)
    check('does not disqualify: %s' % name, QF.eligibility_reasons(1, T0, END, ref, nat) == [])

# the allowance disappears with a later touch: floor present, then gone, and the old band applies
a = series(random.Random(seed), scale=1800, forcing=30)
vel = as_str(a)
ref, nat = make(vel_rows=vel)
why, Fe = QF.racer_floor(1, T0, END, ref, nat)
check('a clean coast gets the floor', not why and Fe is not None and Fe > 0, '(F %.3g q)' % ((Fe or 0) / QF.Q))
speed = QF.norm([Fraction(c) for c in vel[END - T0]])
x = native_with_error(QF.tolerance(speed, Fe))
check('endpoint on the enlarged bound passes through racer_floor', x is not None and QF.speed_terms(speed, x, Fe)['ok'])
nat['touch_ticks'].append(170)   # the later touch, native only
why2, F2 = QF.racer_floor(1, T0, END, ref, nat)
check('inject a later touch: the coast allowance disappears', F2 is None and 'native-pair' in why2, '(%s)' % why2)
check('inject a later touch: the same endpoint now FAILS the old band', x is not None and not QF.speed_terms(speed, x, F2)['ok'])
ref, nat = make(vel_rows=vel)
ref['pair_ticks'].append(175)
why3, F3 = QF.racer_floor(1, T0, END, ref, nat)
check('inject a later reference touch: the allowance disappears', F3 is None and 'ref-pair' in why3)

# ---------------------------------------------------------------- the revised verdict keeps C-092's checks in force
gr = [(120, [(('contact',), (0.0, 0.0))])]
ev_ok_flip, why_flip = CJ.match_groups(gr, [], END)   # a reach/closing flip: the reference's contact is not in the native window
check('a flipped contact event fails the event match', not ev_ok_flip, '(%s)' % (why_flip,))
check('revised verdict FAILS when a flip changed an event, even where the floor passes the speed',
      not QF.revised_ok(True, ev_ok_flip, True, False))
check('revised verdict FAILS on a position miss, even where the floor passes the speed', not QF.revised_ok(False, True, True, False))
check('revised verdict passes with position, events and the floor speed', QF.revised_ok(True, True, True, False))
check('revised verdict fails a speed miss on an ineligible racer (the old band)', not QF.revised_ok(True, True, False, False))
check('revised verdict keeps the speed-diagnostic exemption of C-092', QF.revised_ok(True, True, False, True))
ev_ok_same, _ = CJ.match_groups(gr, [(121, [(('contact',), (0.0, 0.0))])], END)
check('an event one tick late still matches (C-092 unchanged)', ev_ok_same)

print('%d failing checks %s' % (len(FAILS), FAILS))
sys.exit(1 if FAILS else 0)
