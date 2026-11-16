"""Synthetic source road decoding gates; never contains retail geometry/art."""
from pathlib import Path
import copy
import struct
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from road_assets import capture, resolve, template_words, texture_payload, interior_identity, ArtError


class RoadAssets(unittest.TestCase):
    def fixture(self):
        vertices = [[-70, 20, 300, -17, 33, 1, 2, 3, 255],
                    [80, -30, 500, 129, -65, 4, 5, 6, 255],
                    [10, 90, 200, 255, 129, 7, 8, 9, 255]]
        commands = [(0xD7000502, 0x8000FFFF, "-"), (0xFD10003F, 0, "fixture"),
                    (0xF3000000, 0x047FF100, "-"), (0xF5102000, 0x05014060, "-"),
                    (0xF2000000, 0x050FC07C, "-"), (0xFC147E28, 0x44FE793C, "-"),
                    (0x01003006, 0, "vertices"), (0x05000204, 0, "-"),
                    (0x02140000, 0xFFE00040, "-"), (0x05000204, 0, "-")]
        return vertices, commands

    def test_scaled_and_modified_uv_have_distinct_contracts(self):
        vertices, commands = self.fixture()
        result = resolve(vertices, commands, {})
        a, b = result["triangles"]
        self.assertEqual([result["vertices"][i][3:5] for i in a[:3]], [[-9, 32], [64, -65], [127, 128]])
        self.assertEqual(result["vertices"][b[0]][3:5], [-32, 64])
        self.assertEqual(result["vertices"][a[0]][:3], vertices[0][:3])
        self.assertEqual(result["materials"][0]["texture"]["name"], "fixture")
        self.assertEqual(result["accounting"]["modifies"], 1)
        # Wrong UV scaling and swapped material are observable, not tolerated.
        wrong = copy.deepcopy(commands); wrong[0] = (wrong[0][0], 0xFFFF8000, "-")
        self.assertNotEqual(resolve(vertices, wrong, {})["vertices"], result["vertices"])
        wrong = copy.deepcopy(commands); wrong[1] = (wrong[1][0], 0, "wrong")
        self.assertNotEqual(resolve(vertices, wrong, {})["materials"], result["materials"])

    def test_missing_indices_templates_and_unknown_operations_rejected(self):
        vertices, commands = self.fixture()
        bad = [commands[:6] + commands[7:], commands + [(0xDE000000, 0, "missing_join")],
               commands + [(0x03000000, 0, "-")], commands[:6] + [(0x01020040, 0, "vertices")],
               commands + [(0x02140040, 0, "-")]]
        for rows in bad:
            with self.assertRaises(ArtError): resolve(vertices, rows, {})

    def test_template_bounds_and_triangle_order(self):
        raw = struct.pack(">IIII", 0x06000204, 0x00040200, 0xDF000000, 0)
        rows = {"D_80140F0": dict(type="GFX", offset=0), "next": dict(offset=len(raw))}
        words = template_words(raw, rows, "D_80140F0")
        vertices, commands = self.fixture()
        result = resolve(vertices, commands[:7] + [(0xDE000000, 0, "D_80140F0")], {"D_80140F0": words})
        self.assertEqual([t[:3] for t in result["triangles"]], [[0, 1, 2], [2, 1, 0]])
        for data in (raw[:-1], raw[:-8] + struct.pack(">II", 0xDE000000, 0)):
            with self.assertRaises(ArtError): template_words(data, rows, "D_80140F0")

    def test_texture_load_preserves_both_authored_halves(self):
        rows = {"top": dict(type="TEXTURE", format="RGBA16", offset=0, width=8, height=2),
                "bottom": dict(type="TEXTURE", format="RGBA16", offset=32, width=8, height=2)}
        data = struct.pack(">32H", *([0xF801] * 16 + [0x003F] * 16))
        result = texture_payload(data, rows, "top", 32)
        self.assertEqual(result["source_symbols"], ["top", "bottom"])
        self.assertEqual((result["width"], result["height"]), (8, 4))
        self.assertEqual(bytes.fromhex(result["rgba_hex"])[-4:], bytes([0, 0, 255, 255]))
        for bad in ({"top": rows["top"]}, {**rows, "bottom": {**rows["bottom"], "format": "I4"}}):
            with self.assertRaises(ArtError): texture_payload(data, bad, "top", 32)
        with self.assertRaises(ArtError): texture_payload(data[:-1], rows, "top", 32)

    def test_capture_counts_and_closure(self):
        chunks = ["C %d 00000001 0 %s" % (i, " ".join(["0"] * 24)) for i in range(4)]
        spans = ["R %d %d 0 0 0 0" % (i, i) for i in range(3)]
        text = "\n".join(["FZXROAD2 3 1 1 3", *chunks, *spans, "V 0 1 2 3 4 5 0 0 0 255", "G de000000 00000000 D_8014040"])
        self.assertEqual(len(capture(text)[0]), 4)
        for bad in (text.replace("3 1 1", "3 2 1"), "\n".join(text.splitlines()[:-1]), text.replace("C 3 00000001 0 0", "C 3 00000001 0 1")):
            with self.assertRaises(ArtError): capture(bad)

    def test_source_chunk_ownership_inherited_filter_and_interior_control(self):
        vertices, commands = self.fixture()
        spans = [dict(chunk=7, first=6, end=len(commands))]
        result = resolve(vertices, commands, {}, [(0xE3001201, 0x2000)], spans)
        self.assertEqual([o["chunk"] for o in result["origins"]], [7, 7])
        self.assertEqual(result["materials"][0]["other_h"], 0x2000)
        with self.assertRaises(ArtError): resolve(vertices, commands, {}, spans=[])
        reordered = copy.deepcopy(result)
        reordered["triangles"].reverse()
        with self.assertRaises(ArtError): interior_identity(result, reordered)
        result["origins"][1]["conditional_outer"] = True
        inside = copy.deepcopy(result)
        inside["triangles"] = inside["triangles"][:1]
        inside["origins"] = inside["origins"][:1]
        interior_identity(result, inside)
        inside["vertices"][0][0] += 1
        with self.assertRaises(ArtError): interior_identity(result, inside)


if __name__ == "__main__": unittest.main()
