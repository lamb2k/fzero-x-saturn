"""HOST: the race mode rules (saturn/engine/engine_modes.c) against the decompiled source's facts.

Plan P3 item 3. The facts come from the pinned decompilation (nothing from it is committed or printed): the ModeSelect IDs,
which game modes race alone, which never finish at the lap count, Death Race's half-energy rivals and win rule, and which
modes keep times. The driver prints the table; the test compares. Negative controls change engine_modes.c and must fail.
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from import_source_art import DECOMP  # noqa: E402

ENG = ROOT / 'saturn/engine'


def source_facts():
    game = (DECOMP / 'include/fzx_game.h').read_text()
    modes = {}
    body = re.search(r'typedef enum ModeSelect \{(.*?)\} ModeSelect;', game, re.S)[1]
    for m in re.finditer(r'/\*\s*(\d+)\s*\*/\s*(MODE_\w+),', body):
        modes[m[2]] = int(m[1])
    racer = (DECOMP / 'src/game/racer.c').read_text()
    if not re.search(r'case GAMEMODE_TIME_ATTACK:[^;]*?\n[^;]*?gTotalRacers = 1;', racer, re.S):
        raise AssertionError('Time Attack no longer races alone in racer.c')
    if 'racer->energy *= 0.5f;' not in racer or 'gGameMode == GAMEMODE_DEATH_RACE' not in racer:
        raise AssertionError('Death Race rival energy rule changed')
    if not re.search(r'GAMEMODE_DEATH_RACE\) && \(gTotalRacers == \(gCpuRacersRetired \+ 1\)\)', racer):
        raise AssertionError('Death Race win rule changed')
    if 'racer->lap = racer->lapsCompleted = 1;' not in racer:
        raise AssertionError('the lap wrap changed')
    lives = [int(v) for v in re.search(r's8 gTotalLives\[\] = \{(.*?)\};', racer)[1].split(',')]
    return modes, lives


def build(src_dir, out):
    subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(ENG), str(ROOT / 'tests/modes_driver.c'),
                    str(Path(src_dir) / 'engine_modes.c'), '-o', str(out)], check=True)
    return subprocess.run([str(out)], capture_output=True, text=True, check=True).stdout


def table(text):
    rows = {}
    for l in text.splitlines():
        p = l.split()
        if p[0] == 'mode' and p[2] != 'none':
            rows[int(p[1])] = {p[i]: int(p[i + 1]) for i in range(2, len(p), 2)}
        elif p[0] == 'mode':
            rows[int(p[1])] = None
    return rows


class Modes(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)

    def run_table(self, mutate=None):
        d = self.tmp / 'src'
        d.mkdir(exist_ok=True)
        for n in ('engine_modes.c', 'engine_modes.h', 'engine_cup.h'):
            shutil.copy(ENG / n, d / n)
        if mutate:
            p = d / 'engine_modes.c'
            t = p.read_text()
            t2 = mutate(t)
            self.assertNotEqual(t, t2, 'the control changed nothing')
            p.write_text(t2)
        return table(build(d, self.tmp / 'drv'))

    def check(self, rows):
        modes, lives = source_facts()
        self.assertEqual(lives, [5, 4, 3, 2])
        ta, gp, dr, pr = (modes['MODE_TIME_ATTACK'], modes['MODE_GP_RACE'], modes['MODE_DEATH_RACE'], modes['MODE_PRACTICE'])
        self.assertEqual((gp, ta, dr, pr), (0, 1, 2, 5))
        for m in (modes['MODE_VS_BATTLE'], modes['MODE_COURSE_EDIT'], modes['MODE_OPTIONS']):
            self.assertIsNone(rows[m], 'mode %d must not run' % m)
        self.assertEqual(rows[ta]['field'], 0)                      # solo
        for m in (gp, dr, pr):
            self.assertEqual(rows[m]['field'], 1)
        self.assertEqual([rows[m]['endless'] for m in (gp, ta, dr, pr)], [0, 0, 1, 1])
        self.assertEqual(rows[dr]['forced'], 3)                     # Death Race is Master
        self.assertEqual(rows[dr]['pick'], 0)
        self.assertEqual(rows[dr]['energy'], 128)                   # racer.c: rivals' energy x 0.5
        for m in (gp, ta, pr):
            self.assertEqual(rows[m]['energy'], 256)
        self.assertEqual([rows[m]['win'] for m in (gp, ta, dr, pr)], [0, 0, 1, 0])
        self.assertEqual([rows[m]['keeps'] for m in (gp, ta, dr, pr)], [0, 1, 1, 0])   # save.c: TA and Death Race only
        self.assertEqual(rows[ta]['record'], 0)
        self.assertEqual(rows[dr]['record'], 1)
        self.assertEqual(rows[gp]['cup'], 1)


    def test_records_v1(self):
        """A version 1 save (before cup progress) loads with ENG_MODES=1, keeps its times, and the next write is version 2; a corrupt
        version 1 file is refused (the control). The backup library is a stub (tests/records_v1_stub)."""
        out = self.tmp / 'rv1'
        subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-DENG_MODES=1', '-I', str(ROOT / 'tests/records_v1_stub'), '-I', str(ENG),
                        str(ROOT / 'tests/records_v1_driver.c'), str(ENG / 'engine_records.c'), str(ENG / 'engine_modes.c'), '-o', str(out)],
                       check=True)
        r = subprocess.run([str(out)], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertEqual(r.stdout.count('PASS'), 8, r.stdout)
        self.assertNotIn('FAIL', r.stdout)

    def run_entry(self, edit=None):
        src = self.tmp / 'entry'
        src.mkdir(exist_ok=True)
        for f in ('engine_mode_entry.c', 'engine_modes.c'):
            text = (ENG / f).read_text()
            (src / f).write_text(edit(text) if edit and f == 'engine_mode_entry.c' else text)
        out = self.tmp / 'entry-drv'
        subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-DENG_MODES=1', '-I', str(ROOT / 'tests/records_v1_stub'), '-I', str(ENG),
                        str(ROOT / 'tests/mode_entry_driver.c'), str(src / 'engine_mode_entry.c'), str(src / 'engine_modes.c'), '-o', str(out)],
                       check=True)
        r = subprocess.run([str(out)], capture_output=True, text=True)
        return r.returncode, r.stdout

    def test_entry_contract(self):
        """The mode entry and outcome contract (engine_mode_entry.h): capabilities, reject before launch, cancel without mutation,
        accepted entries, outcomes. Controls: each edit of the rules must fail a named case."""
        rc, out = self.run_entry()
        self.assertEqual(rc, 0, out)
        self.assertGreaterEqual(out.count("PASS"), 40, out)
        self.assertNotIn('FAIL', out)
        for name, f in {
            'death race entry open': lambda t: t.replace('if (!r->win_all_retired) c |= FZX_CAP_ENTRY_OPEN;', 'c |= FZX_CAP_ENTRY_OPEN;'),
            'lab flag ignored': lambda t: t.replace(' && !env->lab', ''),
            'master check dropped': lambda t: t.replace('r->class_pick && !env->class_open(cls)', '0'),
            'practice keeps a record': lambda t: t.replace('else if (r->endless) return;           /* Practice never finishes: no outcome, nothing written */', ''),
            'death race records a retire': lambda t: t.replace('case FZX_END_RETIRE: out->kind = FZX_OUTCOME_RETIRED; break;',
                                                                'case FZX_END_RETIRE: out->kind = FZX_OUTCOME_RETIRED; out->record_table = r->record; out->record_eligible = r->record != FZX_MODE_NO_RECORD; break;'),
            'entry writes before failing': lambda t: t.replace('    if (!(env->course_avail(req->course) & r->avail)) return FZX_ENTRY_NO_COURSE;',
                                                               '    out->course = req->course;\n    if (!(env->course_avail(req->course) & r->avail)) return FZX_ENTRY_NO_COURSE;'),
        }.items():
            with self.subTest(name):
                rc, out = self.run_entry(f)
                self.assertNotEqual(rc, 0, name + ': the fixture did not fail')

    def run_ghost(self, edit=None, tsv=None):
        src = self.tmp / 'ghost'
        src.mkdir(exist_ok=True)
        text = (ENG / 'engine_ghost.c').read_text()
        (src / 'engine_ghost.c').write_text(edit(text) if edit else text)
        out = self.tmp / 'ghost-drv'
        subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-I', str(ENG), str(ROOT / 'tests/ghost_driver.c'), str(src / 'engine_ghost.c'),
                        '-lm', '-o', str(out)], check=True)
        r = subprocess.run([str(out)] + ([str(tsv)] if tsv else []), capture_output=True, text=True)
        return r.returncode, r.stdout

    def test_ghost(self):
        """The Time Attack ghost codec (engine_ghost.h): a drive plays back as recorded, damage is refused, an overlong drive is not
        kept. Controls: each edit of the codec must fail a named case."""
        rc, out = self.run_ghost()
        self.assertEqual(rc, 0, out)
        self.assertGreaterEqual(out.count('PASS'), 9, out)
        for name, f in {
            'crc not checked': lambda t: t.replace('if (g->crc != body_crc(g)) return false;', ''),
            'sample count not checked': lambda t: t.replace('return n == g->samples && p.nib >= g->bytes * 2u - 1u;', 'return true;'),
            'midpoint dropped': lambda t: t.replace('(((int64_t)p->cur[i] + p->next[i]) << 15)', '((int64_t)p->cur[i] << 16)'),
            'overrun still kept': lambda t: t.replace('if (rec.over || rec.samples < 2u) return false;', 'if (rec.samples < 2u) return false;'),
            'second difference not used': lambda t: t.replace('const int32_t d = v[i] - rec.prev[i], dd = d - rec.dprev[i];', 'const int32_t d = v[i] - rec.prev[i], dd = d;'),
        }.items():
            with self.subTest(name):
                rc, out = self.run_ghost(f)
                self.assertNotEqual(rc, 0, name + ': the fixture did not fail')

    def run_ghost_store(self, edit=None):
        src = self.tmp / 'gstore'
        src.mkdir(exist_ok=True)
        text = (ENG / 'engine_records.c').read_text()
        t2 = edit(text) if edit else text
        if edit:
            self.assertNotEqual(text, t2, 'the control changed nothing')
        (src / 'engine_records.c').write_text(t2)
        out = self.tmp / 'gstore-drv'
        subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-DENG_MODES=1', '-DENG_GHOST=1', '-I', str(ROOT / 'tests/records_v1_stub'), '-I', str(ENG),
                        str(ROOT / 'tests/ghost_store_driver.c'), str(src / 'engine_records.c'), str(ENG / 'engine_ghost.c'), str(ENG / 'engine_modes.c'),
                        '-lm', '-o', str(out)], check=True)
        r = subprocess.run([str(out)], capture_output=True, text=True)
        return r.returncode, r.stdout

    def test_ghost_store(self):
        """The ghost's backup storage (engine_records.c): a save is a queue of files, one write per menu frame (at most one chunk), the head
        file last; a power cut after any write leaves the previous ghost or the whole new one. Controls: each edit must fail a named case."""
        rc, out = self.run_ghost_store()
        self.assertEqual(rc, 0, out)
        self.assertGreaterEqual(out.count('PASS'), 15, out)
        self.assertNotIn('FAIL', out)
        for name, f in {
            'head first': lambda t: t.replace('gw.next = gw.n > 1u ? 1u : 0u;', 'gw.next = 0u;'),
            'chunks of 600': lambda t: t.replace('#define GH_CHUNK 512u', '#define GH_CHUNK 600u'),
            'no generation on the head': lambda t: t.replace('ghost_gen[gw.slot] = gw.gen;', ''),
        }.items():
            with self.subTest(name):
                rc, out = self.run_ghost_store(f)
                self.assertNotEqual(rc, 0, name + ': the fixture did not fail')

    def test_table(self):
        self.check(self.run_table())

    def test_controls(self):
        for name, f in {
            'time attack with the field': lambda t: t.replace('{ FZX_MODE_TA,       0,', '{ FZX_MODE_TA,       1,'),
            'practice with a finish': lambda t: t.replace('{ FZX_MODE_PRACTICE, 1,    1,', '{ FZX_MODE_PRACTICE, 1,    0,'),
            'death race with full energy': lambda t: t.replace('FZX_AVAIL_DEATH, 128,   1', 'FZX_AVAIL_DEATH, 256,   1'),
            'death race class picked': lambda t: t.replace('{ FZX_MODE_DEATH,    1,    1,      0,', '{ FZX_MODE_DEATH,    1,    1,      1,'),
            'grand prix keeps times': lambda t: t.replace('{ FZX_MODE_GP,       1,    0,      1,   0,     1,  FZX_MODE_NO_RECORD',
                                                           '{ FZX_MODE_GP,       1,    0,      1,   0,     1,  FZX_MODE_REC_TA'),
        }.items():
            with self.subTest(name):
                with self.assertRaises(AssertionError):
                    self.check(self.run_table(f))


if __name__ == '__main__':
    unittest.main()
