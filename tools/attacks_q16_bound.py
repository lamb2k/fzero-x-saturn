#!/usr/bin/env python3
"""Prospective error bound for the Q16 retired coast (C-136 item 2), HOST, reference-only. Used by
tools/attacks_contact_judge.py as an additional score beside strict, C-092 and C-116; tools/attacks_q16_bound_test.py
holds the fixtures. SPECIFICATION (frozen before any native result of the C-136 fresh set was viewed):

The coast operation. A crashed (retired) body, damping only, in the library (sh2zam_vehicle_dynamics.c damp(), vehicle-dynamics-3)
and in the original (racer.c, trueBasis, the crashed branch):
    s = mu(dotu(side, v), ks);  u = mu(dotu(up, v), ku);  f = mu(dotu(fwd, v), kf)
    v'[i] = mu(s, side[i]) + mu(u, up[i]) + mu(f, fwd[i])
with ks = kf = 0.98305196 and ku = 0.94 (Q30), basis rows Q30, v Q16, dotu = R(sum / 2^30), mu(a, g) = R(a g / 2^30),
R = round to nearest with ties toward +infinity (vd_r64: floor((x + 2^(s-1)) / 2^s)), so every rounding residual lies
in (-1/2, +1/2] q and ties are positive. Fifteen roundings a tick: 3 dots, 3 gains, 9 back-projection terms (3 a component).
The reference applies the same linear map M = sum_a g_a b_a b_a^T in binary32 to its own basis (rows fwd, up, side).

Error recursion (q = 2^-16 velocity units, all quantities positive reals). e[t] = u[t] - r[t] with r the reference velocity
and u the native velocity, forcing held fixed (anything the native does besides this damping is NOT allowed for):
    ||e[t+1]|| <= nu[t] ||e[t]|| + b[t] + c[t] (||r[t]|| + ||e[t]||) + h[t] ||r[t]||
  nu[t]  = max over the two reference bases of the tick (after tick t, after tick t+1) of the spectral norm of M (3x3 Jacobi)
  b[t]   = 1.5 sqrt(3) + sqrt(sum_a ((1 + g_a)/2)^2) * ||B||   worst-case box norm of the 15 rounding residuals
           (9 back-projection residuals 3 a component each <= 1/2; for each axis the gain residual <= 1/2 plus g_a times
           the dot residual <= 1/2 carried through the basis)
  c[t]   = spectral norm of M(B[t+1]) - M(B[t]) (the basis the damp used lies between the two bakes)
           + sum_a |dg_a| (gain: float32 of the decimal against the Q30 of the decimal, + 2^-31)
           + 3 * 2 * sqrt(3) 2^-31 * g_max (basis Q30 quantisation)
  h[t]   = 16 2^-24 (the reference's own binary32 evaluation, 16 flops, relative)
  E[0]   = sqrt(3)/2 + sqrt(3) 5e-7/q (the native start rounds the reference velocity to Q16; the trace prints 6 decimals)
Range and saturation: every |component| and every intermediate product must stay under 2^30 q; asserted per tick.
Endpoint: |native speed - reference speed| <= E[N] + sqrt(3) 5e-7/q + 2^-23 ||r[N]|| (print and binary32 sqrt)
          + 1 q (the library's integer vlen). The sum is the ADDED TERM, published separately; the ordinary inclusive band
          max(q, 0.005 |reference speed|) is kept and the tolerance is band + added term (C-136 item 2: the band is kept).
Tier R (rigorous, deterministic): the above, valid for every rounding realisation.
Tier S (assumption-based, offered because tier R is far wider than the arithmetic's own spread, see the test): the 15
residuals are conditionally centred and independent of the past, each in (-1/2, 1/2]; then (Hoeffding) each coordinate of the endpoint error, in any fixed orthonormal frame, has proxy variance S^2 = 1.2416 sum_t (prod_{s>t} nu[s])^2
and exceeds lam = S sqrt(2 ln(6/alpha)) with probability <= alpha/3 (alpha = 1e-4, union over three directions); the speed error is at most
lam + (2 lam^2) / (2 (||r|| - sqrt(3) lam)) (the parallel part plus the perpendicular part's second-order term).
Tier S is NOT a proof: its assumption is the one C-136 says does not follow from the operation count; the signed-residual
diagnostic (residual_stats) is the check that must hold for it.
Everything here is reference-only (it reads the reference's velocities and bases) except where a function says otherwise."""
import math
import struct

Q = 2.0 ** -16
U31 = 2.0 ** -31
DEC = 5e-7                    # the trace prints velocities with 6 decimals
HALF = 1 << 29                # R(x) = (x + 2^29) >> 30 : round to nearest, ties toward +infinity
FOUR = 4


def f32(x):
    return struct.unpack('f', struct.pack('f', x))[0]


def q30(x):
    """Q30 of a decimal, rounded to nearest, ties away from zero (the pack's fx())."""
    y = x * 1073741824.0
    return int(math.floor(y + 0.5)) if y >= 0 else -int(math.floor(-y + 0.5))


KS_DEC, KU_DEC = 0.98305196, 0.94
KS_Q30, KU_Q30 = q30(KS_DEC), q30(KU_DEC)
G_REF = (f32(KS_DEC), f32(KU_DEC), f32(KS_DEC))        # the original's gains, (side, up, fwd), binary32
G_NAT = (KS_Q30 / 2.0 ** 30, KU_Q30 / 2.0 ** 30, KS_Q30 / 2.0 ** 30)


# ---------------------------------------------------------------- the library's damp(), exact integers
def r30(x):
    return (x + HALF) >> 30


def damp_int(fwd, up, side, v, ks=KS_Q30, ku=KU_Q30, kf=KS_Q30):
    """One damp() of a crashed body. fwd/up/side: Q30 integer rows (body[0], body[1], body[2]); v: Q16 integers."""
    dot = lambda b: r30(b[0] * v[0] + b[1] * v[1] + b[2] * v[2])
    s = r30(dot(side) * ks)
    u = r30(dot(up) * ku)
    f = r30(dot(fwd) * kf)
    return tuple(r30(s * side[i]) + r30(u * up[i]) + r30(f * fwd[i]) for i in range(3))


# ---------------------------------------------------------------- linear algebra (3x3, pure python)
def matM(rows, gains):
    """M = sum_a g_a b_a b_a^T for the basis rows (fwd, up, side) and gains (side, up, fwd) -> keyed by axis a."""
    fwd, up, side = rows
    g_side, g_up, g_fwd = gains
    m = [[0.0] * 3 for _ in range(3)]
    for b, g in ((side, g_side), (up, g_up), (fwd, g_fwd)):
        for i in range(3):
            for j in range(3):
                m[i][j] += g * b[i] * b[j]
    return m


def spec_norm_sym(a):
    """Largest |eigenvalue| of a symmetric 3x3 (Jacobi sweeps), times 1 + 1e-12 (margin)."""
    a = [row[:] for row in a]
    for _ in range(60):
        off = abs(a[0][1]) + abs(a[0][2]) + abs(a[1][2])
        if off < 1e-18:
            break
        for p, q in ((0, 1), (0, 2), (1, 2)):
            if abs(a[p][q]) < 1e-300:
                continue
            th = (a[q][q] - a[p][p]) / (2.0 * a[p][q])
            t = (1.0 if th >= 0 else -1.0) / (abs(th) + math.sqrt(th * th + 1.0))
            c = 1.0 / math.sqrt(t * t + 1.0)
            s = t * c
            for k in range(3):
                akp, akq = a[k][p], a[k][q]
                a[k][p], a[k][q] = c * akp - s * akq, s * akp + c * akq
            for k in range(3):
                apk, aqk = a[p][k], a[q][k]
                a[p][k], a[q][k] = c * apk - s * aqk, s * apk + c * aqk
    return max(abs(a[0][0]), abs(a[1][1]), abs(a[2][2])) * (1.0 + 1e-12)


def vnorm(v):
    return math.sqrt(sum(float(c) * float(c) for c in v))


def mat_sub(a, b):
    return [[a[i][j] - b[i][j] for j in range(3)] for i in range(3)]


def basis_norm(rows):
    """Spectral norm of the 3x3 matrix whose rows are the basis axes (sqrt of the largest eigenvalue of B B^T)."""
    g = [[sum(rows[i][k] * rows[j][k] for k in range(3)) for j in range(3)] for i in range(3)]
    return math.sqrt(spec_norm_sym(g))


# ---------------------------------------------------------------- the bound
def residual_box(rows, gains=G_NAT):
    """b[t]: worst-case norm of the 15 rounding residuals of one damp() (see the header)."""
    s = math.sqrt(sum(((1.0 + g) / 2.0) ** 2 for g in gains))
    return 1.5 * math.sqrt(3.0) + s * basis_norm(rows) * (1.0 + 1e-12)


def quant_term():
    """The gain and basis quantisation of the native against the original's binary32 operation, per unit of velocity."""
    dg = sum(abs(a - b) + U31 for a, b in zip(G_REF, G_NAT))
    return dg + 3 * 2 * math.sqrt(3.0) * U31 * max(G_NAT)


def proxy_variance(rows, w, gains=G_NAT):
    """Hoeffding proxy variance of one tick's residual along the unit direction w (tier S)."""
    fwd, up, side = rows
    ax = ((side, gains[0]), (up, gains[1]), (fwd, gains[2]))
    return 0.25 * (3.0 * sum(c * c for c in w) + sum(sum(b[i] * w[i] for i in range(3)) ** 2 * (1.0 + g * g) for b, g in ax))


def coast_bound(r, bases, alpha=1e-4):
    """r: velocities at ticks T..end (q units are NOT used: units, 3-sequences of float/str); bases: reference rows
    (fwd, up, side), each a 3x3 of floats, at the same ticks. Returns a dict with the tier R and tier S added terms in
    speed units (not q) and the recursion's pieces. Raises ValueError when a range assumption fails."""
    n = len(r)
    if len(bases) != n or n < 2:
        raise ValueError('r and bases must cover the same ticks T..end, two at least')
    rf = [[float(c) for c in v] for v in r]
    lim = 2.0 ** 14                      # 2^30 q in speed units
    if any(abs(c) >= lim for v in rf for c in v):
        raise ValueError('range: a velocity component reaches 2^30 q')
    quant = quant_term()
    mats = [matM(b, G_REF) for b in bases]
    nus = [spec_norm_sym(m) for m in mats]
    E = (math.sqrt(3.0) / 2.0) * Q + math.sqrt(3.0) * DEC              # speed units
    es = [E]
    nu_used = []
    for t in range(n - 1):
        nu = max(nus[t], nus[t + 1])
        nu_used.append(nu)
        dm = spec_norm_sym(mat_sub(mats[t + 1], mats[t]))
        b = residual_box(bases[t + 1]) * Q
        c = dm + quant
        h = 16 * 2.0 ** -24
        rn = vnorm(rf[t])
        E = nu * E + b + c * (rn + E) + h * rn
        es.append(E)
    # tier S: three orthogonal directions (the reference direction and two perpendiculars), each with the proxy variance
    # bound 1.2416 (the maximum over directions of proxy_variance), union over the three
    prod = 1.0
    ss = 0.0
    for t in range(n - 2, -1, -1):
        ss += (prod * math.sqrt(1.2416) * Q) ** 2
        prod *= nu_used[t]
    S = math.sqrt(ss)
    lam = S * math.sqrt(2.0 * math.log(2.0 * 3.0 / alpha))
    rn_end = vnorm(rf[-1])
    perp = (2.0 * lam * lam) / (2.0 * (rn_end - math.sqrt(3.0) * lam)) if rn_end > 2.0 * math.sqrt(3.0) * lam else math.sqrt(3.0) * lam
    fixed = math.sqrt(3.0) * DEC + 2.0 ** -23 * rn_end + Q          # print, binary32 sqrt, the library's vlen
    return dict(E_R=E, added_R=E + fixed, S=S, lam=lam, added_S=min(lam + perp, E) + fixed, perp=perp, fixed=fixed,
                nu_max=max(nu_used), nu_min=min(nu_used), ticks=n - 1, E_path=es)


def tolerance(ref_speed, added=0.0):
    """band + added term (C-136: the ordinary inclusive band is kept, the added term is published separately)."""
    return max(Q, 0.005 * abs(ref_speed)) + added


# ---------------------------------------------------------------- signed residuals (diagnostic)
def residual_stats(rows_ref, rows_nat, bases, t0, t1, k):
    """Implied one-tick residuals of racer k over (t0, t1]: d[t+1] = e[t+1] - M(B[t+1]) e[t] with e = native - reference
    velocity (q units). It contains the damping rounding and whatever else the native does differently (that is the point
    of the check). Returns (n, mean per axis, sd per axis) in the world axes and in the body axes (fwd, up, side)."""
    out = []
    for t in range(t0, t1):
        e0 = [(float(rows_nat[(t, k)][5 + i]) - float(rows_ref[(t, k)][5 + i])) / Q for i in range(3)]
        e1 = [(float(rows_nat[(t + 1, k)][5 + i]) - float(rows_ref[(t + 1, k)][5 + i])) / Q for i in range(3)]
        m = matM(bases[t + 1], G_REF)
        d = [e1[i] - sum(m[i][j] * e0[j] for j in range(3)) for i in range(3)]
        body = [sum(bases[t + 1][a][i] * d[i] for i in range(3)) for a in range(3)]
        out.append((d, body))
    n = len(out)
    if not n:
        return 0, None, None, None, None
    mean = lambda j, ax: sum(o[j][ax] for o in out) / n
    sd = lambda j, ax: math.sqrt(sum((o[j][ax] - mean(j, ax)) ** 2 for o in out) / n)
    return (n, [mean(0, a) for a in range(3)], [sd(0, a) for a in range(3)], [mean(1, a) for a in range(3)], [sd(1, a) for a in range(3)])
