"""Repack the C8 four-cup artwork for the scenes selector's active VDP1 bank."""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def bake(source, destination):
    receipt = (source / "course_menu.receipt").read_text().splitlines()
    inputs = {}
    for name in ("COURSEUI.PAK", "course_menu.json"):
        data = (source / name).read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if f"output {digest} {name}" not in receipt:
            raise ValueError(f"Course menu receipt mismatch for {name}")
        inputs[name] = data
    atlas = inputs["COURSEUI.PAK"]
    images = {r["symbol"]: r for r in json.loads(inputs["course_menu.json"])["images"]}

    def pixels(symbol, size, variant="offset"):
        row = images[symbol]
        w, h = size
        if (row["width"], row["height"]) != size:
            raise ValueError(f"Unexpected dimensions for {symbol}")
        at = row[variant]
        count = w * h
        n = count // 2 if row["clut"] else count * 2
        if at < 0 or n > len(atlas) - at:
            raise ValueError(f"Truncated {symbol}")
        data = atlas[at:at+n]
        if row["clut"]:
            palette = row["palette"]
            return b"".join(struct.pack(">HH", palette[v >> 4], palette[v & 15]) for v in data)
        return data

    # Source steady-state coordinates on the 320 x 240 screen, cropped eight
    # lines at each end. Keep the background independent so the cups can move.
    bank = bytearray(b"\x80\x00" * (320 * 224))
    wall = pixels("aTitleBackgroundMainTex", (304, 240))
    for y in range(224):
        start = ((y + 8) * 304) * 2
        left = (y * 320 + 8) * 2
        bank[left:left + 608] = wall[start:start + 608]
    header = pixels("aSelectCourseTex", (160, 24))
    for i, (color,) in enumerate(struct.iter_unpack(">H", header)):
        if color:
            intensity = (color & 31) * 250 // 255
            yellow = 0x8000 | intensity | (intensity << 5)
            struct.pack_into(">H", bank, ((9 + i // 160) * 320 + 80 + i % 160) * 2, yellow)
    for name in ("Jack", "Queen", "King", "Joker"):
        for state in ("dim_offset", "offset"):
            bank.extend(pixels(f"aCupSelect{name}Tex", (64, 96), state))
    destination.mkdir(parents=True, exist_ok=True)
    (destination / "CUPUI.PAK").write_bytes(bank)
    lines = ["# fzx generation receipt v1",
             f"generator {hashlib.sha256(Path(__file__).read_bytes()).hexdigest()} engine_cup_art.py"]
    lines.extend(f"input {hashlib.sha256(data).hexdigest()} {name}" for name, data in inputs.items())
    lines.append(f"output {hashlib.sha256(bank).hexdigest()} CUPUI.PAK")
    (destination / "cup_ui.receipt").write_bytes(("\n".join(lines) + "\n").encode())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("course_menu", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    bake(args.course_menu, args.destination)

