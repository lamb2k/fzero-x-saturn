"""c132_cases.py (C-132, amended C-133): HOST fixtures for lowpcs v3, run from c124_host.py (the C-124 fixture, extended).

The rows are the ymir-cov6 formats (cov-low6 H/E/Q, cov-f6 F, cov-other6 O). The reset trace is the REAL one: c132_reset_fields.txt is
gate6's reset run (fields 4826 to 4892: scene, reported phase, epoch and input count as the Ymir harness wrote them; the chord
script held 0F00 on fields 4834-4863; the reported LEAVE first appears at 4837, the flip to TITLE at 4885). The sequencer state of
the F rows follows the engine's order inside a manager iteration (chord handler, then sh2z_scene_frame, then the race frame), so
the actual phase is LEAVE one field before the report says so. The PC rows are synthetic, placed to the engine's call order.
A verified clean reset must pass; each negative must fail; the conservation, missing-file and wrong-map controls stay."""
import copy, hashlib, json, shutil, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
LOWPCS3 = HERE / 'lowpcs.py'
NONE = 0xFFFFFFFF
A9, A21 = 3, 3
ACC = 4836
FLIP = 4885
SYMS = {'_title_enter': 0x240000, '_art_preload': 0x240100, '_fzx_cup_reset': 0x240200, '_session_defaults': 0x240300,
        '_sh2z_scene_request': 0x240400, '_title_preload': 0x240500, '_title_span': 0x240600, '_title_tick': 0x240700,
        '_cold_other': 0x240800, '_fzx_race_frame': 0x240900}


def real_rows():
    out = []
    for line in (HERE / 'c132_reset_fields.txt').read_text().split('\n'):
        p = line.split()
        if p:
            out.append(dict(field=int(p[0]), scene=int(p[1]), rphase=int(p[2]), ep=int(p[4]), ic=int(p[6])))
    return out


def F(f, **kw):
    d = dict(field=f['field'], pad=0, rscene=f['scene'], rphase=f['rphase'], cur=f['scene'], ph=f['rphase'], nx=f['scene'], pd=NONE,
             ep=f['ep'], a9=A9, a21=A21, rpad=0, ic=f['ic'], rd=0, mo=1000, so=900, ent=0)
    d.update(kw)
    return d


def H(field, fn, o1, o2, n=3, cpu='m', **kw):
    d = dict(cpu=cpu, field=field, pc=SYMS[fn], n=n, o1=o1, o2=o2, cur=4, ph=1, nx=4, pd=NONE, ep=18, a9=A9, a21=A21, ic=0, rd=0)
    d.update(kw)
    return d


def clean():
    """A verified clean accepted reset: the real reset trace, chord injected 4834-4863, accepted in 4836, flip at 4885."""
    rows = real_rows()
    Fs = []
    for r in rows:
        f = r['field']
        pad = 0x0F00 if 4834 <= f <= 4863 else 0
        rpad = 0x0F00 if 4835 <= f <= 4864 else 0
        kw = dict(pad=pad, rpad=rpad)
        if f >= ACC and r['scene'] == 4:
            kw.update(a9=A9 + 1, a21=A21 + 1, ph=2 if r['rphase'] <= 2 else r['rphase'], nx=1)
        if r['scene'] == 1:
            kw.update(a9=A9 + 1, a21=A21 + 1, cur=1, ph=r['rphase'], nx=1)
        Fs.append(F(r, **kw))
    ic = {r['field']: r['ic'] for r in rows}
    # C-133: sh2z_scene_request, session_defaults and fzx_cup_reset hit in the acceptance field with the phase still RUN, so the
    # placement fix moved them to HWRAM: a clean reset has no .low rows in RUN. (See the 'pre-fix image' negative below.)
    Hs = [
        H(4872, '_art_preload', 20, 60, 30, ph=4, nx=1, ep=18, a9=A9 + 1, a21=A21 + 1, ic=ic[4872]),
        H(4872, '_title_preload', 61, 70, 5, ph=4, nx=1, ep=18, a9=A9 + 1, a21=A21 + 1, ic=ic[4872]),
        H(FLIP, '_title_enter', 10, 40, 12, ph=4, nx=1, ep=18, a9=A9 + 1, a21=A21 + 1, ic=ic[FLIP]),
    ]
    Es = [dict(field=ACC, ord=300, kind=1), dict(field=ACC + 1, ord=40, kind=0)]
    Qs = [dict(field=ACC, ord=100, target=1, rank=3, a9=A9, pd=NONE)]
    Os = {('m', SYMS['_title_enter']): 7, ('m', SYMS['_title_span']): 101 * 3, ('m', SYMS['_title_tick']): 18 * 2, ('m', SYMS['_art_preload']): 4}
    return dict(F=Fs, H=Hs, E=Es, Q=Qs, O=Os, hi100=[], conserve=True)


def serialize(S, root, kind='reset', name='reset', bad_files=()):
    src = root / 'src'
    b = src / 'builds' / 'scn'
    b.mkdir(parents=True)
    (b / 'fzs-engine.map').write_text('.low            0x00240000      0x1000 load address 0x06040000\n .text.all\n                0x00240000      0xa00 x.o\n')
    (b / 'fzs-engine.sym').write_text('\n'.join('%08x t %s' % (a, n) for n, a in sorted(SYMS.items(), key=lambda x: x[1])) + '\n06004000 T _main\n')
    (b / 'fzs-engine.bin').write_bytes(b'FZXBIN-fixture')
    (src / 'receipt.json').write_text(json.dumps({'builds': {'scn': {'bin': hashlib.sha256(b'FZXBIN-fixture').hexdigest()}}}))
    ev = root / 'ev'
    rd = ev / 'runs' / name
    rd.mkdir(parents=True)
    low, f6, oth = [], [], []
    for h in S['H']:
        low.append('H %s %d %x %d %d %d %d %d %d %d %d %d %d %d %d' % (h['cpu'], h['field'], h['pc'], h['n'], h['o1'], h['o2'], h['cur'], h['ph'],
                   h['nx'], h['pd'], h['ep'], h['a9'], h['a21'], h['ic'], h['rd']))
    for e in S['E']:
        low.append('E m %d %d %d %d' % (e['field'], e['ord'], e['kind'], e['kind']))
    for q in S['Q']:
        low.append('Q m %d %d %d %d %d %d' % (q['field'], q['ord'], q['target'], q['rank'], q['a9'], q['pd']))
    for f in S['F']:
        f6.append('F %d %x %d %d %d %d %d %d %d %d %d %x %d %d %d %d %d' % (f['field'], f['pad'], f['rscene'], f['rphase'], f['cur'], f['ph'], f['nx'],
                  f['pd'], f['ep'], f['a9'], f['a21'], f['rpad'], f['ic'], f['rd'], f['mo'], f['so'], f['ent']))
    for (c, a), n in sorted(S['O'].items()):
        oth.append('O %s %x %d' % (c, a, n))
    # unsplit totals: H hits per CPU in the 204 file, O hits in the 1 file, a high-RAM line in each
    cov = {}
    for cpu in ('m', 's'):
        p204, p1 = {}, {}
        for h in S['H']:
            if h['cpu'] == cpu:
                p204[h['pc']] = p204.get(h['pc'], 0) + h['n']
        for (c, a), n in S['O'].items():
            if c == cpu:
                p1[a] = p1.get(a, 0) + n
        p204[0x06004000] = 1000
        p100 = {a: 1 for a in S['hi100']} if cpu == 'm' else {}
        p100[0x06004002] = 5
        cov[cpu] = {'204': p204, '1': p1, '100': p100, '101': {0x06004004: 9}}
    if S.get('break_conservation'):
        S['break_conservation'](cov, low, oth)
    for cpu in ('m', 's'):
        tot = []
        allc = 0
        for ph, d in cov[cpu].items():
            (rd / ('cov-%s-%s.txt' % (cpu, ph))).write_text(''.join('%x %d\n' % (a, n) for a, n in sorted(d.items())))
            tot.append('%s phase %s %d' % (cpu, ph, sum(d.values())))
            allc += sum(d.values())
        (rd / 'cov-totals.txt').write_text(((rd / 'cov-totals.txt').read_text() if (rd / 'cov-totals.txt').exists() else '') + '%s all %d\n' % (cpu, allc) + '\n'.join(tot) + '\n')
    for fname, lines in (('cov-low6.txt', low), ('cov-f6.txt', f6), ('cov-other6.txt', oth)):
        if fname not in bad_files:
            (rd / fname).write_text('\n'.join(lines) + '\n')
    rec = {'runs': {name: dict(image='scn', kind=kind, build=str(b), control_build='', source_ev=str(src), gate6_run=S.get('gate6_run', ''))}}
    (ev / 'receipt.json').write_text(json.dumps(rec))
    return ev, b


def score(root, S, extra=(), bad_files=(), kind='reset', name='reset', after=None):
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)
    ev, b = serialize(S, root, kind, name, bad_files)
    if after:
        after(ev, b, root)
    return subprocess.run([sys.executable, str(LOWPCS3), str(ev)] + list(extra), capture_output=True, text=True)


def run_cases(case, tmp):
    root = tmp / 'v3'
    base = clean()
    p = score(root, base)
    case('5 lowpcs v3: a verified clean accepted reset (real reset trace) passes', p.returncode == 0 and 'accepted reset(s)' in p.stdout and '1 accepted reset' in p.stdout)
    if p.returncode:
        print(p.stdout[-2500:])
    case('5 lowpcs v3: all 8 transition symbols accounted for in the run', all(s in p.stdout for s in
         ('_art_preload', '_fzx_cup_reset', '_session_defaults', '_sh2z_scene_request', '_title_enter', '_title_preload', '_title_span', '_title_tick')))

    def mut(fn):
        S = copy.deepcopy(base)
        fn(S)
        return S

    def negative(title, S, expect=None, **kw):
        p = score(root, S, **kw)
        ok = p.returncode == 1 and (expect is None or expect in p.stdout)
        case('%s -> lowpcs v3 exit 1%s' % (title, (' (%s)' % expect) if expect else ''), ok)
        if not ok:
            print(p.stdout[-1800:])

    ic = {r['field']: r['ic'] for r in real_rows()}
    # an allowlisted reset PC in RUN (before any request, no acceptance behind it)
    negative('5 allowlisted reset PC in RUN before the chord', mut(lambda S: S['H'].append(H(4830, '_session_defaults', 5, 9, ph=1, ic=ic[4830]))), 'm/204 _session_defaults')
    # inside live race work
    negative('5 allowlisted reset PC inside fzx_race_frame (racedepth 1) in RUN', mut(lambda S: S['H'].append(H(4833, '_title_tick', 5, 9, ph=1, rd=1, ic=ic[4833]))), 'm/204 _title_tick')
    negative('5 allowlisted reset PC inside fzx_race_frame in LEAVE after acceptance', mut(lambda S: S['H'].append(
        H(ACC + 1, '_session_defaults', 5, 9, ph=2, nx=1, rd=1, a9=A9 + 1, a21=A21 + 1, ic=ic[ACC + 1]))), 'm/LEAVE _session_defaults')
    # unaccepted chord: the request was refused (counters never move), the race goes on
    def unaccepted(S):
        for f in S['F']:
            f.update(cur=4, ph=1 if f['field'] < 4837 else f['ph'], nx=4, pd=NONE, a9=A9, a21=A21, ep=18)
            if f['rscene'] == 1:
                f['rscene'] = 4
        S['H'] = [H(ACC, '_sh2z_scene_request', 101, 110, 6, ph=1, a9=A9, ic=ic[ACC]), H(ACC, '_session_defaults', 120, 128, 9, ph=1, a9=A9, ic=ic[ACC])]
    negative('5 unaccepted chord (request refused, counters unchanged)', mut(unaccepted), 'm/204 _sh2z_scene_request')
    # chord not injected / not consumed / counters
    negative('5 accepted but the chord was never injected by the script', mut(lambda S: [f.update(pad=0) for f in S['F']]), 'chord not injected')
    negative('5 accepted but the chord was never consumed (report pad zero)', mut(lambda S: [f.update(rpad=0) for f in S['F']]), 'chord not injected')
    negative('5 accepted counter moved without the chord counter', mut(lambda S: [f.update(a21=A21) for f in S['F']]), 'counters moved')
    negative('5 latched chord: the accepted counter jumps by two', mut(lambda S: [f.update(a9=f['a9'] + 1) if f['a9'] > A9 else None for f in S['F']]), 'counters moved')

    def results_exit(S):
        for f in S['F']:
            if f['rscene'] == 1:
                f.update(cur=5, rscene=5)
    negative('5 a RACE -> RESULTS exit labelled as reset', mut(results_exit), 'not TITLE')

    def plain_results(S):
        for f in S['F']:
            f.update(a9=A9, a21=A21, pd=NONE, nx=f['cur'])
            if f['rscene'] == 1:
                f.update(cur=5, rscene=5)
        for h in S['H']:
            h.update(a9=A9, a21=A21)
        S['Q'] = []
    negative('5 an ordinary RESULTS exit with reset-work hits behind it', mut(plain_results), 'STRICT')
    # missing boundary evidence
    negative('5 no flip field in the trace', mut(lambda S: S.__setitem__('F', [f for f in S['F'] if f['field'] < 4870])), 'no flip field')
    negative('5 the acceptance field rows missing (PRELOAD work cannot be attributed to a later field)', mut(lambda S: S.__setitem__('F', [f for f in S['F'] if f['field'] < 4860])), 'no flip field')
    negative('5 no cov-f6.txt', base, 'cov-f6.txt missing', bad_files=('cov-f6.txt',))
    negative('5 no cov-low6.txt', base, 'cov-low6.txt missing', bad_files=('cov-low6.txt',))
    negative('5 no cov-other6.txt', base, 'cov-other6.txt missing', bad_files=('cov-other6.txt',))
    # C-133: the RUN check takes precedence over the reset allowlist (the pre-fix image: the three functions in .low)
    def prefix_rows(S):
        post = dict(a9=A9 + 1, a21=A21 + 1, ic=ic[ACC])
        S['H'].extend([H(ACC, '_sh2z_scene_request', 101, 104, 4, ph=1, a9=A9, ic=ic[ACC]),
                       H(ACC, '_sh2z_scene_request', 105, 110, 6, ph=1, pd=1, a9=A9, ic=ic[ACC]),
                       H(ACC, '_session_defaults', 120, 128, 9, ph=1, pd=1, **post),
                       H(ACC, '_fzx_cup_reset', 130, 140, 11, ph=1, pd=1, **post)])
    p = score(root, mut(prefix_rows))
    case('5 C-133: the pre-fix image (the three in .low, accepted request, phase RUN, depth 0) fails, naming all three',
         p.returncode == 1 and all(('m/204 %s' % n) in p.stdout for n in ('_sh2z_scene_request', '_session_defaults', '_fzx_cup_reset')))
    negative('5 C-133: field 4835, valid accepted request, a post-Q allowlisted PC, RUN, depth 0',
             mut(lambda S: S['H'].append(H(ACC, '_session_defaults', 120, 128, 9, ph=1, pd=1, a9=A9 + 1, a21=A21 + 1, ic=ic[ACC]))), 'm/204 _session_defaults')
    negative('5 C-133: a post-Q allowlisted PC in RUN even with a Q event and the marker ordered before it',
             mut(lambda S: (S['Q'].append(dict(field=ACC, ord=100, target=1, rank=3, a9=A9, pd=NONE)),
                            S['H'].append(H(ACC, '_fzx_cup_reset', 130, 140, 11, ph=1, pd=1, a9=A9 + 1, a21=A21 + 1, ic=ic[ACC])))), 'm/204 _fzx_cup_reset')
    negative('5 C-133: the pre-fix rows with RUN masked do not exempt anything else (LEAVE race work still fails)',
             mut(lambda S: (prefix_rows(S), S['H'].append(H(ACC + 1, '_fzx_race_frame', 5, 90, 50, ph=2, nx=1, rd=1, a9=A9 + 1, a21=A21 + 1, ic=ic[ACC + 1])))),
             'm/LEAVE _fzx_race_frame', extra=['--mask-phase', 'RUN'])
    # unlisted function, slave, after the flip, wrong epoch
    negative('5 an unlisted .low function after the accepted request', mut(lambda S: S['H'].append(H(4872, '_cold_other', 5, 9, ph=4, nx=1, a9=A9 + 1, a21=A21 + 1, ic=ic[4872]))), 'm/PRELOAD _cold_other')
    negative('5 a slave .low hit while the race runs', mut(lambda S: S['H'].append(H(4840, '_title_tick', 5, 9, cpu='s', ph=2, nx=1, a9=A9 + 1, a21=A21 + 1, ic=ic[4840]))), 's/LEAVE _title_tick')
    negative('5 a transition hit after the flip field (scene RACE epoch 18)', mut(lambda S: S['H'].append(H(FLIP + 1, '_art_preload', 5, 9, ph=4, nx=1, a9=A9 + 1, a21=A21 + 1, ic=ic[FLIP + 1]))), 'm/PRELOAD _art_preload')
    negative('5 a hit whose epoch has no F row', mut(lambda S: S['H'].append(H(4840, '_title_tick', 5, 9, ep=77, ph=2, ic=ic[4840]))), 'no-epoch-row')
    # a race function in .low: the census-hot cause, in RUN and in a mixed LEAVE field
    S = mut(lambda S: S['H'].extend([H(4830, '_fzx_race_frame', 5, 90, 50, ph=1, rd=1, ic=ic[4830]), H(4831, '_fzx_race_frame', 5, 90, 50, ph=1, rd=1, ic=ic[4831])]))
    negative('5 control: fzx_race_frame in .low in an ordinary running race', S, 'm/204 _fzx_race_frame')
    S = mut(lambda S: S['H'].append(H(ACC + 1, '_fzx_race_frame', 5, 90, 50, ph=2, nx=1, rd=1, a9=A9 + 1, a21=A21 + 1, ic=ic[ACC + 1])))
    negative('5 control: a low race-work hit in a mixed LEAVE field (a clean reset beside it)', S, 'm/LEAVE _fzx_race_frame')
    negative('5 control: the same with the RUN rows masked (the reset boundary cannot hide it)', S, 'm/LEAVE _fzx_race_frame', extra=['--mask-phase', 'RUN'])
    p = score(root, base, extra=['--mask-phase', 'RUN'])
    case('5 control: the clean reset with RUN rows masked still passes', p.returncode == 0)
    # strict phase 100
    S = mut(lambda S: S.__setitem__('hi100', [SYMS['_title_tick']]))
    negative('5 a .low PC before main (phase 100)', S, 'strict phases')
    # conservation, both CPUs
    def drop_h(cov, low, oth):
        cov['m']['204'][SYMS['_art_preload']] = cov['m']['204'].get(SYMS['_art_preload'], 0) + 1
    negative('5 conservation: master H rows + O differ from the unsplit totals', mut(lambda S: S.__setitem__('break_conservation', drop_h)), 'do not reconcile')

    def drop_s(cov, low, oth):
        cov['s']['204'][SYMS['_cold_other']] = 4
    negative('5 conservation: slave hits only in the unsplit totals', mut(lambda S: S.__setitem__('break_conservation', drop_s)), 'do not reconcile')
    def drop_o(cov, low, oth):
        oth[:] = [l for l in oth if not l.startswith('O m %x ' % SYMS['_title_span'])]

    def drop_hrow(cov, low, oth):
        low.pop(0)
    negative('5 conservation: an O row lost from the trace', mut(lambda S: S.__setitem__('break_conservation', drop_o)), 'do not reconcile')
    negative('5 conservation: an H row lost from the trace', mut(lambda S: S.__setitem__('break_conservation', drop_hrow)), 'do not reconcile')
    # wrong map / image / bin
    negative('5 wrong map: map missing', base, 'map or symbols missing', after=lambda ev, b, r: (b / 'fzs-engine.map').unlink())
    negative('5 wrong image: BIN differs from the source receipt', base, 'does not match', after=lambda ev, b, r: (b / 'fzs-engine.bin').write_bytes(b'other'))
    negative('5 wrong image: not an image of the source receipt', base, 'unknown image',
             after=lambda ev, b, r: (r / 'src' / 'receipt.json').write_text(json.dumps({'builds': {'other': {'bin': 'x'}}})))
    negative('5 run without the explicit mapping', base, 'no explicit image', after=lambda ev, b, r: (ev / 'receipt.json').write_text(json.dumps({'runs': {'reset': {'rc': 0}}})))
    negative('5 run folder not in the receipt', base, 'not in the receipt', after=lambda ev, b, r: (ev / 'runs' / 'stray').mkdir())
    # replay of the gate6 run
    g6 = root / 'g6'
    def with_g6(ev, b, r):
        d = r / 'g6'
        d.mkdir()
        for f in (ev / 'runs' / 'reset').glob('cov-*.txt'):
            if f.name.startswith('cov-m-') or f.name.startswith('cov-s-') or f.name == 'cov-totals.txt':
                shutil.copy(f, d / f.name)
        rec = json.loads((ev / 'receipt.json').read_text())
        rec['runs']['reset']['gate6_run'] = str(d)
        (ev / 'receipt.json').write_text(json.dumps(rec))
    p = score(root, base, after=with_g6)
    case('5 replay: PC totals equal the gate6 run -> passes', p.returncode == 0)

    def with_g6_off(ev, b, r):
        with_g6(ev, b, r)
        f = r / 'g6' / 'cov-m-204.txt'
        f.write_text(f.read_text() + '241000 1\n')
    negative('5 replay: PC totals differ from the gate6 run', base, 'differ from the gate6 run', after=with_g6_off)
    # a loop run needs a verified reset
    negative('5 loop run with no verified accepted reset', mut(plain_results), 'no verified accepted reset', kind='loop', name='loop')
