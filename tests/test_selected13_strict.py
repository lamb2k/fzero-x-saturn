"""tools/selected13_strict.py on synthetic captures (no game data): the exit status follows the verdict,
missing buffers fail, and referenced shade entries are compared while unused shade slots may differ."""
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# SELECTED13_STRICT_TOOL lets the regressions be run against an older checker to show they fail there.
TOOL = Path(os.environ.get('SELECTED13_STRICT_TOOL', ROOT / 'tools/selected13_strict.py'))
HOLDS = [1, 300, 301, 500, 501, 542, 583, 624, 664, 665, 674, 684, 685]
SHADES = 0x25C5AB20
OTHER = ['c3-clip-command', 'c3-metadata', 'c3-images', 'c3-textures', 'c3-positions', 'c3-remap',
         'c3-packets-capacity', 'countdown-commands', 'countdown-metadata', 'countdown-textures']


def command(shade_index):
    words = [0] * 16
    words[14] = (SHADES - 0x25C00000) // 8 + shade_index
    return struct.pack('>16H', *words)


def capture(root, name):
    d = root / name
    s = d / 'snapshots'
    s.mkdir(parents=True)
    (d / 'receipt.json').write_text(json.dumps(dict(holds=HOLDS)))
    report = struct.pack('>24I', *([0] * 9 + [SHADES] + [0] * 14)).hex()
    for h in HOLDS:
        (s / f'hold-{h}.json').write_text(json.dumps(dict(
            target=h, checks_pass=True,
            guards=[dict(name='stack', **{'pass': True}), dict(name='road', **{'pass': True}), dict(name='road', **{'pass': True})],
            packets=dict(_fzx_road_stream_report=report))))
        # Three commands referencing shade entries 0, 2 and 5; one without Gouraud (CMDGRDA 0).
        (s / f'hold-{h}.c3-commands').write_bytes(command(0) + command(2) + command(5) + bytes(32))
        (s / f'hold-{h}.c3-shades').write_bytes(bytes(range(64)))
        for k in OTHER:
            (s / f'hold-{h}.{k}').write_bytes(bytes([h & 255, len(k)]) * (16 if k.endswith('commands') or k.endswith('command') else 8))
    return d


class CheckerCase(unittest.TestCase):
    def run_tool(self, base, cand, mode='exact'):
        out = base.parent / 'out.json'
        r = subprocess.run([sys.executable, str(TOOL), str(base), str(cand), mode, str(out)],
                           capture_output=True, text=True)
        verdict = json.loads(out.read_text())['verdict'] if out.exists() else None
        return r.returncode, verdict, r.stdout

    def pair(self, temp):
        root = Path(temp)
        return capture(root, 'base'), capture(root, 'cand')


class Selected13StrictTests(CheckerCase):
    def test_clean_capture_passes(self):
        with tempfile.TemporaryDirectory() as t:
            base, cand = self.pair(t)
            self.assertEqual(self.run_tool(base, cand)[:2], (0, 'PASS'))

    def test_unreferenced_shade_slot_may_differ(self):
        with tempfile.TemporaryDirectory() as t:
            base, cand = self.pair(t)
            p = cand / 'snapshots/hold-542.c3-shades'
            b = bytearray(p.read_bytes()); b[8 * 7] ^= 1; p.write_bytes(bytes(b))  # entry 7 is not referenced
            self.assertEqual(self.run_tool(base, cand)[:2], (0, 'PASS'))

    def test_fail_verdict_exits_nonzero(self):
        with tempfile.TemporaryDirectory() as t:
            base, cand = self.pair(t)
            p = cand / 'snapshots/hold-300.c3-metadata'
            b = bytearray(p.read_bytes()); b[0] ^= 1; p.write_bytes(bytes(b))
            code, verdict, _ = self.run_tool(base, cand)
            self.assertEqual(verdict, 'FAIL'); self.assertNotEqual(code, 0)

    def test_buffer_missing_on_one_side_fails(self):
        with tempfile.TemporaryDirectory() as t:
            base, cand = self.pair(t)
            (cand / 'snapshots/hold-665.c3-images').unlink()
            code, verdict, _ = self.run_tool(base, cand)
            self.assertEqual(verdict, 'FAIL'); self.assertNotEqual(code, 0)

    def test_buffer_missing_on_both_sides_fails(self):
        with tempfile.TemporaryDirectory() as t:
            base, cand = self.pair(t)
            (base / 'snapshots/hold-665.c3-images').unlink(); (cand / 'snapshots/hold-665.c3-images').unlink()
            code, verdict, _ = self.run_tool(base, cand)
            self.assertEqual(verdict, 'FAIL'); self.assertNotEqual(code, 0)

    def test_referenced_shade_change_fails(self):
        with tempfile.TemporaryDirectory() as t:
            base, cand = self.pair(t)
            p = cand / 'snapshots/hold-542.c3-shades'
            b = bytearray(p.read_bytes()); b[8 * 5 + 3] ^= 1; p.write_bytes(bytes(b))  # entry 5 is referenced
            code, verdict, _ = self.run_tool(base, cand)
            self.assertEqual(verdict, 'FAIL'); self.assertNotEqual(code, 0)

    def test_missing_guards_fail(self):
        with tempfile.TemporaryDirectory() as t:
            base, cand = self.pair(t)
            p = cand / 'snapshots/hold-1.json'
            m = json.loads(p.read_text()); m['guards'] = []; p.write_text(json.dumps(m))
            code, verdict, _ = self.run_tool(base, cand)
            self.assertEqual(verdict, 'FAIL'); self.assertNotEqual(code, 0)

    def test_lod_mode_allows_fewer_road_commands_only(self):
        with tempfile.TemporaryDirectory() as t:
            base, cand = self.pair(t)
            p = cand / 'snapshots/hold-542.c3-commands'
            p.write_bytes(p.read_bytes()[:64])
            self.assertEqual(self.run_tool(base, cand, 'lod')[:2], (0, 'PASS'))
            p.write_bytes(p.read_bytes() * 10)
            code, verdict, _ = self.run_tool(base, cand, 'lod')
            self.assertEqual(verdict, 'FAIL'); self.assertNotEqual(code, 0)


class Selected13StrictRegressions(CheckerCase):
    """C-028: each case below used to give a false pass, a crash or no verdict."""
    def edit_meta(self, run, h, change):
        p = run / f'snapshots/hold-{h}.json'
        m = json.loads(p.read_text())
        change(m)
        p.write_text(json.dumps(m))

    def test_truncated_final_command_fails_in_both_modes(self):
        for mode in ('exact', 'lod'):
            with tempfile.TemporaryDirectory() as t:
                base, cand = self.pair(t)
                for run in (base, cand):
                    p = run / 'snapshots/hold-542.c3-commands'
                    p.write_bytes(p.read_bytes()[:-1])  # final record 31 bytes, identical on both sides
                code, verdict, _ = self.run_tool(base, cand, mode)
                self.assertEqual(verdict, 'FAIL', mode)
                self.assertNotEqual(code, 0, mode)

    def test_metadata_naming_another_hold_fails(self):
        with tempfile.TemporaryDirectory() as t:
            base, cand = self.pair(t)
            for run in (base, cand):
                self.edit_meta(run, 300, lambda m: m.update(target=301))
            code, verdict, _ = self.run_tool(base, cand)
            self.assertEqual(verdict, 'FAIL')
            self.assertNotEqual(code, 0)

    def test_string_false_guard_and_checks_fail(self):
        for change in (lambda m: m['guards'][1].update({'pass': 'false'}),
                       lambda m: m.update(checks_pass='false'),
                       lambda m: m['guards'][0].update(name=''),
                       lambda m: m['guards'][0].update(name=7)):
            with tempfile.TemporaryDirectory() as t:
                base, cand = self.pair(t)
                for run in (base, cand):
                    self.edit_meta(run, 624, change)
                code, verdict, _ = self.run_tool(base, cand)
                self.assertEqual(verdict, 'FAIL')
                self.assertNotEqual(code, 0)

    def test_failing_pair_keeps_its_verdict_when_controls_cannot_run(self):
        with tempfile.TemporaryDirectory() as t:
            base, cand = self.pair(t)
            (base / 'snapshots/hold-542.c3-metadata').unlink()
            (cand / 'snapshots/hold-542.c3-metadata').unlink()
            code, verdict, _ = self.run_tool(base, cand)
            self.assertEqual((code, verdict), (1, 'FAIL'))
            res = json.loads((base.parent / 'out.json').read_text())
            row = next(r for r in res['holds'] if r['hold'] == 542)
            self.assertIn('c3-metadata missing (both)', row['reasons'])

    def test_short_road_report_is_unusable_with_json(self):
        with tempfile.TemporaryDirectory() as t:
            base, cand = self.pair(t)
            self.edit_meta(cand, 500, lambda m: m['packets'].update(_fzx_road_stream_report='4333'))
            code, verdict, _ = self.run_tool(base, cand)
            self.assertEqual((code, verdict), (2, 'UNUSABLE'))


class Selected13StrictInputValidation(CheckerCase):
    """C-029: malformed input is UNUSABLE (exit 2, JSON), not a verdict."""
    def test_receipt_true_for_hold_one_is_unusable(self):
        with tempfile.TemporaryDirectory() as t:
            base, cand = self.pair(t)
            p = cand / 'receipt.json'
            r = json.loads(p.read_text()); r['holds'][0] = True; p.write_text(json.dumps(r))
            self.assertEqual(self.run_tool(base, cand)[:2], (2, 'UNUSABLE'))

    def test_metadata_list_is_unusable(self):
        with tempfile.TemporaryDirectory() as t:
            base, cand = self.pair(t)
            (cand / 'snapshots/hold-300.json').write_text('[]')
            code, verdict, _ = self.run_tool(base, cand)
            self.assertEqual((code, verdict), (2, 'UNUSABLE'))
            reason = json.loads((base.parent / 'out.json').read_text())['reason']
            self.assertIn('candidate', reason); self.assertIn('hold-300', reason)

    def test_metadata_invalid_json_is_unusable(self):
        with tempfile.TemporaryDirectory() as t:
            base, cand = self.pair(t)
            (base / 'snapshots/hold-300.json').write_text('{"target": 300,')
            code, verdict, _ = self.run_tool(base, cand)
            self.assertEqual((code, verdict), (2, 'UNUSABLE'))
            reason = json.loads((base.parent / 'out.json').read_text())['reason']
            self.assertIn('base', reason); self.assertIn('hold-300', reason)

    def test_well_formed_record_with_failing_guard_stays_fail(self):
        with tempfile.TemporaryDirectory() as t:
            base, cand = self.pair(t)
            p = cand / 'snapshots/hold-300.json'
            m = json.loads(p.read_text()); m['guards'][0]['pass'] = False; p.write_text(json.dumps(m))
            self.assertEqual(self.run_tool(base, cand)[:2], (1, 'FAIL'))


if __name__ == '__main__':
    unittest.main()
