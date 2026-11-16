#!/usr/bin/env python3
"""Author synthetic colored HUD test sheets. No game pixels or ROM input."""
from pathlib import Path
import hashlib
import struct
import sys

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
from hud_assets import HUD_SHEETS, header


def main():
    sheets = {}
    colors = (0x801F, 0x83E0, 0xFC00, 0xFFFF)
    for index, (name, width, height) in enumerate(HUD_SHEETS):
        stride = (width + 7) & ~7
        data = bytearray(stride * height * 2)
        cell = 12 if name == "aLapCounterSymbolsTex" else 16
        for y in range(height):
            for x in range(width):
                # Transparent border and distinguishable glyph colors/stripes.
                v = 0 if x == 0 or x == width-1 or y % cell in (0, cell-1) else colors[(index+y//cell+x//3)%4]
                struct.pack_into(">H", data, (y*stride+x)*2, v)
        sheets[name] = bytes(data)
    out = TOOLS.parent / "generated/hud_fixture"
    out.mkdir(parents=True, exist_ok=True)
    data = header(sheets, fixture=True)
    (out / "hud.h").write_bytes(data)
    digest = lambda b: hashlib.sha256(b).hexdigest()
    receipt = ["# fzx generation receipt v1"]
    for name in ("hud_probe/make_fixture.py", "hud_assets.py"):
        receipt.append(f"generator {digest((TOOLS/name).read_bytes())} {name}")
    receipt += [f"output {digest(data)} hud.h", ""]
    (out / "source_art.receipt").write_bytes("\n".join(receipt).encode())
    print("Synthetic HUD fixture generated; no source art or game simulation")


if __name__ == "__main__":
    main()
