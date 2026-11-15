"""Importer checks use synthetic records, never bundled F-Zero X assets.

Set FZX_TEST_ROM to a local retail US rev0 image to enable the real-asset check.
The default suite proves parser/ABI/checksum/identity gates; it does not claim
that a retail course was imported or rendered during this run.
"""
from __future__ import annotations

import contextlib
import hashlib
import importlib.util
import io
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("import_course", ROOT / "tools/import_course.py")
imp = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(imp)


def synthetic_record(byte_order="big"):
    """An authored square with distinct signed banks; it is not a game track."""
    endian = ">" if byte_order == "big" else "<"
    buf = bytearray(imp.RECORD_SIZE)
    struct.pack_into("Bbbb", buf, 0, 0, 4, 0, 0)
    buf[9:9 + 12] = b"TEST FIXTURE"
    positions = [(-1000.0, 32.0, -1000.0), (1000.0, 32.0, -1000.0),
                 (1000.0, 32.0, 1000.0), (-1000.0, 32.0, 1000.0)]
    for i, (position, bank) in enumerate(zip(positions, (0, 90, -90, 1))):
        struct.pack_into(endian + "fffhhI", buf, 0x20 + i * 20, *position, 160, 192, 0x40038600)
        struct.pack_into(endian + "h", buf, 0x520 + i * 2, bank)
    for offset, _ in imp.FEATURES.values():
        buf[offset:offset + 64] = b"\xff" * 64
    # Obtained with an independent C translation of upstream Course_CalculateChecksum,
    # cc -std=c11 -O0 -ffp-contract=off. This fixed value detects f64-vs-f32 drift.
    struct.pack_into(endian + "I", buf, 4, 0xFFDB3639)
    return bytes(buf)


def synthetic_rom():
    buf = bytearray(imp.ROM_SIZE)
    buf[:4] = b"\x80\x37\x12\x40"
    buf[0x100:0x108] = b"ABCDEFGH"
    offset = imp.COURSES["mute-city-1"][0]
    buf[offset:offset + imp.RECORD_SIZE] = synthetic_record()
    return bytes(buf)


class CourseRecordTests(unittest.TestCase):
    def test_known_binary_record_checksum_scaling_and_tension(self):
        record = imp.parse_record(synthetic_record(), "big")
        self.assertEqual(record["count"], 4)
        self.assertEqual(record["coordinate_scale"], 16)
        self.assertEqual(record["points"][0]["position"], {"x": -4096000, "y": 131072, "z": -4096000})
        self.assertEqual(record["points"][0]["radius_left"], 655360)
        self.assertEqual(record["points"][0]["radius_right"], 786432)
        self.assertEqual([p["bank"] for p in record["points"]], [0, 16384, 49152, 176])
        self.assertEqual([p["tension"] for p in record["points"]], [51355] * 4)
        self.assertTrue(record["provenance"]["course_checksum_verified"])
        self.assertEqual(record["source_metadata"]["course_checksum"], "ffdb3639")

    def test_little_and_big_endian_records_produce_identical_course(self):
        big = imp.parse_record(synthetic_record("big"), "big")
        little = imp.parse_record(synthetic_record("little"), "little")
        self.assertEqual(big["points"], little["points"])
        self.assertEqual(big["source_metadata"], little["source_metadata"])

    def test_wrong_byte_order_is_not_silently_accepted(self):
        for data, order in ((synthetic_record("big"), "little"), (synthetic_record("little"), "big")):
            with self.subTest(order=order), self.assertRaises(imp.CourseImportError):
                imp.parse_record(data, order)

    def test_integrity_change_is_rejected(self):
        changed = bytearray(synthetic_record())
        struct.pack_into(">f", changed, 0x20, -1001.0)
        with self.assertRaisesRegex(imp.CourseImportError, "checksum mismatch"):
            imp.parse_record(bytes(changed), "big")

    def test_malformed_counts_sizes_and_features(self):
        original = synthetic_record()
        for count in (0, 3, 65, 127, 255):
            changed = bytearray(original)
            changed[1] = count
            with self.subTest(count=count), self.assertRaisesRegex(imp.CourseImportError, "point count"):
                imp.parse_record(bytes(changed), "big")
        for data in (original[:-1], original + b"\0"):
            with self.assertRaisesRegex(imp.CourseImportError, "exactly"):
                imp.parse_record(data, "big")
        changed = bytearray(original)
        changed[0x5E0] = 3  # DASH_MAX is not a dash zone.
        with self.assertRaisesRegex(imp.CourseImportError, "feature dash"):
            imp.parse_record(bytes(changed), "big")

    def test_nonfinite_out_of_bounds_and_degenerate_geometry(self):
        for value in (math.nan, math.inf, -math.inf, 15001.0):
            changed = bytearray(synthetic_record())
            struct.pack_into(">f", changed, 0x20, value)
            with self.subTest(value=value), self.assertRaises(imp.CourseImportError):
                imp.parse_record(bytes(changed), "big")
        changed = bytearray(synthetic_record())
        changed[0x34:0x40] = changed[0x20:0x2C]
        with self.assertRaisesRegex(imp.CourseImportError, "duplicate"):
            imp.parse_record(bytes(changed), "big")
        changed = bytearray(synthetic_record())
        struct.pack_into(">h", changed, 0x2C, 0)
        with self.assertRaisesRegex(imp.CourseImportError, "radii"):
            imp.parse_record(bytes(changed), "big")

    def test_bank_conversion_matches_original_integer_table(self):
        self.assertEqual(imp.bank_to_turns(1), 176)
        self.assertEqual(imp.bank_to_turns(-1), 65360)
        self.assertEqual(imp.bank_to_turns(45), 8192)
        self.assertEqual(imp.bank_to_turns(360), 0)
        self.assertEqual(imp.bank_to_turns(-360), 0)

    def test_renderer_radius_boundaries_and_rejection(self):
        for offset in (0x2C, 0x2E):  # Both left and right radii are constrained.
            for radius in (3, 4097):
                changed = bytearray(synthetic_record())
                struct.pack_into(">h", changed, offset, radius)
                with self.subTest(offset=offset, radius=radius), \
                     self.assertRaisesRegex(imp.CourseImportError, r"radii must be in \[4, 4096\]"):
                    imp.parse_record(bytes(changed), "big")
        for radius in (4, 4096):
            changed = bytearray(synthetic_record())
            points = []
            for i in range(4):
                struct.pack_into(">hh", changed, 0x20 + i * 20 + 12, radius, radius)
                x, y, z, left, right, info = struct.unpack_from(">fffhhI", changed, 0x20 + i * 20)
                points.append({"position": (x, y, z), "radius_left": left, "radius_right": right,
                               "bank_degrees": struct.unpack_from(">h", changed, 0x520 + i * 2)[0],
                               "track_segment_info": info})
            features = {name: [-1] * 4 for name in imp.FEATURES}
            # These are new synthetic boundary fixtures. The independent fixed
            # checksum fixture above continues to test numerical fidelity.
            struct.pack_into(">I", changed, 4, imp.course_checksum(points, features))
            result = imp.parse_record(bytes(changed), "big")
            for point in result["points"]:
                self.assertEqual(point["radius_left"], radius * 4096)
                self.assertEqual(point["radius_right"], radius * 4096)

    def test_renderer_coordinate_overflow_reports_clear_limit(self):
        for offset in (0x20, 0x24, 0x28):
            for value in (-65537.0, 65537.0):
                changed = bytearray(synthetic_record())
                struct.pack_into(">f", changed, offset, value)
                with self.subTest(offset=offset, value=value), \
                     self.assertRaisesRegex(imp.CourseImportError, "renderer limit"):
                    imp.parse_record(bytes(changed), "big")

    def test_fixed_point_rounding_and_overflow(self):
        self.assertEqual(imp.to_fixed(1 / 8192), 1)
        self.assertEqual(imp.to_fixed(-1 / 8192), -1)
        self.assertEqual(imp.to_fixed(2, coordinate=False), 131072)
        for value in (math.nan, math.inf, 524288.0):
            with self.assertRaises(imp.CourseImportError):
                imp.to_fixed(value)


class RomIdentityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = synthetic_rom()

    def test_three_rom_interleavings_are_normalized_by_magic(self):
        v64 = bytearray(len(self.data))
        v64[0::2], v64[1::2] = self.data[1::2], self.data[0::2]
        n64 = bytearray(len(self.data))
        for i in range(4):
            n64[i::4] = self.data[3 - i::4]
        for data, expected_order in ((self.data, "z64-big-endian"),
                                     (bytes(v64), "v64-byte-swapped"),
                                     (bytes(n64), "n64-word-swapped")):
            normalized, actual_order = imp.normalize_rom(data)
            self.assertEqual(normalized, self.data)
            self.assertEqual(actual_order, expected_order)

    def test_plausible_header_and_course_do_not_bypass_rom_identity(self):
        with self.assertRaisesRegex(imp.CourseImportError, "ROM fingerprint mismatch"):
            imp.extract_rom_course(self.data)

    def test_extraction_offset_under_explicitly_synthetic_test_profile(self):
        # This checks the extraction plumbing, not success against a real game image.
        with mock.patch.object(imp, "EXPECTED_ROM_MD5", hashlib.md5(self.data).hexdigest()), \
             mock.patch.object(imp, "EXPECTED_ROM_SHA1", hashlib.sha1(self.data).hexdigest()):
            record, provenance = imp.extract_rom_course(self.data)
        self.assertEqual(record, synthetic_record())
        self.assertEqual(provenance["rom_offset"], 0x2AD1E0)
        self.assertEqual(provenance["course_symbol"], "aCourseMuteCity1")

    def test_wrong_sizes_headers_and_unknown_courses(self):
        for data in (self.data[:-1], b"INVALID" + self.data[7:]):
            with self.assertRaises(imp.CourseImportError):
                imp.normalize_rom(data)
        with self.assertRaisesRegex(imp.CourseImportError, "Unsupported course"):
            imp.extract_rom_course(self.data, "invented-track")

    @unittest.skipUnless(os.environ.get("FZX_TEST_ROM"), "No user-supplied retail ROM; real-asset test not run")
    def test_user_supplied_retail_rom(self):
        record, provenance = imp.extract_rom_course(Path(os.environ["FZX_TEST_ROM"]).read_bytes())
        course = imp.parse_record(record, "big")
        self.assertTrue(provenance["rom_fingerprint_verified"])
        self.assertEqual(course["source_metadata"]["creator_id"], 4)
        self.assertEqual(course["source_metadata"]["venue"], 0)
        self.assertGreaterEqual(course["count"], 4)


def run_receipt_check(receipt):
    return subprocess.run(["sh", str(ROOT / "tools/check_receipt.sh"), str(receipt)],
                          capture_output=True, text=True)


@unittest.skipUnless(shutil.which("sh") and shutil.which("sha256sum"), "receipt check needs sh and sha256sum")
class ReceiptTests(unittest.TestCase):
    """G0.3: builds must refuse generated files that no longer match their receipt."""

    def generate(self, path):
        source, output = path / "synthetic.bin", path / "course.h"
        source.write_bytes(synthetic_record())
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(imp.main(["--record", str(source), "--byte-order", "big", "--output", str(output)]), 0)
        return output

    def test_fresh_outputs_pass_and_receipt_holds_no_paths(self):
        with tempfile.TemporaryDirectory() as temp:
            output = self.generate(Path(temp))
            receipt = output.with_suffix(".receipt")
            text = receipt.read_text()
            self.assertNotIn(temp, text)
            self.assertIn(f"output {hashlib.sha256(output.read_bytes()).hexdigest()} course.h", text)
            result = run_receipt_check(receipt)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_hand_edited_header_is_refused(self):
        # The control: a valid header edited by hand must stop the build.
        with tempfile.TemporaryDirectory() as temp:
            output = self.generate(Path(temp))
            output.write_text(output.read_text() + "/* edited */\n")
            result = run_receipt_check(output.with_suffix(".receipt"))
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("stale: course.h", result.stderr)

    def test_missing_receipt_is_refused(self):
        with tempfile.TemporaryDirectory() as temp:
            output = self.generate(Path(temp))
            output.with_suffix(".receipt").unlink()
            self.assertNotEqual(run_receipt_check(output.with_suffix(".receipt")).returncode, 0)


class OutputTests(unittest.TestCase):
    def test_cli_exports_json_abi_and_compilable_header(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            source, output = path / "synthetic.bin", path / "course.h"
            source.write_bytes(synthetic_record())
            with contextlib.redirect_stdout(io.StringIO()):
                result = imp.main(["--record", str(source), "--byte-order", "big", "--output", str(output)])
            self.assertEqual(result, 0)
            payload = json.loads(output.with_suffix(".json").read_text())
            self.assertEqual(payload["schema_version"], 1)
            self.assertEqual(payload["coordinate_scale"], 16)
            self.assertFalse(payload["provenance"]["rom_fingerprint_verified"])
            self.assertIsNone(payload["provenance"]["verified_rom_version"])
            self.assertEqual(payload["provenance"]["decomp_commit"], imp.DECOMP_COMMIT)
            self.assertEqual(source.read_bytes(), synthetic_record())
            for point in payload["points"]:
                self.assertTrue(all(type(v) is int for v in point["position"].values()))
                self.assertTrue(all(type(point[k]) is int for k in
                                    ("radius_left", "radius_right", "bank", "tension", "track_segment_info")))
            compiler = shutil.which("cc")
            dependency = ROOT / "third_party/SH2ZAM/saturn/include"
            if compiler and dependency.is_dir():
                check = path / "check.c"
                check.write_text('#include "course.h"\nint main(void) { return sizeof(fzx_course_data) == 2052 && '
                                 'fzx_course_data.count == 4 && fzx_course_data.points[3].bank == 176 ? 0 : 1; }\n')
                subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "include"),
                                "-I", str(dependency), str(check), "-o", str(path / "check")], check=True,
                               capture_output=True, text=True)
                subprocess.run([str(path / "check")], check=True)

    def test_failure_does_not_write_outputs_or_mutate_input(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            source, output = path / "invalid.bin", path / "course.h"
            source.write_bytes(bytes(imp.RECORD_SIZE))
            with contextlib.redirect_stderr(io.StringIO()):
                result = imp.main(["--record", str(source), "--byte-order", "big", "--output", str(output)])
            self.assertEqual(result, 2)
            self.assertFalse(output.exists())
            self.assertFalse(output.with_suffix(".json").exists())
            self.assertEqual(source.read_bytes(), bytes(imp.RECORD_SIZE))

    def test_cannot_overwrite_source_file(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "synthetic.bin"
            source.write_bytes(synthetic_record())
            with contextlib.redirect_stderr(io.StringIO()):
                result = imp.main(["--record", str(source), "--byte-order", "big", "--output", str(source)])
            self.assertEqual(result, 2)
            self.assertEqual(source.read_bytes(), synthetic_record())

    def test_unsupported_renderer_bounds_do_not_write_outputs(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            source, output = path / "wide-course.bin", path / "course.h"
            data = bytearray(synthetic_record())
            struct.pack_into(">h", data, 0x2C, 4097)
            source.write_bytes(data)
            errors = io.StringIO()
            with contextlib.redirect_stderr(errors):
                result = imp.main(["--record", str(source), "--byte-order", "big", "--output", str(output)])
            self.assertEqual(result, 2)
            self.assertIn("matching the renderer", errors.getvalue())
            self.assertFalse(output.exists())
            self.assertFalse(output.with_suffix(".json").exists())
            self.assertEqual(source.read_bytes(), bytes(data))


if __name__ == "__main__":
    unittest.main()
