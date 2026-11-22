"""Bake stock GP ending pages, mode signs and options sprites for Saturn."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import struct

from import_course import extract_rom_course, normalize_rom
from import_source_art import YAML, bounded, metadata, mio0, output_location, verify_source
from menu_assets import BASE, rgb_words
from road_pack import fnv


def bake(rom, decomp, dest):
    output_location(dest)
    verify_source(decomp)
    _, provenance = extract_rom_course(rom)
    data = normalize_rom(rom)[0][BASE:]
    yaml = decomp / YAML / "common_assets_compressed.yaml"
    ending = decomp / "src/overlays/ending/ending.c"
    options = decomp / "src/overlays/ovl_i6/options_menu.c"
    rows = metadata(yaml.read_text())
    offsets = sorted({r["offset"] for r in rows.values() if "offset" in r})

    def raw(symbol, size):
        row = rows[symbol]
        start = row["offset"]
        ordinal = offsets.index(start)
        end = offsets[ordinal+1] if ordinal+1 < len(offsets) else len(data)
        span = bounded(data, start, end-start)
        if row["type"] == "TEXTURE":
            return bounded(span, 0, size)
        if row["type"] != "COMPRESSED_TEXTURE":
            raise ValueError(symbol)
        image = mio0(span, size)
        if len(image) != size:
            raise ValueError(symbol)
        return image

    palette = rgb_words(raw("aMenuTextTLUT", 64), "RGBA16")

    def sprite(symbol):
        row = rows[symbol]
        w, h = row["width"], row["height"]
        if row["format"] == "RGBA16":
            pixels = rgb_words(raw(symbol, w*h*2), "RGBA16")
        elif row["format"] == "CI8":
            pixels = [palette[index] for index in raw(symbol, w*h)]
        else:
            raise ValueError(symbol)
        stride = (w+7) & ~7
        padded = bytearray()
        for y in range(h):
            padded.extend(struct.pack(f">{stride}H", *(pixels[y*w:(y+1)*w]+[0]*(stride-w))))
        return stride, h, padded

    table = ending.read_text().split("sEndingTextures[][2] = {",1)[1].split("};",1)[0]
    pairs = re.findall(r"\{\s*(\w+),\s*(\w+)\s*\}", table)
    if len(pairs) != 34:
        raise ValueError("Ending page count changed")
    pages = bytearray()
    maximum = 0
    header = ["/* Generated from stock art. */",
              "static const struct { unsigned at, size, hash, caption; } ending_pages[] = {"]
    for portrait, caption in pairs:
        w,h,page = sprite(portrait)
        if (w,h) != (168,99):
            raise ValueError(portrait)
        caption_offset = 0
        if caption != "NULL":
            cw,ch,pixels = sprite(caption)
            if (cw,ch) != (200,32):
                raise ValueError(caption)
            caption_offset = len(page)
            page.extend(pixels)
        header.append("    {%du,%du,%du,%du},"%(len(pages),len(page),fnv(page),caption_offset))
        pages.extend(page)
        maximum = max(maximum, len(page))
    header.extend(["};",f"#define ENDINGS_BYTES {len(pages)}u",f"#define ENDING_MAX_BYTES {maximum}u"])
    signs = ["aMenuSign"+name+"Tex" for name in ("GpRace","TimeAttack","Practice","DeathRace","Options")]
    ui = signs + ["aCongratulationsTex","aOptionsTex","aOptionsFalconHelmetTex"]
    ui += list(dict.fromkeys(re.findall(r"\b(a(?:Options|Menu)\w+Tex)\b", options.read_text())))
    bank = bytearray()
    header.append("static const struct { unsigned at; unsigned short w, h; } menu_sprites[] = {")
    for i,symbol in enumerate(dict.fromkeys(ui)):
        w,h,pixels = sprite(symbol)
        header.extend([f"#define UI_{symbol} {i}u", f"    {{{len(bank)}u,{w}u,{h}u}},"])
        bank.extend(pixels)
    header.extend(["};",f"#define PAGES_UI_BYTES {len(bank)}u",f"#define PAGES_UI_HASH {fnv(bank)}u"])
    dest.mkdir(parents=True,exist_ok=True)
    receipt = ["# fzx generation receipt v1; regenerate, never edit"]
    for module in ("engine_menu_pages.py","menu_assets.py","import_source_art.py","import_course.py","road_pack.py"):
        digest = hashlib.sha256(Path(__file__).with_name(module).read_bytes()).hexdigest()
        receipt.append(f"generator {digest} {module}")
    receipt.append("input "+provenance["normalized_rom_sha256"]+" owner-rom")
    for path in (yaml,ending,options):
        receipt.append(f"input {hashlib.sha256(path.read_bytes()).hexdigest()} {path.name}")
    for name,blob in (("ENDINGS.PAK",pages),("PAGESUI.PAK",bank),("menu_pages.h",("\n".join(header)+"\n").encode())):
        (dest/name).write_bytes(blob)
        receipt.append(f"output {hashlib.sha256(blob).hexdigest()} {name}")
    (dest/"menu_pages.receipt").write_text("\n".join(receipt)+"\n",newline="\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("decomp",type=Path)
    parser.add_argument("dest",type=Path)
    args = parser.parse_args()
    bake(Path(os.environ["FZX_TEST_ROM"]).read_bytes(), args.decomp, args.dest)
