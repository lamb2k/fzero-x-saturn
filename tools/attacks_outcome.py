#!/usr/bin/env python3
"""Attacks outcome judge (C-168, attacks-5), HOST: generator, closed-loop episode runner, per-episode outcome metrics.

An episode is one paired 600-tick closed-loop run from one converted start state:
  G  the original from tick 0 with the scripted drivers: only bakes the start (both racers at T0) and the drivers' memory.
  C  the original code from the converted start with its scripted drivers (the reference arm of the judge).
  T  C plus the seeded 1 q start-velocity nudge applied one tick after the load: the reference's own variation.
  N  the native pair from the packs of the same state with the same drivers ported into tools/attacks_host/duel_native.c
     (`--mutate` runs the control mutants).
Every interpretation of C-168 is a named parameter of tools/attacks_outcome_config.json.
Everything run here is derived from the owner's ROM: local only, outside the repository.

  attacks_outcome.py manifest --ns score --out DIR          write the episode manifest (parameters only) and its hash
  attacks_outcome.py run --ns score --arms CT --out DIR [--per-cell N] [--cells filter]   run episodes, one JSON line each
"""
import argparse, hashlib, json, math, os, random, re, shutil, struct, subprocess, sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import physics_pack as PP  # noqa: E402

ST_RETIRED = 1 << 18
Q = 1.0 / 65536
CFG = json.load(open(HERE / 'attacks_outcome_config.json'))
FORMATS = (('pos', None, 16), ('vel', None, 16), ('body', None, 30), ('road', None, 30), ('gravity_up', None, 30),
           ('tilt_up', None, 30), ('ground_up', None, 30), ('fwd', None, 16), ('spos', None, 16), ('disp', None, 16),
           ('ground', None, 16), ('bvel', None, 16),
           ('scalars', (0, 2), 30), ('scalars', (2, 6), 16), ('scalars', (6, 9), 12), ('scalars', (9, 12), 16),
           ('scalars', (12, 13), 16), ('scalars', (13, 14), 16),
           ('dynamics', (15, 20), 16), ('dynamics', (20, 21), 30))
STATE_KEYS = {k for k, _, _ in FORMATS}


# ------------------------------------------------------------------ machines and cells
def machine_table(attacks_h):
    rows = re.findall(r'\{\s*(\d+)u,\s*(\d+)u,\s*(\d+)u,\s*(\d+)u,\s*(\d+)u,\s*(\d+)u\s*\}', Path(attacks_h).read_text())[:30]
    return [dict(strength=int(r[0]), max_energy=int(r[1]) / 256.0, weight=int(r[3])) for r in rows]


def bands(machines):
    order = sorted(range(30), key=lambda i: (machines[i]['weight'], i))
    return {'light': order[:10], 'middle': order[10:20], 'heavy': order[20:]}


def cells(cfg=CFG):
    c = cfg['cells']
    return [(m, e, p, b) for m in c['maneuvers'] for e in c['environments'] for p in c['placements'] for b in c['attacker_bands']]


def rng_for(*parts):
    return random.Random(int.from_bytes(hashlib.sha256('|'.join(map(str, parts)).encode()).digest()[:8], 'big'))


def episode_params(ns, cell, idx, machines, cfg=CFG):
    """Everything an episode needs, from the namespace, the cell and the index alone."""
    man, env, plc, band = cell
    bd = bands(machines)
    ctag = '/'.join(cell)
    combos = [(vb, s, lo) for vb in ('light', 'middle', 'heavy') for s in (1, -1) for lo in (0, 1)]
    blk = list(range(12)); rng_for(cfg['namespaces'][ns], ctag, 'block', idx // 12).shuffle(blk)
    vb, s, low = combos[blk[idx % 12]]
    r = rng_for(cfg['namespaces'][ns], ctag, 'episode', idx)
    u = lambda lo_hi: r.uniform(*lo_hi)
    a_m, v_m = r.choice(bd[band]), r.choice(bd[vb])
    pl, d = cfg['placement'], cfg['driver']
    lane = s * u(pl['wall_lane_frac_tube'] if env == 'tube' else pl['wall_lane_frac']) if plc == 'wall' else u(pl['interior_lane_frac'])
    p = dict(ns=ns, cell=list(cell), idx=idx, attacker=a_m, victim=v_m, victim_band=vb, side=s, low=low,
             lane_frac=round(lane, 6), att_side=-s, near=round(u(d['near']), 4), bump_off=round(u(d['bump_offset']), 4),
             tap_int=r.randint(*d['tap_interval']), launch0=r.randint(*d['launch_jitter']), launch1=r.randint(*d['launch_jitter']),
             attack_from=cfg['t0'] + r.randint(*d['attack_from_offset']))
    lo_f = cfg['energy']['low_frac_bump'] if man == 'bump' else cfg['energy']['low_frac']
    p['energy'] = [round(u(lo_f) * machines[a_m]['max_energy'], 3), round(u(lo_f) * machines[v_m]['max_energy'], 3)] if low else None
    return p


def fixture_params(name, idx, machines, cfg=CFG):
    """A short exact episode: the victim starts with energy so low that any hit is lethal (a forced lethal hit; the wall family
    pushes a wall-exposed victim), the attacker at full energy, followed for `horizon` ticks."""
    spec = cfg['fixtures']['spec'][name]
    p = episode_params('fixtures', tuple(spec['cell']), idx, machines, cfg)
    r = rng_for(cfg['namespaces']['fixtures'], name, 'energy', idx)
    p.update(fixture=name, ticks=cfg['fixtures']['horizon'],
             energy=[machines[p['attacker']]['max_energy'], round(r.uniform(*spec['victim_energy']), 3)])
    return p


# ------------------------------------------------------------------ state conversion (C-142 form)
def f32(x):
    return struct.unpack('f', struct.pack('f', x))[0]


def convert_state(text):
    out = []
    for line in text.splitlines():
        f = line.split()
        if not f or f[0].startswith('#') or f[0] not in STATE_KEYS:
            continue
        key, a, b, vals = f[0], f[1], f[2], [float.fromhex(x) for x in f[3:]]
        for fk, sl, bits in FORMATS:
            if fk != key:
                continue
            lo, hi = sl if sl else (0, len(vals))
            for i in range(lo, hi):
                vals[i] = f32(PP.fx(vals[i], bits) / float(1 << bits))
        out.append('%s %s %s %s' % (key, a, b, ' '.join(float.hex(x) for x in vals)))
    return '\n'.join(out) + '\n'


# ------------------------------------------------------------------ traces
def load_trace(path):
    rows = {}
    for line in open(path):
        r = line.rstrip('\n').split('\t')
        rows[(int(r[0]), int(r[1]))] = (float(r[9]), int(r[13]), int(r[14]), int(r[15]), int(r[16]), int(r[17]), int(r[18]))
    return rows   # energy, flags, attack, spin_timer, kos, since, toucher


def ref_log(path):
    """per tick: touch, first, contact damage per racer, K records (victim, attacker)"""
    lg = {}
    for line in open(path):
        c = line[0]
        if c not in 'TDK':
            continue
        f = line.split(); t = int(f[1]); a, b = int(f[2]), int(f[3]); v0 = float.fromhex(f[4])
        e = lg.setdefault(t, dict(touch=0, first=0, cd=[0.0, 0.0], ko=[], dm=[0.0, 0.0]))
        if c == 'D':
            e['dm'][a] += v0
        if c == 'T':
            e['touch'] = 1; e['first'] |= int(v0 == 0)
        elif c == 'D' and b == 1:
            e['cd'][a] += v0
        elif c == 'K':
            e['ko'].append((a, b))
    return lg


def nat_log(path):
    lg = {}
    for line in open(path):
        f = line.split()
        if f[0] == 'N':
            t = int(f[1]); touch = int(f[5])
            e = lg.setdefault(t, dict(touch=0, first=0, cd=[0.0, 0.0], ko=[], dm=[0.0, 0.0]))
            e['touch'] = touch; e['first'] = int(touch and int(f[6]))
            if touch:
                e['cd'] = [int(f[10]) * Q, int(f[11]) * Q]; e['dm'] = list(e['cd'])
        elif f[0] == 'K':
            lg.setdefault(int(f[1]), dict(touch=0, first=0, cd=[0.0, 0.0], ko=[], dm=[0.0, 0.0]))['ko'].append((int(f[2]), int(f[3])))
    return lg


def outcome(rows, lg, t0, n, max_e, start_energy, cfg=CFG):
    """The episode's outcome record from a trace (ticks t0 + 1 .. t0 + n) and the arm's pair log. Same code for every arm."""
    emin = cfg['event_min_drop']
    E = lambda t, k: start_energy[k] if t == t0 else rows[(t, k)][0]
    first = [t for t in range(t0 + 1, t0 + n + 1) if lg.get(t, {}).get('first')]
    touch = lambda t: lg.get(t, {}).get('touch', 0)
    o = dict(contacts=len(first), nd=[0, 0], sd=[0.0, 0.0], sp=[0.0, 0.0], hit=0, ko=0, ko_tick=None, wall_ko=0, retire=0,
             retire_racer=[0, 0], spin_outs=[0, 0], side_starts=0, spin_starts=0, first_damage_tick=None, bad=[],
             other_ticks=[0, 0], other_total=[0.0, 0.0], wall_ko_w={}, destroyed=[0, 0], destroy_tick=None)
    ends = first[1:] + [t0 + n + 1]
    for t_first, t_end in zip(first, ends):
        for j in (0, 1):
            for t in range(t_first, t_end):
                if not touch(t) or rows[(t - 1, j)][1] & ST_RETIRED:
                    continue
                drop = E(t - 1, j) - rows[(t, j)][0]
                if drop >= emin:
                    o['nd'][j] += 1; o['sd'][j] += drop / max_e[j]; o['sp'][j] += E(t - 1, j) / max_e[j]
                    if j == 1 and o['first_damage_tick'] is None:
                        o['first_damage_tick'] = t
                    break
    if CFG['reading_contact_unit'] == 'all':
        o['nd_all'] = len(first)
    o['hit'] = int(o['nd'][1] > 0)
    kos = rows[(t0, 0)][4]
    oth = {}   # (t, k): non-contact energy drop
    for t in range(t0 + 1, t0 + n + 1):   # a retired machine hit for more than 25 is destroyed (Racer_ReceiveDamage)
        for k in (0, 1):
            if (rows[(t - 1, k)][1] & ST_RETIRED) and lg.get(t, {}).get('dm', [0, 0])[k] > 25.0:
                o['destroyed'][k] += 1; o['destroy_tick'] = o['destroy_tick'] or t - t0
    for t in range(t0 + 1, t0 + n + 1):
        for k in (0, 1):
            if rows[(t - 1, k)][1] & ST_RETIRED:
                continue
            od = E(t - 1, k) - rows[(t, k)][0] - (lg.get(t, {}).get('cd', [0.0, 0.0])[k] if touch(t) else 0.0)
            if od >= emin:
                oth[(t, k)] = od; o['other_ticks'][k] += 1; o['other_total'][k] += od
    for t in range(t0 + 1, t0 + n + 1):
        for k in (0, 1):
            cur, prv = rows[(t, k)], rows[(t - 1, k)]
            if (cur[1] & ST_RETIRED) and not (prv[1] & ST_RETIRED):
                o['retire'] += 1; o['retire_racer'][k] += 1
            if cur[3] and not prv[3]:
                o['spin_outs'][k] += 1
            if cur[2] != prv[2] and cur[2] in (1, 2) and k == 0:
                o['side_starts' if cur[2] == 1 else 'spin_starts'] += 1
        if rows[(t, 0)][4] > kos:
            o['ko'] += rows[(t, 0)][4] - kos; kos = rows[(t, 0)][4]
            if o['ko_tick'] is None:
                o['ko_tick'] = t - t0
            drop = E(t - 1, 1) - rows[(t, 1)][0]
            cd = lg.get(t, {}).get('cd', [0.0, 0.0])[1]
            other = drop - cd
            if other >= emin and other > cd:
                o['wall_ko'] = 1
            for w in (5, 30, 60):   # reading variants: victim non-contact damage within w ticks before the KO tick (inclusive)
                if any(oth.get((u, 1), 0) >= emin for u in range(max(t0 + 1, t - w), t + 1)):
                    o['wall_ko_w'][str(w)] = 1
    # predeclared structural checks (must hold exactly in every arm)
    bad = o['bad']
    age0 = rows[(t0, 1)][5]   # the credit needs a human contact within the window; one before the episode counts while its age is within 120 ticks
    if o['ko'] and not first and age0 + (o['ko_tick'] or 0) > 120: bad.append('ko without contact')
    if o['ko_tick'] and first and o['ko_tick'] + t0 < first[0] and age0 + o['ko_tick'] > 120: bad.append('ko before first contact')
    if o['hit'] and not first: bad.append('hit without contact')
    if o['wall_ko'] and not o['ko']: bad.append('wall ko without ko')
    if max(o['retire_racer']) > 1: bad.append('double retire')
    o['energy_rises'] = [sum(1 for t in range(t0 + 1, t0 + n + 1) if rows[(t, k)][0] > E(t - 1, k) + 1e-3) for k in (0, 1)]   # pit recharge, KO regain: counted, not a check
    for k in (0, 1):
        for t in range(t0 + 1, t0 + n + 1):
            if (rows[(t, k)][1] & ST_RETIRED) and not (rows[(t - 1, k)][1] & ST_RETIRED) and rows[(t, k)][0] != 0.0:
                bad.append('retire energy not 0'); break
    return o


# ------------------------------------------------------------------ one episode
def run(cmd, **kw):
    p = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if p.returncode != 0:
        raise RuntimeError('failed: %s\n%s' % (' '.join(map(str, cmd)), p.stderr[-800:]))
    return p.stdout


def scenario_args(p, cfg):
    man = p['cell'][0]
    a = ['--scenario', man, '--machines', '%d,0,%d,0' % (p['attacker'], p['victim']), '--attack-from', str(p['attack_from']),
         '--att-side', str(p['att_side']), '--near', str(p['near']), '--bump-off', str(p['bump_off']), '--tap-int', str(p['tap_int']),
         '--launch0', str(p['launch0']), '--launch1', str(p['launch1'])]
    if p['cell'][2] == 'wall' or abs(p['lane_frac']) > 0:
        a += ['--lane-frac', str(p['lane_frac'])]
    return a


def run_episode(p, env, arms='CT', keep=None, mutates=('',)):
    """C-168 (attacks-5): one paired closed-loop episode. G (the original from tick 0) only bakes the converted start and the drivers'
    memory at T0; C is the reference from the converted start with its scripted drivers, T the same with the 1 q post-load nudge, N the
    native pair from the same packs with the same drivers ported to the runner (one run per mutant in `mutates`)."""
    cfg, T0 = CFG, CFG['t0']
    N = p.get('ticks', CFG['episode_ticks'])
    machines = env['machines']
    tube = p['cell'][1] == 'tube'
    course = env['course_tube'] if tube else env['course_open']
    native = env['native_tube'] if tube else env['native_open']
    work = Path(keep) if keep else Path(env['tmp']) / ('ep-%s-%d-%s' % (p['ns'], p['idx'], '-'.join(p['cell'])))
    work.mkdir(parents=True, exist_ok=True)
    max_e = [machines[p['attacker']]['max_energy'], machines[p['victim']]['max_energy']]
    base = [env['ref'], '--course', course, '--rom', env['rom']]
    sa = scenario_args(p, cfg)
    res = dict(p)
    try:
        g_tr, g_pl, pads, drv = work / 'g.tsv', work / 'g.pairlog', work / 'ref.pads', work / 'g.drv'
        s0, s1 = work / 'p0.src', work / 'p1.src'
        if not (work / 'p1.pack').exists() or not g_tr.exists():
            cmd = base + sa + ['--ticks', str(T0 + 2), '--trace', str(g_tr), '--pairlog', str(g_pl), '--pads', str(pads),
                               '--bake-at', str(T0), '--bake0', str(s0), '--bake1', str(s1), '--drv-out', str(drv)]
            if p['energy']:
                cmd += ['--set-energy', '%d:0:%s' % (T0, p['energy'][0]), '--set-energy', '%d:1:%s' % (T0, p['energy'][1])]
            run(cmd)
            for k in (0, 1):
                (work / ('p%d.pack' % k)).write_bytes(PP.build(PP.parse(work / ('p%d.src' % k)), 3))
        g_rows = load_trace(g_tr)
        start_e = []   # the start state's energies: the converted bake values (what both arms load)
        for k in (0, 1):
            raw = (work / ('p%d.src' % k)).read_text()
            sc = [l for l in convert_state(raw).splitlines() if l.startswith('scalars ')][0].split()
            start_e.append(float.fromhex(sc[3 + 10]))
        res['start_energy'] = start_e
        for arm in ('C', 'T'):   # closed-loop scripted drivers on their own state; T = C with the perturbation
            if arm not in arms:
                continue
            cs = []
            for k in (0, 1):
                q = work / ('c%d.src' % k); q.write_text(convert_state((work / ('p%d.src' % k)).read_text())); cs.append(q)
            r_tr, r_pl = work / (arm.lower() + '.tsv'), work / (arm.lower() + '.pairlog')
            cmd = base + sa + ['--ticks', str(T0 + N + 1), '--trace', str(r_tr), '--pairlog', str(r_pl),
                               '--load-state', '%d:%s:%s' % (T0 + 1, cs[0], cs[1])]
            if keep:
                cmd += ['--pads', str(work / (arm.lower() + '.pads'))]
            if arm == 'T':   # the reference's own 1 q variation: +-sensitivity_q Q16 steps on every start velocity component, applied one tick AFTER the load
                rr = rng_for(CFG['namespaces'][p['ns']], '/'.join(p['cell']), 'perturb', p['idx'])
                for k in (0, 1):
                    for ax in range(3):
                        cmd += ['--perturb', '%d:%d:1:%d:%s' % (T0 + 2, k, ax, float((1 if rr.random() < 0.5 else -1) * CFG['sensitivity_q'] * Q).hex())]
            run(cmd)
            res[arm] = outcome(load_trace(r_tr), ref_log(r_pl), T0, N, max_e, start_e)
        if 'N' in arms:
            p1 = g_rows[(T0, 1)]
            col = [l for l in open(g_pl).read().split('\n') if l.startswith('P %d ' % (T0 + 1))][0].split()
            tap_at, tap_dir = open(drv).read().split()
            for mutate in mutates:
                n_tr, n_pl = work / ('n%s.tsv' % mutate), work / ('n%s.pairlog' % mutate)
                cmd = [native, '--pack0', str(work / 'p0.pack'), '--pack1', str(work / 'p1.pack'), '--pads', str(pads), '--from', str(T0),
                       '--ticks', str(N), '--machines', '%d,%d' % (p['attacker'], p['victim']), '--since', str(p1[5]), '--toucher', str(p1[6]),
                       '--touching', col[3], '--trace', str(n_tr), '--pairlog', str(n_pl), '--drive', p['cell'][0],
                       '--attack-from', str(p['attack_from']), '--att-side', str(p['att_side']), '--near', str(p['near']),
                       '--bump-off', str(p['bump_off']), '--tap-int', str(p['tap_int']), '--launch0', str(p['launch0']),
                       '--launch1', str(p['launch1']), '--drv-tap', '%s,%s' % (tap_at, tap_dir)]
                if p['cell'][2] == 'wall' or abs(p['lane_frac']) > 0:
                    cmd += ['--lane-frac', str(p['lane_frac'])]
                if keep:
                    cmd += ['--pads-out', str(work / ('n%s.pads' % mutate))]
                if mutate:
                    cmd += ['--mutate', mutate]
                run(cmd)
                nrows = load_trace(n_tr)
                nrows.update({(T0, k): g_rows[(T0, k)] for k in (0, 1)})
                res['N' + (':' + mutate if mutate else '')] = outcome(nrows, nat_log(n_pl), T0, N, max_e, start_e)
    finally:
        if not keep:
            shutil.rmtree(work, ignore_errors=True)
    return res


_ENV = {}


def _work(args):
    p, arms, keep, mutates = args
    return run_episode(p, _ENV, arms, keep, mutates)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('cmd', choices=['manifest', 'run'])
    ap.add_argument('--ns', required=True, choices=list(CFG['namespaces']))
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--arms', default='CT', help='any of C T N (G, the bake of the converted start, always runs)')
    ap.add_argument('--per-cell', type=int, default=CFG['cells']['episodes_per_cell'])
    ap.add_argument('--first', type=int, default=0, help='first episode index in each cell')
    ap.add_argument('--cells', default='', help='comma list of cell prefixes, e.g. side/open')
    ap.add_argument('--mutate', default='', help='comma list of native mutants (N arm only); a run with mutants writes only the N:<name> arms')
    ap.add_argument('--keep', action='store_true', help='keep each episode directory (packs, pads) under OUT/episodes')
    ap.add_argument('--jobs', type=int, default=6)
    ap.add_argument('--tables', help='attacks_tables.h'); ap.add_argument('--ref'); ap.add_argument('--rom')
    ap.add_argument('--course-open'); ap.add_argument('--course-tube'); ap.add_argument('--native-open'); ap.add_argument('--native-tube')
    ap.add_argument('--tmp', default='/dev/shm/ao')
    a = ap.parse_args()
    a.jobs = min(a.jobs, 6)   # coordinator's cap (fzx-cpu-cap, 2026-11-22): at most 6 workers, run under nice
    machines = machine_table(a.tables)
    sel = [c for c in cells() if not a.cells or any('/'.join(c).startswith(x) for x in a.cells.split(','))]
    items = [episode_params(a.ns, c, i, machines) for c in sel for i in range(a.first, a.first + a.per_cell)]
    a.out.mkdir(parents=True, exist_ok=True)
    if a.cmd == 'manifest':
        assert a.ns in CFG['namespaces']
        data = '\n'.join(json.dumps(x, sort_keys=True) for x in items) + '\n'
        (a.out / ('manifest-%s.jsonl' % a.ns)).write_text(data)
        print('manifest %s: %d episodes in %d cells, sha256 %s' % (a.ns, len(items), len(sel), hashlib.sha256(data.encode()).hexdigest()))
        return
    _ENV.update(machines=machines, ref=a.ref, rom=a.rom, course_open=a.course_open, course_tube=a.course_tube, native_open=a.native_open,
                native_tube=a.native_tube, tmp=a.tmp)
    Path(a.tmp).mkdir(parents=True, exist_ok=True)
    mutates = tuple(m for m in a.mutate.split(',') if m) or ('',)
    tag = '%s%s' % (a.arms, ('-' + '+'.join(mutates)) if a.mutate else '')
    outf = a.out / ('episodes-%s-%s.jsonl' % (a.ns, tag))
    keep = lambda p: str(a.out / 'episodes' / ('%s-%d' % ('-'.join(p['cell']), p['idx']))) if a.keep else None
    jobs = [(p, a.arms, keep(p), mutates) for p in items]
    with ProcessPoolExecutor(a.jobs, initializer=_ENV.update, initargs=(dict(_ENV),)) as ex, open(outf, 'w') as f:
        for r in ex.map(_work, jobs, chunksize=4):
            f.write(json.dumps(r, sort_keys=True) + '\n')
    print('wrote', outf, len(jobs))


if __name__ == '__main__':
    main()
