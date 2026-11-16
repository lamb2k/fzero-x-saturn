"""The raster's range rejection must be meaningful, including under UBSan."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which("gcc"), "Host C compiler required")
class ModelControls(unittest.TestCase):
    def test_unsafe_plane_control(self):
        source = (ROOT / "src/fzx_model.c").read_text()
        guard = "if(whole>(INT64_MAX-255)/256||whole<(INT64_MIN+255)/256)return false;"
        self.assertIn(guard, source)
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            unit = folder / "model.c"
            for broken in (False, True):
                unit.write_text(source.replace(guard, "") if broken else source)
                subprocess.run(["gcc", "-std=c11", "-O1", "-g", "-fsanitize=undefined",
                                "-fno-sanitize-recover=all", "-I" + str(ROOT / "include"),
                                str(ROOT / "tests/test_model.c"), str(unit),
                                str(ROOT / "src/fzx_material.c"), "-o", str(folder / "gate")],
                               check=True, capture_output=True)
                result = subprocess.run([str(folder / "gate")], capture_output=True, text=True)
                if broken:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("signed integer overflow", result.stderr)
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)
