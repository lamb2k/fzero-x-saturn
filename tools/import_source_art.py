#!/usr/bin/env python3
"""Bounded US rev0 art extraction for the native Saturn port.

This is an offline source-recipe reader, not a display-list interpreter. It
accepts only Blue Falcon's five pinned draw functions. Vertex identities,
signed normals, UVs, triangle order and material state survive conversion.
The lossless JSON is an intermediate, not a claim of native visual parity.
ROM-derived outputs belong in ignored generated/ or private evidence only.
Source layouts: pinned CC0 fzerox and MIT G-Diffuser; see NOTICE.md.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

from import_course import (DECOMP_COMMIT, UPSTREAM_COMMIT, extract_rom_course,
                           normalize_rom)

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT / "upstream/G-Diffuser/decomp"
DRAW = "src/overlays/ovl_i9/machine_draw.c"
YAML = "assets/yaml/us/rev0/"
# True ROM segment bounds, not subsection offsets or decoded lengths.
SEGMENTS = {
    "machine_custom_gfx": (0x17B1E0, 0x1B8550),
    "machine_global_gfx": (0x1E23F0, 0x22B0A0),
    "machine_models": (0x22B0A0, 0x235130),
    "hud_gfx": (0x1B8550, 0x1E23F0),
    "mute_city_textures": (0x235130, 0x239A80),
}
HUD = ("aHudTimeTex", "aHudEnergyTex", "aLapCounterSymbolsTex",
       "aSpeedDigitsTex", "aKmhTex", "aTimerSymbolsTex", "aLapTex")


class ArtError(ValueError):
    """Unsupported source or malformed asset; never silently substitute art."""


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def bounded(data: bytes, offset: int, size: int) -> bytes:
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ArtError("Asset span exceeds its source segment")
    return data[offset:offset + size]


def mio0(data: bytes, limit: int = 4 * 1024 * 1024) -> bytes:
    """MIO0 with independent stream bounds and overlapping back references."""
    if len(data) < 16 or data[:4] != b"MIO0":
        raise ArtError("Missing MIO0 header")
    size, pairs, literals = struct.unpack_from(">III", data, 4)
    if not 0 < size <= limit or not 16 <= pairs <= literals <= len(data):
        raise ArtError("Invalid MIO0 size or stream offsets")
    mask_end, pair_end = pairs, literals
    mask_pos, bits, mask = 16, 0, 0
    out = bytearray()
    while len(out) < size:
        if bits == 0:
            if mask_pos >= mask_end:
                raise ArtError("Truncated MIO0 mask stream")
            mask, bits = data[mask_pos], 8
            mask_pos += 1
        if mask & 0x80:
            out += bounded(data, literals, 1)
            literals += 1
        else:
            if pairs + 2 > pair_end:
                raise ArtError("Truncated MIO0 pair stream")
            pair = struct.unpack_from(">H", data, pairs)[0]
            pairs += 2
            length, distance = (pair >> 12) + 3, (pair & 0xFFF) + 1
            if distance > len(out) or len(out) + length > size:
                raise ArtError("Invalid MIO0 back reference")
            for _ in range(length):
                out.append(out[-distance])
        mask = (mask << 1) & 255
        bits -= 1
    return bytes(out)


def metadata(text: str) -> dict:
    """Read only the flat asset rows used here; no YAML execution/dependency."""
    rows, current = {}, None
    for line in text.splitlines():
        key = re.fullmatch(r"([A-Za-z_][A-Za-z_0-9]*):", line)
        if key:
            current = rows.setdefault(key[1], {})
        elif current is not None:
            field = re.fullmatch(r"  (type|format|width|height|offset|count|symbol): ([A-Za-z_0-9]+)", line)
            if field:
                current[field[1]] = (int(field[2], 0) if field[1] in
                                     ("width", "height", "offset", "count") else field[2])
    return rows


def stock_function(text: str, name: str) -> str:
    match = re.search(r"Gfx\* " + re.escape(name) + r"\(Gfx\* gfx\) \{(.*?)\n\}", text, re.S)
    if not match:
        raise ArtError(f"Missing pinned source function {name}")
    active, stack, out = True, [], []
    for line in match[1].splitlines():
        directive = line.strip()
        if directive == "#ifdef EXPANSION_KIT":
            stack.append(active)
            active = False
        elif directive == "#endif":
            if not stack:
                raise ArtError("Unbalanced source conditional")
            active = stack.pop()
        elif directive.startswith("#"):
            raise ArtError("Unsupported source conditional")
        elif active:
            out.append(line)
    if stack:
        raise ArtError("Unclosed source conditional")
    return "\n".join(out)


def commands(body: str):
    calls = list(re.finditer(r"\b(g[SD]P\w+)\((.*?)\);", body, re.S))
    if len(calls) != len(re.findall(r"\bg[SD]P\w+\s*\(", body)):
        raise ArtError("Unparsed graphics command")
    for call in calls:
        yield call[1], [x.strip() for x in call[2].split(",")]


def decode_texture(data: bytes, fmt: str, width: int, height: int) -> bytes:
    """RGBA8888 matching pinned G-Diffuser tools/n64_texels.py.

    The visual oracle expands 5-bit channels with floor(v*255/31), and IA4's
    3-bit intensity with v*36 (maximum 252). Retain that convention explicitly.
    """
    count = width * height
    if width <= 0 or height <= 0 or count > 1024 * 1024:
        raise ArtError("Invalid texture dimensions")
    if fmt not in ("RGBA16", "I4", "IA4"):
        raise ArtError(f"Unsupported texture format {fmt}")
    stride = width * 2 if fmt == "RGBA16" else (width + 1) // 2
    expected = stride * height
    if len(data) != expected:
        raise ArtError("Texture byte count mismatch")
    result = bytearray()
    for i in range(count):
        if fmt == "RGBA16":
            v = struct.unpack_from(">H", data, i * 2)[0]
            rgb = [(v >> shift) & 31 for shift in (11, 6, 1)]
            rgba = [c * 255 // 31 for c in rgb] + [255 * (v & 1)]
        else:
            x, y = i % width, i // width
            v = (data[y * stride + x // 2] >> (4 if x % 2 == 0 else 0)) & 15
            if fmt == "I4":
                rgba = [v * 17] * 4
            else:
                intensity = (v >> 1) * 36
                rgba = [intensity] * 3 + [255 * (v & 1)]
        result.extend(rgba)
    return bytes(result)


def rgb1555(rgba: bytes, width: int, height: int) -> tuple[bytes, int]:
    """VDP1 RGB, transparent zero, big endian SH-2 words, 8-pixel row padding.

    Only binary alpha is accepted. Model intensity alpha belongs to its material
    combiner and must not be reduced to a transparent mask by this function.
    """
    if len(rgba) != width * height * 4 or width <= 0 or height <= 0:
        raise ArtError("Invalid native texture dimensions")
    stride = (width + 7) & ~7
    out = bytearray(stride * height * 2)
    for i in range(width * height):
        r, g, b, a = rgba[i * 4:i * 4 + 4]
        if a not in (0, 255):
            raise ArtError("Fractional alpha requires material conversion")
        value = (0x8000 | (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)) if a else 0
        struct.pack_into(">H", out, ((i // width) * stride + i % width) * 2, value)
    return bytes(out), stride


class SourceArt:
    def __init__(self, rom: bytes, decomp: Path = DECOMP):
        self.rom, self.decomp = rom, decomp
        self.rows, self.segments, self.inputs = {}, {}, {}
        for family, (start, end) in SEGMENTS.items():
            path = YAML + family + ".yaml"
            raw = (decomp / path).read_bytes()
            self.inputs[path] = sha(raw)
            for symbol, row in metadata(raw.decode()).items():
                if symbol in self.rows:
                    raise ArtError(f"Duplicate asset symbol {symbol}")
                self.rows[symbol] = dict(row, family=family)
            segment = bounded(rom, start, end - start)
            self.segments[family] = mio0(segment) if segment[:4] == b"MIO0" else segment
        self.draw = (decomp / DRAW).read_bytes()
        self.inputs[DRAW] = sha(self.draw)
        self.inputs["src/game/racer.c"] = sha((decomp / "src/game/racer.c").read_bytes())

    def texture(self, symbol: str) -> dict:
        row = self.rows[symbol]
        if row["type"] != "TEXTURE":
            raise ArtError("Expected texture metadata")
        w, h, fmt = row["width"], row["height"], row["format"]
        size = w * h * 2 if fmt == "RGBA16" else ((w + 1) // 2) * h
        raw = bounded(self.segments[row["family"]], row["offset"], size)
        rgba = decode_texture(raw, fmt, w, h)
        return dict(symbol=symbol, family=row["family"], offset=row["offset"],
                    format=fmt, width=w, height=h, source_sha256=sha(raw), rgba=rgba)

    def model(self, lod: int) -> dict:
        if lod not in range(1, 6):
            raise ArtError("Blue Falcon has five authored LODs")
        name = f"Machine_DrawBlueFalconLod{lod}"
        vertices, triangles, materials, cache = [], [], [], {}
        state = dict(tile=None, primitive=None, environment="racer.bodyRGB", combine=None)
        for command, args in commands(stock_function(self.draw.decode(), name)):
            if args[0] != "gfx++":
                raise ArtError("Unsupported graphics destination")
            if command == "gDPPipeSync":
                continue
            if command == "gDPSetCombineLERP":
                combine = args[1:]
                if combine not in [
                    "TEXEL0 ENVIRONMENT TEXEL0_ALPHA ENVIRONMENT 0 0 0 ENVIRONMENT COMBINED 0 SHADE 0 0 0 0 COMBINED".split(),
                    "PRIMITIVE ENVIRONMENT TEXEL0 ENVIRONMENT 0 0 0 ENVIRONMENT COMBINED 0 SHADE 0 0 0 0 COMBINED".split(),
                ]:
                    raise ArtError("Unsupported Blue Falcon combiner")
                state["combine"] = combine
            elif command in ("gDPSetPrimColor", "gDPSetEnvColor"):
                state["primitive" if command == "gDPSetPrimColor" else "environment"] = list(map(int, args[-4:]))
            elif command == "gSPTexture":
                if args[1:4] != ["0xFFFF", "0xFFFF", "0"] or args[-1] != "G_ON":
                    raise ArtError("Unsupported source texture scaling")
                state["tile"] = int(args[4])
            elif command == "gSPVertex":
                match = re.fullmatch(r"&?(D_[0-9A-Fa-f]+)(?:\[(\d+)\])?", args[1])
                if not match:
                    raise ArtError("Unsupported source vertex expression")
                symbol, first = match[1], int(match[2] or 0)
                count, base = map(int, args[2:])
                row = self.rows[symbol]
                if row["type"] != "VTX" or first + count > row["count"] or not 0 < count <= 64 or base != 0:
                    raise ArtError("Source vertex load exceeds declared array/cache")
                cache = {}
                for index in range(count):
                    offset = row["offset"] + 16 * (first + index)
                    raw = bounded(self.segments[row["family"]], offset, 16)
                    x, y, z, flag, u, v, nx, ny, nz, a = struct.unpack(">hhhHhhbbbB", raw)
                    cache[index] = len(vertices)
                    vertices.append(dict(symbol=symbol, index=first + index,
                                         position=[x, y, z], flag=flag, uv=[u, v],
                                         normal=[nx, ny, nz], alpha=a))
            elif command in ("gSP1Triangle", "gSP2Triangles"):
                indices = list(map(int, args[1:]))
                if len(indices) != (4 if command == "gSP1Triangle" else 8):
                    raise ArtError("Triangle argument count mismatch")
                if state["tile"] is None or state["combine"] is None:
                    raise ArtError("Triangle lacks source material state")
                if state not in materials:
                    materials.append(dict(state))
                material = materials.index(state)
                for i in range(0, len(indices), 4):
                    a, b, c, flag = indices[i:i + 4]
                    if flag or any(v not in cache for v in (a, b, c)):
                        raise ArtError("Triangle references unloaded vertex or unsupported flag")
                    triangles.append(dict(vertices=[cache[v] for v in (a, b, c)], material=material))
            else:
                raise ArtError(f"Unhandled Blue Falcon command {command}")
        if not vertices or not triangles:
            raise ArtError("Empty model")
        return dict(lod=lod, source_function=name, vertices=vertices,
                    triangles=triangles, materials=materials)

    def texture_bindings(self) -> list[dict]:
        bindings = []
        for command, args in commands(stock_function(self.draw.decode(), "Machine_DrawLoadBlueFalconTextures")):
            if command == "gDPPipeSync":
                continue
            if command not in ("gDPLoadMultiBlock", "gDPLoadMultiBlock_4b"):
                raise ArtError(f"Unhandled texture command {command}")
            bindings.append(dict(tile=int(args[3]), symbol=args[1], source_command=command, arguments=args[2:]))
        if [b["tile"] for b in bindings] != list(range(1, 8)):
            raise ArtError("Incomplete Blue Falcon texture bindings")
        # Tile zero is inherited from the source machine setup display list.
        # Do not invent a texture: retain this explicit unresolved dependency.
        bindings.insert(0, dict(tile=0, symbol=None, dependency="D_303A6A8 race setup / look-at material"))
        return bindings


def verify_source(decomp: Path):
    for path, expected in ((decomp, DECOMP_COMMIT), (decomp.parent, UPSTREAM_COMMIT)):
        head = subprocess.run(["git", "-C", str(path), "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
        changed = subprocess.run(["git", "-C", str(path), "diff", "--name-only", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
        if head != expected or changed:
            raise ArtError("Art extraction requires clean pinned upstream source")


def export(rom: bytes, out: Path, decomp: Path = DECOMP) -> dict:
    # Reuse the same fingerprint boundary as the course importer. No output
    # directory is created until the ROM and pinned source have been verified.
    output_location(out)
    _, provenance = extract_rom_course(rom)
    normalized, _ = normalize_rom(rom)
    verify_source(decomp)
    source = SourceArt(normalized, decomp)
    bindings = source.texture_bindings()
    models = [source.model(lod) for lod in range(1, 6)]
    names = list(dict.fromkeys([b["symbol"] for b in bindings if b["symbol"]] +
                              list(HUD) + [f"D_A00{i:X}000_235130" for i in range(7)]))
    outputs, textures = {}, []
    for name in names:
        texture = source.texture(name)
        rgba = texture.pop("rgba")
        texture["rgba_file"] = name + ".rgba"
        outputs[texture["rgba_file"]] = rgba
        if texture["format"] == "RGBA16":
            native, stride = rgb1555(rgba, texture["width"], texture["height"])
            texture.update(native_file=name + ".rgb1555", native_stride=stride)
            outputs[texture["native_file"]] = native
        textures.append(texture)
    manifest = dict(schema=1, source_commit=UPSTREAM_COMMIT, decomp_commit=DECOMP_COMMIT,
                    rom_sha256=provenance["normalized_rom_sha256"], source_inputs=source.inputs,
                    model_scale_game_units=[0.1, 0.1, 0.1], uv_fraction_bits=5,
                    color_expansion="G-Diffuser tools/n64_texels.py: RGB5 floor(v*255/31), IA3 v*36",
                    models=models, texture_bindings=bindings, textures=textures,
                    unresolved=["Tile 0 machine setup/look-at state", "Native model material rendering and lighting",
                                "Source model pose/camera integration", "Source-reference still/motion comparison"])
    outputs["source_art.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    out.mkdir(parents=True, exist_ok=True)
    receipt = ["# fzx generation receipt v1"]
    for filename in ("import_source_art.py", "import_course.py"):
        receipt.append(f"generator {sha((ROOT / 'tools' / filename).read_bytes())} {filename}")
    for filename, digest in source.inputs.items():
        receipt.append(f"generator {digest} ../upstream/G-Diffuser/decomp/{filename}")
    receipt.append(f"input {sha(rom)} owner-rom")
    for name, data in outputs.items():
        (out / name).write_bytes(data)
        receipt.append(f"output {sha(data)} {name}")
    (out / "source_art.receipt").write_text("\n".join(receipt) + "\n", encoding="utf-8")
    return dict(lod_triangles=[len(m["triangles"]) for m in models], textures=len(textures),
                native_texture_bytes=sum(len(v) for k, v in outputs.items() if k.endswith(".rgb1555")))


def output_location(out: Path):
    """Only ignored generated/ or the configured private evidence tree."""
    target = out.resolve() / "source_art.json"
    evidence = os.environ.get("FZX_EVIDENCE")
    permitted = target.is_relative_to(ROOT.resolve() / "generated")
    if evidence:
        permitted |= target.is_relative_to(Path(evidence).resolve())
    if not permitted:
        raise ArtError("Art output must stay in generated/ or configured FZX_EVIDENCE")
    if target.is_relative_to(ROOT.resolve()):
        result = subprocess.run(["git", "-C", str(ROOT), "check-ignore", "--quiet", str(target)],
                                capture_output=True)
        if result.returncode != 0:
            raise ArtError("In-repository art output must use a git-ignored directory")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "generated/source_art")
    args = parser.parse_args()
    path = os.environ.get("FZX_TEST_ROM")
    if not path:
        parser.error("Set FZX_TEST_ROM locally; do not put its path in logs")
    try:
        rom = Path(path).read_bytes()
        print(json.dumps(export(rom, args.output), sort_keys=True))
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as exc:
        # OSError includes private filenames: keep it out of console output.
        print(f"source-art import failed: {type(exc).__name__}" +
              (f": {exc}" if isinstance(exc, ArtError) else ""), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
