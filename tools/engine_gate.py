#!/usr/bin/env python3
"""Engine gate: the gate matrix of AGENTS.md (Workflow) for one engine commit, with a pass/fail table and a JSON receipt.

Every lane and the integrator run this one script (docs/RETRO_PLAN_20261120.md, Start doing 1). It builds the images in
a private WSL stage, runs them under the one Ymir harness (ymir-gate), scores them and writes a receipt. Tiers: HOST
(fzx-phys), EMULATOR (Ymir); every timing is MODELLED. Hardware is never run here. See tools/ENGINE_GATE.md.

    python tools/engine_gate.py --quick                    # lanes: Mute City, sound on, about 10 minutes
    python tools/engine_gate.py --full --controls          # integrator: the whole matrix and every control
    python tools/engine_gate.py --full --rev HEAD --base origin/engine --label my-lane

Exit status: 0 when every check passes (and, with --controls, every control fails as it should); 1 otherwise.
Nothing ROM-derived leaves the stage or the evidence folder: the receipt holds hashes, counts and timings only.
"""
import argparse, collections, concurrent.futures as cf, copy, datetime, hashlib, json, os, re, shutil, struct, subprocess, sys, time
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
TOOLS = REPO / 'tools'
GATE = TOOLS / 'engine_gate'
# Local paths: environment first, then <evidence root>/host/tools/ymir-gate/local.json (outside git), then defaults
# relative to the evidence root ($FZX_EVIDENCE, else ~/fzx-evidence) and to this checkout.
EV_ROOT = Path(os.environ.get('FZX_EVIDENCE') or Path.home() / 'fzx-evidence')
HARNESS_DIR = EV_ROOT / 'host' / 'tools' / 'ymir-gate'
LOCAL = json.loads((HARNESS_DIR / 'local.json').read_text()) if (HARNESS_DIR / 'local.json').exists() else {}


def local(env, key, default):
    return Path(os.environ.get(env) or LOCAL.get(key) or default)


DEF_EVIDENCE = local('FZX_GATE_EVIDENCE', 'evidence', EV_ROOT / 'host' / 'claude-gate')
DEF_RECEIPTS = local('FZX_GATE_RECEIPTS', 'receipts', EV_ROOT / 'host' / 'gate-receipts')
DEF_HARNESS = local('FZX_GATE_HARNESS', 'harness', HARNESS_DIR / 'ymir-gate.exe')
DEF_BIOS = local('FZX_GATE_BIOS', 'bios', HARNESS_DIR / 'bios.bin')
DEF_SH2ZAM = local('FZX_GATE_SH2ZAM', 'sh2zam', REPO.parent / 'SH2ZAM')
DEF_PRACTICE = local('FZX_GATE_PRACTICE', 'practice_expected', EV_ROOT / 'host' / 'practice-expected' / 'practice-expected.json')
DEF_MUSIC = local('FZX_GATE_MUSIC', 'music', EV_ROOT / 'host' / 'claude-snd' / 'music-all')
DISTRO = os.environ.get('FZX_GATE_DISTRO', 'Ubuntu-24.04')
FIELD_MS = 1000.0 / 59.94
PAINT = 0xa55aa55a


def log(msg):
    print('[%s] %s' % (time.strftime('%H:%M:%S'), msg), flush=True)


def wslpath(p):
    p = str(Path(p).resolve())
    return '/mnt/%s%s' % (p[0].lower(), p[2:].replace('\\', '/'))


def pct(a, q):
    b = sorted(a)
    return b[min(len(b) - 1, int(q * len(b)))] if b else 0.0


def margin_ok(p99, lim):
    """ENGINE_PLAN 5 gate 9: the unrounded critical-path p99 times the margin stays within the budget (33.3 ms x 1.36).
    Exact rational arithmetic on the measured value and the matrix's decimal limits, so the boundary does not depend on
    float rounding (C-127 finding 1: 24.49 ms is 33.3064 ms and fails). Returns (ok, product in ms)."""
    from fractions import Fraction
    prod = Fraction(p99) * Fraction(repr(lim['critical_margin']))
    return prod <= Fraction(repr(lim['critical_budget_ms'])), float(prod)


def mean(a):
    return sum(a) / len(a) if a else 0.0


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def git(*args):
    return subprocess.run(['git', '-C', str(REPO)] + list(args), check=True, capture_output=True, text=True).stdout.strip()


# ---------------------------------------------------------------------------------------------------------------------
# Reading an image's memory and its frame rows
# ---------------------------------------------------------------------------------------------------------------------

def load_syms(path):
    sym = {}
    for line in Path(path).read_text().splitlines():
        p = line.split()
        if len(p) == 3:
            sym.setdefault(p[2].split('.lto_priv.')[0], int(p[0], 16))
    return sym


class Mem:
    """Work RAM from a harness dump pair (NAME.wram-high at 0x06000000, NAME.wram-low at 0x00200000)."""

    def __init__(self, run_dir, name, sym):
        self.hi = (Path(run_dir) / (name + '.wram-high')).read_bytes()
        self.lo = (Path(run_dir) / (name + '.wram-low')).read_bytes()
        self.sym = sym

    def raw(self, a, n):
        a &= 0x07ffffff
        if 0x06000000 <= a < 0x06100000:
            return self.hi[a - 0x06000000:a - 0x06000000 + n]
        return self.lo[a - 0x00200000:a - 0x00200000 + n]

    def words(self, s, n):
        return struct.unpack('>%dI' % n, self.raw(self.sym['_' + s], 4 * n))

    def has(self, s):
        return '_' + s in self.sym

    def master_stack_unused(self):
        lo, hi = self.words('fzx_sky_stack_start', 1)[0], self.words('fzx_sky_stack_end', 1)[0]
        a = lo
        while a < hi and struct.unpack('>I', self.raw(a, 4))[0] == PAINT:
            a += 4
        return a - lo


def frame_rows(mem, window_tick):
    """The engine's frame rows (fzx_e0_report: rows at [3], count at [1], FRT ticks per field at [12]).
    Returns per-row timings in ms over the window (from tick window_tick on drive images, every row on E0)."""
    field = mem.has('fzx_field_report')
    drive = mem.has('fzx_e1_report')
    rw, fmt = (60, '>3I16H2h3H2B2H') if field else (48, '>3I16H2h') if drive else (36, '>3I12H')
    rep = mem.words('fzx_e0_report', 16)
    ms = FIELD_MS / rep[12] if rep[12] else 0.0
    rows = [struct.unpack(fmt, mem.raw(rep[3] + rw * i, rw)) for i in range(rep[1])]
    idx = [i for i, x in enumerate(rows) if x[0] >= window_tick] if drive else list(range(len(rows)))
    balrows = None
    if mem.has('fzx_bal_rows'):
        balrows = struct.unpack('>6144H', mem.raw(mem.sym['_fzx_bal_rows'], 2 * 6144))
    iv, prev = [], None
    for i in idx:
        x = rows[i]
        if x[2]:
            if prev is not None:
                iv.append(x[2] - prev)
            prev = x[2]
    sel = [rows[i] for i in idx]
    if field:
        busy = [(x[15] + x[4] + x[22] + x[23] + x[18]) * ms for x in sel]
    elif drive:
        busy = [(x[15] + x[4] + x[16] + x[18]) * ms for x in sel]
    else:
        busy = [x[4] * ms for x in sel]
    clouds = [((balrows[i] & 0x7fff) if balrows and i < 6144 and balrows[i] & 0x8000 else 0) * ms for i in idx]
    slave = [x[6] * ms + c for x, c in zip(sel, clouds)]
    crit = [b + x[5] * ms for b, x in zip(busy, sel)]
    vdp1 = [x[13] * ms for x in sel if x[2]]
    hist = collections.Counter(min(v, 3) for v in iv)
    return dict(rows=len(rows), window=len(sel), intervals=len(iv), h1=hist[1], h2=hist[2], h3=hist[3],
                over_two=sum(1 for v in iv if v > 2), worst=max(iv) if iv else 0,
                fps=round(59.94 * len(iv) / max(1, sum(iv)), 2),
                master=[round(mean(busy), 2), round(pct(busy, .99), 2), round(max(busy, default=0), 2)],
                slave=[round(mean(slave), 2), round(pct(slave, .99), 2), round(max(slave, default=0), 2)],
                critical=[round(mean(crit), 2), round(pct(crit, .99), 2), round(max(crit, default=0), 2)],
                critical_p99_raw=pct(crit, .99),   # unrounded: the margin judge (gate 9) uses this
                vdp1_p99=round(pct(vdp1, .99), 2), road_cmds_max=rep[9])


# ---------------------------------------------------------------------------------------------------------------------
# Harness outputs
# ---------------------------------------------------------------------------------------------------------------------

def read_result(run_dir):
    p = Path(run_dir) / 'result.txt'
    return p.read_text().splitlines() if p.exists() else []


def result_kv(lines, prefix=''):
    """'NAME value' lines (optionally under a mark prefix) as a dict; checkpoints as an ordered list."""
    kv, chk = {}, []
    for line in lines:
        p = line.split()
        if prefix:
            if not p or p[0] != prefix:
                continue
            p = p[1:]
        if len(p) >= 3 and p[0] == 'chk':
            chk.append((int(p[1]), p[2]))
        elif len(p) == 2:
            kv[p[0]] = p[1]
    return kv, chk


def marks(lines):
    out = {}
    for line in lines:
        p = line.split()
        if len(p) >= 4 and p[0] == 'mark':
            out[p[1]] = int(p[3])
    return out


def error_text(run_dir):
    p = Path(run_dir) / 'error.txt'
    return p.read_text().splitlines()[0] if p.exists() else ''


def host_ref(path):
    chk, final, finish, retire = [], None, None, None
    for line in Path(path).read_text().splitlines():
        p = line.split()
        if p and p[0] == 'chk':
            chk.append((int(p[1]), p[2]))
        elif p and p[0] == 'ticks':
            final = p[3]
        elif p and p[0] == 'finish_ms':
            finish = None if p[1] == '-' else int(p[1])
            retire = None if p[3] == '-' else int(p[3])
    return dict(chk=chk, final=final, finish_ms=finish, retire_tick=retire)


# ---------------------------------------------------------------------------------------------------------------------
# The gate
# ---------------------------------------------------------------------------------------------------------------------

def check_controls(matrix):
    """Matrix rules for controls that count only at an intended stop (C-124): a control with expect_open also names its
    harness error and expect_mark_not; ctl-smpc stops before title and is paired with a test arm that reaches title
    with its sections balanced (expect_pass)."""
    by = {c['name']: c for c in matrix.get('controls', [])}
    for c in by.values():
        if c.get('expect_open') and not (c.get('expect_error') and c.get('expect_mark_not')):
            raise SystemExit('matrix: control %s has expect_open without expect_error and expect_mark_not' % c['name'])
        if c.get('pair') and not by.get(c['pair'], {}).get('expect_pass'):
            raise SystemExit('matrix: control %s pairs with %s, which is not an expect_pass arm' % (c['name'], c['pair']))
    s = by.get('ctl-smpc')
    if s and not (s.get('expect_open') and s.get('expect_mark_not') == 'title' and s.get('expect_error') and s.get('pair')):
        raise SystemExit('matrix: ctl-smpc must keep expect_error, expect_open, expect_mark_not=title and its pair')


def _swap(a, i, j):
    a[i], a[j] = a[j], a[i]
    return a


# Practice fixture controls: each edits a copy of what the image reported (the expected rows are never touched).
PR_OBS_MUT = {
    'roster': lambda o: dict(o, roster=_swap(o['roster'], 1, 2)),          # two rivals' sprites swapped: still 29 distinct machines
    'entrants': lambda o: dict(o, machine=_swap(o['machine'], 3, 4)),      # two rows' machines swapped in the field
    'grid': lambda o: dict(o, grid=_swap(o['grid'], 3, 4)),                # still places 1..30, wrong order
    'gridsrc': lambda o: dict(o, src=2),                                    # the right grid from the wrong source
    'seed': lambda o: dict(o, ident=o['ident'][:2] + [o['ident'][2] + 1] + o['ident'][3:]),
    'count': lambda o: dict(o, ident=o['ident'][:1] + [o['ident'][1] - 1] + o['ident'][2:]),
}
PR_ARCH_MUT = {
    'bundle': lambda a: dict(a, bundle_rows_match=False, bundle_diffs=['control: a row of the linked bundle edited']),
}


class Gate:
    def __init__(self, args, matrix):
        self.a, self.m = args, matrix
        check_controls(matrix)
        self.lim = matrix['limits']
        self.ev = Path(args.evidence) / args.label
        self.stage = args.stage
        self.checks, self.controls = [], []
        self.discs, self.results = {}, {}
        self.times = {}
        self.ev.mkdir(parents=True, exist_ok=True)
        (self.ev / 'tmp').mkdir(exist_ok=True)

    # -- WSL stage ---------------------------------------------------------------------------------------------------
    def stage_cmd(self, *args, check=True):
        cmd = ['wsl', '-d', DISTRO, '-u', 'root', '-e', 'bash', wslpath(GATE / 'stage.sh')] + [str(a) for a in args]
        r = subprocess.run(cmd, capture_output=True, text=True)
        out = (r.stdout + r.stderr).strip()
        if check and r.returncode:
            raise RuntimeError('stage %s failed:\n%s' % (args[0], out[-3000:]))
        return r.returncode, out

    def ensure_lib(self, pin):
        tar = self.ev / 'tmp' / ('lib-%s.tar' % pin)
        if not tar.exists():
            subprocess.run(['git', '-C', str(self.a.sh2zam), 'archive', '-o', str(tar), pin], check=True)
        self.stage_cmd('lib', self.stage, pin, wslpath(tar))

    def prepare_tree(self, rev):
        """Stage REV's tree, its SH2ZAM pin and the legacy submodule; regenerate stale ROM-free outputs; list receipts."""
        sha = git('rev-parse', rev)
        pin = git('show', '%s:saturn/engine/sh2zam.pin' % sha).strip()
        sub = git('ls-tree', sha, 'third_party/SH2ZAM').split()[2][:7]
        tar = self.ev / 'tmp' / ('tree-%s.tar' % sha[:12])
        git('archive', '-o', str(tar), sha)
        self.ensure_lib(pin)
        self.ensure_lib(sub)
        self.stage_cmd('tree', self.stage, wslpath(tar), sub)
        regen = []
        for g in self.m.get('regenerate', []):
            rc, out = self.stage_cmd('regen', self.stage, g['receipt'], *g['cmd'].replace('{engine_pin}', pin).split(), check=False)
            regen.append(out.splitlines()[-1] if out else 'REGEN %s rc %d' % (g['receipt'], rc))
        _, out = self.stage_cmd('receipts', self.stage)
        stale = [l.split()[1] for l in out.splitlines() if l.startswith('STALE ')]
        tar.unlink()
        log('tree %s: pin %s, legacy submodule %s; %s' % (sha[:12], pin, sub, '; '.join(regen)))
        return dict(sha=sha, pin=pin, submodule=sub, regenerated=regen, stale_receipts=stale)

    def build(self, where, name, vars_):
        rc, out = self.stage_cmd('build', self.stage, wslpath(where), name, *vars_, check=False)
        d = Path(where) / name
        if rc or not (d / 'fzs-engine.bin').exists():
            return dict(name=name, vars=vars_, ok=False, error=out[-1500:])
        if not self.a.keep:
            (d / 'fzs-engine.map').unlink(missing_ok=True)
        self.archive_practice(d)
        return dict(name=name, vars=vars_, ok=True, bin=sha256(d / 'fzs-engine.bin'), dir=str(d),
                    outdir=(d / 'outdir.txt').read_text().strip())

    # -- harness -------------------------------------------------------------------------------------------------------
    def harness(self, build_dir, run_dir, mode, arg, cap, audio=False, cue=None):
        run_dir = Path(run_dir)
        if run_dir.exists():
            shutil.rmtree(run_dir)
        run_dir.parent.mkdir(parents=True, exist_ok=True)
        env = dict(os.environ, GATE_AUDIO='1' if audio else '0')
        # Machine rule (owner, 2026-11-20): every Ymir launch waits for a free slot. FZX_GATE_SLOT_CMD is a command
        # that exits 0 when a run may start (the lane skill's ymir_slot.sh); it is retried every 60 s.
        slot = os.environ.get('FZX_GATE_SLOT_CMD')
        while slot and subprocess.run(slot, shell=True, capture_output=True).returncode != 0:
            time.sleep(60)
        t0 = time.time()
        r = subprocess.run([str(self.a.harness), str(self.a.bios), str(cue or Path(build_dir) / 'fzs-engine.cue'),
                            str(Path(build_dir) / 'fzs-engine.sym'), str(run_dir), str(cap), mode, arg],
                           capture_output=True, text=True, env=env)
        (run_dir.parent / (run_dir.name + '.log')).write_text(
            '\n'.join(l for l in (r.stdout + r.stderr).splitlines() if not l.startswith(('debug', 'info'))))
        return dict(rc=r.returncode, seconds=round(time.time() - t0, 1), cue=str(cue or Path(build_dir) / 'fzs-engine.cue'))

    # -- the shipping disc ---------------------------------------------------------------------------------------------
    def mixed_disc(self, build_dir):
        """The shipping mixed-mode disc for a sound-on image: data track 1 from its ISO plus every music track, as
        tools/disc_cdda.py lays it out (--ids all). The music tracks are written once per gate run and hard-linked into
        each image's disc. Validated: the image's compiled CD track table equals the music's, the CUE lists 1 + N
        tracks (MODE1/2352, then AUDIO), and every file has its TOC length. Returns (cue or None, detail)."""
        sys.path.insert(0, str(TOOLS))
        import disc_cdda
        music = Path(self.a.music)
        manifest = json.loads((music / 'manifest.json').read_text())['tracks']
        ids = [t['music_id'] for t in manifest]
        if not hasattr(self, 'cdda_table_ok'):
            tmp = self.ev / 'tmp' / 'cdda'
            disc_cdda.write_header(tmp / 'cdda_tracks.h', [dict(music_id=a, track=k + 2, sectors=t['sectors'], loop_sector=t['loop_sector'])
                                                           for k, t in enumerate(manifest) for a in [t['music_id']] + t.get('aliases', [])])
            _, compiled = self.stage_cmd('cdda', self.stage)
            self.cdda_table_ok = (tmp / 'cdda_tracks.h').read_text().strip() == compiled.strip()
        if not self.cdda_table_ok:
            return None, "the image's CD track table (generated/cdda/cdda_tracks.h) differs from the music manifest"
        shared = self.ev / 'discs' / 'audio'
        if not (shared / 'fzs-engine.cue').exists():
            disc_cdda.build(Path(build_dir) / 'fzs-engine.iso', music, ids, shared, 'fzs-engine')
        out = Path(build_dir) / 'mixed'
        shutil.rmtree(out, ignore_errors=True)
        disc_cdda.build(Path(build_dir) / 'fzs-engine.iso', music, [], out, 'fzs-engine')   # data track 1 only
        for k in range(len(ids)):
            name = 'fzs-engine (Track %02d).bin' % (k + 2)
            os.link(shared / name, out / name)
        shutil.copy(shared / 'fzs-engine.cue', out / 'fzs-engine.cue')
        cue = (out / 'fzs-engine.cue').read_text()
        files = re.findall(r'FILE "([^"]+)" BINARY', cue)
        modes = re.findall(r'TRACK (\d\d) (\S+)', cue)
        bad = []
        if len(files) != 1 + len(ids) or len(modes) != 1 + len(ids):
            bad.append('CUE lists %d tracks, want %d' % (len(modes), 1 + len(ids)))
        if not modes or modes[0][1] != 'MODE1/2352' or any(m != 'AUDIO' for _, m in modes[1:]):
            bad.append('track modes %s' % [m for _, m in modes][:3])
        iso_sectors = (Path(build_dir) / 'fzs-engine.iso').stat().st_size // 2048
        if (out / files[0]).stat().st_size != iso_sectors * 2352:
            bad.append('track 1 length')
        for f, t in zip(files[1:], manifest):
            if (out / f).stat().st_size != (150 + t['sectors']) * 2352:
                bad.append('%s length' % f)
        audio_sectors = sum(150 + t['sectors'] for t in manifest)
        detail = '%d tracks: data %d sectors, audio %d sectors (%.1f min); track 1 %s' % (
            len(modes), iso_sectors, audio_sectors, (iso_sectors + audio_sectors) / 4500, sha256(out / files[0])[:12])
        return (None if bad else out / 'fzs-engine.cue'), ('; '.join(bad) if bad else detail)

    def record_ms(self, hosts, build_dir, offset=0):
        """The Mute City record a loop script waits for (RECORD_MS): the image's own R01 HOST finish (fzx-phys, the
        player alone), plus OFFSET for the control that must time out. This line has no attack HOST (fzx-atk): an
        image whose symbols hold fzx_atk_chk stops the gate rather than wait for the wrong record."""
        if ' _fzx_atk_chk' in (Path(build_dir) / 'fzs-engine.sym').read_text():
            raise SystemExit('RECORD_MS: %s is built with ENG_ATTACKS and this gate has no fzx-atk reference' % build_dir)
        if 'mute-city-1' not in hosts:
            hosts['mute-city-1'] = self.host(self.ev / 'host', 'mute-city-1', '0.004,2.5')
        ref = hosts['mute-city-1']
        if not ref or ref['finish_ms'] is None:
            raise SystemExit('no Mute City HOST finish for RECORD_MS (%s)' % build_dir)
        return ref['finish_ms'] + offset

    def script_for(self, name, course_index=None, record_ms=None):
        text = (GATE / 'scripts' / name).read_text()
        if course_index is not None:
            text = text.replace('COURSE', '%02x' % course_index)
        if 'RECORD_MS' in text:
            if record_ms is None:
                raise SystemExit('%s waits for RECORD_MS: no record given' % name)
            text = text.replace('RECORD_MS', str(record_ms))
        if not self.a.clips:
            text = '\n'.join(l for l in text.splitlines() if not l.startswith('clip ')) + '\n'
        # The record is in the file name: ctl-record's loop.txt (RECORD_MS + 1) must not overwrite the loop's.
        p = self.ev / 'scripts' / ('%s-%s%s' % (course_index if course_index is not None else 'x',
                                                'r%d-' % record_ms if record_ms is not None else '', name))
        p.parent.mkdir(exist_ok=True)
        p.write_text(text)
        return p

    # -- checks ------------------------------------------------------------------------------------------------------
    def add(self, check, subject, ok, value='', detail='', info=False, sink=None):
        row = dict(check=check, subject=subject, status='INFO' if info else ('PASS' if ok else 'FAIL'),
                   value=value, detail=detail)
        (self.checks if sink is None else sink).append(row)
        return row

    def score_race(self, run, run_dir, build_dir, host, sink=None):
        """A race image run to its end (race harness)."""
        lines = read_result(run_dir)
        kv, chk = result_kv(lines)
        name = run['name']
        ok = kv.get('exit') == 'ok' and int(kv.get('status', 99)) < 16
        self.add('run', name, ok, 'status %s, %s fields' % (kv.get('status', '-'), kv.get('fields', '-')),
                 error_text(run_dir), sink=sink)
        if not ok:
            return None
        sym = load_syms(Path(build_dir) / 'fzs-engine.sym')
        mem = Mem(run_dir, 'final', sym)
        fr = frame_rows(mem, self.lim['race_window_tick'])
        self.cadence_checks(name, fr, sink)
        if host is not None:
            self.host_check(name, kv, chk, host, full_final=True, sink=sink)
        self.memory_checks(name, mem, kv, sink)
        return fr

    def cadence_checks(self, name, fr, sink=None, margin=True):
        self.add('cadence', name, fr['over_two'] <= self.lim['over_two'],
                 '1F/2F/3F+ %d/%d/%d, over two %d, worst %dF, %.2f fps' % (fr['h1'], fr['h2'], fr['h3'], fr['over_two'], fr['worst'], fr['fps']),
                 '%d intervals, rows %d (window %d)' % (fr['intervals'], fr['rows'], fr['window']), sink=sink)
        self.add('timing', name, True, 'master %s | slave %s | critical %s ms (mean/p99/max); VDP1 p99 %s' % (
            '/'.join(map(str, fr['master'])), '/'.join(map(str, fr['slave'])), '/'.join(map(str, fr['critical'])), fr['vdp1_p99']),
            'MODELLED (Ymir)', info=True, sink=sink)
        if margin:
            ok, product = margin_ok(fr['critical_p99_raw'], self.lim)
            self.add('margin', name, ok,
                     'critical p99 %.4f ms x %s = %.4f ms (limit %s)' % (
                         fr['critical_p99_raw'], self.lim['critical_margin'], product, self.lim['critical_budget_ms']),
                     'ENGINE_PLAN 5 gate 9 (unrounded p99 x margin <= budget); MODELLED', sink=sink)

    def host_check(self, name, kv, chk, host, full_final, sink=None, prefix='', against=''):
        """SH-2 against HOST: every checkpoint the image wrote equals HOST's, the finish (or retirement) matches, and
        for a race run to the reference's end, the final state hash too."""
        if host is None:
            self.add('host', name, False, 'no HOST reference (fails closed)', sink=sink)
            return
        e1 = lambda i: kv.get('%s_fzx_e1_report%d' % (prefix, i), kv.get('e1_report%d' % i))
        n = len(chk)
        hostc = dict(host['chk'])
        same = sum(1 for t, h in chk if hostc.get(t) == h)
        fin_ms = int(e1(2) or 0)
        finished = e1(3) == '1'
        notes = []
        ok = n > 0 and same == n
        if full_final:
            final = '%08x%08x' % (int(e1(6) or 0), int(e1(7) or 0))
            ok = ok and final == host['final'] and n == len(host['chk'])
            notes.append('final %s %s' % (final, 'equal' if final == host['final'] else '!= HOST %s' % host['final']))
        if host['finish_ms'] is not None:
            ok = ok and finished and fin_ms == host['finish_ms']
            notes.append('finish %d ms, HOST %d' % (fin_ms, host['finish_ms']))
        else:
            ok = ok and not finished
            notes.append('retires as HOST (tick %s)' % host['retire_tick'] if not finished else 'finished; HOST retires')
        self.add('host', name, ok, 'checkpoints %d/%d equal%s' % (same, n, (' (HOST %s)' % against) if against else ''),
                 '; '.join(notes), sink=sink)

    def memory_checks(self, name, mem, kv, sink=None, prefix=''):
        g = lambda s: kv.get('%s_fzx_%s' % (prefix, s), kv.get('_fzx_%s' % s))
        ms = mem.master_stack_unused()
        ss, trips = int(g('slave_road_stack_unused') or 0), int(g('slave_road_guard_trips') or 0)
        ok = ms >= self.lim['master_stack_min'] and ss >= self.lim['slave_stack_min'] and trips <= self.lim['slave_guard_trips']
        self.add('memory', name, ok, 'master stack unused %d B, slave %d B, guard trips %d' % (ms, ss, trips),
                 'floors %d / %d / %d' % (self.lim['master_stack_min'], self.lim['slave_stack_min'], self.lim['slave_guard_trips']), sink=sink)

    def menus_check(self, name, lines, sink=None):
        worst = over = 0
        where = {}
        for m in marks(lines):
            kv, _ = result_kv(lines, m)
            worst = max(worst, int(kv.get('_fzx_scn_ctl6', 0)))
            n = int(kv.get('_fzx_scn_ctl7', 0))
            over = max(over, n)
            # fzx_scn_gap_at[k & 7] (a harness with the gap dump): scene << 16 | phase << 8 | fields of the k-th gap
            for k in range(max(1, n - 7), n + 1):
                v = int(kv.get('_fzx_scn_gap_at%d' % (k & 7), 0))
                if v and k not in where:
                    where[k] = '#%d by %s: scene %d phase %d, %d fields' % (k, m, v >> 16, (v >> 8) & 0xFF, v & 0xFF)
        detail = 'fzx_scn_ctl[7], [6] at every mark'
        if where:
            detail += '; gaps (fzx_scn_gap_at): ' + '; '.join(where[k] for k in sorted(where))
        self.add('menus', name, over == 0 and worst <= self.lim['menu_worst_fields'],
                 'menu frames over two %d, worst gap %d fields' % (over, worst), detail, sink=sink)

    def score_scenes(self, run, run_dir, build_dir, host, sink=None, host_label=''):
        lines = read_result(run_dir)
        kv, _ = result_kv(lines)
        name, kind = run['name'], run['kind']
        mk = marks(lines)
        ok = kv.get('exit') == 'ok'
        self.add('script', name, ok, '%d marks, %s fields' % (len(mk), kv.get('fields', '-')), error_text(run_dir), sink=sink)
        sym = load_syms(Path(build_dir) / 'fzs-engine.sym')
        if kind == 'loop':
            r = {}
            for m in ('results1', 'results2'):
                if m in mk:
                    mkv, mchk = result_kv(lines, m)
                    r[m] = (mkv, mchk)
            if len(r) == 2:
                (k1, c1), (k2, c2) = r['results1'], r['results2']
                f1, f2 = k1.get('_fzx_e1_report2'), k2.get('_fzx_e1_report2')
                stills = all((Path(run_dir) / ('race%d_t2000.rgba' % i)).exists() for i in (1, 2))
                same_still = stills and sha256(Path(run_dir) / 'race1_t2000.rgba') == sha256(Path(run_dir) / 'race2_t2000.rgba')
                eq = bool(c1) and c1 == c2 and f1 == f2 and f1 not in (None, '0') and same_still
                self.add('loop', name, eq, 'cold %s ms / retry %s ms; checkpoints %d / %d %s; tick-2000 stills %s' % (
                    f1, f2, len(c1), len(c2), 'equal' if c1 == c2 else 'DIFFERENT', 'equal' if same_still else 'DIFFERENT'), sink=sink)
                if host is not None:
                    self.host_check(name + ' cold', k1, c1, host, full_final=False, sink=sink, prefix='')
                for m in ('results1', 'results2'):
                    mem = Mem(run_dir, m, sym)
                    self.cadence_checks('%s %s' % (name, m), frame_rows(mem, self.lim['race_window_tick']), sink, margin=False)
                    self.memory_checks('%s %s' % (name, m), mem, r[m][0], sink)
            else:
                self.add('loop', name, False, 'marks results1/results2 missing', error_text(run_dir), sink=sink)
            if ok:
                self.add('reboot', name, 'reboot' in mk, 'records survive a reboot (script waits for the saved name)', sink=sink)
        elif kind == 'reset':
            fm = [m for m in mk if m.startswith('from_')]
            fires = []
            good = ok and len(fm) > 0
            for m in fm:
                mkv, _ = result_kv(lines, m)
                fires.append(int(mkv.get('_fzx_scn_report21', -1)))
                good = good and mkv.get('_fzx_scn_report1') == '1'
            good = good and fires == sorted(set(fires)) and len(fm) == run.get('expect_marks', len(fm))
            self.add('reset', name, good, '%d scenes reset to TITLE (%s); fires %s' % (len(fm), ' '.join(m[5:] for m in fm), fires),
                     error_text(run_dir), sink=sink)
        elif kind == 'cancel':
            cm = [m for m in mk if m.startswith('cancel')]
            good = ok and len(cm) == 3
            notes = []
            for m in cm:
                mkv, _ = result_kv(lines, m)
                good = good and mkv.get('_fzx_scn_report1') == '1' and mkv.get('_fzx_scn_report11') == '3'
                notes.append('%s scene %s load %s' % (m, mkv.get('_fzx_scn_report1'), mkv.get('_fzx_scn_report11')))
            raced = 'race_after' in mk and result_kv(lines, 'race_after')[0].get('_fzx_scn_report1') == '4'
            good = good and raced
            self.add('cancel', name, good, '%d of 3 loads cancelled to TITLE; next cold load races: %s' % (
                sum(1 for n in notes if n.endswith('scene 1 load 3')), 'yes' if raced else 'no'), '; '.join(notes) or error_text(run_dir), sink=sink)
        elif kind == 'practice':
            self.score_practice(run, run_dir, build_dir, lines, mk, sym, host, host_label, sink)
        elif kind == 'course':
            if 'results' in mk:
                mkv, mchk = result_kv(lines, 'results')
                if host is not None:
                    self.host_check(name, mkv, mchk, host, full_final=False, sink=sink, against=host_label)
                mem = Mem(run_dir, 'results', sym)
                self.cadence_checks(name, frame_rows(mem, self.lim['race_window_tick']), sink)
                self.memory_checks(name, mem, mkv, sink)
                load = result_kv(lines, 'race_start')[0].get('_fzx_scn_report10')
                self.add('load', name, True, 'cold LOAD %s fields' % load, info=True, sink=sink)
            else:
                self.add('host', name, False, 'no RESULTS mark', error_text(run_dir), sink=sink)
        self.menus_check(name, lines, sink)

    # -- Practice course rows (claude/modes; C-166 item 1, C-169 item 1) ---------------------------------------------------
    # The expectation is the HOST reference's own rows (tools/practice_expected.py reads the four fzx-gp-ref reports per course),
    # never the stage's generated bundle: that is the file the image links, and a bundle that agrees with itself says nothing. The
    # image's live state at race_start (the field's identity, the machines its rows and its sprites were built from, the grid and
    # where it came from) is compared with it exactly. The expected file and the stage bundle's identity are archived next to
    # the BIN (practice-identity.json) when the image is built, so a rescore needs neither the stage nor the current tree.
    PR_CLASSES = 4

    @staticmethod
    def parse_practice_rows(text):
        vals = [int(x) for x in re.findall(r'-?\d+', text.split('= {', 1)[1])] if '= {' in text else []
        return vals

    def archive_practice(self, d):
        """After a build whose image carries the live field identity: write practice-identity.json (BIN hash, the expected rows
        and their hash, the stage bundle's text hashes, and whether the bundle's rows equal the expected ones)."""
        d = Path(d)
        if '_fzx_field_ident' not in (d / 'fzs-engine.sym').read_text():
            return
        sys.path.insert(0, str(TOOLS))
        import practice_expected
        doc = dict(schema='fzx-practice-identity-1', bin=sha256(d / 'fzs-engine.bin'))
        try:
            exp = practice_expected.load(self.a.practice_expected)
            doc['expected'] = exp
            doc['expected_sha256'] = exp['sha256']
        except (SystemExit, OSError, ValueError) as e:
            doc['error'] = 'expected rows: %s' % e
            (d / 'practice-identity.json').write_text(json.dumps(doc, indent=1, sort_keys=True) + '\n')
            return
        texts = {}
        for n in ('course_practice.c', 'course_practice.h', 'course_practice.receipt'):
            rc, out = self.stage_cmd('cat', self.stage, 'generated/courses/select/' + n, check=False)
            texts[n] = out if rc == 0 else None
        doc['bundle'] = {n: (hashlib.sha256(t.encode()).hexdigest() if t is not None else None) for n, t in texts.items()}
        diffs = []
        rows = self.parse_practice_rows(texts['course_practice.c'] or '')
        n = len(self.m['courses'])
        if len(rows) != n * self.PR_CLASSES * 30 * 8:
            diffs.append('the stage bundle has %d values, want %d' % (len(rows), n * self.PR_CLASSES * 30 * 8))
        else:
            for c in self.m['courses']:
                for k in range(self.PR_CLASSES):
                    e = exp['courses'].get(c['slug'], {}).get(str(k))
                    if e is None:
                        diffs.append('%s class %d: not in the expected rows' % (c['slug'], k))
                        continue
                    base = (c['index'] * self.PR_CLASSES + k) * 240
                    vr = [rows[base + 8 * i:base + 8 * i + 8] for i in range(30)]
                    got = dict(grid=[v[0] for v in vr], energy=[v[3] for v in vr], machine=[v[5] for v in vr])
                    for key in ('grid', 'energy', 'machine'):
                        if got[key] != e[key]:
                            diffs.append('%s class %d %s' % (c['slug'], k, key))
        doc['bundle_diffs'] = diffs[:12]
        doc['bundle_rows_match'] = not diffs
        (d / 'practice-identity.json').write_text(json.dumps(doc, indent=1, sort_keys=True) + '\n')

    @staticmethod
    def practice_archive(build_dir):
        p = Path(build_dir) / 'practice-identity.json'
        if not p.is_file():
            return None, 'no practice-identity.json next to the BIN (the image was not built by this gate with the expected rows)'
        doc = json.loads(p.read_text())
        if doc.get('error'):
            return None, doc['error']
        if doc.get('bin') != sha256(Path(build_dir) / 'fzs-engine.bin'):
            return None, 'the archived identity is of another BIN'
        sys.path.insert(0, str(TOOLS))
        import practice_expected
        if doc['expected'].get('sha256') != practice_expected.digest(doc['expected']) or doc.get('expected_sha256') != doc['expected']['sha256']:
            return None, 'the archived expected rows do not match their hash'
        return doc, ''

    def practice_observed(self, mem, sym):
        """The live state at race_start, as the image holds it."""
        return dict(
            ident=list(mem.words('fzx_field_ident', 8)),
            machine=list(mem.raw(sym['_fzx_field_machine'], 30)),
            roster=list(mem.raw(sym['_fzx_e2_roster_machine'], 30)),
            roster_set=list(mem.words('fzx_e2_roster_set', 30)),
            grid=list(mem.raw(sym['_fzx_field_grid'], 30)),
            src=mem.words('fzx_field_grid_src', 1)[0],
            report=mem.words('fzx_field_report', 16)[0])

    @staticmethod
    def first_diff(got, want):
        d = [i for i in range(min(len(got), len(want))) if got[i] != want[i]]
        return '%d of %d differ (first at vehicle %d: %s, want %s)' % (len(d), len(want), d[0], got[d[0]], want[d[0]]) if d else 'length %d, want %d' % (len(got), len(want))

    def practice_fixture(self, name, run_dir, build_dir, lines, mk, sym, want, mutate=None, sink=None, mutate_arch=None):
        """The Practice fixture at RACE entry: the mode, class, course and pack identity, the player's machine and skin, the slider
        bits, and the live field against the frozen expected row: the vehicle count, the seed, the 30 entrants (the machine each
        vehicle's row and each rival's sprites were built from) and the ordered start grid, which must come from the course header
        (fzx_field_grid_src 1) and equal the HOST grid exactly. Fails closed on a missing archive, expected row or mark.
        want: slug, index, row, mode, cls. mutate: a control's edit of a copy of the observed state (the oracle is never touched)."""
        why = []
        if 'race_start' not in mk:
            self.add('fixture', name, False, 'no race_start mark', error_text(run_dir), sink=sink)
            return False
        arch, err = self.practice_archive(build_dir)
        if arch is not None and mutate_arch:
            arch = mutate_arch(copy.deepcopy(arch))
        if arch is None:
            self.add('fixture', name, False, 'FIXTURE FAILED, %s' % err, 'the expected rows are archived with the BIN when the gate builds it', sink=sink)
            return False
        exp = arch['expected']['courses'].get(want['slug'], {}).get(str(want['cls']))
        if exp is None:
            self.add('fixture', name, False, 'FIXTURE FAILED, no expected row for %s class %d' % (want['slug'], want['cls']), sink=sink)
            return False
        if not arch.get('bundle_rows_match'):
            why.append('the linked bundle differs from the HOST rows (%s)' % '; '.join(arch.get('bundle_diffs', [])[:3]))
        mem = Mem(run_dir, 'race_start', sym)
        ses = struct.unpack('>HBBHBBBBBBI', mem.raw(sym['_fzx_session'], 16))
        version, mode, course, machine, skin, players, diff, cup, pr, conf, slider = ses
        if mode != want['mode']:
            why.append('mode %d, want %d' % (mode, want['mode']))
        if diff != want['cls']:
            why.append('class %d, want %d' % (diff, want['cls']))
        if course != want['index']:
            why.append('course %d, want %d (%s)' % (course, want['index'], want['slug']))
        if mem.words('fzx_cid_index', 1)[0] != want['row']:
            why.append('course row %d, want %d' % (mem.words('fzx_cid_index', 1)[0], want['row']))
        if machine != exp['machine'][0] or skin != 1 or slider != 0:
            why.append('player machine %d skin %d slider bits %x, want %d / 1 / 0' % (machine, skin, slider, exp['machine'][0]))
        obs = self.practice_observed(mem, sym)
        if mutate:
            obs = mutate(copy.deepcopy(obs))
        ident = obs['ident']
        if obs['report'] != 1:
            why.append('field not loaded (report %d: solo or failed)' % obs['report'])
        if ident[0] != 0x46494431:
            why.append('no live field identity (magic %08x)' % ident[0])
        if ident[1] != exp['count']:
            why.append('field count %d, want %d' % (ident[1], exp['count']))
        if ident[2] != exp['seed']:
            why.append('field seed %d, want %d' % (ident[2], exp['seed']))
        if (ident[3], ident[4], ident[5]) != (want['row'], want['cls'], want['mode']):
            why.append('the field was built for row %d class %d mode %d, want %d / %d / %d' % (ident[3], ident[4], ident[5], want['row'], want['cls'], want['mode']))
        if obs['machine'] != exp['machine']:
            why.append('the field\'s entrants: %s' % self.first_diff(obs['machine'], exp['machine']))
        if obs['roster'][1:] != exp['machine'][1:]:
            why.append('the sprites\' roster: %s' % self.first_diff(obs['roster'][1:], exp['machine'][1:]))
        rs = obs['roster_set'][1:]
        mach = obs['roster'][1:]
        if (len(set(rs)) != len(set(mach)) or any(x == 0 or x & 3 for x in rs)
                or any((rs[a] == rs[b]) != (mach[a] == mach[b]) for a in range(len(rs)) for b in range(a))):
            why.append('rival sprite sets are not one per machine')
        if obs['src'] != 1:
            why.append('grid source %d, want 1 (the course header)' % obs['src'])
        if obs['grid'] != exp['grid']:
            why.append('start grid: %s' % self.first_diff(obs['grid'], exp['grid']))
        ok = not why
        seen = 'mode %d class %d course %d, row %d, machine %d skin %d, field %s of %d, seed %d, entrants and sprites = HOST rows %s, grid from the header = HOST grid %s' % (
            mode, diff, course, mem.words('fzx_cid_index', 1)[0], machine, skin, 'loaded' if obs['report'] == 1 else 'NOT loaded', ident[1], ident[2],
            'yes' if obs['machine'] == exp['machine'] and obs['roster'][1:] == exp['machine'][1:] else 'NO',
            'yes' if obs['src'] == 1 and obs['grid'] == exp['grid'] else 'NO')
        self.add('fixture', name, ok, ('Practice %s: %s' % (want['slug'], seen)) if ok else ('FIXTURE FAILED, %s; seen: %s' % ('; '.join(why), seen)),
                 ('expected rows %s (HOST reports); opponent machines %s' % (arch['expected_sha256'][:12], ' '.join(map(str, sorted(exp['machine'][1:]))))) if ok
                 else 'asked: Practice (mode %d) class %d, %s' % (want['mode'], want['cls'], want['slug']), sink=sink)
        return ok

    def practice_window(self):
        w = self.m.get('practice')
        if not w:
            raise SystemExit('matrix: no "practice" section (checkpoint period and window end)')
        text = (GATE / 'scripts' / 'course_pr.txt').read_text()
        if 'poke _fzx_e0_hold_tick %d
' % w['end_tick'] not in text:
            raise SystemExit('matrix: practice end_tick %d is not the window course_pr.txt holds at' % w['end_tick'])
        return w

    def practice_host(self, name, mchk, host, host_label, sink):
        """SH-2 against HOST at the Practice window's checkpoints. Fails closed: the HOST reference must exist and hold every expected
        checkpoint, the image must have written exactly the expected set (period and end tick are the matrix's, not whatever
        happened to be captured), and each pair must agree."""
        w = self.practice_window()
        want = list(range(w['checkpoint_period'], w['end_tick'] + 1, w['checkpoint_period']))
        if host is None or not host.get('chk'):
            self.add('host', name, False, 'no HOST reference (fails closed)', 'expected checkpoints at ticks %s' % want, sink=sink)
            return
        hostc = dict(host['chk'])
        miss_host = [t for t in want if t not in hostc]
        got = [t for t, _ in mchk]
        same = sum(1 for t, h in mchk if hostc.get(t) == h)
        ok = not miss_host and got == want and same == len(want)
        why = []
        if miss_host:
            why.append('HOST lacks %d of the expected checkpoints (%s)' % (len(miss_host), ','.join(map(str, miss_host[:4]))))
        if got != want:
            why.append('the image wrote ticks %s, expected %s' % (','.join(map(str, got[:20])) or 'none', '%d..%d every %d (%d)' % (want[0], want[-1], w['checkpoint_period'], len(want))))
        self.add('host', name, ok, ('checkpoints %d/%d equal (HOST %s; the expected set %d..%d every %d; no finish in Practice)' % (
            same, len(want), host_label, want[0], want[-1], w['checkpoint_period'])) if ok else ('HOST CHECK FAILED: %s; %d/%d equal' % ('; '.join(why) or 'a checkpoint differs', same, len(want))), sink=sink)

    def score_practice(self, run, run_dir, build_dir, lines, mk, sym, host, host_label, sink):
        """Practice course row: the fixture at RACE entry, then the cadence, margin and memory of the tick-420 window up to the explicit
        end-of-window mark (Practice has no finish), the SH-2 checkpoints against HOST (no finish asked), and the normal exit with
        zero race-record writes."""
        name = run['name']
        slug = run.get('course')
        c = next((x for x in self.m['courses'] if x['slug'] == slug), None)
        want = dict(slug=slug, index=c['index'] if c else -1, row=c['index'] if c else -1, mode=5, cls=1)
        self.practice_fixture(name, run_dir, build_dir, lines, mk, sym, want, sink=sink)
        if 'results' in mk:
            mkv, mchk = result_kv(lines, 'results')
            self.practice_host(name, mchk, host, host_label or slug, sink)
            mem = Mem(run_dir, 'results', sym)
            self.cadence_checks(name, frame_rows(mem, self.lim['race_window_tick']), sink)
            self.memory_checks(name, mem, mkv, sink)
            load = result_kv(lines, 'race_start')[0].get('_fzx_scn_report10')
            self.add('load', name, True, 'cold LOAD %s fields' % load, info=True, sink=sink)
        else:
            self.add('host', name, False, 'no end-of-window mark (results)', error_text(run_dir), sink=sink)
        if 'quit' in mk and 'race_start' in mk:
            a, b = Mem(run_dir, 'race_start', sym), Mem(run_dir, 'quit', sym)
            gens = (a.words('file_gen', 2), b.words('file_gen', 2)) if a.has('file_gen') else (None, None)
            kq = result_kv(lines, 'quit')[0]
            quit_ok = kq.get('_fzx_scn_report1') == '5' and b.words('fzx_scn_pause', 8)[4] == 1
            self.add('exit', name, quit_ok and gens[0] is not None and gens[0] == gens[1],
                     'QUIT from the pause menu reaches RESULTS (scene %s); record file generations %s before and %s after' % (
                         kq.get('_fzx_scn_report1'), gens[0], gens[1]), 'zero race-record writes', sink=sink)
        else:
            self.add('exit', name, False, 'no quit mark', error_text(run_dir), sink=sink)

    def determinism(self, name, dir_a, dir_b, dumps, sink=None, build_dir=None):
        """Two runs: per-field ticks and pixel hashes, work RAM, VRAM and stills identical. The matrix's
        determinism_mask (symbol, bytes) is cleared in work RAM first: the SMPC OREG buffer holds the RTC, which
        Ymir reads from the host clock."""
        a, b = Path(dir_a), Path(dir_b)
        diffs = []
        masks = []
        if build_dir is not None:
            sym = load_syms(Path(build_dir) / 'fzs-engine.sym')
            masks = [(sym[n], k) for n, k in self.m.get('determinism_mask', []) if n in sym]

        def masked(data, base):
            data = bytearray(data)
            for addr, k in masks:
                o = (addr & 0x07ffffff) - base
                if 0 <= o < len(data):
                    data[o:o + k] = bytes(min(k, len(data) - o))
            return bytes(data)
        if (a / 'fields.txt').read_bytes() != (b / 'fields.txt').read_bytes():
            diffs.append('fields.txt (tick/pixel per field)')
        for d in dumps:
            for ext in ('.wram-high', '.wram-low', '.vdp1-vram', '.vdp2-vram', '.rgba'):
                fa, fb = a / (d + ext), b / (d + ext)
                if fa.exists() and fb.exists():
                    base = 0x06000000 if ext == '.wram-high' else 0x00200000 if ext == '.wram-low' else None
                    da, db = fa.read_bytes(), fb.read_bytes()
                    if base is not None:
                        da, db = masked(da, base), masked(db, base)
                    if da != db:
                        diffs.append(d + ext)
        self.add('determinism', name, not diffs, 'two runs: %s' % ('identical (per-field ticks and pixels, RAM, VRAM)' if not diffs else 'DIFFERENT'),
                 ', '.join(diffs[:6]) or ('masked: %s' % ', '.join('%s %d B' % (n, k) for n, k in self.m.get('determinism_mask', []))), sink=sink)

    # -- the main flow ----------------------------------------------------------------------------------------------
    def run(self):
        a, m = self.a, self.m
        mode = 'full' if a.full else 'quick'
        t_start = time.time()
        head = self.prepare_tree(a.rev)
        self.head = head
        receipt = dict(gate='engine_gate.py', version=1, mode=mode, label=a.label, started=datetime.datetime.now().isoformat(timespec='seconds'),
                       rev=a.rev, head=head['sha'], base=None, stage=self.stage, harness=sha256(a.harness)[:16],
                       tiers='HOST (fzx-phys), EMULATOR (Ymir, timing MODELLED); HARDWARE not run')
        # Stale receipts the engine build itself does not read are reported, not failed.
        self.add('receipts', 'generated', True, '%d stale receipts' % len(head['stale_receipts']),
                 ' '.join(head['stale_receipts'][:12]) + ('; ' + '; '.join(head['regenerated']) if head['regenerated'] else ''), info=True)

        runs = [r for r in m['runs'] if mode in r['modes']]
        if a.only:
            want = set(a.only.split(','))
            runs = [r for r in runs if r['name'] in want or r['kind'] in want]
        if a.courses:
            courses = [c for c in m['courses'] if c['slug'] in a.courses.split(',')]
        else:
            courses = m['courses'] if mode == 'full' else [c for c in m['courses'] if c['slug'] == 'mute-city-1']
        ident = list(m['identity'][mode])
        need = list(dict.fromkeys(ident + [r['image'] for r in runs]))

        # Builds (one after another in the stage).
        t0 = time.time()
        builds = {}
        bdir = self.ev / 'builds'
        # The runs use each image as gated (the lane's switches at their defaults). With --identity-vars the
        # identity images are built a second time with those variables, under their own name, and only those
        # builds are compared with the base (before 2026-11-20 the runs used the identity builds instead).
        id_builds = {}
        for img in need:
            builds[img] = self.build(bdir, img, m['images'][img]['vars'])
            log('build %-16s %s' % (img, builds[img]['bin'][:16] if builds[img]['ok'] else 'FAILED'))
            self.add('build', img, builds[img]['ok'], builds[img].get('bin', '')[:16], builds[img].get('error', '')[-300:].replace('\n', ' '))
        for img in ident:
            if a.identity_vars:
                id_builds[img] = self.build(bdir, img + '.identity', m['images'][img]['vars'] + a.identity_vars.split())
                log('build %-16s %s (identity: %s)' % (img, id_builds[img]['bin'][:16] if id_builds[img]['ok'] else 'FAILED', a.identity_vars))
            else:
                id_builds[img] = builds[img]
        self.times['builds'] = round(time.time() - t0)
        self.head_builds = builds

        # The shipping mixed-mode disc (data + every CD audio track) for each sound-on image a run uses.
        t0 = time.time()
        for img in dict.fromkeys(r['image'] for r in runs):
            if m['images'][img].get('sound') and builds[img]['ok']:
                cue, detail = self.mixed_disc(builds[img]['dir'])
                self.discs[img] = cue
                self.add('disc', img, cue is not None, 'mixed-mode disc' if cue else 'no shipping disc', detail)
                log('disc %-17s %s' % (img, detail))
        self.times['discs'] = round(time.time() - t0)

        # Legacy recipe pair.
        if mode in m['legacy']['modes'] or a.legacy:
            t0 = time.time()
            leg = {}
            for kind in ('recipe', 'off'):
                rc, out = self.stage_cmd('legacy', self.stage, wslpath(self.ev / 'legacy'), kind, kind, check=False)
                got = re.search(r'LEGACY %s ([0-9a-f]{64})' % kind, out)
                leg[kind] = got.group(1) if got else None
                self.add('legacy', kind, bool(got) and got.group(1).startswith(m['legacy'][kind]),
                         (got.group(1)[:16] if got else 'build failed'), 'expected %s' % m['legacy'][kind][:16] if got else out[-300:].replace('\n', ' '))
            receipt['legacy'] = leg
            self.legacy_bins = leg
            self.times['legacy'] = round(time.time() - t0)

        # Identity against the base.
        base_bins = None
        if a.base:
            base_sha = git('rev-parse', a.base)
            receipt['base'] = base_sha
            base_bins = self.base_bins(base_sha, ident, head)
            for img in ident:
                hb, bb = id_builds[img].get('bin'), base_bins.get(img)
                self.add('identity', img, hb is not None and hb == bb, '%s vs base %s' % ((hb or '-')[:16], (bb or '-')[:16]),
                         'base %s%s%s' % (base_sha[:12], (' with ' + a.identity_vars) if a.identity_vars else '',
                                          ('; base with ' + a.base_vars) if a.base_vars else ''))
        if a.expect:
            for spec in a.expect.split(','):
                img, pre = spec.split('=')
                hb = builds.get(img, {}).get('bin', '')
                self.add('reported', img, hb.startswith(pre), '%s vs reported %s' % (hb[:16], pre))

        # HOST references.
        hosts = {}
        hdir = self.ev / 'host'
        slugs = set(r['course'] for r in runs if r.get('course'))
        if any(r['kind'] in ('course', 'practice') for r in runs):
            slugs |= set(c['slug'] for c in courses)
        if a.controls:
            slugs |= {'mute-city-1'}
        for slug in sorted(slugs):
            hosts[slug] = self.host(hdir, slug, '0.004,2.5')

        # Harness runs.
        t0 = time.time()
        jobs = []
        for r in runs:
            if not builds[r['image']]['ok']:
                continue
            if r['kind'] in ('course', 'practice'):
                for c in courses:
                    rr = dict(r, name='course-%s' % c['slug'], course=c['slug'])
                    jobs.append((rr, 'script', self.script_for(r['script'], c['index'])))
            elif r['kind'] == 'race':
                jobs.append((r, 'race', '-'))
                if r.get('determinism'):
                    jobs.append((dict(r, name=r['name'] + '.2', repeat=True), 'race', '-'))
            else:
                rec = self.record_ms(hosts, builds[r['image']]['dir']) if r['kind'] == 'loop' else None
                jobs.append((r, 'script', self.script_for(r['script'], record_ms=rec)))
                if r.get('determinism_full') and mode == 'full':
                    jobs.append((dict(r, name=r['name'] + '.2', repeat=True), 'script', self.script_for(r['script'], record_ms=rec)))
        ctl_jobs = self.control_jobs(builds, hosts) if a.controls else []
        results = self.execute(jobs + ctl_jobs)
        self.times['runs'] = round(time.time() - t0)
        self.results = results
        for r, kind, arg in jobs:
            if not r.get('repeat'):
                self.disc_check(r)

        # Scoring.
        for r, kind, arg in jobs:
            if r.get('repeat'):
                continue
            rd = self.ev / 'runs' / r['name']
            bd = builds[r['image']]['dir']
            host = hosts.get(r.get('course')) if r.get('course') else None
            if r['kind'] == 'race':
                self.score_race(r, rd, bd, host)
            else:
                self.score_scenes(r, rd, bd, host)
        for r, kind, arg in jobs:
            if r.get('repeat'):
                base_name = r['name'][:-2]
                dumps = ['final'] if r['kind'] == 'race' else list(marks(read_result(self.ev / 'runs' / base_name)))
                self.determinism(base_name, self.ev / 'runs' / base_name, self.ev / 'runs' / r['name'], dumps,
                                 build_dir=builds[r['image']]['dir'])
        if a.controls:
            self.score_controls(builds, hosts)

        receipt['builds'] = {k: dict(bin=v.get('bin'), vars=v['vars'], outdir=v.get('outdir')) for k, v in builds.items()}
        receipt['runs'] = {k: v for k, v in results.items()}
        receipt['checks'] = self.checks
        receipt['controls'] = self.controls
        self.times['total'] = round(time.time() - t_start)
        receipt['seconds'] = self.times
        failed = [c for c in self.checks if c['status'] == 'FAIL']
        ctl_bad = [c for c in self.controls if c['status'] != 'PASS']
        receipt['verdict'] = 'PASS' if not failed and not ctl_bad else 'FAIL'
        receipt['failed'] = ['%s %s' % (c['check'], c['subject']) for c in failed]
        self.write(receipt)
        if not a.keep:
            self.cleanup()
        return 0 if receipt['verdict'] == 'PASS' else 1

    def host(self, hdir, slug, driver):
        rc, out = self.stage_cmd('host', self.stage, wslpath(hdir), self.head['sha'][:12], slug, driver, check=False)
        f = Path(hdir) / ('%s-%s.txt' % (slug, driver))
        return host_ref(f) if f.is_file() else None

    def base_bins(self, base_sha, ident, head):
        """BIN hashes of the base's identity images: from the shared receipt cache, else built in this stage."""
        cache = Path(self.a.receipts) / ('%s.bins.json' % base_sha)
        vars_key = lambda img: ' '.join(self.m['images'][img]['vars'])
        if self.a.base_vars:
            # The base with extra vars: always built here, kept out of the shared cache (its entries mean default vars);
            # the cache is not read on this path, so a bad or unrelated cache file cannot stop it.
            if base_sha != head['sha']:
                self.prepare_tree(base_sha)
            got = {}
            for img in ident:
                b = self.build(self.ev / 'base-builds', img, self.m['images'][img]['vars'] + self.a.base_vars.split())
                if b['ok']:
                    got[img] = b['bin']
                log('base %-16s %s (with %s)' % (img, b.get('bin', 'FAILED')[:16], self.a.base_vars))
            if base_sha != head['sha']:
                self.prepare_tree(head['sha'])
            return got
        have = json.loads(cache.read_text()) if cache.exists() else {}
        missing = [img for img in ident if have.get(img, {}).get('vars') != vars_key(img)] if not self.a.rebuild_base else list(ident)
        if missing and base_sha == head['sha'] and not self.a.identity_vars:
            for img in missing:  # the head is the base: its own builds are the base's
                if self.head_builds[img]['ok']:
                    have[img] = dict(bin=self.head_builds[img]['bin'], vars=vars_key(img), stage=self.stage)
            missing = []
            cache.parent.mkdir(parents=True, exist_ok=True)
            cache.write_text(json.dumps(have, indent=1) + '\n')
        if missing:
            if base_sha != head['sha']:
                self.prepare_tree(base_sha)
            for img in missing:
                b = self.build(self.ev / 'base-builds', img, self.m['images'][img]['vars'])
                if b['ok']:
                    have[img] = dict(bin=b['bin'], vars=vars_key(img), stage=self.stage)
                log('base %-16s %s' % (img, b.get('bin', 'FAILED')[:16]))
            if base_sha != head['sha']:
                self.prepare_tree(head['sha'])
            cache.parent.mkdir(parents=True, exist_ok=True)
            cache.write_text(json.dumps(have, indent=1) + '\n')
        return {k: v['bin'] for k, v in have.items()}

    def execute(self, jobs):
        res = {}

        def one(job):
            r, kind, arg = job
            b = self.ctl_builds.get(r['image_build']) if r.get('image_build') else None
            bd = b['dir'] if b else self.builds_dir(r['image'])
            cap = r.get('cap', 40000 if r['kind'] in ('loop', 'cancel', 'reset') else 30000)
            pokes = r.get('pokes', '-') if kind == 'race' else str(arg)
            sound = self.m['images'][r['image']].get('sound', False)
            cue = None
            if sound and r.get('disc') != 'data':
                cue = self.discs.get(r.get('image_build') or r['image'])
            out = self.harness(bd, self.ev / 'runs' / r['name'], kind, pokes if kind == 'race' else str(arg), cap,
                               audio=sound, cue=cue)
            # The run-to-image mapping, explicit in the receipt (census tools read it; they never guess from names).
            out.update(image=r['image'], kind=r.get('kind', ''), build=str(bd), control_build=r.get('image_build') or '')
            return r['name'], out

        log('running %d harness jobs, %d at a time' % (len(jobs), self.a.jobs))
        with cf.ThreadPoolExecutor(max_workers=self.a.jobs) as ex:
            for name, out in ex.map(one, jobs):
                res[name] = out
                log('run %-28s rc %d  %5.0f s' % (name, out['rc'], out['seconds']))
        return res

    def builds_dir(self, img):
        return str(self.ev / 'builds' / img)

    def disc_check(self, r, sink=None):
        """A sound-on run must have used the shipping mixed-mode disc; its audio is reported (INFO)."""
        if not self.m['images'][r['image']].get('sound'):
            return
        res = self.results.get(r['name'], {})
        mixed = res.get('cue', '').endswith(os.path.join('mixed', 'fzs-engine.cue'))
        self.add('disc', r['name'], mixed, 'shipping mixed-mode disc' if mixed else 'NON-SHIPPING disc: data track only',
                 res.get('cue', '-')[-60:], sink=sink)
        kv, _ = result_kv(read_result(self.ev / 'runs' / r['name']))
        if 'audio_peak' in kv:
            self.add('audio', r['name'], True, 'peak %s, non-zero samples %s, hash %s' % (kv['audio_peak'], kv['audio_nonzero'], kv.get('audio_hash', '-')),
                     'EMULATOR; music from CD-DA needs the mixed disc', info=True, sink=sink)

    # -- controls ------------------------------------------------------------------------------------------------------
    ctl_builds = {}

    def control_jobs(self, builds, hosts):
        jobs = []
        for c in self.selected_controls():
            if c.get('vars'):
                key = c['name']
                vars_ = self.m['images'][c['image']]['vars'] + c['vars']
                self.ctl_builds[key] = self.build(self.ev / 'ctl-builds', key, vars_)
                log('control build %-14s %s' % (key, self.ctl_builds[key].get('bin', 'FAILED')[:16]))
                if self.m['images'][c['image']].get('sound') and self.ctl_builds[key]['ok'] and c.get('kind'):
                    self.discs[key] = self.mixed_disc(self.ctl_builds[key]['dir'])[0]
            if c['check'] in ('identity', 'legacy', 'determinism') or c.get('reuse'):
                continue
            r = dict(name=c['name'], image=c['image'], kind=c['kind'], course=c.get('course'), pokes=c.get('pokes', '-'), disc=c.get('disc'),
                     image_build=c['name'] if c.get('vars') else None)
            if c['kind'] == 'race':
                jobs.append((r, 'race', '-'))
            else:
                idx = next((x['index'] for x in self.m['courses'] if x['slug'] == c.get('course')), None) if c['kind'] in ('course', 'practice') else None
                bd = self.ctl_builds[c['name']]['dir'] if c.get('vars') else builds[c['image']]['dir']
                rec = self.record_ms(hosts, bd, c.get('record_offset', 0)) if c['kind'] == 'loop' else None
                jobs.append((r, 'script', self.script_for(c['script'], idx, rec)))
        return jobs

    def score_controls(self, builds, hosts):
        for c in self.selected_controls():
            sink = []
            name, chk = c['name'], c['check']
            if chk == 'identity':
                b = self.ctl_builds[name]
                base = builds[c['image']].get('bin')
                self.add('identity', name, b.get('bin') == base, '%s vs %s' % ((b.get('bin') or '-')[:16], (base or '-')[:16]), sink=sink)
            elif chk == 'legacy':
                off = getattr(self, 'legacy_bins', {}).get('off')
                if off is None:
                    rc, out = self.stage_cmd('legacy', self.stage, wslpath(self.ev / 'legacy'), 'off', 'off', check=False)
                    got = re.search(r'LEGACY off ([0-9a-f]{64})', out)
                    off = got.group(1) if got else ''
                self.add('legacy', name, off.startswith(self.m['legacy']['recipe']), '%s vs recipe %s' % (off[:16], self.m['legacy']['recipe'][:16]), sink=sink)
            elif chk == 'fixture' and c.get('reuse'):
                rd = self.ev / 'runs' / c['reuse']
                lines = read_result(rd)
                cc = next(x for x in self.m['courses'] if x['slug'] == c['course'])
                bd = builds[c['image']]['dir']
                sym = load_syms(Path(bd) / 'fzs-engine.sym')
                mut = PR_OBS_MUT.get(c['mutate'])
                if c['mutate'] == 'noarchive':   # a BIN folder without its archived identity (a stand-in folder with only the BIN)
                    tmp = self.ev / 'tmp' / 'noarchive'
                    shutil.rmtree(tmp, ignore_errors=True)
                    tmp.mkdir(parents=True)
                    for f in ('fzs-engine.bin', 'fzs-engine.sym'):
                        shutil.copy(Path(bd) / f, tmp / f)
                    bd = str(tmp)
                self.practice_fixture(name, rd, bd, lines, marks(lines), sym, dict(slug=cc['slug'], index=cc['index'], row=cc['index'], mode=5, cls=1), mutate=mut, sink=sink,
                                      mutate_arch=PR_ARCH_MUT.get(c['mutate']))
            elif chk == 'host' and c.get('practice_host'):
                rd = self.ev / 'runs' / c['reuse']
                lines = read_result(rd)
                _, mchk = result_kv(lines, 'results')
                href = self.host(self.ev / 'host', c['course'], '0.004,2.5')
                m = c['practice_host']
                if m == 'no-host':
                    href = None
                elif m == 'host-lacks':   # HOST's file lost the last expected checkpoint
                    href = dict(href, chk=[x for x in href['chk'] if x[0] != self.practice_window()['end_tick'] // 256 * 256])
                elif m == 'subset':       # the image captured only part of the window
                    mchk = mchk[:-1]
                elif m == 'late':         # the image's checkpoints stop short of the window end
                    mchk = mchk[:len(mchk) // 2]
                self.practice_host(name, mchk, href, c['course'], sink)
            elif chk == 'host' and c.get('reuse'):
                rd = self.ev / 'runs' / c['reuse']
                kv, chkl = result_kv(read_result(rd))
                href = self.host(self.ev / 'host', 'mute-city-1', c['host_driver'])
                self.host_check(name, kv, chkl, href, full_final=True, sink=sink, against=c['host_driver'])
            elif chk == 'disc':
                self.disc_check(dict(name=name, image=c['image']), sink=sink)
                rd, ref = self.ev / 'runs' / name, self.ev / 'runs' / c['against']
                ka, kb = result_kv(read_result(rd))[0], result_kv(read_result(ref))[0]
                self.add('audio', name, True, 'data-only audio hash %s (non-zero %s) vs mixed-disc %s run %s (non-zero %s)' % (
                    ka.get('audio_hash'), ka.get('audio_nonzero'), c['against'], kb.get('audio_hash'), kb.get('audio_nonzero')), info=True, sink=sink)
                self.score_scenes(dict(name=name, kind=c['kind']), rd, self.head_builds[c['image']]['dir'], None, sink=sink)
            elif chk == 'determinism':
                self.determinism(name, self.ev / 'runs' / c['reuse'], self.ev / 'runs' / c['against'], ['final'], sink=sink,
                                 build_dir=builds['field']['dir'])
            else:
                rd = self.ev / 'runs' / name
                bd = self.ctl_builds[name]['dir'] if c.get('vars') else builds[c['image']]['dir']
                if c['kind'] == 'race':
                    host = hosts.get(c.get('course')) if c.get('course') else None
                    self.score_race(dict(name=name), rd, bd, host, sink=sink)
                else:
                    hslug = c.get('host_course', c.get('course'))
                    host = hosts.get(hslug) if hslug else None
                    if hslug and host is None:
                        host = hosts[hslug] = self.host(self.ev / 'host', hslug, '0.004,2.5')
                    self.score_scenes(dict(name=name, kind=c['kind'], course=c.get('course')), rd, bd, host, sink=sink, host_label=hslug or '')
            target = [r for r in sink if r['check'] == chk]
            # A control whose run or script dies before its check is scored has still failed; the table says which.
            failed = [r for r in sink if r['check'] in (chk, 'run', 'script') and r['status'] == 'FAIL']
            shown = ('; '.join('%s: %s' % (r['subject'], r['value']) for r in (failed or target))
                     + ''.join('; %s' % r['value'] for r in sink if r['check'] == 'audio' and chk == 'disc'))
            if c.get('expect_error'):
                # The control counts only when its run failed at the intended place (an earlier boot, disc or scene
                # failure must not satisfy it): the harness error is expect_error (RECORD_MS filled as for the run)
                # and the run passed expect_mark.
                bd = self.ctl_builds[name]['dir'] if c.get('vars') else builds[c['image']]['dir']
                want = c['expect_error'].replace('RECORD_MS', str(self.record_ms(hosts, bd, c.get('record_offset', 0))))
                got, seen = error_text(self.ev / 'runs' / name), marks(read_result(self.ev / 'runs' / name))
                at = (got == want and (not c.get('expect_mark') or c['expect_mark'] in seen)
                      and not (c.get('expect_mark_not') and c['expect_mark_not'] in seen))
                shown = 'error "%s" (want "%s"); marks reached %s' % (got, want, ','.join(seen) or '-')
                if c.get('expect_open'):
                    # Two words [begun, ended] in the error dump: the run must have stopped inside one section.
                    try:
                        mem = Mem(self.ev / 'runs' / name, 'error', load_syms(Path(bd) / 'fzs-engine.sym'))
                        w = mem.words(c['expect_open'], 2)
                    except (OSError, KeyError, struct.error):
                        w = None
                    at = at and w is not None and w[0] == w[1] + 1
                    shown += '; %s %s' % (c['expect_open'], '%d begun, %d ended' % w if w else 'not read')
                failed = failed if at else []
            if c.get('expect_pass'):
                # A test arm, not a control: it counts when its run passed, reached expect_mark, and the expect_pass
                # counter pair [begun, ended] is balanced (and non-zero) in that mark's own dump.
                rd = self.ev / 'runs' / name
                bd = self.ctl_builds[name]['dir'] if c.get('vars') else builds[c['image']]['dir']
                seen = marks(read_result(rd))
                try:
                    w = Mem(rd, c['expect_mark'], load_syms(Path(bd) / 'fzs-engine.sym')).words(c['expect_pass'], 2)
                except (OSError, KeyError, struct.error):
                    w = None
                ok = (not failed and bool(target) and c['expect_mark'] in seen and w is not None and w[0] == w[1] and w[0] > 0)
                shown = '%s; mark %s %s; %s %s' % (shown, c['expect_mark'], 'reached' if c['expect_mark'] in seen else 'NOT reached',
                                                   c['expect_pass'], '%d begun, %d ended' % w if w else 'not read')
                self.controls.append(dict(control=name, check=chk, how=c['how'], status='PASS' if ok else 'FAIL', shown=shown[:400]))
                continue
            self.controls.append(dict(control=name, check=chk, how=c['how'], status='PASS' if failed else 'FAIL', shown=shown[:400]))
        # A paired control counts only when its arm counted (C-124: the forced overlap with the aligned quiet arm).
        st = {r['control']: r for r in self.controls}
        for c in self.selected_controls():
            if c.get('pair') and c['name'] in st:
                arm = st.get(c['pair'])
                if not arm or arm['status'] != 'PASS':
                    st[c['name']]['status'] = 'FAIL'
                    st[c['name']]['shown'] = (st[c['name']]['shown'] + '; pair %s %s' % (c['pair'], 'not run' if not arm else 'failed'))[:400]

    def selected_controls(self):
        want = set(self.a.control_names.split(',')) if self.a.control_names else None
        return [c for c in self.m['controls'] if want is None or c['name'] in want]

    # -- output ---------------------------------------------------------------------------------------------------------
    def write(self, receipt):
        rp = self.ev / 'receipt.json'
        rp.write_text(json.dumps(receipt, indent=1) + '\n')
        cache = Path(self.a.receipts)
        cache.mkdir(parents=True, exist_ok=True)
        shutil.copy(rp, cache / ('%s-%s-%s.json' % (receipt['head'][:12], receipt['mode'], self.a.label)))
        lines = ['# Engine gate %s: %s' % (receipt['verdict'], self.a.label), '',
                 'Head `%s` (%s), base `%s`, mode %s, stage `%s`, harness %s. %s.' % (
                     receipt['head'][:12], receipt['rev'], (receipt['base'] or '-')[:12], receipt['mode'], self.stage,
                     receipt['harness'], receipt['tiers']), '',
                 '| Check | Subject | Result | Value | Detail |', '| --- | --- | --- | --- | --- |']
        for c in self.checks:
            lines.append('| %s | %s | %s | %s | %s |' % (c['check'], c['subject'], c['status'], c['value'], c['detail'].replace('|', '/')))
        if self.controls:
            lines += ['', '| Control | Check | Fails as it should | Shown | How |', '| --- | --- | --- | --- | --- |']
            for c in self.controls:
                lines.append('| %s | %s | %s | %s | %s |' % (c['control'], c['check'], c['status'], c['shown'].replace('|', '/'), c['how']))
        lines += ['', 'Time: %s s (builds %s, runs %s).' % (self.times.get('total'), self.times.get('builds'), self.times.get('runs'))]
        (self.ev / 'summary.md').write_text('\n'.join(lines) + '\n')
        print('\n'.join(lines))

    def cleanup(self):
        """Rebuildable outputs go; results, fields, receipts and logs stay (the disk hygiene rule)."""
        gone = []
        for p in list((self.ev).rglob('*')):
            if p.is_file() and (p.suffix in ('.iso', '.rgba') or p.name.endswith(('.wram-high', '.wram-low', '.vdp1-vram', '.vdp2-vram'))):
                gone.append(str(p.relative_to(self.ev)))
                p.unlink()
        for d in [self.ev / 'discs'] + list(self.ev.rglob('mixed')):
            if d.is_dir():
                gone.append(str(d.relative_to(self.ev)) + '/ (mixed-mode disc)')
                shutil.rmtree(d, ignore_errors=True)
        shutil.rmtree(self.ev / 'tmp', ignore_errors=True)
        (self.ev / 'cleanup-manifest.txt').write_text('deleted (rebuildable: disc images, dumps, stills)\n' + '\n'.join(gone) + '\n')


class Rescore(Gate):
    """--rescore: score an earlier run's evidence again (same label) without building or running anything."""

    def stage_cmd(self, *args, check=True):
        if args[0] == 'legacy':
            f = self.ev / 'legacy' / args[3] / 'fzero-x-saturn.bin'
            return (0, 'LEGACY %s %s' % (args[3], sha256(f))) if f.exists() else (1, 'no legacy build kept')
        if args[0] == 'cdda':
            return Gate.stage_cmd(self, *args, check=check)
        raise RuntimeError('rescore does not touch the stage (%s)' % args[0])

    def prepare_tree(self, rev):
        return dict(sha=git('rev-parse', rev), pin='-', submodule='-', regenerated=['rescored'], stale_receipts=[])

    def build(self, where, name, vars_):
        d = Path(where) / name
        if not (d / 'fzs-engine.bin').exists():
            return dict(name=name, vars=vars_, ok=False, error='not in the evidence')
        return dict(name=name, vars=vars_, ok=True, bin=sha256(d / 'fzs-engine.bin'), dir=str(d),
                    outdir=(d / 'outdir.txt').read_text().strip() if (d / 'outdir.txt').exists() else '')

    def mixed_disc(self, build_dir):
        cue = Path(build_dir) / 'mixed' / 'fzs-engine.cue'
        return (cue, 'kept from the run') if cue.exists() else (None, 'not kept')

    def host(self, hdir, slug, driver):
        f = Path(hdir) / ('%s-%s.txt' % (slug, driver))
        return host_ref(f) if f.is_file() else None

    def execute(self, jobs):
        res = {}
        for r, kind, arg in jobs:
            b = self.ctl_builds.get(r['image_build']) if r.get('image_build') else None
            bd = b['dir'] if b else self.builds_dir(r['image'])
            sound = self.m['images'][r['image']].get('sound', False)
            mixed = Path(bd) / 'mixed' / 'fzs-engine.cue'
            cue = mixed if sound and r.get('disc') != 'data' and mixed.exists() else Path(bd) / 'fzs-engine.cue'
            ok = result_kv(read_result(self.ev / 'runs' / r['name']))[0].get('exit') == 'ok'
            res[r['name']] = dict(rc=0 if ok else 1, seconds=None, cue=str(cue), rescored=True)
        return res


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group()
    g.add_argument('--quick', action='store_true', help='lanes: Mute City, sound on (the default)')
    g.add_argument('--full', action='store_true', help='integrator: every image, sound on and off, six courses, legacy pair')
    ap.add_argument('--controls', action='store_true', help='also run every check\'s failing control')
    ap.add_argument('--rev', default='HEAD', help='commit to gate (default HEAD)')
    ap.add_argument('--base', default='origin/engine', help='identity base (default origin/engine); "" skips identity')
    ap.add_argument('--rebuild-base', action='store_true', help="build the base's images here even if the receipt cache has them")
    ap.add_argument('--identity-vars', default='', help='make vars for the identity builds (a lane\'s switches off)')
    ap.add_argument('--base-vars', default='', help='make vars added to the base\'s identity builds too (built in this '
                    'stage, never read from or written to the shared receipt cache): for a switch that already exists '
                    'on the base, compare both with it off')
    ap.add_argument('--expect', default='', help='image=binprefix,... : BINs already reported for this code')
    ap.add_argument('--only', default='', help='run names or kinds to run (comma list)')
    ap.add_argument('--courses', default='', help='course slugs for course runs (comma list)')
    ap.add_argument('--legacy', action='store_true', help='build the legacy pair in quick mode too')
    ap.add_argument('--label', default='', help='evidence subfolder (default <head>-<mode>)')
    ap.add_argument('--stage', default=os.environ.get('FZX_GATE_STAGE', 'claude-gate-20261120'), help='WSL stage name (claude-gate-*)')
    ap.add_argument('--seed-stage', default='', help='create the stage from this one (generated/, upstream/, ref/build)')
    ap.add_argument('--matrix', default=str(TOOLS / 'engine_gate_matrix.json'))
    ap.add_argument('--evidence', default=str(DEF_EVIDENCE))
    ap.add_argument('--receipts', default=str(DEF_RECEIPTS))
    ap.add_argument('--harness', default=str(DEF_HARNESS))
    ap.add_argument('--bios', default=str(DEF_BIOS))
    ap.add_argument('--sh2zam', default=str(DEF_SH2ZAM))
    ap.add_argument('--practice-expected', default=str(DEF_PRACTICE), help='the Practice fixture\'s expected rows (tools/practice_expected.py over the HOST reports)')
    ap.add_argument('--music', default=str(DEF_MUSIC),
                    help='bgm_cdda.py output (local, ROM-derived) for the shipping mixed-mode discs')
    ap.add_argument('--jobs', type=int, default=max(2, min(12, (os.cpu_count() or 4) - 2)))
    ap.add_argument('--clips', action='store_true', help='keep the scripts\' clip windows')
    ap.add_argument('--control-names', default='', help='with --controls: only these controls (comma list)')
    ap.add_argument('--rescore', action='store_true', help="score an earlier run's evidence (same --label) again")
    ap.add_argument('--keep', action='store_true', help='keep discs, dumps and stills')
    a = ap.parse_args()
    for p in (a.harness, a.bios):
        if not Path(p).exists():
            sys.exit('missing %s (tools/ENGINE_GATE.md, "Setup")' % p)
    if not a.label:
        a.label = '%s-%s%s' % (git('rev-parse', '--short=12', a.rev), 'full' if a.full else 'quick', '-ctl' if a.controls else '')
    matrix = json.loads(Path(a.matrix).read_text())
    gate = (Rescore if a.rescore else Gate)(a, matrix)
    if a.seed_stage:
        log(gate.stage_cmd('init', a.stage, a.seed_stage)[1])
    sys.exit(gate.run())


if __name__ == '__main__':
    main()
