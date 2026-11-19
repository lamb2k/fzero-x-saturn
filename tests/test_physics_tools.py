"""HOST tests for the E1 physics tools: pack baker rounding and layout, the course-line
agreement hash, and the qualification scorer, each with a control. No ROM data: the
physics source here is synthetic."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import physics_pack  # noqa: E402
import physics_qualify  # noqa: E402

NODE_WORDS = 3 + 3 + 1 + 2 + 2 + 9 + 3 + 4
HEADER_WORDS = 8 + 25
BODY_WORDS = 7 + 6 + 27 + 15 + 14


def synthetic_source(segments=4, zones=1):
    h = lambda x: float(x).hex()
    lines = ["# fzx-physics-source v1", "course %d 0 %s" % (segments, h(4000.0 * segments))]
    for i in range(segments):
        v = [1000.0 * i, 0, -500.0, 0, 1, 0, 300, 300, 0.5, 4000.0, 4000.0 * i] + [0.0] * 9 + [1000.0, 2000.0, 3000.0]
        lines.append("seg %d %d %s" % (i, 0x8000040, " ".join(h(x) for x in v)))
        if i == 1 and zones:
            lines.append("zone 1 3 %s" % " ".join(h(x) for x in (0.4, 0.6, -100, 0, 0, 100, 0, 0)))
    lines.append("machine 0 0 " + " ".join(h(0.01 * k) for k in range(25)))
    lines.append("state %d 0 %s" % (0x60008000, " ".join(h(x) for x in (0, 1, 0, 0, 460, 0))))
    for key, n in (("pos", 3), ("vel", 3), ("body", 9), ("road", 9), ("gravity_up", 3), ("tilt_up", 3),
                   ("ground_up", 3), ("fwd", 3), ("spos", 3), ("disp", 3), ("ground", 3), ("bvel", 3)):
        lines.append("%s 0 0 %s" % (key, " ".join(h(0.5) for _ in range(n))))
    lines.append("scalars 0 0 " + " ".join(h(0.5) for _ in range(14)))
    return "\n".join(lines) + "\n"


class PackTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(__file__).resolve().parent / "_physics_tmp.txt"
        self.tmp.write_text(synthetic_source())

    def tearDown(self):
        self.tmp.unlink()

    def test_rounding_is_nearest_away_from_zero(self):
        self.assertEqual(physics_pack.fx(0.5 / 65536, 16), 1)
        self.assertEqual(physics_pack.fx(-0.5 / 65536, 16), -1)
        self.assertEqual(physics_pack.fx(1.0, 30), 1 << 30)
        with self.assertRaises(ValueError):  # control: out of range
            physics_pack.fx(40000.0, 16)

    def test_layout_size(self):
        data = physics_pack.build(physics_pack.parse(self.tmp))
        words = HEADER_WORDS + 4 * NODE_WORDS + 9 + BODY_WORDS
        self.assertEqual(len(data), 4 * words)
        self.assertEqual(struct.unpack(">I", data[:4])[0], physics_pack.MAGIC)
        # Node 1 carries its zone: first 0, count 1; side-bounded flag set.
        node1 = HEADER_WORDS + NODE_WORDS
        shape, zones = struct.unpack(">ii", data[4 * (node1 + 23):4 * (node1 + 25)])
        self.assertEqual(shape, 1 | (1 << 8))
        self.assertEqual(zones, 0 | (1 << 16))

    def test_line_hash_matches_the_c_definition(self):
        src = physics_pack.parse(self.tmp)
        h = 0xCBF29CE484222325
        for w in [16000 * 4096, 4] + [4000 * 4096 * i for i in range(4)]:
            for b in struct.pack(">I", w):
                h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
        self.assertEqual(physics_pack.line_hash(src), h)
        src["course"] = (4, 0, [16001.0])  # control: another lap length
        self.assertNotEqual(physics_pack.line_hash(src), h)


class ScoreTests(unittest.TestCase):
    def test_rules(self):
        self.assertTrue(physics_qualify.score("rel", 100.0, 100.9, 0.01)[0])
        self.assertFalse(physics_qualify.score("rel", 100.0, 101.1, 0.01)[0])
        self.assertTrue(physics_qualify.score("abs", 10, 11, 1)[0])
        self.assertFalse(physics_qualify.score("abs", 10, 12, 1)[0])
        self.assertTrue(physics_qualify.score("exact", 15, 15, 0)[0])
        self.assertFalse(physics_qualify.score("exact", 15, 16, 0)[0])
        # Wall energy: 3% of the loss, at least 0.1 energy unit.
        self.assertTrue(physics_qualify.score("wall", 2.0, 2.09, 0.03)[0])
        self.assertFalse(physics_qualify.score("wall", 2.0, 2.11, 0.03)[0])
        self.assertFalse(physics_qualify.score("rel", 1.0, None, 0.01)[0])

    def test_unlock_on_second_crossing(self):
        rows = [{"tick": t, "lap": lap, "flags": flags, "laps_completed": 0}
                for t, lap, flags in ((0, 0, 0), (1, 1, 0), (2, 1, 0), (3, 2, 1 << 20), (4, 2, 1 << 20))]
        self.assertTrue(physics_qualify.derived("C03_lap_boost", rows, {})["unlock_on_second_crossing"])
        rows[3]["flags"] = 0  # control: unlocked a tick late
        self.assertFalse(physics_qualify.derived("C03_lap_boost", rows, {})["unlock_on_second_crossing"])


if __name__ == "__main__":
    unittest.main()
