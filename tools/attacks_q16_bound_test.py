#!/usr/bin/env python3
"""Fixtures and controls for the C-136 retired-coast bound (tools/attacks_q16_bound.py), HOST, no ROM. Frozen with the
specification before any native result of the C-136 fresh set was viewed. Prints PASS/FAIL per check, exit 1 on any FAIL.

  python3 tools/attacks_q16_bound_test.py [--probe PATH]     PATH: the compiled tools/attacks_host/damp_probe.c (optional)

Positives: the integer damp() model equals the library's compiled damp() (when the probe is given) on oblique, tube and
tie-heavy inputs; signed ties (R(2^29) = 1, R(-2^29) = 0, residuals in (-1/2, 1/2]); soundness of tiers R (every tick of
long coasts on axis-aligned, oblique, tube and curving bases, forcing in whole q so only the damping differs); the exact
Q16 start; an endpoint exactly on the bound passes (inclusive) and the next representable error above it fails.
Must-fail: a biased damp (truncation toward zero in every stage, and a constant bias per tick) whose accumulated error
exceeds the bound; a later touch, native-only interaction or any ineligibility makes the added term disappear.
Reported (not pass/fail): the spread of the real arithmetic against the bound (tightness), the smallest constant bias
per tick each tier detects on a 90-tick coast, and how much of the actual worst case an adversarial search reaches."""
import math
import random
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import attacks_q16_bound as QB  # noqa: E402
import attacks_q16_floor as QF  # noqa: E402

FAILS = []
Q = QB.Q


def check(name, cond, detail=''):
    print('%-4s %s %s' % ('PASS' if cond else 'FAIL', name, detail))
    if not cond:
        FAILS.append(name)


def rot_basis(rng, tube=False):
    """A random orthonormal basis (fwd, up, side), as floats; tube: up near a horizontal axis."""
    def unit(v):
        n = math.sqrt(sum(c * c for c in v)); return [c / n for c in v]
    up = unit([1.0, 0.1 * rng.random(), 0.05 * rng.random()]) if tube else unit([rng.uniform(-.3, .3), 1.0, rng.uniform(-.3, .3)])
    f = [rng.uniform(-1, 1) for _ in range(3)]
    d = sum(a * b for a, b in zip(f, up)); f = unit([a - d * b for a, b in zip(f, up)])
    s = [f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0]]
    return [f, up, s]


def rotate(rows, ang):
    """Rotate the basis about its forward axis by ang (the ground curving under the body)."""
    f, up, s = rows
    c, sn = math.cos(ang), math.sin(ang)
    return [f, [c * up[i] + sn * s[i] for i in range(3)], [c * s[i] - sn * up[i] for i in range(3)]]


def q30rows(rows):
    return [[QB.q30(c) for c in r] for r in rows]


def simulate(rng, ticks, tube=False, curve=0.0, speed=2000.0, forcing=3, native_step=None):
    """Reference (float64, the original's operation M with its binary32 gains) and native (integer damp) coasts with the same
    whole-q forcing. Returns (r, bases, u) with r, u in units."""
    rows = rot_basis(rng, tube)
    v = [rng.uniform(-1, 1) for _ in range(3)]
    n = math.sqrt(sum(c * c for c in v)); v = [c / n * speed * Q for c in v]
    r, u, bases = [v], [], [rows]
    u0 = [round(c / Q) for c in v]
    u.append(u0)
    ang = curve
    for _ in range(ticks):
        rows = rotate(rows, ang) if ang else rows
        bases.append(rows)
        m = QB.matM(rows, QB.G_REF)
        f = [rng.randint(-forcing, forcing) for _ in range(3)]
        rv = [sum(m[i][j] * r[-1][j] for j in range(3)) + f[i] * Q for i in range(3)]
        r.append(rv)
        step = native_step or (lambda B, vel: QB.damp_int(B[0], B[1], B[2], vel))
        nv = step(q30rows(rows), u[-1])
        u.append([nv[i] + f[i] for i in range(3)])
    return r, bases, [[c * Q for c in x] for x in u]


# ---------------------------------------------------------------- positives
def test_model_vs_c(probe):
    rng = random.Random(136)
    lines, want = [], []
    for k in range(3000):
        rows = q30rows(rot_basis(rng, tube=(k % 3 == 0)))
        v = [rng.randint(-3_000_000, 3_000_000) if k % 5 else rng.randint(-40, 40) for _ in range(3)]
        lines.append(' '.join(map(str, rows[0] + rows[1] + rows[2] + v + [QB.KS_Q30, QB.KU_Q30, QB.KS_Q30, 1])))
        want.append(QB.damp_int(rows[0], rows[1], rows[2], v))
    got = subprocess.run([probe], input='\n'.join(lines) + '\n', capture_output=True, text=True).stdout.split('\n')
    got = [tuple(int(x) for x in g.split()) for g in got if g.strip()]
    bad = sum(1 for a, b in zip(got, want) if a != b)
    check('damp-model-equals-library', len(got) == len(want) and bad == 0, '(%d inputs, %d differ)' % (len(want), bad))
    # the model must be able to differ: round-half-away instead of ties-up changes tie inputs
    ties = QB.damp_int([1 << 30, 0, 0], [0, 1 << 30, 0], [0, 0, 1 << 30], [1, 1, 1])
    check('tie-case-in-library-rule', QB.r30(1 << 29) == 1 and QB.r30(-(1 << 29)) == 0 and QB.r30(-(1 << 29) - 1) == -1,
          'R(2^29)=1 R(-2^29)=0 R(-2^29-1)=-1 ' + str(ties))


def test_residual_range():
    rng = random.Random(7)
    worst = 0.0
    for _ in range(200000):
        x = rng.randint(-(1 << 40), 1 << 40)
        res = QB.r30(x) - x / 2.0 ** 30
        worst = max(worst, abs(res))
        assert -0.5 < res <= 0.5, (x, res)
    check('residual-in-half-open-interval', worst <= 0.5, '(max |residual| %.6f q)' % worst)


def test_soundness():
    rng = random.Random(1361)
    cases = [('axis-aligned', dict(), 91), ('oblique', dict(), 91), ('tube', dict(tube=True), 91), ('curving', dict(curve=2e-3), 91),
             ('long', dict(), 400), ('slow', dict(speed=40.0), 91), ('fast', dict(speed=3.0e5), 91)]
    ratios = []
    for name, kw, ticks in cases:
        worst_ratio, violated = 0.0, 0
        for k in range(60):
            r, bases, u = simulate(rng, ticks, **kw)
            if name == 'axis-aligned':
                bases[:] = [[[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]] for _ in bases]
                r, _b, u = _axis(rng, ticks)
            b = QB.coast_bound(r, bases)
            for t in range(len(r)):
                e = math.dist(r[t], u[t])
                if e > b['E_path'][t] * (1 + 1e-12):
                    violated += 1
                worst_ratio = max(worst_ratio, e / b['E_path'][t])
        ratios.append((name, worst_ratio))
        check('bound-sound-%s' % name, violated == 0, '(60 coasts of %d ticks, worst actual/bound %.3f)' % (ticks, worst_ratio))
    return ratios


def _axis(rng, ticks):
    rows = [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]
    v = [rng.uniform(-1, 1) * 2000 * Q for _ in range(3)]
    r, u = [v], [[round(c / Q) for c in v]]
    for _ in range(ticks):
        m = QB.matM(rows, QB.G_REF)
        f = [rng.randint(-3, 3) for _ in range(3)]
        r.append([sum(m[i][j] * r[-1][j] for j in range(3)) + f[i] * Q for i in range(3)])
        nv = QB.damp_int(*q30rows(rows), u[-1])
        u.append([nv[i] + f[i] for i in range(3)])
    return r, None, [[c * Q for c in x] for x in u]


def test_start_and_inclusive():
    r = [[1.0, -2.0, 0.5]] * 3
    b = [[[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]] * 3
    res = QB.coast_bound(r, b)
    check('exact-q16-start-term', res['E_path'][0] == math.sqrt(3.0) / 2.0 * Q + math.sqrt(3.0) * QB.DEC, '(E0 %.3f q)' % (res['E_path'][0] / Q))
    sp = 2.3
    tol = QB.tolerance(sp, res['added_R'])
    check('on-the-bound-passes', abs((sp + tol) - sp) <= tol)
    nxt = math.nextafter(tol, math.inf)
    check('next-representable-above-fails', not (nxt <= tol))
    check('band-kept', QB.tolerance(sp, 0.0) == max(Q, 0.005 * sp))


# ---------------------------------------------------------------- must-fail and reports
def trunc_damp(B, vel):
    """damp() with truncation toward zero in every stage instead of round-half-up (the arithmetic fault)."""
    tz = lambda x: x >> 30 if x >= 0 else -((-x) >> 30)
    fwd, up, side = B
    dot = lambda b: tz(b[0] * vel[0] + b[1] * vel[1] + b[2] * vel[2])
    s, uu, f = tz(dot(side) * QB.KS_Q30), tz(dot(up) * QB.KU_Q30), tz(dot(fwd) * QB.KS_Q30)
    return tuple(tz(s * side[i]) + tz(uu * up[i]) + tz(f * fwd[i]) for i in range(3))


def biased_damp(bias):
    def step(B, vel):
        v = QB.damp_int(B[0], B[1], B[2], vel)
        return tuple(c - (bias if c > 0 else -bias if c < 0 else 0) for c in v)
    return step


def endpoint_fail(rng, ticks, native_step, n=40, speed=2000.0, tier='added_R'):
    fails = 0
    for _ in range(n):
        r, bases, u = simulate(rng, ticks, speed=speed, native_step=native_step)
        b = QB.coast_bound(r, bases)
        err = abs(QB.vnorm(u[-1]) - QB.vnorm(r[-1]))
        if err > QB.tolerance(QB.vnorm(r[-1]), b[tier]):
            fails += 1
    return fails, n


def test_must_fail():
    rng = random.Random(1362)
    f, n = endpoint_fail(rng, 90, biased_damp(12), tier='added_R')
    check('biased-damp-beyond-bound-fails-R', f > 0, '(12 q a tick toward zero: %d of %d coasts fail tier R)' % (f, n))
    rng = random.Random(1362)
    f0, n = endpoint_fail(rng, 90, None, tier='added_R')
    check('unbiased-never-fails-R', f0 == 0, '(%d of %d)' % (f0, n))
    rng = random.Random(1362)
    f0s, n = endpoint_fail(rng, 90, None, tier='added_S')
    print('INFO unbiased coasts failing tier S: %d of %d (alpha 1e-4 per coast)' % (f0s, n))
    # eligibility: a later touch removes the allowance
    ref = dict(rows={(t, 0): ['0'] * 13 + [str(1 << 18)] + ['0'] * 6 for t in range(0, 12)}, pair_ticks=[], damage_ticks={0: []}, events=[])
    nat = dict(rows={(t, 0): ['0'] * 13 + [str(1 << 18)] + ['0'] * 6 for t in range(0, 12)}, touch_ticks=[], events=[])
    check('eligible-without-touch', QF.eligibility_reasons(0, 0, 10, ref, nat) == [])
    nat2 = dict(nat, touch_ticks=[7])
    why = QF.eligibility_reasons(0, 0, 10, ref, nat2)
    check('later-touch-removes-allowance', 'native-pair' in why, str(why))


def detect_bias(tier):
    """Smallest integer constant bias (q a tick toward zero) failing at least half of 30 coasts of 90 ticks."""
    for bias in range(1, 80):
        f, n = endpoint_fail(random.Random(1363), 90, biased_damp(bias), n=30, tier=tier)
        if f * 2 >= n:
            return bias
    return None


def adversary(rng, ticks=90, tries=400):
    """Hill-climb the start velocity and the whole-q forcing to maximise the final error against the exact real coast."""
    best, bestr = 0.0, None
    rows = q30rows(rot_basis(rng))
    fr = [[rows[0][i] / 2.0 ** 30 for i in range(3)], [rows[1][i] / 2.0 ** 30 for i in range(3)], [rows[2][i] / 2.0 ** 30 for i in range(3)]]
    for _ in range(tries):
        v0 = [rng.randint(-4000, 4000) for _ in range(3)]
        f = [[rng.randint(-2, 2) for _ in range(3)] for _ in range(ticks)]
        m = QB.matM(fr, QB.G_REF)
        r = [[c * Q for c in v0]]; u = [list(v0)]
        for t in range(ticks):
            r.append([sum(m[i][j] * r[-1][j] for j in range(3)) + f[t][i] * Q for i in range(3)])
            nv = QB.damp_int(rows[0], rows[1], rows[2], u[-1]); u.append([nv[i] + f[t][i] for i in range(3)])
        e = math.dist(r[-1], [c * Q for c in u[-1]]) / Q
        if e > best:
            best = e
    b = QB.coast_bound([r_ for r_ in r], [fr] * len(r))
    return best, b['E_R'] / Q


def main():
    probe = sys.argv[sys.argv.index('--probe') + 1] if '--probe' in sys.argv else None
    if probe:
        test_model_vs_c(probe)
    test_residual_range()
    ratios = test_soundness()
    test_start_and_inclusive()
    test_must_fail()
    best, bound = adversary(random.Random(99))
    print('INFO adversarial search (400 starts/forcings): worst actual end error %.2f q of tier R %.1f q (ratio %.3f)' % (best, bound, best / bound))
    rng = random.Random(5)
    r, bases, u = simulate(rng, 90)
    b = QB.coast_bound(r, bases)
    print('INFO 90-tick coast: tier R E=%.2f q, added R %.2f q, tier S lambda %.2f q, added S %.2f q, nu %.9f' % (
        b['E_R'] / Q, b['added_R'] / Q, b['lam'] / Q, b['added_S'] / Q, b['nu_max']))
    print('INFO smallest constant bias detected (half of coasts fail): tier R %s q/tick, tier S %s q/tick' % (detect_bias('added_R'), detect_bias('added_S')))
    f, n = endpoint_fail(random.Random(1364), 90, trunc_damp, tier='added_R')
    fs, _ = endpoint_fail(random.Random(1364), 90, trunc_damp, tier='added_S')
    print('INFO truncation-toward-zero damp: %d of %d coasts fail tier R, %d tier S' % (f, n, fs))
    f1, n = endpoint_fail(random.Random(1365), 90, biased_damp(1), tier='added_R')
    f1s, _ = endpoint_fail(random.Random(1365), 90, biased_damp(1), tier='added_S')
    print('INFO 1 q a tick toward zero (the judge coast-bias control): %d of %d fail tier R, %d tier S' % (f1, n, f1s))
    print('tests: %d failing' % len(FAILS))
    sys.exit(1 if FAILS else 0)


if __name__ == '__main__':
    main()
