#!/usr/bin/env python3
"""Extract the stock Mute City backdrop locally, retaining palette and alpha.

This card exports source assets and their selected bindings. It does not choose
a Saturn cloud/fade approximation or claim that the backdrop is rendered.
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

from import_course import DECOMP_COMMIT, UPSTREAM_COMMIT, extract_rom_course, normalize_rom
from import_source_art import (ArtError, DECOMP, ROOT, YAML, bounded, decode_texture,
                               metadata, output_location, rgb1555, verify_source)
from road_environment import course_fog

SOURCE = "src/overlays/ovl_i3/background.c"
HEADER = "src/overlays/ovl_i3/background.h"
CATALOG = YAML + "common_assets_compressed.yaml"
ROM_BASE, ROM_END = 0x2B9EA0, 0x524920


def sha(data):
    return hashlib.sha256(data).hexdigest()


def catalog(text):
    """Extend the bounded flat metadata reader with CI palette references."""
    rows = metadata(text)
    current = None
    seen = set()
    for line in text.splitlines():
        match = re.fullmatch(r"([A-Za-z_]\w*):", line)
        if match:
            current = match[1]
            if current in seen:
                raise ArtError("Duplicate backdrop metadata symbol")
            seen.add(current)
        elif current:
            match = re.fullmatch(r"  (colors|tlut): (0x[0-9A-Fa-f]+|[0-9]+)", line)
            if match:
                if match[1] in rows[current]:
                    raise ArtError("Duplicate backdrop palette field")
                rows[current][match[1]] = int(match[2], 0)
    return rows


def initializer(source, declaration):
    match = re.search(declaration + r"\s*=\s*\{(.*?)\};", source, re.S)
    if not match:
        raise ArtError("Missing source backdrop initializer")
    return re.sub(r"//[^\n]*|/\*.*?\*/", "", match[1], flags=re.S)


def bindings(source, header, course):
    """Select the first stock course. Unsupported venues fail explicitly."""
    if len(course) != 0x7E0 or course[2:4] != b"\0\0":
        raise ArtError("Backdrop slice requires Mute City venue and Purple sky")
    fog = course_fog(source, header, 0)
    layout = re.search(r'typedef struct CourseVenueFloor\s*\{(.*?)\}\s*CourseVenueFloor;', header, re.S)
    if not layout or re.findall(r'\b(\w+)\s*;', layout[1]) != ['texture','xScale','zScale','xScrollSpeed','zScrollSpeed']:
        raise ArtError("Source venue-floor layout changed")
    floors = re.findall(r"&([A-Za-z_]\w*)", initializer(source, r"CourseVenueFloor\* sCourseVenueFloors\[\]"))
    if len(floors) != 11 or floors[0] != "sVenueFloorMuteCity":
        raise ArtError("Source venue selection changed")
    floor = [x.strip() for x in initializer(source, r"CourseVenueFloor " + floors[0]).split(",")]
    if len(floor) != 5 or floor[0] != "aVenueMuteCityTex":
        raise ArtError("Source floor binding changed")
    floor_numbers = [float(x.removesuffix("f")) for x in floor[1:]]
    sky = [x.strip() for x in initializer(source, r"CourseSkyboxes " + fog['symbol']).split(",")]
    if len(sky) != 11 or sky[0] != "aSkyboxPurpleTex" or sky[-1] != "SKYBOX_CLOUDY":
        raise ArtError("Source Purple sky binding changed")
    colors = [int(x, 0) for x in sky[1:-1]]
    if any(not 0 <= c <= 255 for c in colors):
        raise ArtError("Invalid source backdrop color")
    table = initializer(source, r"BackgroundSpriteInitData\* sBackgroundSpriteCourseInitData\[\]")
    lists = re.findall(r"\b(sBackgroundSpriteInit\w+)\b", table)
    if len(lists) != 24 or lists[0] != "sBackgroundSpriteInitMuteCity":
        raise ArtError("Source course skyline selection changed")
    body = initializer(source, r"BackgroundSpriteInitData " + lists[0] + r"\[\]")
    entries = re.findall(r"\{\s*(BG_SPRITE_\w+)\s*,\s*(-?[0-9]+(?:\.[0-9]+)?)f\s*\}", body)
    residue = re.sub(r"\{\s*BG_SPRITE_\w+\s*,\s*-?[0-9]+(?:\.[0-9]+)?f\s*\}", "", body)
    if residue.replace(",", "").strip() or len(entries) != 7 or entries[-1] != ("BG_SPRITE_END", "0.0"):
        raise ArtError("Unsupported source skyline list")
    # Bind by the actual C enum/table index, never by the explanatory comments.
    enum = re.search(r"typedef enum BackgroundSpriteId\s*\{(.*?)\}\s*BackgroundSpriteId;", header, re.S)
    if not enum:
        raise ArtError("Missing source skyline enum")
    enum_body = re.sub(r"//[^\n]*|/\*.*?\*/", "", enum[1], flags=re.S)
    enum_names = [x.strip() for x in enum_body.split(',') if x.strip()]
    if not enum_names or any(not re.fullmatch(r'BG_SPRITE_\w+', x) for x in enum_names) or len(set(enum_names)) != len(enum_names):
        raise ArtError("Unsupported source skyline enum")
    pair_body = initializer(source, r"TexturePtr sBackgroundSpritePalettePairs\[\]\[2\]")
    pair_rows = re.findall(r"\{\s*(\w+)\s*,\s*(\w+)\s*\}", pair_body)
    residue = re.sub(r"\{\s*\w+\s*,\s*\w+\s*\}", "", pair_body)
    if residue.replace(',', '').strip() or len(pair_rows) != len(enum_names) or pair_rows[0] != ('NULL', 'NULL'):
        raise ArtError("Source skyline palette table shape changed")
    pairs = dict(zip(enum_names, pair_rows))
    sprites = []
    for name, angle in entries[:-1]:
        if name not in pairs or not 0 <= float(angle) < 360:
            raise ArtError("Unbound source skyline entry")
        texture, palette = pairs[name]
        sprites.append(dict(id=name, angle_degrees=float(angle), texture=texture, palette=palette))
    # These are the source loads for raw (not MIO0) selected common assets.
    required = (
        "sBackgroundCtx.venueFloor = sCourseVenueFloors[gVenueType];",
        "sBackgroundSpriteR = sBackgroundCtx.skybox->backgroundSpriteR;",
        "sBackgroundSpriteG = sBackgroundCtx.skybox->backgroundSpriteG;",
        "sBackgroundSpriteB = sBackgroundCtx.skybox->backgroundSpriteB;",
        "sCloudTexture = func_80078104(aCloudTex, TEX_SIZE(aCloudTex, sizeof(u8)), 0, 0, 0);",
        "courseInfo->racerFogColors[0] = sBackgroundCtx.skybox->courseFogR;",
        "courseInfo->racerFogColors[1] = sBackgroundCtx.skybox->courseFogG;",
        "courseInfo->racerFogColors[2] = sBackgroundCtx.skybox->courseFogB;",
    )
    if any(x not in source for x in required):
        raise ArtError("Source backdrop initialization wiring changed")
    return dict(course_index=0, venue=0, skybox=0,
                floor=dict(texture=floor[0], scale=floor_numbers[:2], scroll_speed=floor_numbers[2:]),
                sky=dict(texture=sky[0], flags=sky[-1], course_fog=colors[:3],
                         racer_fog_override=colors[3:6],
                         racer_fog=colors[:3] if colors[3:6] == [0,0,0] else colors[3:6],
                         sprite_rgb=colors[6:9]),
                cloud=dict(texture="aCloudTex", fractional_alpha=True), sprites=sprites)


def decode_ia8(data, width, height):
    if width <= 0 or height <= 0 or width * height > 1024 * 1024 or len(data) != width * height:
        raise ArtError("Invalid IA8 backdrop dimensions")
    return bytes(c for v in data for c in ((v >> 4) * 17,) * 3 + ((v & 15) * 17,))


def decode_ci4(data, palette, width, height):
    stride = (width + 1) // 2
    if width <= 0 or height <= 0 or width * height > 1024 * 1024 or len(data) != stride * height or len(palette) != 32:
        raise ArtError("Invalid CI4 backdrop dimensions or palette")
    rgba = decode_texture(palette, "RGBA16", 16, 1)
    result = bytearray()
    for y in range(height):
        for x in range(width):
            index = (data[y * stride + x // 2] >> (0 if x & 1 else 4)) & 15
            result.extend(rgba[index * 4:index * 4 + 4])
    return bytes(result)


def read_asset(rom, rows, name):
    row = rows.get(name, {})
    if row.get("type") != "TEXTURE" or row.get("symbol") != name:
        raise ArtError("Missing or unsupported backdrop asset")
    fmt = row.get("format")
    if fmt == "TLUT":
        if row.get("colors") != 16:
            raise ArtError("Skyline requires its sixteen authored palette entries")
        width, height, size = 16, 1, 32
    else:
        width, height = row.get("width", 0), row.get("height", 0)
        if not 0 < width <= 512 or not 0 < height <= 512:
            raise ArtError("Invalid backdrop metadata dimensions")
        if fmt == "RGBA16":
            size = width * height * 2
        elif fmt == "IA8":
            size = width * height
        elif fmt == "CI4":
            size = ((width + 1) // 2) * height
        else:
            raise ArtError("Unsupported backdrop texture format")
    offset = row.get("offset", -1)
    if offset < 0 or offset + size > ROM_END - ROM_BASE:
        raise ArtError("Backdrop asset outside source segment")
    # Do not let an enlarged metadata extent consume the next source object.
    end = min((r['offset'] for r in rows.values() if r.get('offset', -1) > offset), default=ROM_END-ROM_BASE)
    if offset + size > end:
        raise ArtError("Backdrop extent overlaps the next source object")
    data = bounded(rom, ROM_BASE + offset, size)
    return dict(symbol=name, format=fmt, width=width, height=height,
                source_offset=ROM_BASE+offset, source_bytes=size, source_sha256=sha(data)), data


def export(rom, out, decomp=DECOMP):
    output_location(out)
    course, provenance = extract_rom_course(rom)
    normalized, _ = normalize_rom(rom)
    verify_source(decomp)
    inputs = {name: (decomp / name).read_bytes() for name in (SOURCE, HEADER, CATALOG)}
    selected = bindings(inputs[SOURCE].decode(), inputs[HEADER].decode(), course)
    rows = catalog(inputs[CATALOG].decode())
    names = [selected[k]['texture'] for k in ('floor', 'sky', 'cloud')]
    names += [s[k] for s in selected['sprites'] for k in ('texture', 'palette')]
    outputs, records = {}, []
    for name in dict.fromkeys(names):
        record, raw = read_asset(normalized, rows, name)
        fmt, w, h = record['format'], record['width'], record['height']
        outputs[name+'.texels'] = raw
        record['raw_file'] = name+'.texels'
        if fmt == 'CI4':
            palettes = {s['palette'] for s in selected['sprites'] if s['texture'] == name}
            if len(palettes) != 1:
                raise ArtError("Ambiguous selected skyline palette")
            palette_name = palettes.pop()
            palette_record, palette = read_asset(normalized, rows, palette_name)
            if palette_record['format'] != 'TLUT' or rows[name].get('tlut') != (0x0F000000 | rows[palette_name]['offset']):
                raise ArtError("Source skyline palette pointer disagrees with metadata")
            rgba = decode_ci4(raw, palette, w, h)
            record['palette'] = palette_name
        elif fmt == 'IA8':
            rgba = decode_ia8(raw, w, h)
        else:
            rgba = decode_texture(raw, 'RGBA16', w, h)
        outputs[name+'.rgba'] = rgba
        record.update(rgba_file=name+'.rgba', rgba_sha256=sha(rgba))
        if fmt != 'IA8':
            native, stride = rgb1555(rgba, w, h)
            outputs[name+'.rgb1555'] = native
            record.update(native_file=name+'.rgb1555', native_stride=stride, native_sha256=sha(native))
        else:
            record['native_status'] = 'unimplemented: retain all sixteen alpha levels'
        records.append(record)
    manifest = dict(schema=1, source_commit=UPSTREAM_COMMIT, decomp_commit=DECOMP_COMMIT,
                    rom_sha256=provenance['normalized_rom_sha256'], course_sha256=sha(course),
                    source_inputs={n:sha(b) for n,b in inputs.items()}, bindings=selected, assets=records,
                    unresolved=['Native cloud fractional alpha and source scroll state',
                                'Native skyline tint and vertex-alpha fade',
                                'Source background geometry/camera and painter integration',
                                'Source-reference still/motion and console checks'])
    outputs['backdrop.json'] = (json.dumps(manifest, indent=2)+'\n').encode()
    out.mkdir(parents=True, exist_ok=True)
    if any((out/name).is_symlink() for name in [*outputs, 'backdrop.receipt']):
        raise ArtError("Refusing a redirected generated backdrop file")
    receipt = ['# fzx generation receipt v1']
    for name in ('backdrop_assets.py', 'import_source_art.py', 'import_course.py', 'road_environment.py', 'hud_assets.py', 'model_assets.py'):
        receipt.append('generator %s %s' % (sha((ROOT/'tools'/name).read_bytes()), name))
    for name, data in inputs.items():
        receipt.append('generator %s ../upstream/G-Diffuser/decomp/%s' % (sha(data), name))
    receipt.append('input %s owner-rom' % sha(rom))
    for name, data in outputs.items():
        (out/name).write_bytes(data)
        receipt.append('output %s %s' % (sha(data), name))
    (out/'backdrop.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    return dict(assets=len(records), skyline_instances=len(selected['sprites']),
                source_bytes=sum(r['source_bytes'] for r in records),
                native_rgb_bytes=sum(len(v) for k,v in outputs.items() if k.endswith('.rgb1555')),
                cloud_alpha_levels=len(set(outputs['aCloudTex.rgba'][3::4])))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'generated/backdrop')
    args = parser.parse_args()
    path = os.environ.get('FZX_TEST_ROM')
    if not path:
        parser.error('Set FZX_TEST_ROM locally; keep its path out of logs')
    try:
        print(json.dumps(export(Path(path).read_bytes(), args.output), sort_keys=True))
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as exc:
        print('backdrop import failed: '+type(exc).__name__+((': '+str(exc)) if isinstance(exc, ArtError) else ''), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
