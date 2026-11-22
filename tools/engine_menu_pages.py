"""Bake stock GP ending pages, mode signs and options sprites for Saturn."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import struct

from import_course import extract_rom_course, normalize_rom
from import_source_art import YAML, bounded, metadata, mio0, output_location, verify_source
from menu_assets import BASE, rgb_words, sign_variant
from road_pack import fnv


GUEST_WINNERS = (
    ("robotnik", "Nobody beats the Eggman!"),
    ("metal_sonic", "Target eliminated."),
)


# (stock ending row, top scanline, first column, exclusive last column).
# All samples share a nine-pixel cap height and three descender scanlines.
CAPTION_GLYPHS = {
    "N": (24,10,8,15), "o": (14,4,101,107), "b": (3,4,48,54),
    "d": (14,4,19,25), "y": (1,4,16,22), "e": (3,4,55,61),
    "a": (27,4,32,38), "t": (3,4,69,73), "s": (3,4,62,68),
    "h": (10,9,14,20), "E": (20,10,14,21), "g": (1,4,59,65),
    "m": (3,4,12,22), "n": (3,4,79,85), "!": (20,10,101,103),
    "H": (20,10,33,41), "T": (10,9,6,13), "l": (1,4,86,88),
    "w": (20,10,76,85), ",": (14,4,85,87), "I": (20,10,70,72),
    "r": (1,4,34,38), "S": (28,10,56,63), "i": (10,9,21,23),
    "c": (17,10,43,50), ".": (27,4,107,109),
}


def guest_caption(text, stock):
    box = bytearray(stock[20])
    for y in range(2, 30):
        start = y * 400
        box[start+4:start+388] = box[start+380:start+382] * 192
    pen = 6
    for ch in text:
        if ch == " ":
            pen += 4
            continue
        page, top, x0, x1 = CAPTION_GLYPHS[ch]
        width = x1 - x0
        if pen + width > 190:
            raise ValueError("Guest caption exceeds stock line width")
        for dy in range(12):
            src = 2*((top+dy)*200+x0)
            dst = 2*((10+dy)*200+pen)
            box[dst:dst+2*width] = stock[page][src:src+2*width]
        pen += width + 1
    return box


def guest_page(master, portrait, caption):
    """Use the stock bevel, ink and caption glyphs around a crop of the guest master."""
    from PIL import Image
    source = Image.open(master).convert("RGBA")
    if source.size != (1075, 1463):
        raise ValueError("Review guest bust crop for changed master dimensions")
    bust = source.crop((0, 0, 1075, 620)).resize((164, 95), Image.Resampling.LANCZOS)
    ground = Image.new("RGBA", bust.size, (24, 24, 24, 255))
    pixels = Image.alpha_composite(ground, bust).convert("RGB")
    page = bytearray(portrait)
    for y in range(95):
        for x in range(164):
            r, g, b = pixels.getpixel((x, y))
            struct.pack_into(">H", page, ((y+2)*168+x+2)*2,
                             0x8000 | (r>>3) | ((g>>3)<<5) | ((b>>3)<<10))
    return page + caption


def bake(rom, decomp, dest, guest_ui=None):
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
    stock_captions = []
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
        stock_captions.append(page[caption_offset:] if caption_offset else None)
        header.append("    {%du,%du,%du,%du},"%(len(pages),len(page),fnv(page),caption_offset))
        pages.extend(page)
        maximum = max(maximum, len(page))
    guest_inputs = []
    if guest_ui:
        for name, line in GUEST_WINNERS:
            master = guest_ui / "masters" / (name + ".png")
            guest_inputs.append(master)
            page = guest_page(master, sprite(pairs[0][0])[2], guest_caption(line, stock_captions))
            header.append("    {%du,%du,%du,33264u}," % (len(pages), len(page), fnv(page)))
            pages.extend(page)
    header.append("};")
    header.append(f"#define ENDING_PAGE_COUNT {len(pairs)+len(guest_inputs)}u")
    header.extend([f"#define ENDINGS_BYTES {len(pages)}u",f"#define ENDING_MAX_BYTES {maximum}u"])
    signs = ["aMenuSign"+name+"Tex" for name in ("GpRace","TimeAttack","Practice","DeathRace","Options")]
    ui = signs + ["aCongratulationsTex","aOptionsTex","aOptionsFalconHelmetTex"]
    ui += list(dict.fromkeys(re.findall(r"\b(a(?:Options|Menu)\w+Tex)\b", options.read_text())))
    bank = bytearray()
    header.append("static const struct { unsigned at; unsigned short w, h; } menu_sprites[] = {")
    for i,symbol in enumerate(dict.fromkeys(ui)):
        w,h,pixels = sprite(symbol)
        header.extend([f"#define UI_{symbol} {i}u", f"    {{{len(bank)}u,{w}u,{h}u}},"])
        bank.extend(pixels)
    # Use the cartridge grid, center-cropped by eight scanlines for Saturn.
    # The stable wallpaper, heading and dim signs occupy one sprite.
    width, height = 320, 224
    backing = bytearray(b"\x80\x00" * (width*height))
    def overlay(pixels, w, h, x, y):
        for sy in range(max(0, -y), min(h, height-y)):
            for sx in range(w):
                src = 2*(sy*w+sx)
                if pixels[src] or pixels[src+1]:
                    dst = 2*((y+sy)*width+x+sx)
                    backing[dst:dst+2] = pixels[src:src+2]
    wallpaper = rgb_words(raw("aTitleBackgroundMainTex", 304*240*2), "RGBA16")
    overlay(sign_variant(wallpaper,304,75,16),304,240,8,-8)
    heading = rgb_words(raw("aSelectModeTex",128*24//2),"I4")
    overlay(b"".join(struct.pack(">H", (v & 0x83ff) if v else 0) for v in heading),128,24,100,5)
    vs = "aMenuSignVsBattleTex"
    w,h,pixels = sprite(vs)
    header.extend([f"#define UI_{vs} {len(dict.fromkeys(ui))}u", f"    {{{len(bank)}u,{w}u,{h}u}},"])
    bank.extend(pixels)
    for mode, suffix in ((0,"GpRace"),(1,"TimeAttack"),(2,"DeathRace"),(3,"VsBattle"),(5,"Practice"),(6,"Options")):
        w,h,pixels = sprite("aMenuSign"+suffix+"Tex")
        dim = sign_variant([v for v, in struct.iter_unpack(">H", pixels)], w, 128, 11)
        overlay(dim,w,h,32+(mode%4)*64,30+(mode//4)*91)
    header.extend([f"#define UI_MODE_BOARD {len(dict.fromkeys(ui))+1}u", f"    {{{len(bank)}u,320u,224u}},"])
    bank.extend(backing)
    header.extend(["};",f"#define PAGES_UI_BYTES {len(bank)}u",f"#define PAGES_UI_HASH {fnv(bank)}u"])
    dest.mkdir(parents=True,exist_ok=True)
    receipt = ["# fzx generation receipt v1; regenerate, never edit"]
    for module in ("engine_menu_pages.py","menu_assets.py","import_source_art.py","import_course.py","road_pack.py"):
        digest = hashlib.sha256(Path(__file__).with_name(module).read_bytes()).hexdigest()
        receipt.append(f"generator {digest} {module}")
    receipt.append("input "+provenance["normalized_rom_sha256"]+" owner-rom")
    for path in (yaml,ending,options,*guest_inputs):
        receipt.append(f"input {hashlib.sha256(path.read_bytes()).hexdigest()} {path.name}")
    for name,blob in (("ENDINGS.PAK",pages),("PAGESUI.PAK",bank),("menu_pages.h",("\n".join(header)+"\n").encode())):
        (dest/name).write_bytes(blob)
        receipt.append(f"output {hashlib.sha256(blob).hexdigest()} {name}")
    (dest/"menu_pages.receipt").write_text("\n".join(receipt)+"\n",newline="\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("decomp",type=Path)
    parser.add_argument("dest",type=Path)
    parser.add_argument("--guest-ui", type=Path, help="Guest UI directory containing the two portrait masters")
    args = parser.parse_args()
    bake(Path(os.environ["FZX_TEST_ROM"]).read_bytes(), args.decomp, args.dest, args.guest_ui)
