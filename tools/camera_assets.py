#!/usr/bin/env python3
"""Export the thirteen source viewport records, locally; no graphics interpreter."""
from pathlib import Path
import argparse
import hashlib
import os
import re
import struct
from import_course import extract_rom_course, normalize_rom
from import_source_art import DECOMP, output_location, verify_source, mio0

NAMES = ("FullScreen", "TopHalf", "BottomHalf", "LeftHalf", "RightHalf",
         "TopLeftQuarter", "TopRightQuarter", "BottomLeftQuarter", "BottomRightQuarter",
         "TopCenterQuarter", "BottomCenterQuarter", "LeftCenterQuarter", "RightCenterQuarter")


def decode(segment, source):
    entries = re.findall(r"aVp(\w+):\s+type: VP\s+offset: (0x[0-9A-Fa-f]+)\s+symbol: aVp\1", source)
    if [name for name, _ in entries] != list(NAMES):
        raise ValueError("Source viewport table changed")
    rows = []
    for name, offset in entries:
        start = int(offset, 16)
        if start + 16 > len(segment):
            raise ValueError("Truncated viewport")
        row = struct.unpack_from(">8h", segment, start)
        if row[0] <= 0 or row[1] <= 0 or row[3] or row[7]:
            raise ValueError("Invalid viewport scale or padding")
        rows.append(row)
    return rows


def header(rows):
    if len(rows) != 13 or any(len(row) != 8 for row in rows):
        raise ValueError("Expected thirteen viewport records")
    lines = ["/* Generated viewport data; regenerate, never edit. */",
             "#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__",
             "#define VP_HALF(x) (((x)>>8)&255),((x)&255)",
             "#else", "#define VP_HALF(x) ((x)&255),(((x)>>8)&255)", "#endif"]
    for name, row in zip(NAMES, rows):
        lines.append("unsigned char aVp%s[16] __attribute__((aligned(8))) = {%s};" %
                     (name, ",".join("VP_HALF(%d)" % (x & 65535) for x in row)))
    lines += ["#undef VP_HALF", "static const void* const ref_viewports[13] = {",
              ",".join("aVp" + name for name in NAMES), "};"]
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--art", choices=("fixture", "retail"), required=True)
    ap.add_argument("--output", type=Path)
    args = ap.parse_args()
    out = args.output or Path("generated") / ("camera_" + args.art)
    output_location(out)
    if args.art == "retail":
        path = os.environ.get("FZX_TEST_ROM")
        if not path:
            raise SystemExit("FZX_TEST_ROM is required locally")
        raw = Path(path).read_bytes()
        extract_rom_course(raw)
        rom, _ = normalize_rom(raw)
        verify_source(DECOMP)
        source = (DECOMP / "assets/yaml/us/rev0/course_track_gfx.yaml").read_bytes()
        segment = mio0(rom[0x16C8A0:0x17B1E0])
        rows = decode(segment, source.decode())
    else:
        # Synthetic camera fixture. Does not assert retail viewport identities.
        raw = b"synthetic viewport fixture v1"
        source = b""
        rows = [(640, 480, 511, 0, 640, 480, 511, 0)] * 13
    out.mkdir(parents=True, exist_ok=True)
    data = header(rows).encode()
    (out / "viewports.h").write_bytes(data)
    digest = lambda b: hashlib.sha256(b).hexdigest()
    receipt = ["# fzx generation receipt v1; regenerate, never edit",
               "generator %s camera_assets.py" % digest(Path(__file__).read_bytes()),
               "generator %s import_course.py" % digest(Path(__file__).with_name("import_course.py").read_bytes()),
               "generator %s import_source_art.py" % digest(Path(__file__).with_name("import_source_art.py").read_bytes()),
               "input %s %s" % (digest(raw), args.art),
               "input %s source-layout" % digest(source),
               "output %s viewports.h" % digest(data)]
    (out / "viewports.receipt").write_bytes(("\n".join(receipt) + "\n").encode())
    print("camera viewport export: 13 records (" + args.art + ")")


if __name__ == "__main__":
    main()
