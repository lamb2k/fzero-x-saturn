"""Native HUD atlas/header emitter shared by retail export and synthetic probe."""
import struct

HUD_SHEETS = (
    ("aHudTimeTex", 24, 16), ("aHudEnergyTex", 72, 16),
    ("aLapCounterSymbolsTex", 8, 72), ("aSpeedDigitsTex", 12, 160),
    ("aKmhTex", 20, 16), ("aTimerSymbolsTex", 8, 224), ("aLapTex", 16, 12),
)


def header(native_sheets, *, fixture=False):
    """Input words are big endian RGB1555 with each row padded to eight pixels."""
    atlas, rows = bytearray(), []
    for symbol, width, height in HUD_SHEETS:
        stride = (width + 7) & ~7
        data = native_sheets[symbol]
        if len(data) != stride * height * 2:
            raise ValueError("HUD sheet size does not match source geometry")
        # Padding must stay transparent; normal sprites draw the padded width.
        for y in range(height):
            if any(data[(y*stride+width)*2:(y+1)*stride*2]):
                raise ValueError("HUD padding would draw over adjacent glyphs")
        rows.append(f"    {{{len(atlas)}u,{width}u,{stride}u,{height}u}}")
        atlas.extend(data)
    words = struct.unpack(f">{len(atlas)//2}H", atlas)
    lines = ["/* Generated native HUD atlas; regenerate, never edit. */",
             '#include "fzx_hud.h"', f"#define FZX_HUD_FIXTURE {int(fixture)}",
             f"static const fzx_hud_atlas_t fzx_hud_atlas = {{{len(atlas)}u, {{",
             ",\n".join(rows), "}};", "static const uint16_t fzx_hud_pixels[] = {"]
    lines.extend("    " + ",".join(f"0x{v:04x}" for v in words[i:i+12]) + ","
                 for i in range(0, len(words), 12))
    lines += ["};", "_Static_assert(sizeof(fzx_hud_pixels) <= 0x6bfe0u, \"HUD texture budget\");", ""]
    return "\n".join(lines).encode()
