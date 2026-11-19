"""tools/selected13_strict.py on synthetic captures (no game data): the exit status follows the verdict,
missing buffers fail, and referenced shade entries are compared while unused shade slots may differ."""
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / 'tools/selected13_strict.py'
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
            checks_pass=True, guards=[dict(name='stack', **{'pass': True}), dict(name='tables', **{'pass': True})],
            packets=dict(_fzx_road_stream_report=report))))
        # Three commands referencing shade entries 0, 2 and 5; one without Gouraud (CMDGRDA 0).
        (s / f'hold-{h}.c3-commands').write_bytes(command(0) + command(2) + command(5) + bytes(32))
        (s / f'hold-{h}.c3-shades').write_bytes(bytes(range(64)))
        for k in OTHER:
            (s / f'hold-{h}.{k}').write_bytes(bytes([h & 255, len(k)]) * 8)
    return d


class Selected13StrictTests(unittest.TestCase):
    def run_tool(self, base, cand, mode='exact'):
        out = base.parent / 'out.json'
        r = subprocess.run([sys.executable, str(TOOL), str(base), str(cand), mode, str(out)],
                           capture_output=True, text=True)
        verdict = json.loads(out.read_text())['verdict'] if out.exists() else None
        return r.returncode, verdict, r.stdout

    def pair(self, temp):
        root = Path(temp)
        return capture(root, 'base'), capture(root, 'cand')

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


if __name__ == '__main__':
    unittest.main()
