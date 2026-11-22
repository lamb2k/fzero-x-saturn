#!/usr/bin/env python3
"""The Q16 retired-coast floor for the attacks judge (C-116, O-123), HOST. Reference-only rule, frozen before
any rescore (the freeze commit is recorded in the lane's hand-over). Used by tools/attacks_contact_judge.py.

C-116 item 1 (eligibility). Evaluate both racers in every window. A racer qualifies only if it is already
retired at the window start and coasting under damp() throughout, with no later contact, collision, state
transition or changed outcome; a native-only interaction fails eligibility too. Every other endpoint keeps
the existing band, and C-092's position, event, damage, KO identity and finish checks stay in force for
everyone. In this judge's words (eligibility_reasons; any reason makes the racer ineligible):
  retired-at-start   the reference flags at tick T hold the retired bit;
  ref-flags          the reference flags are identical on every tick T..end+1 (no state transition);
  ref-attack-spin    the reference attack and spin-out fields stay 0 on every tick;
  ref-pair           no reference pair touch, KO record or colliding bit in (T, end+1], and no tick where the
                     pair could be within reach (the closing and reach intervals of the judge's own ambiguity
                     test), so no contact can happen to either racer;
  ref-damage         no damage call on the racer in (T, end+1] (a wall or edge hit is a collision);
  native-retired     the native racer keeps the retired bit on every tick, with its flags constant;
  native-attack-spin the native attack and spin-out fields stay 0;
  native-pair        the native pair log shows no touch or push in (T, end+1] (a native-only interaction);
  native-events      the native event list holds no event of the racer, and no contact, in (T, end+1];
  ref-events         the same for the reference's events.
A missing tick in either trace is a reason too (nothing to judge).

C-116 item 2 (the floor, derived per eligible racer and window). q = 2^-16, k = 0.98305196 (the crashed
damping), R rounds each component to the nearest multiple of q, ties to even. From the window's first
tick with reference velocity r[0]: u[0] = R(r[0]); for each later reference tick
u[t+1] = R(r[t+1] + k*(u[t] - r[t])). The reference supplies the forcing; the model decays its own
accumulated error and rounds once per component per tick (applying the same rounding twice without a
transform between is idempotent, so there is no second rounding). At the endpoint F = |norm(u[end]) -
norm(r[end])|. The inclusive speed tolerance is max(q, 0.005*|reference speed|, F), used with the existing
speed comparison (|native speed - reference speed| <= tolerance). It is the discrepancy of the
reference-only ideal trajectory and grants no allowance for native bias. The three terms and the actual
error are published per racer and window.

The arithmetic is exact (fractions): the trace's decimal velocities are read as written and k as the
decimal 0.98305196, so a tie is a tie."""
import math
from fractions import Fraction

Q_FRAC = Fraction(1, 65536)
K_FRAC = Fraction('0.98305196')
Q = float(Q_FRAC)
SPEED_REL = 0.005
ST_RETIRED = 1 << 18
REACH2, REACH_ALONG = 2116.0, 92.0


def rq(x):
    """Round a Fraction to the nearest multiple of q, ties to even (Fraction rounding is half-even)."""
    return round(Fraction(x) / Q_FRAC) * Q_FRAC


def coast_model(r, k=K_FRAC):
    """r: reference velocities for ticks T..end, each a 3-sequence of strings/Fractions/ints. Returns u[T..end],
    the reference-only ideal Q16 trajectory, as tuples of Fractions."""
    rf = [tuple(Fraction(c) for c in v) for v in r]
    u = [tuple(rq(c) for c in rf[0])]
    for t in range(len(rf) - 1):
        u.append(tuple(rq(rf[t + 1][i] + k * (u[t][i] - rf[t][i])) for i in range(3)))
    return rf, u


def norm(v):
    return math.sqrt(sum(float(c) * float(c) for c in v))


def floor_term(r, k=K_FRAC):
    """F = |norm(u[end]) - norm(r[end])| for the reference velocities r (ticks T..end)."""
    rf, u = coast_model(r, k)
    return abs(norm(u[-1]) - norm(rf[-1]))


def tolerance(ref_speed, F=0.0):
    """The existing speed band, enlarged by the floor when F is given: max(q, 0.005*|reference speed|, F)."""
    return max(Q, SPEED_REL * abs(ref_speed), F)


def speed_terms(ref_speed, nat_speed, F=None):
    """The published terms of one racer's endpoint speed check. F None: the existing band (ineligible)."""
    t_q, t_rel = Q, SPEED_REL * abs(ref_speed)
    tol = tolerance(ref_speed, F or 0.0)
    err = abs(nat_speed - ref_speed)
    return dict(tol_q=t_q, tol_rel=t_rel, tol_floor=F, tol=tol, speed_err=err, ok=err <= tol)


# ---------------------------------------------------------------- eligibility
def _state(rows, t, k):
    r = rows[(t, k)]
    return dict(flags=int(r[13]), attack=int(r[14]), spin=int(r[15]))


def event_involves(key, k):
    """Whether an event key of the judge's vocabulary concerns racer k (a contact concerns both)."""
    if key[0] == 'contact':
        return True
    if key[0] == 'ko':
        return k in key[1:]
    return key[1] == k


def eligibility_reasons(k, t0, end, ref, nat):
    """Why racer k is not a retired coast in the window (T = t0, endpoint tick end); [] when it is.
    ref: dict(rows, pair_ticks, damage_ticks (per racer), events), nat: dict(rows, touch_ticks, events)."""
    why = []
    last = end + 1
    rr, nr = ref['rows'], nat['rows']
    ticks = range(t0, last + 1)
    if any((t, k) not in rr for t in range(t0, end + 1)) or any((t, k) not in nr for t in range(t0 + 1, end + 1)):
        return ['missing-tick']
    rs = [_state(rr, t, k) for t in ticks if (t, k) in rr]
    if not (rs[0]['flags'] & ST_RETIRED):
        why.append('retired-at-start')
    if len({s['flags'] for s in rs}) > 1:
        why.append('ref-flags')
    if any(s['attack'] or s['spin'] for s in rs):
        why.append('ref-attack-spin')
    if any(t0 < t <= last for t in ref['pair_ticks']):
        why.append('ref-pair')
    if any(t0 < t <= last for t in ref['damage_ticks'].get(k, ())):
        why.append('ref-damage')
    if any(t0 < t <= last and event_involves(key, k) for t, key, _ in ref['events']):
        why.append('ref-events')
    ns = [_state(nr, t, k) for t in range(t0 + 1, last + 1) if (t, k) in nr]
    if not all(s['flags'] & ST_RETIRED for s in ns) or len({s['flags'] for s in ns}) > 1:
        why.append('native-retired')
    if any(s['attack'] or s['spin'] for s in ns):
        why.append('native-attack-spin')
    if any(t0 < t <= last for t in nat['touch_ticks']):
        why.append('native-pair')
    if any(t0 < t <= last and event_involves(key, k) for t, key, _ in nat['events']):
        why.append('native-events')
    return why


def racer_floor(k, t0, end, ref, nat):
    """The racer's floor for the window: (reasons, F), with F None when the racer is not eligible."""
    why = eligibility_reasons(k, t0, end, ref, nat)
    if why:
        return why, None
    return [], floor_term([ref['rows'][(t, k)][5:8] for t in range(t0, end + 1)])


def revised_ok(pos_ok, ev_ok, speed_ok_revised, speed_diagnostic):
    """The revised window verdict: C-092's position and event checks in force, the speed check with the floor
    (or the window is speed-diagnostic, as in C-092)."""
    return bool(pos_ok and ev_ok and (speed_ok_revised or speed_diagnostic))
