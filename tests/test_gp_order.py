"""HOST: tools/gp_order.py against synthetic six-round cups (C-127 finding 2).

A synthetic run lays out the cup (engine_cup.h fzx_cup_t), the RESULTS record (fzx_classification_t, engine_scenes.c
scn_cls) and the field (veh[]) in small work-RAM images named by the harness marks. The well-formed cup must PASS.
Codex's C-127 case (correct grids, points and player times, but every course ID zero, the round never advancing, FINAL
throughout and empty RESULTS payloads) must FAIL, and so must each fault on its own.
"""
from pathlib import Path
import os
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCORER = Path(os.environ.get('GP_ORDER_SCORER', ROOT / 'tools' / 'gp_order.py'))   # override: score an older copy
POINTS = [100, 93, 87, 81, 76, 71, 66, 62, 58, 54, 50, 47, 44, 41, 38,
          35, 33, 31, 29, 27, 25, 23, 22, 21, 20, 19, 18, 17, 16, 15]
CUP_AT, VEH_AT, CLS_AT, SIZE = 0x1000, 0x2000, 0x3000, 0x4000
READY, RACING, SCORED, FINAL = 1, 2, 3, 5
PLAYER_MS = [111280, 90560, 92738, 102326, 125976, 117113]


def fnv(h, b):
    for x in b:
        h = ((h ^ x) * 16777619) & 0xffffffff
    return h


def cls_bytes(run_id, cup, rnd, course, rows, player_place, valid=1, hash_run=None):
    head = bytes([cup, rnd, course, len(rows), 1, player_place, 0])
    body = b''.join(bytes([e, 1, 1, 3]) + struct.pack('<I', ms) for e, ms in rows)
    rh = fnv(fnv(2166136261, head), body)
    ah = fnv(fnv(fnv(2166136261, struct.pack('<I', run_id if hash_run is None else hash_run)), head), body)
    out = struct.pack('>I', run_id) + head + bytes([valid]) + struct.pack('>II', rh, ah)
    out += b''.join(struct.pack('>BBBBI', e, 1, 1, 3, ms) for e, ms in rows)
    return out + bytes(20 + 8 * 30 - len(out))


def image(state, cup, rnd, run_id, accepted, total, last_place, last, grid_order=None, scn=None):
    d = bytearray(SIZE)
    d[CUP_AT:CUP_AT + 4] = bytes([state, cup, 0, rnd])
    d[CUP_AT + 4:CUP_AT + 16] = struct.pack('>III', 1, run_id, accepted)
    d[CUP_AT + 20] = 30                                   # entrants.count
    d[CUP_AT + 144:CUP_AT + 264] = struct.pack('>30i', *total)
    d[CUP_AT + 264:CUP_AT + 294] = bytes(last_place)
    d[CUP_AT + 324:CUP_AT + 324 + len(last)] = last
    if grid_order is not None:                            # front first: larger distance ahead
        for pos, e in enumerate(grid_order):
            struct.pack_into('>iiiiiI', d, VEH_AT + 64 * e, 100000 - 100 * pos, 0, 0, 0, 0, 0)
    if scn is not None:
        d[CLS_AT:CLS_AT + len(scn)] = scn
    return bytes(d)


def write_cup(where, fault=()):
    """Six rounds of the Jack Cup; `fault` names the corruptions to apply."""
    where = Path(where)
    (where / 'fzs-engine.sym').write_text(
        '%08x B _cup.lto_priv.0\n%08x b _veh\n%08x b _scn_cls\n' % (0x06000000 + CUP_AT, 0x06000000 + VEH_AT, 0x06000000 + CLS_AT))
    run = where / 'run'
    run.mkdir()
    total, last_place, prev_fin, prev_last, prev_run = [0] * 30, [0] * 30, None, bytes(260), 0
    for r in range(6):
        rnd = 0 if 'round-const' in fault else r
        course = 0 if 'course-zero' in fault else r
        if 'cup-order' in fault and r in (1, 2):
            course = 3 - r
        run_id = 65537 + r if not ('repeat-run' in fault and r == 3) else 65537 + r - 1
        grid = list(range(30)) if prev_fin is None else prev_fin[::-1]
        fin = [(r * 7 + k * 11) % 30 for k in range(30)]
        rows = [(e, PLAYER_MS[r] if e == 0 else 80000 + 500 * k) for k, e in enumerate(fin)]
        for k, e in enumerate(fin):
            total[e] += POINTS[k]
            last_place[e] = k + 1
        last = cls_bytes(run_id, 0, rnd, course, rows, fin.index(0) + 1)
        state = FINAL if (r == 5 or 'final-throughout' in fault) else SCORED
        start = image(RACING, 0, rnd, run_id, prev_run, [t for t in total], last_place, prev_last, grid)
        # the start image carries the previous totals; the scorer reads only state, round, run and the field there
        scn = last
        if 'results-rows' in fault and r == 2:                       # RESULTS shows a different (self-consistent) order
            swapped = [rows[1], rows[0]] + rows[2:]
            scn = cls_bytes(run_id, 0, rnd, course, swapped, [e for e, _ in swapped].index(0) + 1)
        if 'results-stale' in fault and r == 4:                      # RESULTS still shows the previous race
            scn = prev_last
        results = image(state, 0, rnd, run_id, run_id, total, last_place, last, scn=scn)
        standings = image(state, 0, rnd, run_id, run_id, total, last_place, last)
        if 'empty-results' in fault:
            results = b''
        for mark, data in (('start', start), ('results', results), ('standings', standings)):
            (run / ('round%d_%s.wram-high' % (r + 1, mark))).write_bytes(data)
        prev_fin, prev_last, prev_run = fin, last, run_id
    return run, where / 'fzs-engine.sym'


def score(fault=(), extra=()):
    with tempfile.TemporaryDirectory() as t:
        run, sym = write_cup(t, fault)
        p = subprocess.run([sys.executable, str(SCORER), str(run), str(sym), '6',
                            '--player-ms', ','.join(map(str, PLAYER_MS)), *extra],
                           capture_output=True, text=True)
        return p.returncode, p.stdout


class GpOrder(unittest.TestCase):
    def test_well_formed_cup_passes(self):
        rc, out = score()
        self.assertEqual(rc, 0, out)
        self.assertIn('VERDICT PASS', out)

    def test_codex_c127_synthetic_fails(self):
        rc, out = score(('course-zero', 'round-const', 'final-throughout', 'empty-results'))
        self.assertEqual(rc, 1, out)
        self.assertIn('VERDICT FAIL', out)

    def test_each_fault_fails(self):
        expect = {
            'course-zero': 'cup order',
            'round-const': 'cup order',
            'final-throughout': 'standings cup state 5, want 3',
            'empty-results': 'empty or short',
            'results-rows': 'RESULTS record at results',
            'results-stale': 'RESULTS record at results',
            'cup-order': 'cup order',
            'repeat-run': 'repeats an earlier round',
        }
        for fault, why in expect.items():
            with self.subTest(fault=fault):
                rc, out = score((fault,))
                self.assertEqual(rc, 1, out)
                self.assertIn(why, out)

    def test_existing_controls_still_fail(self):
        for ctl in ('missing-round', 'wrong-points'):
            with self.subTest(control=ctl):
                rc, out = score(extra=('--control', ctl))
                self.assertEqual(rc, 1, out)


if __name__ == '__main__':
    unittest.main()
