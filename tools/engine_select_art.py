"""Pack one original three-quarter view per stock machine for the SELECT scene.

Inputs are checked player-profile outputs of racer_sprites.py. Keep generated
pixels, palettes, headers and receipts local, alongside the other race packs.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def sha(data):
    return hashlib.sha256(data).hexdigest()


def bake(source, dest, guest_ui=None):
    atlas = bytearray()
    rows = []
    receipts = []
    for family in range(30):
        folder = source / f"c{family:02}s1"
        pack = (folder / "RSPRITE.PAK").read_bytes()
        raw = (folder / "racer_sprites.json").read_bytes()
        info = json.loads(raw)
        identities = {name: digest for kind, digest, name in
                      (line.split(maxsplit=2) for line in (folder / "racer_sprites.receipt").read_text().splitlines()
                       if line.startswith("output ")) if kind == "output"}
        if (identities.get("RSPRITE.PAK") != sha(pack) or identities.get("racer_sprites.json") != sha(raw)
                or info["pack_sha256"] != sha(pack) or info["family"] != family or info["skin"] != 1):
            raise ValueError(f"Player sprite identity mismatch for machine {family}")
        cell = next(c for c in info["cells"] if c["pitch"] == 32 and c["yaw"] == -40)
        width, height, _, _, at, group, _ = struct.unpack_from(">HHhhIHH", pack, info["header_bytes"] + 16 * cell["index"])
        colors = struct.unpack_from(">I", pack, 16)[0] + group * 32
        size = width * height // 2
        if (width != cell["width"] or height != cell["height"] or width % 8 or at + size > len(pack)
                or colors + 32 > len(pack)):
            raise ValueError(f"Invalid player sprite cell for machine {family}")
        rows.append((len(atlas), width, height))
        atlas.extend(pack[colors:colors+32])
        atlas.extend(pack[at:at+size])
        atlas.extend(bytes(-len(atlas) % 32))
        receipts.append(f"input {sha(pack)} c{family:02}s1/RSPRITE.PAK")
    rows.extend([(0, 0, 0)] * 5)
    guest_mask = 0
    if guest_ui:
        for family, folder, key in ((33, "robotnik", "grobotniks0"), (34, "metal_sonic", "gmetal-sonics0")):
            manifest = json.loads((guest_ui / folder / "manifest.json").read_bytes())
            slot = next(slot for slot in manifest["slots"] if slot["image"] == "select_small")
            data = (guest_ui / folder / "select_small.bin").read_bytes()
            if (manifest["guest_key"] != key or slot["width"] != 40 or slot["height"] != 40
                    or slot["stride"] != 40 or slot["format"] != "RGB1555"
                    or len(data) != 3200 or sha(data) != slot["pixel_sha256"]):
                raise ValueError(f"Invalid guest portrait for {folder}")
            rows[family] = (len(atlas), 40, 40)
            atlas.extend(data)
            guest_mask |= 1 << family
            receipts.append(f"input {sha(data)} {folder}/select_small.bin")
    fnv = 2166136261
    for b in atlas:
        fnv = ((fnv ^ b) * 16777619) & 0xffffffff
    dest.mkdir(parents=True, exist_ok=True)
    header = (f"#define SELECT_ART_BYTES {len(atlas)}u\n#define SELECT_ART_HASH 0x{fnv:08X}u\n"
              f"#define SELECT_GUEST_MASK {guest_mask}ull\n"
              "static const struct { unsigned offset, width, height; } select_art[35] = {\n"
              + "".join(f"    {{ {a}, {w}, {h} }},\n" for a,w,h in rows) + "};\n").encode()
    (dest/"select_art.h").write_bytes(header)
    (dest/"SELECT.PAK").write_bytes(atlas)
    receipts.extend((f"output {sha(atlas)} SELECT.PAK", f"output {sha(header)} select_art.h"))
    (dest/"select_art.receipt").write_text("\n".join(["# fzx generation receipt v1; regenerate, never edit",
        f"generator {sha(Path(__file__).read_bytes())} engine_select_art.py", *receipts])+"\n", newline="\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("player_sets", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--guest-ui", type=Path)
    args = parser.parse_args()
    bake(args.player_sets, args.output, args.guest_ui)
