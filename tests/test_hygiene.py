"""G0.2: the hygiene scan must pass on this tree and fail on each planted problem.

The planted files are the gate's controls. Their contents are built at run
time, so no private path or ROM header is ever committed.
"""
from __future__ import annotations

import contextlib
import importlib.util
import io
from pathlib import Path
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("hygiene_scan", ROOT / "tools/hygiene_scan.py")
scan = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(scan)


def run(*paths):
    with contextlib.redirect_stdout(io.StringIO()) as out, contextlib.redirect_stderr(io.StringIO()):
        status = scan.main([str(p) for p in paths])
    return status, out.getvalue()


class HygieneScanTests(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.dir)

    def plant(self, name, data):
        path = self.dir / name
        path.write_bytes(data if isinstance(data, bytes) else data.encode())
        return path

    def test_clean_text_passes(self):
        status, _ = run(self.plant("notes.md", "Supply your ROM at /path/to/F-Zero-X.z64.\n"))
        self.assertEqual(status, 0)

    def test_planted_rom_stub_fails(self):
        status, out = run(self.plant("stub.z64", "not really a rom\n"))
        self.assertEqual(status, 1)
        self.assertIn("forbidden file type .z64", out)

    def test_rom_header_under_any_name_fails(self):
        for magic in scan.N64_MAGICS:
            with self.subTest(magic=magic.hex()):
                status, out = run(self.plant("data.txt", magic + b"F-ZERO X" + b"\0" * 64))
                self.assertEqual(status, 1)
                self.assertIn("N64 ROM header", out)

    def test_planted_home_paths_fail(self):
        homes = ["C:" + "\\" + "Users" + "\\" + "someone" + "\\work\\rom.z64",
                 "/" + "home/" + "someone/work/rom",
                 "/" + "Users/" + "someone/rom",
                 "cd /" + "root/" + "work"]
        for home in homes:
            with self.subTest(home=home):
                status, out = run(self.plant("log.txt", f"built from {home}\n"))
                self.assertEqual(status, 1)
                self.assertIn("private absolute path", out)

    def test_planted_local_machine_paths_fail(self):
        # Built from parts at run time, so the test file itself stays clean.
        paths = ["c:" + "\\" + "users" + "\\" + "someone" + "\\x.txt",
                 "E:" + "\\" + "Project" + "\\docs\\notes.md",
                 "D:/" + "Games/" + "saturn/disc.cue",
                 "\\\\" + "wsl$" + "\\Ubuntu\\home",
                 "\\\\" + "wsl.localhost" + "\\Ubuntu\\srv"]
        for path in paths:
            with self.subTest(path=path):
                status, out = run(self.plant("log.txt", f"read {path}\n"))
                self.assertEqual(status, 1)
                self.assertTrue("private absolute path" in out or "local machine path" in out, out)

    def test_planted_bios_and_rom_names_fail(self):
        names = ["Bios " + "Saturn 1.01a (U) " + "[!]" + ".bin", "Game " + "(USA)" + ".z64", "Game (E) " + "[!]" + ".v64",
                 "sega" + "_101.bin", "mpr" + "-17933.bin", "Sega Saturn " + "BIOS.bin"]
        for name in names:
            with self.subTest(name=name):
                status, out = run(self.plant("notes.md", f"boot with {name}\n"))
                self.assertEqual(status, 1)
                self.assertIn("BIOS or ROM dump file name", out)

    def test_allowed_generic_forms_pass(self):
        lines = ["Set FZX_GATE_BIOS=<bios file>; the ROM is $FZX_TEST_ROM.",
                 "python3 tools/import_course.py --rom /path/to/F-Zero-X.z64",
                 "copy to C:" + "\\" + "path\\to\\evidence; or %USERPROFILE%\\fzx-evidence; ~/fzx-evidence",
                 "if path[1] == ':': path = '/mnt/' + path[0].lower()", "rom.z64 and A.BIN are generic"]
        status, out = run(self.plant("notes.md", "\n".join(lines) + "\n"))
        self.assertEqual(status, 0, out)

    def test_binary_and_oversized_files_fail(self):
        status, out = run(self.plant("blob.dat", b"\0\1\2"))
        self.assertEqual(status, 1)
        self.assertIn("binary content", out)
        status, out = run(self.plant("big.txt", "x" * (scan.MAX_BYTES + 1)))
        self.assertEqual(status, 1)
        self.assertIn("larger than", out)

    @unittest.skipUnless(shutil.which("git") and (ROOT / ".git").exists(), "needs a git checkout")
    def test_tracked_tree_is_clean(self):
        status, out = run()
        self.assertEqual(status, 0, out)


if __name__ == "__main__":
    unittest.main()
