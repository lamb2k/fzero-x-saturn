"""The census can resolve the same symbol file after a stored run is rescored."""
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import sys
import unittest
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from engine_gate import Gate, Rescore


class ReceiptMapping(unittest.TestCase):
    def test_live_and_saved_run_mapping(self):
        with TemporaryDirectory() as folder:
            root = Path(folder)
            ordinary = dict(name="loop", image="scenes", kind="loop")
            repeat = dict(ordinary, name="loop.2")
            control = dict(ordinary, name="ctl-quiet", kind="reset", image_build="quiet-arm")
            jobs = [(r, "script", "pad.txt") for r in (ordinary, repeat, control)]
            alt = root / "ctl-builds" / "quiet-arm"
            for run, _, _ in jobs:
                run_dir = root / "runs" / run["name"]
                run_dir.mkdir(parents=True)
                (run_dir / "result.txt").write_text("exit ok\n" if run is not control else "exit fail\n")
            for cls in (Gate, Rescore):
                gate = cls.__new__(cls)
                gate.ev = root
                gate.a = SimpleNamespace(jobs=1)
                gate.m = {"images": {"scenes": {"sound": False}}}
                gate.ctl_builds = {"quiet-arm": {"dir": str(alt)}}
                gate.harness = Mock(side_effect=lambda *a, **k: {"rc": 0, "seconds": 1})
                rows = gate.execute(jobs)
                for run, _, _ in jobs:
                    with self.subTest(writer=cls.__name__, run=run["name"]):
                        rec = rows[run["name"]]
                        self.assertEqual(rec["image"], "scenes")
                        self.assertEqual(rec["kind"], run["kind"])
                        self.assertEqual(rec["build"], str(alt if run is control else root / "builds" / "scenes"))
                        self.assertEqual(rec["control_build"], run.get("image_build", ""))
                        if cls is Rescore:
                            self.assertTrue(rec["rescored"])
                            self.assertEqual(rec["rc"], int(run is control))
                if cls is Rescore:
                    gate.harness.assert_not_called()


if __name__ == "__main__":
    unittest.main()

