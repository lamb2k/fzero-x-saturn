"""Native material gate against the pinned HLE shader and source equations."""
from pathlib import Path
import shutil
import os
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHADER = ROOT / "upstream/G-Diffuser/libultraship/src/fast/shaders/opengl/default.shader.glsl"


@unittest.skipUnless(SHADER.exists() and shutil.which("gcc") and shutil.which("g++"),
                     "Pinned renderer and host compilers required")
class NativeMaterialTests(unittest.TestCase):
    def test_source_shader_and_controls(self):
        text = SHADER.read_text()
        start = text.index("    vec4 filter3point(")
        end = text.index("\n    }", start) + len("\n    }")
        oracle = text[start:end]
        original = (ROOT / "src/fzx_material.c").read_text()
        controls = [("uv-scale", original.replace("tc * scale", "tc * (int32_t)(scale + 1u)")),
                    ("reflection", original.replace("65535u - s.a", "65535u")),
                    ("filter-diagonal", original.replace("fx + fy < 256u", "fx + fy < 200u"))]
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder / "material_oracle.inc").write_text(oracle)
            def run(source, compiler="gcc", cpp="g++"):
                flags = shlex.split(os.environ.get("FZX_TEST_CFLAGS", ""))
                unit = folder / "material.c"
                unit.write_text(source)
                subprocess.run([compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-Wconversion", "-Wshadow", "-Wpedantic",
                                *flags, "-I" + str(ROOT / "include"), "-c", str(unit), "-o", str(folder / "material.o")], check=True, capture_output=True)
                subprocess.run([cpp, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-I" + str(ROOT / "include"),
                                *flags, "-I" + str(folder), str(ROOT / "tests/material_driver.cpp"), str(folder / "material.o"),
                                "-o", str(folder / "gate")], check=True, capture_output=True)
                return subprocess.run([str(folder / "gate")], capture_output=True, text=True)
            result = run(original)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("PASS", result.stdout)
            if shutil.which("clang") and shutil.which("clang++"):
                result = run(original, "clang", "clang++")
                self.assertEqual(result.returncode, 0, result.stderr)
            for name, control in controls:
                self.assertNotEqual(control, original)
                with self.subTest(control=name):
                    result = run(control)
                    self.assertNotEqual(result.returncode, 0, "Broken material control was not detected")


if __name__ == "__main__":
    unittest.main()
