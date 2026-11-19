"""HOST checks of tools/handling_fixtures.py (E1 plays-right fixtures).

These cover the tool itself with synthetic rows: script timing, the one-tick
shift, input-word decoding and the metric reductions, each with a control that
must change the result. The reference runs need the harness and stay local.
"""
import math
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import handling_fixtures as H  # noqa: E402


def row(**values):
    base = dict(speed=0.0, accel_force=0.0, height=8.0, energy=178.0, max_energy=178.0, flags=0, boost=0,
                heading_side=0.0, lateral=0.0, radius_left=300.0, v_side=0.0, v_forward=0.0, slide_boost=0.0,
                fx=1.0, fz=0.0, x=0.0, y=0.0, z=0.0, attack=0)
    base.update(values)
    return base


class ScriptTests(unittest.TestCase):
    def test_every_fixture_action_starts_a_step(self):
        for name, (course, lines, action, kind, driver) in H.fixtures().items():
            starts = [sum(t for t, _, _ in lines[:i]) for i in range(len(lines))]
            self.assertIn(action, starts, name)
            self.assertIn(course, ("pad", "corridor", "straight"), name)
            later = H.delayed(lines, action)
            self.assertEqual(sum(t for t, _, _ in later), sum(t for t, _, _ in lines) + 1, name)

    def test_delay_repeats_the_previous_inputs(self):
        lines = [(10, "-", 0), (5, "A", 0), (7, "A", 64)]
        self.assertEqual(H.delayed(lines, 15), [(10, "-", 0), (5, "A", 0), (1, "A", 0), (7, "A", 64)])
        with self.assertRaises(ValueError):
            H.delayed(lines, 12)

    def test_script_text(self):
        self.assertEqual(H.script([(460, "-", 0), (0, "A", 0), (3, "A+Z", -64)]), "460 - 0 0\n3 A+Z -64 0\n")

    def test_replay_words(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "inputs.hex"
            p.write_text("80000000\nC000C100\n00003f00\n")
            self.assertEqual(H.replay_script(p), "1 A 0 0\n1 A+B -63 0\n1 - 63 0\n")
            self.assertEqual(H.replay_script(p, 1).count("\n"), 4)


class CourseTests(unittest.TestCase):
    def test_records(self):
        for record, count in ((H.pad_course(), 16), (H.corridor_course(), 24)):
            self.assertEqual(len(record), H.RECORD_SIZE)
            self.assertEqual(struct.unpack_from(">Bbbb", record, 0)[1], count)
            self.assertNotEqual(struct.unpack_from(">I", record, 4)[0], 0)
        corridor = H.corridor_course()
        self.assertEqual(struct.unpack_from(">b", corridor, H.FEATURE_OFFSETS["dash"] + 1)[0], 0)
        self.assertEqual(struct.unpack_from(">b", corridor, H.FEATURE_OFFSETS["dash"] + 2)[0], -1)
        self.assertEqual(struct.unpack_from(">b", corridor, H.FEATURE_OFFSETS["jump"] + 5)[0], 0)

    def test_checksum_follows_the_content(self):
        a = H.course_record([(0.0, 0.0, 1000.0 * i, 300, 300, 0) for i in range(8)])
        b = H.course_record([(0.0, 0.0, 1000.0 * i, 300, 301, 0) for i in range(8)])
        self.assertNotEqual(a[4:8], b[4:8])


class MetricTests(unittest.TestCase):
    def test_launch(self):
        rows = [row() for _ in range(5)] + [row(speed=min(30.0, 3.0 * i), accel_force=2.0 - 0.1 * i) for i in range(1, 40)]
        m = H.analyse("launch", rows, 5)
        self.assertEqual(m["v_top"], 30.0)
        self.assertEqual(m["ticks_to_50pct"], 4)
        self.assertEqual(m["ticks_to_90pct"], 8)
        slower = [row() for _ in range(5)] + [row(speed=min(30.0, 1.5 * i)) for i in range(1, 40)]
        self.assertGreater(H.analyse("launch", slower, 5)["ticks_to_90pct"], m["ticks_to_90pct"])

    def test_turn_rate_survives_a_full_circle(self):
        rate = 2.0  # degrees a tick: more than 180 degrees over the steady window
        rows = [row(speed=30.0, v_forward=30.0) for _ in range(10)]
        for i in range(1, 301):
            a = math.radians(rate * i)
            rows.append(row(speed=30.0, v_forward=30.0, fx=math.cos(a), fz=math.sin(a)))
        m = H.analyse("turn", rows, 10)
        self.assertAlmostEqual(m["yaw_rate_steady_deg_per_tick"], rate, places=6)
        self.assertAlmostEqual(m["turn_radius"], 30.0 / math.radians(rate), places=3)

    def test_wall(self):
        rows = [row(speed=33.0) for _ in range(20)]
        rows += [row(speed=33.0, heading_side=-0.5)] * 5
        rows += [row(speed=30.0, heading_side=-0.5, energy=166.5)] + [row(speed=31.0, heading_side=-0.5, energy=166.5)] * 80
        m = H.analyse("wall", rows, 20)
        self.assertTrue(m["hit"])
        self.assertEqual(m["ticks_to_wall"], 5)
        self.assertAlmostEqual(m["approach_angle_deg"], 30.0, places=4)
        self.assertAlmostEqual(m["energy_lost_contact"], 11.5)
        self.assertAlmostEqual(m["speed_into_wall"], 16.5)
        clean = [row(speed=33.0) for _ in range(120)]
        self.assertFalse(H.analyse("wall", clean, 20)["hit"])

    def test_race(self):
        rows = [row(intro=1, lap1=0, lap2=0, lap3=0, race_time=0, laps_completed=1) for _ in range(H.INTRO)]
        for i in range(300):
            finished = H.F_FINISHED if i >= 250 else 0
            rows.append(row(speed=30.0, flags=finished, race_time=100 * i, lap1=1000, lap2=2000, lap3=3000,
                            laps_completed=1 + (i >= 100) + (i >= 200) + (i >= 250), intro=0))
        m = H.race_metrics(rows)
        self.assertTrue(m["finished"])
        self.assertEqual(m["finish_tick"], H.INTRO + 250)
        self.assertEqual(m["lap_times_ms"], [1000, 2000, 3000])
        self.assertEqual(m["lap_cross_ticks"], [H.INTRO + 100, H.INTRO + 200, H.INTRO + 250])
        unfinished = [dict(r, flags=0) for r in rows]
        self.assertFalse(H.race_metrics(unfinished)["finished"])

    def test_delta(self):
        d = H.numeric_delta({"a": 1.0, "b": [1, 2], "c": True, "d": None}, {"a": 1.5, "b": [1, 4], "c": False, "d": 2})
        self.assertEqual(d, {"a": 0.5, "b": [0, 2], "c": "changed"})


if __name__ == "__main__":
    unittest.main()
