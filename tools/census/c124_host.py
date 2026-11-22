"""c124_host.py: HOST fixtures for the C-124 amendments (claude-mem4), extended in C-132 with section 5 (lowpcs v3, c132_cases.py). Exit 0 when every case behaves as required.
1 --base-vars ignores a corrupt shared cache (the afeadea gate is shown aborting on it), builds only with the base flags
  and leaves the cache byte-identical. 2 ctl-smpc counts only at its intended stop and with its paired arm; the matrix
  check refuses ctl-smpc without its fields. 3/4 lowpcs: a clean synthetic census passes; every missing artifact,
  unknown image, unmapped run, totals mismatch, bad moved field and strict hit fails."""
import importlib.util, json, shutil, struct, subprocess, sys, tempfile
from pathlib import Path
from types import SimpleNamespace

WT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
LOWPCS = HERE / 'lowpcs-v2.py'      # the C-124 census (cases 3/4); v3 (lowpcs.py, C-132) is exercised in c132_cases.py
results = []


def case(name, ok):
    results.append((name, ok))
    print('%-72s %s' % (name, 'ok' if ok else 'WRONG'))


def load(path, mod):
    spec = importlib.util.spec_from_file_location(mod, path)
    m = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(WT / 'tools'))
    spec.loader.exec_module(m)
    return m


eg = load(WT / 'tools' / 'engine_gate.py', 'eg_new')
tmp = Path(tempfile.mkdtemp())
try:
    old_src = subprocess.run(['git', '-C', str(WT), 'show', 'afeadea:tools/engine_gate.py'], capture_output=True, text=True, check=True).stdout
    (tmp / 'eg_old.py').write_text(old_src)
    eg_old = load(tmp / 'eg_old.py', 'eg_old')

    # 1 --base-vars with a corrupt shared cache
    for mod, label in ((eg, 'new'), (eg_old, 'afeadea')):
        g = mod.Gate.__new__(mod.Gate)
        d = tmp / ('bv-' + label); d.mkdir()
        g.ev = d; g.a = SimpleNamespace(base_vars='BASEFLAG=7', receipts=str(d), rebuild_base=False, identity_vars='HEADFLAG=3')
        g.m = {'images': {'unit': {'vars': ['NORMAL=1']}}}
        calls = []; g.prepare_tree = calls.append
        built = []
        g.build = lambda path, img, vs, built=built: (built.append(vs), {'ok': True, 'bin': 'b' * 64} if vs == ['NORMAL=1', 'BASEFLAG=7'] else {'ok': False})[1]
        cache = d / 'base.bins.json'; cache.write_bytes(b'{"unit": {"bin": "stale", "vars": "NORMAL=1"')   # truncated JSON
        before = cache.read_bytes()
        try:
            got = g.base_bins('base', ['unit'], {'sha': 'head'}); err = None
        except ValueError as e:
            got, err = None, e
        if label == 'new':
            case('1 base-vars: override built with the base flags only (NORMAL=1 BASEFLAG=7)', got == {'unit': 'b' * 64} and built == [['NORMAL=1', 'BASEFLAG=7']])
            case('1 base-vars: base tree, then back to the head', calls == ['base', 'head'])
            case('1 base-vars: corrupt shared cache untouched (byte-identical)', cache.read_bytes() == before)
        else:
            case('1 control: the afeadea gate aborts the override on the corrupt cache', err is not None and got is None)

    # 2 ctl-smpc pairing and its matrix rules
    g = eg.Gate.__new__(eg.Gate)
    d = tmp / 'smpc'; d.mkdir(); g.ev = d
    g.a = SimpleNamespace(control_names='')
    sym = d / 'sym'; sym.mkdir(); (sym / 'fzs-engine.sym').write_text('06000000 B _fzx_smpc_quiet\n')
    g.ctl_builds = {'ctl-smpc': {'dir': str(sym)}, 'arm-smpc2': {'dir': str(sym)}}
    g.record_ms = lambda *a: 0
    ctl = {'name': 'ctl-smpc', 'check': 'loop', 'kind': 'loop', 'image': 'i', 'how': 'x', 'vars': ['C=1'], 'pair': 'arm-smpc2',
           'expect_error': 'until timed out: until 1 1 3000', 'expect_open': 'fzx_smpc_quiet', 'expect_mark_not': 'title'}
    arm = {'name': 'arm-smpc2', 'check': 'loop', 'kind': 'loop', 'image': 'i', 'how': 'y', 'vars': ['C=2'],
           'expect_mark': 'title', 'expect_pass': 'fzx_smpc_quiet'}
    g.selected_controls = lambda: [ctl, arm]
    for n in ('ctl-smpc', 'arm-smpc2'):
        (d / 'runs' / n).mkdir(parents=True)

    def setup(c_err, c_words, c_marks, a_ok, a_words, a_marks):
        rc, ra = d / 'runs' / 'ctl-smpc', d / 'runs' / 'arm-smpc2'
        (rc / 'error.txt').write_text(c_err + '\nfield 3682\n')
        (rc / 'error.wram-high').write_bytes(struct.pack('>II', *c_words)); (rc / 'error.wram-low').write_bytes(b'')
        (rc / 'result.txt').write_text(''.join('mark %s field %d\n' % (m, i) for i, m in enumerate(c_marks)))
        (ra / 'result.txt').write_text(''.join('mark %s field %d\n' % (m, i) for i, m in enumerate(a_marks)))
        for f in ra.glob('title.*'):
            f.unlink()
        if a_words:
            (ra / 'title.wram-high').write_bytes(struct.pack('>II', *a_words)); (ra / 'title.wram-low').write_bytes(b'')
        g.score_scenes = lambda c, rd, *a, sink, **kw: sink.append(dict(
            check='loop', subject=c['name'], value='v',
            status='FAIL' if c['name'] == 'ctl-smpc' else ('PASS' if a_ok else 'FAIL')))
        g.controls = []
        g.score_controls({'i': {'dir': str(sym)}}, {})
        return {r['control']: r['status'] for r in g.controls}

    T = 'until timed out: until 1 1 3000'
    good_arm = (True, (3, 3), ['title', 'select', 'reboot'])
    cases = [
        ('2 ctl-smpc: hang before title, one open, arm balanced at title -> counts', (T, (2, 1), []) + good_arm, 'PASS', 'PASS'),
        ('2 ctl-smpc: sections closed (begun = ended) -> does not count', (T, (2, 2), []) + good_arm, 'FAIL', 'PASS'),
        ('2 ctl-smpc: two sections open -> does not count', (T, (3, 1), []) + good_arm, 'FAIL', 'PASS'),
        ('2 ctl-smpc: another timeout -> does not count', ('untilword timed out: x', (2, 1), []) + good_arm, 'FAIL', 'PASS'),
        ('2 ctl-smpc: title reached -> does not count', (T, (2, 1), ['title']) + good_arm, 'FAIL', 'PASS'),
        ('2 pair: arm loop fails -> arm and ctl-smpc do not count', (T, (2, 1), [], False, (3, 3), ['title']), 'FAIL', 'FAIL'),
        ('2 pair: arm counters unbalanced at title -> neither counts', (T, (2, 1), [], True, (3, 2), ['title']), 'FAIL', 'FAIL'),
        ('2 pair: arm counters zero -> neither counts', (T, (2, 1), [], True, (0, 0), ['title']), 'FAIL', 'FAIL'),
        ('2 pair: arm never reached title (no dump) -> neither counts', (T, (2, 1), [], True, None, ['select']), 'FAIL', 'FAIL'),
    ]
    for name, args, want_c, want_a in cases:
        st = setup(*args)
        case(name, st.get('ctl-smpc') == want_c and st.get('arm-smpc2') == want_a)
    g.selected_controls = lambda: [ctl]
    st = setup(T, (2, 1), [], *good_arm)
    case('2 pair: arm not selected -> ctl-smpc does not count', st.get('ctl-smpc') == 'FAIL')
    for drop in ('expect_open', 'expect_mark_not', 'expect_error', 'pair'):
        m = {'controls': [dict((k, v) for k, v in ctl.items() if k != drop), arm]}
        try:
            eg.check_controls(m); ok = False
        except SystemExit:
            ok = True
        case('2 matrix: ctl-smpc without %s is refused' % drop, ok)
    m = {'controls': [dict(ctl, expect_mark_not='results1'), arm]}
    try:
        eg.check_controls(m); ok = False
    except SystemExit:
        ok = True
    case('2 matrix: ctl-smpc with expect_mark_not other than title is refused', ok)
    try:
        eg.check_controls(json.loads((WT / 'tools' / 'engine_gate_matrix.json').read_text())); ok = True
    except SystemExit:
        ok = False
    case('2 matrix: the repo matrix passes its own check', ok)

    # 3/4 lowpcs on a synthetic census
    def census(root, mutate=None):
        if root.exists():
            shutil.rmtree(root)
        b = root / 'builds' / 'scn'; b.mkdir(parents=True)
        (b / 'fzs-engine.map').write_text('.low            0x00240000      0x100 load address 0x06040000\n .text.title_enter\n                0x00240000       0x40 x.o\n .text.cold\n                0x00240040       0x40 x.o\n')
        (b / 'fzs-engine.sym').write_text('00240000 t _title_enter\n00240040 t _cold\n06004000 T _main\n')
        r = root / 'builds' / 'fld'; r.mkdir(parents=True)
        (r / 'fzs-engine.map').write_text('.text  0x06004000 0x100\n'); (r / 'fzs-engine.sym').write_text('06004000 T _main\n')
        runs = {'loop': dict(image='scn', kind='loop', build=str(b), control_build=''),
                'race-field': dict(image='fld', kind='race', build=str(r), control_build='')}
        files = {
            'loop': {'cov-m-100.txt': '6004000 5\n', 'cov-s-100.txt': '', 'cov-m-101.txt': '6004000 10\n', 'cov-s-101.txt': '',
                     'cov-m-204.txt': '6004000 100\n', 'cov-s-204.txt': '6004000 7\n', 'cov-m-214.txt': '240000 3\n6004000 2\n', 'cov-s-214.txt': '6004000 1\n',
                     'cov-m-1.txt': '240000 4\n', 'cov-exit-fields.txt': '501 4 1 f00 5 1\n',
                     'cov-totals.txt': 'm all 124\nm phase 1 4\nm phase 100 5\nm phase 101 10\nm phase 204 100\nm phase 214 5\ns all 8\ns phase 100 0\ns phase 101 0\ns phase 204 7\ns phase 214 1\n',
                     'fields.txt': '499 4 0\n500 4 0\n501 1 0\n502 1 0\n'},
            'race-field': {'cov-m-100.txt': '6004000 1\n', 'cov-m-101.txt': '6004000 1\n', 'cov-m-201.txt': '6004000 9\n',
                           'cov-exit-fields.txt': '', 'cov-totals.txt': 'm all 11\nm phase 100 1\nm phase 101 1\nm phase 201 9\ns all 0\n'}}
        rec = {'builds': {'scn': {}, 'fld': {}}, 'runs': runs}
        if mutate:
            mutate(rec, files, root)
        for n, fs in files.items():
            rd = root / 'runs' / n; rd.mkdir(parents=True, exist_ok=True)
            for f, t in fs.items():
                if t is not None:
                    (rd / f).write_text(t)
        (root / 'receipt.json').write_text(json.dumps(rec))
        return subprocess.run([sys.executable, str(LOWPCS), str(root)], capture_output=True, text=True)

    root = tmp / 'census'
    p = census(root)
    case('3 lowpcs: clean synthetic census passes (exit 0)', p.returncode == 0)
    if p.returncode:
        print(p.stdout[-1500:])

    def drop(run, f):
        return lambda rec, files, root: files[run].__setitem__(f, None)

    def setf(run, f, t):
        return lambda rec, files, root: files[run].__setitem__(f, t)

    negatives = [
        ('3 no census files at all', lambda rec, files, root: files.__setitem__('loop', {'fields.txt': '1 1\n'})),
        ('3 cov-totals.txt missing', drop('loop', 'cov-totals.txt')),
        ('3 cov-m-204 missing in a loop run', lambda rec, files, root: (files['loop'].__setitem__('cov-m-204.txt', None),
            files['loop'].__setitem__('cov-totals.txt', files['loop']['cov-totals.txt'].replace('m phase 204 100\n', '').replace('m all 124', 'm all 24')))),
        ('3 cov-m-201 missing in a race run', lambda rec, files, root: (files['race-field'].__setitem__('cov-m-201.txt', None),
            files['race-field'].__setitem__('cov-totals.txt', 'm all 2\nm phase 100 1\nm phase 101 1\ns all 0\n'))),
        ('3 map missing', lambda rec, files, root: rec.__setitem__('runs', dict(rec['runs'], loop=dict(rec['runs']['loop'], build=str(root / 'nowhere'))))),
        ('3 unknown image', lambda rec, files, root: rec['runs']['loop'].__setitem__('image', 'scenes-mystery')),
        ('3 run without explicit mapping (old receipt)', lambda rec, files, root: rec['runs'].__setitem__('loop', {'rc': 0})),
        ('3 run folder not in the receipt', lambda rec, files, root: files.__setitem__('stray', dict(files['race-field']))),
        ('3 control build not under ctl-builds', lambda rec, files, root: rec['runs']['loop'].__setitem__('control_build', 'ctl-x')),
        ('3 empty receipt runs', lambda rec, files, root: rec.__setitem__('runs', {})),
        ('3 .low with no text', setf('loop', 'cov-m-1.txt', '240000 4\n') if False else
         (lambda rec, files, root: None)),
        ('4 phase file disagrees with totals', setf('loop', 'cov-s-204.txt', '6004000 6\n')),
        ('4 phases do not sum to all', lambda rec, files, root: files['loop'].__setitem__('cov-totals.txt', files['loop']['cov-totals.txt'].replace('m all 124', 'm all 125'))),
        ('4 214 total != moved counts', setf('loop', 'cov-exit-fields.txt', '501 4 1 f00 4 1\n')),
        ('4 moved field without the reset chord', setf('loop', 'cov-exit-fields.txt', '501 4 1 400 5 1\n')),
        ('4 moved field RACE -> RESULTS', setf('loop', 'cov-exit-fields.txt', '501 4 5 f00 5 1\n')),
        ('4 moved field not in the trace', setf('loop', 'fields.txt', '499 4 0\n500 4 0\n501 4 0\n')),
        ('4 loop run with no moved field', lambda rec, files, root: (files['loop'].__setitem__('cov-exit-fields.txt', ''),
            files['loop'].__setitem__('cov-m-214.txt', None), files['loop'].__setitem__('cov-s-214.txt', None),
            files['loop'].__setitem__('cov-totals.txt', files['loop']['cov-totals.txt'].replace('m phase 214 5\n', '').replace('s phase 214 1\n', '').replace('m all 124', 'm all 119').replace('s all 8', 's all 7')))),
        ('4 214 ran a .low function off the reviewed list', setf('loop', 'cov-m-214.txt', '240040 3\n6004000 2\n')),
        ('3 master .low PC in 100', lambda rec, files, root: (files['loop'].__setitem__('cov-m-100.txt', '6004000 4\n240040 1\n'))),
        ('3 slave .low PC in 204', lambda rec, files, root: (files['loop'].__setitem__('cov-s-204.txt', '6004000 6\n240040 1\n'))),
        ('3 .low PC in standalone race 201', lambda rec, files, root: (rec['runs']['race-field'].update(image='scn', build=str(root / 'builds' / 'scn')),
            files['race-field'].__setitem__('cov-m-201.txt', '6004000 8\n240040 1\n'))),
    ]
    for name, mut in negatives:
        if name == '3 .low with no text':
            continue
        p = census(root, mut)
        case('%s -> lowpcs exit 1' % name, p.returncode == 1)
    # a map whose .low has no .text: build it by hand
    p = census(root, lambda rec, files, root: None)
    (root / 'builds' / 'scn' / 'fzs-engine.map').write_text('.low            0x00240000      0x100 load address 0x06040000\n')
    p = subprocess.run([sys.executable, str(LOWPCS), str(root)], capture_output=True, text=True)
    case('3 .low with no text -> lowpcs exit 1', p.returncode == 1)
    # the old lowpcs (v1) on the no-census and missing-map evidence: Codex's false green, reproduced
    v1 = HERE / 'lowpcs-v1.py'
    census(root, lambda rec, files, root: files.__setitem__('loop', {'fields.txt': '1 1\n'}))
    p = subprocess.run([sys.executable, str(v1), str(root)], capture_output=True, text=True)
    case('3 control: lowpcs v1 passes the no-census evidence (the false green)', p.returncode == 0)
    # 5 lowpcs v3 (C-132): real harness rows, the clean reset, the negatives and the controls
    sys.path.insert(0, str(HERE))
    import c132_cases
    c132_cases.run_cases(case, tmp)
finally:
    shutil.rmtree(tmp, ignore_errors=True)

bad = [n for n, ok in results if not ok]
print('\n%d cases, %d wrong%s' % (len(results), len(bad), (': ' + '; '.join(bad)) if bad else ''))
sys.exit(1 if bad else 0)
