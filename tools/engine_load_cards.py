"""Bake the Jack and Queen Cup nameplates and track silhouettes from the original course menu."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct


COURSES = ("MUTE CITY", "SILENCE", "SAND OCEAN", "DEVIL'S FOREST", "BIG BLUE", "PORT TOWN",
           "SECTOR ALPHA", "RED CANYON", "DEVIL'S FOREST", "MUTE CITY", "BIG BLUE", "WHITE LAND",
           "FIRE FIELD", "SILENCE", "SECTOR", "RED CANYON", "WHITE LAND", "MUTE CITY",
           "RAINBOW ROAD", "DEVIL'S FOREST", "SPACE PLANT", "SAND OCEAN", "PORT TOWN", "BIG HAND")

def preview_masks(folder):
    pack = (folder / "CPREVIEW.PAK").read_bytes()
    receipt = (folder / "course_preview.receipt").read_text().splitlines()
    if "output " + hashlib.sha256(pack).hexdigest() + " CPREVIEW.PAK" not in receipt:
        raise ValueError("Course preview pack and receipt disagree")
    magic, version, size, count, _, _, poses, desc, xyz, faces, cameras, *_ = struct.unpack_from(">8s14I", pack)
    if (magic, version, size, count, poses) != (b"FZXCPV01", 1, len(pack), 24, 360):
        raise ValueError("Unexpected course preview pack")
    # One still of the source's pitch-20 orbit. Fit each projected course into
    # a 128x64 panel, preserving aspect and the inclines in the original mesh.
    view = [v / 65536 for v in struct.unpack_from(">18i", pack, cameras + 35 * 72)]
    masks = []
    for course in range(len(COURSES)):
        first, count, face, num_faces = struct.unpack_from(">4I", pack, desc + course * 16)
        points = []
        for vertex in range(first, first + count):
            pos = struct.unpack_from(">3h", pack, xyz + 6 * vertex)
            p = [view[12+k] + sum(pos[j] * .25 * view[4*j+k] for j in range(3)) for k in range(3)]
            points.append((p[0]*view[16]*160/-p[2], -p[1]*view[17]*120/-p[2]))
        left, top = map(min, zip(*points))
        right, bottom = map(max, zip(*points))
        scale = min(122 / (right-left), 58 / (bottom-top))
        points = [(64 + (x-(left+right)*.5)*scale, 32 + (y-(top+bottom)*.5)*scale) for x,y in points]
        pixels = [bytearray(128) for _ in range(64)]
        for index in range(face, face + num_faces):
            ids = struct.unpack_from(">4H", pack, faces + 8 * index)
            for tri in ((0,1,2),(0,2,3)):
                corners = [points[ids[i]] for i in tri]
                for y in range(math.ceil(min(p[1] for p in corners)-.5), math.ceil(max(p[1] for p in corners)-.5)):
                    crossings = []
                    for a,b in zip(corners, corners[1:]+corners[:1]):
                        if min(a[1],b[1]) <= y+.5 < max(a[1],b[1]):
                            crossings.append(a[0]+(y+.5-a[1])*(b[0]-a[0])/(b[1]-a[1]))
                    if len(crossings) == 2:
                        lo,hi = sorted(math.ceil(x-.5) for x in crossings)
                        pixels[y][lo:hi] = b"\1" * (hi-lo)
        mask = bytearray()
        for ty in range(0,64,8):
            for tx in range(0,128,8):
                for row in pixels[ty:ty+8]:
                    mask.append(sum(row[tx+b] << (7-b) for b in range(8)))
        masks.append(mask)
    return masks


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
    masks = preview_masks(source.parent / "course_menu_preview")
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
        out.write("static const uint8_t card_tracks[%d][1024] = {\n" % len(COURSES))
        for mask in masks:
            out.write("{\n")
            for i in range(0,len(mask),32):
                out.write(",".join(map(str,mask[i:i+32]))+",\n")
            out.write("},\n")
        out.write("};\n")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("source", type=Path)
    p.add_argument("header", type=Path)
    args = p.parse_args()
    bake(args.source,args.header)
