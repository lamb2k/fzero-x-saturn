"""HOST: signed shifts and range in saturn/engine/engine_field.c (C-127 finding 3).

The statics, the pose functions (line_pose, line_pose_fast, tube_at) and the pack's station block are cut out of
engine_field.c as it stands, and out of tests/engine_field_prefix.c.txt (the file as engine 6cca653 had it), and run on
Linux in four variants (E2_FASTPOSE and RIVAL_TUBES on and off) with signed laterals, negative packed coordinates and
Q1.14 rows of both signs, under gcc and clang with UBSan. The library calls are stubbed alike for both texts.
Checks: the fixed text runs clean under -fsanitize=undefined; its pose, station and pack hashes equal the old text's
(run with only the shift check off) on every in-range case; the range ends behave as documented (lateral saturates at
+-2^23, a pack coordinate of 2^19 Q4 is refused, stations_ok refuses a span past int32 and a width past 0x7fff).
Controls: the old text trips UBSan on the shift; the fixed text with one shift put back trips it; the saturation, the pack
bound and the span test each removed give a different answer.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
NEW = ROOT / 'saturn/engine/engine_field.c'
OLD = ROOT / 'tests/engine_field_prefix.c.txt'
DRIVER = ROOT / 'tests/engine_field_shift_driver.c'
LIB = ROOT / 'third_party/SH2ZAM/saturn/include'


def between(text, start, end, keep_end=False):
    i = text.index(start)
    j = text.index(end, i)
    return text[i:j + (len(end) if keep_end else 0)]


def parts(text):
    pack_start = '    {\n        typedef struct { int32_t pos[3]; int16_t fwd[3], up[3], left[3]; uint16_t pad; } pack_pose_t;'
    pack_end = 'nodes[0].start != 0) { fzx_field_report[0] = 5; fzx_e2_report[0] = 4; return false; }\n    }\n'
    return {
        'field_statics.inc': between(text, 'enum { MAX_STATIONS', 'static const sh2z_vd_track_t *trk;'),
        'field_funcs_a.inc': between(text, 'static int16_t q14(', 'static bool build_line('),
        'field_funcs_b.inc': between(text, '/* World pose of a line-space state', '#if E2_PACK\n/* The field counts laps'),
        'field_pack.inc': between(text, pack_start, pack_end, True),
    }


def run_text(case, tmp, text, cc, fast, tubes, new, sanitize='undefined', extra=()):
    d = tmp / ('b%s%d%d%s' % (new, fast, tubes, cc))
    d.mkdir(exist_ok=True)
    for name, body in parts(text).items():
        (d / name).write_text(body)
    cmd = [cc, '-O2', '-std=c11', '-Wall', '-Wextra', '-Wno-unused-function', '-Werror',
           '-DE2_FASTPOSE=%d' % fast, '-DRIVAL_TUBES=%d' % tubes, '-DE2_PACK=1', '-DENG_RIVAL_BEHAVE=0',
           '-fsanitize=' + sanitize, '-fno-sanitize-recover=all', '-no-pie', *extra,
           '-I' + str(d), '-I' + str(LIB), str(DRIVER), '-o', str(d / 't'), '-lm']
    if new:
        cmd.insert(1, '-DNEW_TEXT=1')
    build = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    case.assertEqual(build.returncode, 0, build.stderr)
    return subprocess.run([str(d / 't')], capture_output=True, text=True, timeout=60)


@unittest.skipUnless(os.name != 'nt' and shutil.which('gcc') and shutil.which('clang') and (LIB / 'sh2zam/line_follower_experimental.h').exists(),
                     'Linux HOST compilers and the SH2ZAM headers (third_party/SH2ZAM) required')
class EngineFieldShift(unittest.TestCase):
    def test_source_rule(self):
        old, new = OLD.read_text(), NEW.read_text()
        for pat in ('t->lateral << 8', 'pp[0].pos[c] << 12', 'pp[i].pos[c] << 12', '->fwd[c] << 16', '->up[c] << 16', 'half_width << 16'):
            self.assertIn(pat, old, pat)
            self.assertNotIn(pat, new, pat)
        self.assertIn('static inline int32_t shl_s32(int32_t v, unsigned n) { return (int32_t)((uint32_t)v << n); }', new)

    def test_shifts_and_range(self):
        old, new = OLD.read_text(), NEW.read_text()
        with tempfile.TemporaryDirectory(prefix='fzx-field-shift-') as directory:
            tmp = Path(directory)
            for cc in ('gcc', 'clang'):
                noshift = '-fno-sanitize=shift' if cc == 'gcc' else '-fno-sanitize=shift'
                for fast in (0, 1):
                    for tubes in (0, 1):
                        label = '%s fast=%d tubes=%d' % (cc, fast, tubes)
                        n = run_text(self, tmp, new, cc, fast, tubes, 1)
                        self.assertEqual(n.returncode, 0, label + ': ' + n.stderr)
                        # The old text with only the shift check off: the numbers it always produced.
                        o = run_text(self, tmp, old, cc, fast, tubes, 0, extra=(noshift,))
                        self.assertEqual(o.returncode, 0, label + ': ' + o.stderr)
                        keep = lambda out: [l for l in out.splitlines() if l.startswith(('pose ', 'pack ', 'q4 0x', 'q4 524287 ', 'q4 -524288 '))]
                        nl = [l for l in n.stdout.splitlines() if l.startswith(('pose ', 'pack '))]
                        ol = [l for l in o.stdout.splitlines() if l.startswith(('pose ', 'pack '))]
                        self.assertEqual(nl, ol, label)
                        self.assertEqual(len(nl), 2)
                        npk = {l.split()[1]: l for l in n.stdout.splitlines() if l.startswith('q4 ')}
                        opk = {l.split()[1]: l for l in o.stdout.splitlines() if l.startswith('q4 ')}
                        for q in ('524287', '-524288'):   # inside +-2^19: loaded, same stations
                            self.assertEqual(npk[q], opk[q], label)
                            self.assertIn('loads=1', npk[q])
                        for q in ('524288', '-524289', '2147483647', '-2147483648'):
                            self.assertIn('loads=0 status=5/4', npk[q], label)
                            self.assertIn('loads=1', opk[q], label + ' (old text wraps and loads)')
                        # Control: the old text trips the shift check.
                        t = run_text(self, tmp, old, cc, fast, tubes, 0)
                        self.assertNotEqual(t.returncode, 0, label)
                        self.assertIn('shift', t.stderr, label)
                        print('%s: pose/pack hashes equal old; old trips UBSan: %s' % (label, t.stderr.strip().splitlines()[0][-70:]))
                        if not fast and not tubes:
                            nlat = {l.split()[1]: l for l in n.stdout.splitlines() if l.startswith('lateral ')}
                            olat = {l.split()[1]: l for l in o.stdout.splitlines() if l.startswith('lateral ')}
                            h = lambda d, k: d[k].split('hash=')[1]
                            self.assertEqual(h(nlat, '8388607'), h(olat, '8388607'))
                            self.assertEqual(h(nlat, '-8388608'), h(olat, '-8388608'))
                            for hi, lo in (('8388608', '8388607'), ('16777216', '8388607'), ('2147483647', '8388607'),
                                           ('-8388609', '-8388608'), ('-16777216', '-8388608'), ('-2147483648', '-8388608')):
                                self.assertEqual(h(nlat, hi), h(nlat, lo), 'saturates: %s' % hi)
                                self.assertNotEqual(h(olat, hi), h(olat, lo), 'old wraps: %s' % hi)
                            for l in n.stdout.splitlines():
                                if l.startswith('ok '):
                                    print(cc + ': ' + l)
                            want = ['ok flat=1', 'ok span INT32_MAX (3->4, 4->5)=1', 'ok span 0 between big pair=1',
                                    'ok span -2^31 (10->11)=1', 'ok span -2^31-1 (10->11)=0', 'ok span 2^31 (20->21)=0', 'ok span 2^31+1 wrap (last->0)=0',
                                    'ok width 0x7fff=1', 'ok width 0x8000=0', 'ok width 0xffff=0']
                            self.assertEqual([l for l in n.stdout.splitlines() if l.startswith('ok ')], want)
            # Controls on the fixed text: each change must be seen.
            controls = (
                ('shl_s32(pp[i].pos[c], 12); st->fwd', '(pp[i].pos[c] << 12); st->fwd', 'trip'),
                ('shl_s32(a->fwd[c], 16) +', '(((int32_t)a->fwd[c]) << 16) +', 'trip'),
                ('mul30(l[c], q8_q16(t->lateral))', 'mul30(l[c], t->lateral << 8)', 'trip'),
                ('q8 > 0x7fffff ? 0x7fffff00 : q8 < -0x800000 ? INT32_MIN : shl_s32(q8, 8)', 'shl_s32(q8, 8)', 'lateral'),
                ('(uint32_t)pp[i].pos[c] + 0x80000u >= 0x100000u', '0', 'pack'),
                ('d > INT32_MAX || d < INT32_MIN', 'd > INT32_MAX + 1ll || d < INT32_MIN', 'span'),
                ('nodes[i].half_width > 0x7fffu', 'nodes[i].half_width > 0xfffeu', 'width'),
            )
            for before, after, kind in controls:
                self.assertEqual(new.count(before), 1, before)
                mutated = new.replace(before, after)
                r = run_text(self, tmp, mutated, 'gcc', 1, 1, 1)
                good = run_text(self, tmp, new, 'gcc', 1, 1, 1)
                if kind == 'trip':
                    self.assertNotEqual(r.returncode, 0, before)
                    self.assertIn('shift', r.stderr)
                elif kind == 'lateral':
                    r0 = run_text(self, tmp, mutated, 'gcc', 0, 0, 1)
                    self.assertEqual(r0.returncode, 0, r0.stderr)
                    g0 = run_text(self, tmp, new, 'gcc', 0, 0, 1)
                    self.assertNotEqual([l for l in r0.stdout.splitlines() if l.startswith('lateral ')],
                                        [l for l in g0.stdout.splitlines() if l.startswith('lateral ')])
                else:
                    self.assertEqual(r.returncode, 0, r.stderr)
                    self.assertNotEqual(r.stdout, good.stdout, before)
                print('rejected: ' + after)


if __name__ == '__main__':
    unittest.main()
