"""Source-art decoder gates. All default data is synthetic; retail is opt-in."""
from __future__ import annotations

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import import_source_art as art


def literal_mio0(payload):
    masks = bytes([255] * ((len(payload) + 7) // 8))
    start = 16 + len(masks)
    return b"MIO0" + struct.pack(">III", len(payload), start, start) + masks + payload


class DecoderTests(unittest.TestCase):
    def test_mio0_literals_and_overlapping_copy(self):
        self.assertEqual(art.mio0(literal_mio0(b"synthetic texture")), b"synthetic texture")
        # A literal then seven copies of the previous byte. Reads its own output.
        data = b"MIO0" + struct.pack(">III", 8, 17, 19) + b"\x80\x40\x00A"
        self.assertEqual(art.mio0(data), b"A" * 8)

    def test_mio0_controls(self):
        cases = [b"MIO0", b"NOPE" + bytes(20),
                 b"MIO0" + struct.pack(">III", 8, 17, 19) + b"\x00\x40\x00A",
                 b"MIO0" + struct.pack(">III", 7, 17, 19) + b"\x80\x40\x00A",
                 b"MIO0" + struct.pack(">III", 1, 16, 16) + b"A",
                 literal_mio0(b"abc")[:-1]]
        for data in cases:
            with self.subTest(data=data), self.assertRaises(art.ArtError):
                art.mio0(data)
        with self.assertRaises(art.ArtError):
            art.mio0(literal_mio0(b"abc"), limit=2)

    def test_rgba_channels_alpha_and_native_endian_padding(self):
        raw = struct.pack(">5H", 0xF801, 0x07C1, 0x003F, 0x0001, 0xFFFE)
        rgba = art.decode_texture(raw, "RGBA16", 5, 1)
        self.assertEqual(rgba, bytes([255, 0, 0, 255, 0, 255, 0, 255,
                                      0, 0, 255, 255, 0, 0, 0, 255, 255, 255, 255, 0]))
        native, stride = art.rgb1555(rgba, 5, 1)
        self.assertEqual(stride, 8)
        self.assertEqual(struct.unpack(">8H", native), (0x801F, 0x83E0, 0xFC00, 0x8000, 0, 0, 0, 0))
        # Swapping the source word order must not accidentally yield the same art.
        swapped = b"".join(raw[i:i + 2][::-1] for i in range(0, len(raw), 2))
        self.assertNotEqual(art.decode_texture(swapped, "RGBA16", 5, 1), rgba)

    def test_intensity_nibble_order_and_alpha(self):
        self.assertEqual(art.decode_texture(b"\x1f", "I4", 2, 1), bytes([17] * 4 + [255] * 4))
        self.assertEqual(art.decode_texture(b"\xe3", "IA4", 2, 1), bytes([252, 252, 252, 0, 36, 36, 36, 255]))
        with self.assertRaises(art.ArtError):
            art.rgb1555(art.decode_texture(b"\x1f", "I4", 2, 1), 2, 1)
        with self.assertRaises(art.ArtError):
            art.decode_texture(b"\0", "RGBA16", 1, 1)
        with self.assertRaises(art.ArtError):
            art.decode_texture(bytes(4), "CI4", 2, 2)

    def test_output_guard(self):
        with self.assertRaises(art.ArtError):
            art.output_location(ROOT / "src/retail-art-must-not-land")
        art.output_location(ROOT / "generated/source_art")
        with tempfile.TemporaryDirectory() as folder:
            with mock.patch.dict(os.environ, {"FZX_EVIDENCE": folder}):
                art.output_location(Path(folder) / "rom/art")
                with self.assertRaises(art.ArtError):
                    art.output_location(Path(folder) / "../escape")


@unittest.skipUnless((art.DECOMP / art.DRAW).exists(), "Pinned source submodule absent")
class RecipeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # The sizes/positions are synthetic and have no relation to retail art.
        rom = bytearray(0x239A80)
        struct.pack_into(">hhhHhhbbbB", rom, 0x17B1E0 + 0x40F8,
                         -123, 456, -789, 0, -64, 1024, -127, 0, 126, 255)
        cls.source = art.SourceArt(bytes(rom))

    def test_exhaustive_color_expansion_matches_pinned_oracle(self):
        path = art.DECOMP.parent / "tools/n64_texels.py"
        spec = importlib.util.spec_from_file_location("source_texels", path)
        oracle = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(oracle)
        payload = b"".join(struct.pack(">H", v) for v in range(65536))
        self.assertEqual(art.decode_texture(payload, "RGBA16", 256, 256), oracle.decode("RGBA16", payload, 256, 256))
        for fmt in ("I4", "IA4"):
            payload = bytes(range(256))
            self.assertEqual(art.decode_texture(payload, fmt, 32, 16), oracle.decode(fmt, payload, 32, 16))
            # Odd-width rows have their own unused low nibble, not a shared texel.
            self.assertEqual(art.decode_texture(payload[:6], fmt, 3, 3), oracle.decode(fmt, payload[:6], 3, 3))

    def test_authored_lods_and_materials(self):
        counts = []
        for lod in range(1, 6):
            model = self.source.model(lod)
            counts.append(len(model["triangles"]))
            self.assertEqual(model["materials"][0]["environment"], "racer.bodyRGB")
            for tri in model["triangles"]:
                self.assertTrue(all(0 <= i < len(model["vertices"]) for i in tri["vertices"]))
        self.assertEqual(counts, [132, 96, 64, 24, 11])
        v = self.source.model(1)["vertices"][0]
        self.assertEqual(v["position"], [-123, 456, -789])
        self.assertEqual(v["uv"], [-64, 1024])
        self.assertEqual(v["normal"], [-127, 0, 126])
        self.assertEqual(v["alpha"], 255)
        self.assertEqual(v["symbol"], "D_30040F8")

    def test_bad_source_commands_and_indices_rejected(self):
        original = self.source.draw
        try:
            for before, after in [(b"gSP2Triangles(gfx++, 0, 1, 2, 0", b"gSP2Triangles(gfx++, 99, 1, 2, 0"),
                                  (b"gSPVertex(gfx++, D_30040F8, 20, 0)", b"gSPVertex(gfx++, D_30040F8, 200, 0)"),
                                  (b"gSPVertex(gfx++, D_30040F8", b"gSPUnknown(gfx++, D_30040F8")]:
                self.source.draw = original.replace(before, after)
                with self.subTest(after=after), self.assertRaises(art.ArtError):
                    self.source.model(1)
        finally:
            self.source.draw = original

    def test_texture_binding_and_hud_sources(self):
        bindings = self.source.texture_bindings()
        self.assertEqual([b["tile"] for b in bindings], list(range(8)))
        self.assertIsNone(bindings[0]["symbol"])
        for b in bindings[1:]:
            self.assertGreater(len(self.source.texture(b["symbol"])["rgba"]), 0)
        for name in art.HUD:
            t = self.source.texture(name)
            self.assertEqual(t["format"], "RGBA16")

    def test_compressed_venue_offsets_use_decoded_segment(self):
        rom = bytearray(0x239A80)
        payload = bytearray(0x7000)
        payload[0x6000:0x6002] = b"\xf8\x01"
        # Synthetic compressed segment must fit the actual ROM span. All-zero
        # output compressed with repeated distance-one runs, including one red word.
        flags, pairs, literals = [], bytearray(), bytearray()
        cursor = 0
        while cursor < len(payload):
            n = 0
            if cursor and payload[cursor] == payload[cursor - 1]:
                while n < 18 and cursor + n < len(payload) and payload[cursor + n] == payload[cursor - 1]:
                    n += 1
            if n >= 3:
                flags.append(0)
                pairs.extend(struct.pack(">H", (n - 3) << 12))
                cursor += n
            else:
                flags.append(1)
                literals.append(payload[cursor])
                cursor += 1
        masks = bytearray((len(flags) + 7) // 8)
        for i, bit in enumerate(flags):
            masks[i // 8] |= bit << (7 - i % 8)
        packed = b"MIO0" + struct.pack(">III", len(payload), 16 + len(masks), 16 + len(masks) + len(pairs)) + masks + pairs + literals
        self.assertLess(len(packed), 0x4950)
        rom[0x235130:0x235130 + len(packed)] = packed
        source = art.SourceArt(bytes(rom))
        self.assertEqual(source.texture("D_A006000_235130")["rgba"][:4], bytes([255, 0, 0, 255]))
        # Mutated compressed header must fail, not quietly read ROM-relative data.
        rom[0x235134:0x235138] = bytes(4)
        with self.assertRaises(art.ArtError):
            art.SourceArt(bytes(rom))

    def test_wrong_rom_rejected_before_writing(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "never-created"
            with mock.patch.dict(os.environ, {"FZX_EVIDENCE": folder}), self.assertRaises(ValueError):
                art.export(bytes(32), output)
            self.assertFalse(output.exists())

    def test_synthetic_export_receipt_and_mutation(self):
        rom = bytes(0x239A80)
        source = art.SourceArt(rom)
        source.segments["mute_city_textures"] = bytes(0x7000)
        with tempfile.TemporaryDirectory() as folder, \
                mock.patch.object(art, "extract_rom_course", return_value=(b"", {"normalized_rom_sha256": art.sha(rom)})), \
                mock.patch.object(art, "normalize_rom", return_value=(rom, "synthetic")), \
                mock.patch.object(art, "SourceArt", return_value=source), \
                mock.patch.object(art, "verify_source"):
            output = Path(folder)
            with mock.patch.dict(os.environ, {"FZX_EVIDENCE": folder}):
                summary = art.export(rom, output)
            self.assertEqual(summary["textures"], 21)
            self.assertEqual(summary["lod_triangles"], [132, 96, 64, 24, 11])
            manifest = json.loads((output / "source_art.json").read_text())
            self.assertEqual(manifest["uv_fraction_bits"], 5)
            self.assertNotIn(b"\r", (output / "source_art.receipt").read_bytes())
            for line in (output / "source_art.receipt").read_text().splitlines():
                if not line or line.startswith("#") or line.startswith("input "):
                    continue
                kind, digest, name = line.split()
                path = output / name if kind == "output" else ROOT / "tools" / name
                self.assertEqual(art.sha(path.read_bytes()), digest)
            original = (output / "aKmhTex.rgb1555").read_bytes()
            if shutil.which("sh") and shutil.which("sha256sum"):
                command = ["sh", str(ROOT / "tools/check_receipt.sh"), str(output / "source_art.receipt")]
                self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
                (output / "aKmhTex.rgb1555").write_bytes(bytes([original[0] ^ 1]) + original[1:])
                self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)

    @unittest.skipUnless(os.environ.get("FZX_TEST_ROM"), "Local retail ROM not configured")
    def test_retail_export_and_receipt(self):
        rom = Path(os.environ["FZX_TEST_ROM"]).read_bytes()
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            with mock.patch.dict(os.environ, {"FZX_EVIDENCE": folder}):
                summary = art.export(rom, output)
            self.assertEqual(summary["lod_triangles"], [132, 96, 64, 24, 11])
            self.assertEqual(summary["textures"], 21)
            for line in (output / "source_art.receipt").read_text().splitlines():
                if line.startswith("output "):
                    _, digest, name = line.split()
                    self.assertEqual(hashlib.sha256((output / name).read_bytes()).hexdigest(), digest)
            manifest = json.loads((output / "source_art.json").read_text())
            self.assertEqual(len(manifest["models"]), 5)


if __name__ == "__main__":
    unittest.main()
