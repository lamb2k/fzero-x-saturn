"""Bake Jack Cup loading nameplates from the original course-menu font atlas."""
import argparse
import json
from pathlib import Path


COURSES = ("MUTE CITY", "SILENCE", "SAND OCEAN", "DEVIL'S FOREST", "BIG BLUE", "PORT TOWN")


def bake(source, dest):
    atlas = (source / "COURSEUI.PAK").read_bytes()
    manifest = json.loads((source / "course_menu.json").read_text())
    table = manifest["fonts"]["3"]
    patterns = bytearray()
    layouts = []
    palette = [0]
    for name in COURSES:
        # Course-menu Font 3 is a 16-line proportional font. Draw each source
        # glyph into the line, then store the line as Saturn 8x8 4bpp cells.
        pixels = [bytearray(320) for _ in range(16)]
        x = extent = 0
        for ch in name:
            if ch == " ":
                x += table["space"]
                continue
            idx = table["symbols"].index("aFont3PrimeTex") if ch == "'" else ord(ch) - ord("A")
            glyph = manifest["images"][table["images"][idx]]
            w, h = glyph["width"], glyph["height"]
            if not glyph["clut"] or h != 16 or x+w > 320:
                raise ValueError("Course glyph layout changed")
            for row in range(h):
                for col in range(w):
                    at = row*w + col
                    b = atlas[glyph["offset"] + at//2]
                    color = glyph["palette"][(b & 15) if at%2 else b >> 4]
                    if color:
                        if color not in palette:
                            palette.append(color)
                        pixels[row][x+col] = palette.index(color)
            extent = max(extent, x+w)
            x += table["advance"][idx]
        cols = (extent+7)//8
        layouts.append((len(patterns)//2, cols))
        for tile_y in range(2):
            for tile_x in range(cols):
                for row in pixels[8*tile_y:8*tile_y+8]:
                    for col in range(tile_x*8, tile_x*8+8, 2):
                        patterns.append(row[col]*16 + row[col+1])
    if len(palette) > 16 or max(c for _,c in layouts) > 40:
        raise ValueError("Course label exceeds its NBG3 lease")
    dest.parent.mkdir(parents=True, exist_ok=True)
    words = [int.from_bytes(patterns[i:i+2],"big") for i in range(0,len(patterns),2)]
    with dest.open("w", newline="\n") as out:
        out.write("/* Generated from the original course menu atlas. */\n")
        out.write("static const uint16_t card_colors[16] = {"+",".join(map(str,palette))+"};\n")
        out.write("static const struct { uint16_t at, cols; } cards[] = {")
        out.write(",".join("{%d,%d}" % layout for layout in layouts)+"};\n")
        out.write("static const uint16_t card_cells[] = {\n")
        for i in range(0,len(words),16):
            out.write(",".join(map(str,words[i:i+16]))+",\n")
        out.write("};\n")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("source", type=Path)
    p.add_argument("header", type=Path)
    args = p.parse_args()
    bake(args.source,args.header)
