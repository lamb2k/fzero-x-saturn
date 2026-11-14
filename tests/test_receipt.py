"""Generation receipts (card G0.3). Synthetic course records only.

Control: editing the generated header by hand must make the receipt check, and
therefore the Saturn build, stop with a clear message.
"""
from __future__ import annotations

import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CHECK = ROOT / "tools/check_receipt.sh"
SPEC = importlib.util.spec_from_file_location("test_import_course", ROOT / "tests/test_import_course.py")
fixtures = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(fixtures)


def check(receipt: Path) -> subprocess.CompletedProcess:
    return subprocess.run(("sh", str(CHECK), str(receipt)), capture_output=True, text=True)


class ReceiptTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        record = self.dir / "course.bin"
        record.write_bytes(fixtures.synthetic_record())
        self.header = self.dir / "course.h"
        status = fixtures.imp.main(["--record", str(record), "--byte-order", "big",
                                    "--output", str(self.header)])
        self.assertEqual(status, 0)
        self.receipt = self.dir / "course.h.receipt"

    def tearDown(self):
        self.tmp.cleanup()

    def test_fresh_output_passes(self):
        result = check(self.receipt)
        self.assertEqual(result.returncode, 0, result.stderr)
        text = self.receipt.read_text(encoding="utf-8")
        self.assertIn("tools/import_course.py", text)
        self.assertIn("course.json", text)

    def test_hand_edited_header_is_refused(self):
        self.header.write_text(self.header.read_text(encoding="utf-8") + "/* edit */\n",
                               encoding="utf-8")
        result = check(self.receipt)
        self.assertEqual(result.returncode, 1)
        self.assertIn("STALE", result.stderr)

    def test_changed_generator_is_refused(self):
        text = self.receipt.read_text(encoding="utf-8").replace(
            "  tools/import_course.py", "  tools/receipt.py")
        self.receipt.write_text(text, encoding="utf-8")
        self.assertEqual(check(self.receipt).returncode, 1)

    def test_missing_receipt_is_refused(self):
        self.receipt.unlink()
        result = check(self.receipt)
        self.assertEqual(result.returncode, 1)
        self.assertIn("missing", result.stderr)


if __name__ == "__main__":
    unittest.main()
