"""Source backdrop extraction boundaries and receipt controls (no ROM needed)."""
from pathlib import Path
import hashlib
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import backdrop_state as bg


class BackdropStateTests(unittest.TestCase):
    def setUp(self):
        if not bg.SOURCE.is_file():
            self.skipTest('source submodule not initialized')
        self.source = bg.SOURCE.read_text()

    def test_source_boundary(self):
        native = bg.native(self.source)
        self.assertNotIn('gGfxPool', native)
        self.assertNotIn('Background_UpdateStars', native)
        self.assertNotIn('Background_UpdateBackgroundSprites', native)
        self.assertEqual(native.count('native_Background_UpdateCloudVtx(vtx,'), 2)
        self.assertIn('native_vertices[native_pool]', native)
        for old, new in [('1.2f;', '1.3f;'), ('cloudScroll->xScroll +=', 'cloudScroll->xScroll -='),
                         ('cloudHeightAboveEye == 0.0f', 'cloudHeightAboveEye <= 0.0f')]:
            changed = self.source.replace(old, new)
            self.assertNotEqual(changed, self.source)
            with self.assertRaisesRegex(ValueError, 'Source backdrop changed'):
                bg.native(changed)

    def test_missing_or_ambiguous_boundary(self):
        with self.assertRaises(ValueError):
            bg.function(self.source, 'MissingBackgroundFunction')
        for text in ('no match', 'needle needle'):
            with self.assertRaises(ValueError):
                bg.once(text, 'needle', 'replacement')

    def test_deterministic_receipt_and_stale_control(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)/'background.c'
            bg.generate(bg.SOURCE, output)
            first = output.read_bytes()
            receipt = output.with_suffix('.receipt').read_bytes()
            bg.generate(bg.SOURCE, output)
            self.assertEqual(output.read_bytes(), first)
            self.assertEqual(output.with_suffix('.receipt').read_bytes(), receipt)
            for line in receipt.decode().splitlines()[1:]:
                kind, digest, name = line.split(' ', 2)
                path = ROOT/'tools'/name if kind == 'generator' else output.parent/name
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), digest)
            expected = [line.split()[1] for line in receipt.decode().splitlines()
                        if line.startswith('output ')][0]
            command = ['bash', str(ROOT/'tools/check_receipt.sh'), str(output.with_suffix('.receipt'))]
            if os.name == 'posix':
                self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
            output.write_bytes(first+b'\n/* stale */\n')
            self.assertNotEqual(hashlib.sha256(output.read_bytes()).hexdigest(), expected)
            if os.name == 'posix':
                result = subprocess.run(command, capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('stale:', result.stderr)


if __name__ == '__main__':
    unittest.main()
