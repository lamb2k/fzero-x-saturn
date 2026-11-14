"""Negative controls for tools/hygiene_scan.py (card G0.2).

Each test plants one forbidden file in a scratch git repository and requires the
scan to fail; the clean baseline must pass, so a scan that always fails is caught too.
"""
from __future__ import annotations

import contextlib
import importlib.util
import io
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("hygiene_scan", ROOT / "tools/hygiene_scan.py")
scan = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(scan)


class HygieneScanTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        subprocess.run(("git", "init", "-q", str(self.root)), check=True)
        (self.root / "README.md").write_text("clean fixture\n", encoding="utf-8")
        (self.root / "main.c").write_text("int main(void) { return 0; }\n", encoding="utf-8")

    def tearDown(self):
        self.tmp.cleanup()

    def run_scan(self) -> tuple[int, str]:
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            status = scan.main(["--root", str(self.root)])
        return status, out.getvalue()

    def test_clean_tree_passes(self):
        self.assertEqual(self.run_scan()[0], 0)

    def test_planted_z64_stub_fails(self):
        (self.root / "stub.z64").write_bytes(b"\x80\x37\x12\x40" + bytes(60))
        status, out = self.run_scan()
        self.assertEqual(status, 1)
        self.assertIn("forbidden file type .z64", out)
        self.assertIn("N64 ROM header magic", out)

    def test_planted_home_path_fails(self):
        home = "/" + "home/someone/private/F-Zero-X.z64"
        (self.root / "notes.md").write_text(f"ROM is at {home}\n", encoding="utf-8")
        status, out = self.run_scan()
        self.assertEqual(status, 1)
        self.assertIn("absolute private path", out)

    def test_other_private_path_forms_fail(self):
        for text in ("/" + "Users/someone/x", "C:\\" + "Users\\someone", "/" + "root/.cache"):
            with self.subTest(text=text):
                (self.root / "notes.md").write_text(text + "\n", encoding="utf-8")
                self.assertEqual(self.run_scan()[0], 1)

    def test_renamed_rom_by_magic_fails(self):
        (self.root / "data.txt").write_bytes(b"\x37\x80\x40\x12" + b"x" * 60)
        status, out = self.run_scan()
        self.assertEqual(status, 1)
        self.assertIn("N64 ROM header magic", out)

    def test_binary_and_unknown_types_fail(self):
        (self.root / "blob.dat").write_bytes(b"abc\0def")
        status, out = self.run_scan()
        self.assertEqual(status, 1)
        self.assertIn("unknown file type", out)
        self.assertIn("binary content", out)

    def test_ignored_files_are_not_published(self):
        (self.root / ".gitignore").write_text("*.z64\n", encoding="utf-8")
        (self.root / "local.z64").write_bytes(b"\x80\x37\x12\x40")
        self.assertEqual(self.run_scan()[0], 0)

    def test_repository_itself_is_clean(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            status = scan.main(["--root", str(ROOT)])
        self.assertEqual(status, 0, out.getvalue())


if __name__ == "__main__":
    unittest.main()
