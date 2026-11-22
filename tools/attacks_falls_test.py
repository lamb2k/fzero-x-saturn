#!/usr/bin/env python3
"""The falls card (C-096 item 5) checks, HOST, as frozen in falls-freeze.txt (sections 5b-5g).
usage: attacks_falls_test.py OUTDIR [--mutate NAME]   -> prints one line per check and PASS/FAIL; exit 1 on any FAIL.
Paths from the environment (all local, ROM-derived): FZX_HOST (the HOST tree with ref/build and tools, default the
repository root), FZX_RUN (the judge's run directory holding ho-pads/, the held-out set the falls windows come from),
FZX_COURSE1, FZX_COURSE2 (the Mute City and tube course records), FZX_ROM.
Retained under C-116 item 3/4: the pre-fix controls (--mutate fall-keep-energy, fall-wrong) must each still FAIL.
Windows run the native duel runner from the hooked reference's own state (as attacks_moving.py's replays) and
compare: retirement tick, bit 18 and energy exactly 0.000000; K records (tick, victim, attacker) and their count;
racer 0's energy after a credit (the regain, once); with reference-only age/toucher settings for the boundary."""
import json, os, subprocess, sys
from pathlib import Path
H = os.environ.get('FZX_HOST') or str(Path(__file__).resolve().parent.parent)
sys.path.insert(0, H + '/tools')
import attacks_duel as AD, attacks_contact_judge as J

RUN = Path(os.environ['FZX_RUN'])
REF = H + '/ref/build/gcc-O2-duel/fzx-ref'
C1, C2, ROM = os.environ['FZX_COURSE1'], os.environ['FZX_COURSE2'], os.environ['FZX_ROM']
OUT = Path(sys.argv[1]); OUT.mkdir(parents=True, exist_ok=True)
MUT = sys.argv[sys.argv.index('--mutate') + 1] if '--mutate' in sys.argv else ''
TABLE = {k: tuple(v) for k, v in json.loads(open(H + '/tools/attacks_heldout_c092.json').read()).items() if not k.startswith('_')}
fails = []


def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode:
        raise SystemExit('failed: %s\n%s' % (' '.join(cmd), p.stderr[-1500:]))


def ref_run(sc, tag, until, extra=()):
    """The hooked reference over the recorded pads, with reference-only settings; trace + pair log + a bake range."""
    script, mref, mnat, ticks, cid, afrom = TABLE[sc]
    d = RUN / 'ho-pads' / sc
    o = OUT / ('ref-%s-%s' % (sc, tag)); o.mkdir(exist_ok=True)
    if not (o / 'ref.tsv').exists():
        run([REF, '--course', C1 if cid == 1 else C2, '--rom', ROM, '--attack-from', str(afrom), '--scenario', script,
             '--machines', mref, '--ticks', str(until + 1), '--replay-pads', str(d / 'ref.pads'), '--trace', str(o / 'ref.tsv'),
             '--pairlog', str(o / 'ref.pairlog')] + list(extra))
    return o


def native(sc, refdir, t0, end, since=None, toucher=None):
    """The native runner from the reference's state after t0 (refdir's own trace and bake), to end."""
    script, mref, mnat, ticks, cid, afrom = TABLE[sc]
    d = RUN / 'ho-pads' / sc
    rows = AD.load(refdir / 'ref.tsv'); lg = J.load_ref_log(refdir / 'ref.pairlog')
    w = OUT / ('n-%s-%s-%d-%d-%s' % (sc, refdir.name, t0, end, MUT or 'base')); w.mkdir(exist_ok=True)
    bk = w / 'bake'; bk.mkdir(exist_ok=True)
    if not (w / 'n.tsv').exists():
        cmdline = (refdir / 'cmd.txt').read_text().split() if (refdir / 'cmd.txt').exists() else []
        run([REF, '--course', C1 if cid == 1 else C2, '--rom', ROM, '--attack-from', str(afrom), '--scenario', script,
             '--machines', mref, '--ticks', str(t0 + 1), '--replay-pads', str(d / 'ref.pads'), '--bake-range', '%d:%d' % (t0, t0),
             '--bake-dir', str(bk)] + cmdline)
        for k in (0, 1):
            run([sys.executable, H + '/tools/physics_pack.py', str(bk / ('p%d-%05d.src' % (k, t0))), str(w / ('p%d.pack' % k))])
        with open(w / 'draws.txt', 'w') as df:
            for t in range(t0 + 1, end + 1):
                for (k, ctx, vv) in lg['D'].get(t, []):
                    if k in (0, 1) and not (int(vv[2]) & ((1 << 27) | (1 << 25))) and J.f32sub(vv[1], vv[0]) < 0.0:
                        st = float(rows[(t, k)][19])
                        if st != 0.0:
                            df.write('%d %d %d\n' % (t, k, 1 if st > 0 else -1))
        r1 = AD.state(rows, t0, 1)
        nb = H + '/ref/build/' + ('fzx-duel-native' if cid == 1 else 'fzx-duel-native-tubes')
        run([nb, '--pack0', str(w / 'p0.pack'), '--pack1', str(w / 'p1.pack'), '--pads', str(d / 'ref.pads'), '--from', str(t0),
             '--ticks', str(end - t0), '--machines', mnat, '--since', str(r1['since'] if since is None else since),
             '--toucher', str(r1['toucher'] if toucher is None else toucher), '--touching', str(lg['P'][t0 + 1]['colliding']),
             '--trace', str(w / 'n.tsv'), '--pairlog', str(w / 'n.pairlog'), '--spin-draws', str(w / 'draws.txt'),
             '--regain-pending', '1' if any(att == 0 for (_, att, _) in lg['K'].get(t0, [])) else '0'] + (['--mutate', MUT] if MUT else []))
    return AD.load(w / 'n.tsv'), J.load_nat_log(w / 'n.pairlog'), rows, lg


def check(name, ok, detail):
    print('%-58s %s  %s' % (name, 'PASS' if ok else 'FAIL', detail))
    if not ok:
        fails.append(name)


def window(name, sc, refdir, t0, end, since=None, toucher=None):
    n, nl, rows, lg = native(sc, refdir, t0, end, since, toucher)
    for k in (0, 1):   # the native window starts from the reference's state after t0
        n[(t0, k)] = rows[(t0, k)]
    for k in (0, 1):
        rt = next((t for t in range(t0 + 1, end + 1) if (AD.state(rows, t, k)['flags'] & (1 << 18)) and not (AD.state(rows, t - 1, k)['flags'] & (1 << 18))), None)
        nt = next((t for t in range(t0 + 1, end + 1) if (t, k) in n and (AD.state(n, t, k)['flags'] & (1 << 18)) and not (AD.state(n, t - 1, k)['flags'] & (1 << 18))), None) if (t0 + 1, k) in n else None
        if rt is None and nt is None:
            continue
        e_ref = rows[(rt, k)][9] if rt else None
        e_nat = n[(rt, k)][9] if rt and (rt, k) in n else None
        before = n[(rt - 1, k)][9] if rt and (rt - 1, k) in n else None
        check('%s: racer %d retirement' % (name, k), rt == nt and float(e_ref) == 0.0 and e_nat == '0.000000' and
              (before is None or (float(before) != 0.0) == (float(rows[(rt - 1, k)][9]) != 0.0)),
              'ref tick %s energy %s; native tick %s energy %s (before %s)' % (rt, e_ref, nt, e_nat, before))
    kr = sorted((t, v, a) for t in range(t0 + 1, end + 1) for (v, a, _) in lg['K'].get(t, []))
    kn = sorted((t, v, a) for t, recs in nl['K'].items() if t0 < t <= end for (v, a) in recs)
    check('%s: KO records' % name, kr == kn, 'ref %s native %s' % (kr, kn))
    for (t, v, a) in kr:
        e = [(float(rows[(tt, 0)][9]), float(n[(tt, 0)][9])) for tt in range(t + 1, min(t + 4, end) + 1) if (tt, 0) in n]
        check('%s: regain after the KO at %d' % (name, t), all(abs(x - y) < 0.01 for x, y in e), 'racer 0 energy ref/native %s' % e)
    return kr, kn


# a. the three held-out windows, and (f) further starts before each event
EV = [('h-cpu-t-m3', 3314), ('h-cpu-t-m3', 3430), ('h-ko-m22', 3027), ('h-ko-m22', 2907)]
base = {sc: ref_run(sc, 'base', 3999) for sc in ('h-cpu-t-m3', 'h-ko-m22')}
for sc in base:   # the hooked reference with no settings reproduces the judge's reference trace
    a, b = (RUN / 'ho-pads' / sc / 'ref.tsv').read_bytes(), (base[sc] / 'ref.tsv').read_bytes()
    check('reference %s reproduces the judge trace' % sc, a[:len(b)] == b or b[:len(a)] == a, '%d / %d bytes' % (len(a), len(b)))
for sc, te in EV:
    for lead in (30, 17, 45, 75):
        window('%s event %d lead %d' % (sc, te, lead), sc, base[sc], te - lead, te - lead + 90)

# b. boundary: the age at the fall test 119 / 120 / 121 (set before tick 3314 to 118 / 119 / 120)
for age in (119, 120, 121):
    o = ref_run('h-cpu-t-m3', 'age%d' % age, 3400, ['--set-age', '3314:1:%d' % (age - 1)])
    (o / 'cmd.txt').write_text('--set-age 3314:1:%d' % (age - 1))
    rows = AD.load(o / 'ref.tsv')
    seen = AD.state(rows, 3314, 1)['since']
    kr, kn = window('boundary age %d' % age, 'h-cpu-t-m3', o, 3313, 3340, since=age - 1, toucher=0)
    check('boundary age %d: the reference decides as frozen' % age, seen == age and (bool(kr) == (age <= 120)),
          'reference age after 3314 %d, K %s' % (seen, kr))

# c. unassisted falls: h-ko-m22's racer 0 (no toucher) above; h-cpu-t-m3 3314 with the toucher cleared
o = ref_run('h-cpu-t-m3', 'notoucher', 3400, ['--set-toucher', '3314:1:-1'])
(o / 'cmd.txt').write_text('--set-toucher 3314:1:-1')
kr, kn = window('unassisted fall (toucher cleared)', 'h-cpu-t-m3', o, 3313, 3340, toucher=-1)
check('unassisted fall: no KO on either side', not kr and not kn, 'ref %s native %s' % (kr, kn))

print('falls card %s: %d failing checks %s' % ('mutation ' + MUT if MUT else 'candidate', len(fails), fails))
sys.exit(1 if fails else 0)
