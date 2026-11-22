"""HOST: the engine gate's margin judge (ENGINE_PLAN 5 gate 9, C-127 finding 1).

The rule is the unrounded critical-path p99 times 1.36 within 33.3 ms (33.3 / 1.36 = 24.48529... ms). The judge used
to compare the p99 rounded to 0.01 ms against the rounded 24.5 ms, which passed 24.49 (33.3064 ms) and 24.50 (33.32 ms).
"""
from pathlib import Path
import json
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import engine_gate  # noqa: E402

LIM = json.loads((ROOT / 'tools' / 'engine_gate_matrix.json').read_text())['limits']


def old_judge(p99):
    """The judge before C-127: p99 rounded to 0.01 ms against the matrix's rounded 24.5 ms."""
    return round(p99, 2) <= 24.5


class Margin(unittest.TestCase):
    def test_limits_are_the_plan(self):
        self.assertEqual(LIM['critical_budget_ms'], 33.3)
        self.assertEqual(LIM['critical_margin'], 1.36)
        self.assertNotIn('critical_p99_ms', LIM)

    def test_boundary(self):
        cases = [   # p99 ms, passes, p99 x 1.36
            (24.40, True),    # 33.184
            (24.48, True),    # 33.2928
            (24.4852, True),  # 33.299872
            (24.4853, False), # 33.300008
            (24.49, False),   # 33.3064
            (24.50, False),   # 33.32
            (24.84, False),   # f439f54 field
            (26.55, False),   # f439f54 Sand Ocean
        ]
        for p99, want in cases:
            with self.subTest(p99=p99):
                ok, product = engine_gate.margin_ok(p99, LIM)
                self.assertEqual(ok, want, 'p99 %.4f x 1.36 = %.6f' % (p99, product))
                self.assertAlmostEqual(product, p99 * 1.36, places=9)

    def test_old_judge_passes_what_the_rule_fails(self):
        """Control: the rounded judge accepts the three boundary misses that the rule rejects."""
        for p99 in (24.4853, 24.49, 24.50, 24.504):
            with self.subTest(p99=p99):
                self.assertTrue(old_judge(p99))
                self.assertFalse(engine_gate.margin_ok(p99, LIM)[0])

    def test_frame_rows_keeps_the_unrounded_p99(self):
        """An E0 report whose critical p99 is 24.4895 ms: rounded 24.49, the raw value reaches the judge."""
        ms = engine_gate.FIELD_MS / 1000              # 1000 FRT ticks per field
        ticks = round(24.4895 / ms)                    # master busy only (x[4]); wait x[5] = 0
        rows = b''.join(struct.pack('>3I12H', 0, 0, 2 * (i + 1), 0, ticks, *([0] * 10)) for i in range(200))

        class FakeMem:
            def has(self, name): return False
            def words(self, name, n): return [0, 200, 0, 0x06001000] + [0] * 8 + [1000] + [0] * 3
            def raw(self, a, n): return rows[a - 0x06001000:a - 0x06001000 + n]

        fr = engine_gate.frame_rows(FakeMem(), 0)
        raw = ticks * ms
        self.assertEqual(fr['critical_p99_raw'], raw)
        self.assertEqual(fr['critical'][1], round(raw, 2))
        self.assertTrue(old_judge(fr['critical'][1]))
        self.assertFalse(engine_gate.margin_ok(fr['critical_p99_raw'], LIM)[0])


if __name__ == '__main__':
    unittest.main()
