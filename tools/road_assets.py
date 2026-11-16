#!/usr/bin/env python3
"""Resolve the pinned course renderer's HOST capture into a native mesh pack.

This offline reader accepts only the source course's small fixed template set.
It does not execute arbitrary display lists and is never linked into the game.
Raw vertices, resolved UVs, material state and source chunk identities survive.
The resulting JSON and texture bytes are private conversion intermediates.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import struct
import subprocess

from import_course import extract_rom_course, normalize_rom
from import_source_art import (DECOMP, YAML, ArtError, bounded, decode_texture,
                               metadata, mio0, output_location, verify_source)

ROOT = Path(__file__).resolve().parents[1]
TEMPLATES = ("D_8014008", "D_8014040", "D_8014078", "D_80140F0", "D_8014138", "D_8014180",
             "D_80141C8", "D_8014210", "D_8014268", "D_80142C0", "D_8014308",
             "D_8014350", "D_8014398", "D_80143E0", "D_8014430", "D_8014480",
             "D_80144D0", "D_8014520", "D_8014580", "D_80145E0", "D_8014640")


def signed16(value):
    return ((value + 32768) & 65535) - 32768


def capture(text):
    lines = text.splitlines()
    try:
        magic, nc, nv, ng, ns = lines[0].split()
        nc, nv, ng, ns = int(nc), int(nv), int(ng), int(ns)
        if magic != "FZXROAD2" or not 3 <= nc <= 1024 or ns != nc or not 0 < nv <= 65536 or not 0 < ng <= 65536:
            raise ValueError()
        if len(lines) != 1 + nc + 1 + ns + nv + ng:
            raise ValueError()
        chunks, vertices, commands = [], [], []
        for i, line in enumerate(lines[1:nc + 2]):
            words = line.split()
            if len(words) != 28 or words[0] != "C" or int(words[1]) != i:
                raise ValueError()
            flags, segment = int(words[2], 16), int(words[3])
            points = list(map(int, words[4:]))
            if not 0 <= flags <= 0xFFFFFFFF or not 0 <= segment < 64 or any(not -32768 <= p <= 32767 for p in points):
                raise ValueError()
            chunks.append(dict(id=i, flags=flags, segment=segment, points=points))
        spans = []
        for i, line in enumerate(lines[nc + 2:nc + 2 + ns]):
            words = line.split()
            if len(words) != 7 or words[0] != "R" or int(words[1]) != i:
                raise ValueError()
            chunk, first, end, vfirst, vend = map(int, words[2:])
            if chunk != i or not 0 <= first <= end <= ng or not 0 <= vfirst <= vend <= nv:
                raise ValueError()
            if spans and (first < spans[-1]["end"] or vfirst != spans[-1]["vend"]):
                raise ValueError()
            spans.append(dict(chunk=chunk, first=first, end=end, vfirst=vfirst, vend=vend))
        for i, line in enumerate(lines[nc + 2 + ns:nc + 2 + ns + nv]):
            words = line.split()
            if len(words) != 11 or words[0] != "V" or int(words[1]) != i:
                raise ValueError()
            values = list(map(int, words[2:]))
            if any(not -32768 <= p <= 32767 for p in values[:5]) or any(not 0 <= c <= 255 for c in values[5:]):
                raise ValueError()
            vertices.append(values)
        for line in lines[nc + 2 + ns + nv:]:
            words = line.split()
            if len(words) != 4 or words[0] != "G":
                raise ValueError()
            a, b = int(words[1], 16), int(words[2], 16)
            if not 0 <= a <= 0xFFFFFFFF or not 0 <= b <= 0xFFFFFFFF:
                raise ValueError()
            commands.append((a, b, words[3]))
        if chunks[-1]["points"] != chunks[0]["points"]:
            raise ValueError("closing chunk differs")
        return chunks, vertices, commands, spans
    except (ValueError, IndexError) as exc:
        raise ArtError("Malformed/truncated source road capture") from exc


def template_words(data, rows, name):
    if name not in TEMPLATES or rows.get(name, {}).get("type") != "GFX":
        raise ArtError("Unsupported course template: " + name)
    start = rows[name]["offset"]
    end = min((row["offset"] for row in rows.values() if row["offset"] > start), default=len(data))
    raw = bounded(data, start, end - start)
    if not raw or len(raw) % 8 or len(raw) > 512:
        raise ArtError("Invalid course template span")
    result = [struct.unpack_from(">II", raw, i) for i in range(0, len(raw), 8)]
    if result[-1] != (0xDF000000, 0) or any(a >> 24 in (1, 0xDE, 0xDF, 0xFD) for a, _ in result[:-1]):
        raise ArtError("Course template contains an unsupported reference or termination")
    return result[:-1]


def resolve(vertices, commands, templates, prelude=(), spans=None):
    """Resolve only source-course cache, triangle and material operations.

    Loaded UVs use the reference's signed16(tc * scale >> 16). ModifyVertex
    writes already-scaled ST, so applying texture scale again would be wrong.
    Material records retain all active tile and combiner state for native use.
    """
    cache = [None] * 32
    resolved, vertex_ids, triangles, origins = [], {}, [], []
    materials, material_ids = [], {}
    state = dict(geometry=0, other_h=0, other_l=0, tiles={}, sizes={}, texture=None,
                 load=None, tile=0, scale=[0, 0], enabled=False, combine=None, prim=None)
    stats = dict(loads=0, modifies=0, templates={}, degenerates=0)
    owners = [-1] * len(commands)
    if spans is not None:
        for span in spans:
            for i in range(span["first"], span["end"]):
                if owners[i] != -1:
                    raise ArtError("Overlapping source chunk command spans")
                owners[i] = span["chunk"]
    origin = dict(command=-1, template=None, template_command=-1, chunk=-1)

    def vertex_id(item):
        key = tuple(item)
        if key not in vertex_ids:
            vertex_ids[key] = len(resolved)
            resolved.append(list(item))
        return vertex_ids[key]

    def triangle(word):
        slots = [(word >> shift) & 255 for shift in (16, 8, 0)]
        if any(v & 1 or v >= 64 or cache[v // 2] is None for v in slots):
            raise ArtError("Triangle references an unloaded vertex")
        if not state["enabled"] or state["texture"] is None or state["load"] is None or state["combine"] is None:
            raise ArtError("Triangle has incomplete course material state")
        if str(state["tile"]) not in state["tiles"] or str(state["tile"]) not in state["sizes"]:
            raise ArtError("Triangle uses an undefined tile")
        key = json.dumps(state, sort_keys=True, separators=(",", ":"))
        if key not in material_ids:
            material_ids[key] = len(materials)
            materials.append(json.loads(key))
        ids = [cache[v // 2] for v in slots]
        p = [resolved[k][:3] for k in ids]
        u, v = [[p[j][i] - p[0][i] for i in range(3)] for j in (1, 2)]
        if all(u[(i + 1) % 3] * v[(i + 2) % 3] == u[(i + 2) % 3] * v[(i + 1) % 3] for i in range(3)):
            stats["degenerates"] += 1  # Retained: source accounting never silently drops geometry.
        triangles.append(ids + [material_ids[key]])
        if spans is not None and origin["chunk"] < 0:
            raise ArtError("Triangle has no source chunk owner")
        origins.append(dict(origin))

    def command(a, b, name="-", nested=False):
        op = a >> 24
        if op == 1 and not nested:
            n, end = (a >> 12) & 255, (a >> 1) & 127
            start = end - n
            if name != "vertices" or not 0 < n <= 32 or start < 0 or end > 32 or b + n > len(vertices):
                raise ArtError("Invalid course vertex load")
            for i in range(n):
                item = list(vertices[b + i])
                item[3:5] = [signed16(item[3 + k] * state["scale"][k] >> 16) for k in range(2)]
                cache[start + i] = vertex_id(item)
            stats["loads"] += 1
        elif op == 2:
            slot, where = (a & 65535) // 2, (a >> 16) & 255
            if a & 1 or slot >= 32 or cache[slot] is None or where not in (0x10, 0x14):
                raise ArtError("Unsupported course vertex modification")
            item = list(resolved[cache[slot]])
            if where == 0x14:
                item[3:5] = [signed16(b >> 16), signed16(b & 65535)]
            else:
                item[5:9] = [(b >> shift) & 255 for shift in (24, 16, 8, 0)]
            cache[slot] = vertex_id(item)
            stats["modifies"] += 1
        elif op == 6:
            triangle(a & 0xFFFFFF)
            triangle(b)
        elif op == 5 and b == 0:
            triangle(a & 0xFFFFFF)
        elif op == 0xDE and not nested:
            if a != 0xDE000000 or b or name not in templates:
                raise ArtError("Unknown course template reference")
            stats["templates"][name] = stats["templates"].get(name, 0) + 1
            origin["template"] = name
            for index, (x, y) in enumerate(templates[name]):
                origin["template_command"] = index
                command(x, y, nested=True)
        elif op == 0xD7:
            if a & 0xFFFFF800 != 0xD7000000:
                raise ArtError("Unsupported course texture command")
            state.update(tile=(a >> 8) & 7, scale=[b >> 16, b & 65535], enabled=bool(a & 0xFE))
        elif op == 0xD9:
            state["geometry"] = (state["geometry"] & (a & 0xFFFFFF)) | b
        elif op in (0xE2, 0xE3):
            length = (a & 255) + 1
            shift = 32 - ((a >> 8) & 255) - length
            if a & 0x00FF0000 or not 0 <= shift <= 31 or length + shift > 32:
                raise ArtError("Invalid course othermode")
            mask = ((1 << length) - 1) << shift
            if b & ~mask:
                raise ArtError("Invalid course othermode bits")
            field = "other_l" if op == 0xE2 else "other_h"
            state[field] = (state[field] & ~mask) | b
        elif op == 0xFD and not nested:
            if name == "-" or b:
                raise ArtError("Unnamed course texture")
            state["texture"] = dict(name=name, image=a)
        elif op == 0xF3:
            state["load"] = [a, b]
        elif op in (0xF5, 0xF2):
            state["tiles" if op == 0xF5 else "sizes"][str((b >> 24) & 7)] = [a, b]
        elif op == 0xFC:
            state["combine"] = [a, b]
        elif op == 0xFA:
            if a != 0xFA000000:
                raise ArtError("Unexpected source primitive LOD")
            state["prim"] = b
        elif op in (0xE6, 0xE7, 0xE8) and a & 0xFFFFFF == 0 and b == 0:
            pass
        else:
            raise ArtError("Unsupported course opcode %02x" % op)

    for a, b in prelude:
        if a >> 24 != 0xE3:
            raise ArtError("Unexpected inherited race othermode command")
        command(a, b)
    for index, (a, b, name) in enumerate(commands):
        origin.update(command=index, template=None, template_command=-1, chunk=owners[index])
        command(a, b, name)
    if not triangles:
        raise ArtError("Source road contains no triangles")
    return dict(vertices=resolved, triangles=triangles, origins=origins, materials=materials, accounting=stats)


def race_othermode(data, rows):
    """Read the fixed race/background lists' inherited texture/filter settings.

    Background_DrawBackgroundSprites restores TLUT_NONE after its CI images;
    Background_DrawStars restores perspective. Neither changes the other
    inherited fields. Course_Draw then replaces render mode and combiner.
    """
    result = []
    for name in ("D_303A8F8", "D_303A9E0", "D_303AA40"):
        if rows.get(name, {}).get("type") != "GFX":
            raise ArtError("Missing inherited race setup")
        start = rows[name]["offset"]
        end = min(r["offset"] for r in rows.values() if r["offset"] > start)
        raw = bounded(data, start, end - start)
        if not raw or len(raw) % 8 or len(raw) > 512:
            raise ArtError("Invalid inherited race setup span")
        words = [struct.unpack_from(">II", raw, i) for i in range(0, len(raw), 8)]
        ends = [i for i, word in enumerate(words) if word == (0xDF000000, 0)]
        if len(ends) != 1 or any(word != (0, 0) for word in words[ends[0] + 1:]):
            raise ArtError("Invalid inherited race setup termination")
        result.extend((a, b) for a, b in words[:ends[0]] if a >> 24 == 0xE3)
    return result


def texture_payload(data, rows, name, texels):
    """A course load can consume two adjacent authored texture halves.

    In particular tunnel tiles 6/7 share a 2048-texel load whose source symbols
    describe separate 64x16 halves. Account for both instead of truncating it.
    """
    row = rows.get(name)
    if row is None or row.get("type") != "TEXTURE" or row.get("format") != "RGBA16":
        raise ArtError("Missing course texture " + name)
    width = row["width"]
    if not 0 < texels <= 2048 or texels % width:
        raise ArtError("Invalid source course texture load length")
    used, covered = [], 0
    offsets = {r["offset"]: (n, r) for n, r in rows.items()}
    while covered < texels:
        found = offsets.get(row["offset"] + covered * 2)
        if found is None:
            raise ArtError("Missing adjacent course texture half")
        symbol, part = found
        if part.get("format") != "RGBA16" or part.get("width") != width or part.get("type") != "TEXTURE":
            raise ArtError("Adjacent course texture has incompatible layout")
        size = part["width"] * part["height"]
        if covered + size > texels:
            raise ArtError("Course texture load truncates an authored image")
        used.append(symbol)
        covered += size
    raw = bounded(data, row["offset"], texels * 2)
    rgba = decode_texture(raw, "RGBA16", width, texels // width)
    return dict(width=width, height=texels // width, source_offset=row["offset"], source_symbols=used,
                source_sha256=hashlib.sha256(raw).hexdigest(), rgba_hex=rgba.hex())


def conditional_faces(result, chunks):
    """Source D_800F89D8 guards this extra unjoined pipe/tunnel pass only."""
    for tri, origin in zip(result["triangles"], result["origins"]):
        flags = chunks[origin["chunk"]]["flags"]
        shape, join = flags & 0x1C0, flags & 0x7E00
        tile = result["materials"][tri[3]]["tile"]
        base = {0x80: 2, 0x140: 6}.get(shape)
        origin["conditional_outer"] = (base is not None and join == 0 and tile == base + 1 and
                                       origin["template"] in ("D_80140F0", "D_8014138"))


def interior_identity(full, inside):
    def rows(mesh, drop):
        output = []
        for tri, origin in zip(mesh["triangles"], mesh["origins"]):
            if drop and origin.get("conditional_outer"):
                continue
            row = [origin["chunk"], [mesh["vertices"][v] for v in tri[:3]], mesh["materials"][tri[3]]]
            output.append(row)
        return output
    if rows(full, True) != rows(inside, False):
        raise ArtError("Source interior pass differs beyond classified conditional faces")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cc", choices=("gcc", "clang"), default="gcc")
    ap.add_argument("--opt", choices=("0", "2"), default="0")
    ap.add_argument("--output", type=Path, default=Path("generated/road"))
    args = ap.parse_args()
    output_location(args.output)
    raw_path = os.environ.get("FZX_TEST_ROM")
    if not raw_path:
        raise SystemExit("FZX_TEST_ROM is required locally")
    raw = Path(raw_path).read_bytes()
    record, provenance = extract_rom_course(raw)
    rom, _ = normalize_rom(raw)
    verify_source(DECOMP)
    # Always rebuild. A caller-provided capture must never receive a receipt
    # claiming that it was produced by the current source/header inputs.
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "course.bin").write_bytes(record)
    with (args.output / "build.log").open("w") as log:
        subprocess.run(["bash", str(ROOT / "ref/build.sh"), args.cc, "-O" + args.opt], cwd=ROOT,
                       env=dict(os.environ, FZX_ROAD_EXPORT="1", FZX_SOURCE_CAMERA="0"),
                       check=True, stdout=log, stderr=subprocess.STDOUT)
    host = ROOT / "ref/build" / (args.cc + "-O" + args.opt + "-road")
    exported = subprocess.check_output([str(host / "fzx-ref"), str((args.output / "course.bin").resolve())])
    (args.output / "capture.txt").write_bytes(exported)
    inside_bytes = subprocess.check_output([str(host / "fzx-ref"), str((args.output / "course.bin").resolve()), "--interior"])
    (args.output / "capture-interior.txt").write_bytes(inside_bytes)
    segment = mio0(rom[0x16C8A0:0x17B1E0])
    rows = metadata((DECOMP / YAML / "course_track_gfx.yaml").read_text())
    chunks, vertices, commands, spans = capture(exported.decode())
    names = sorted({name for a, _, name in commands if a >> 24 == 0xDE})
    templates = {name: template_words(segment, rows, name) for name in names}
    # The all-course geometry writer uses the course-preview setup. The race
    # uses the same shape functions/templates with D_8014008 instead. Bind that
    # explicit source race setup; retain the capture so this substitution is
    # reviewable. Fog factor/color and interior face selection remain dynamic.
    if commands[:2] != [(0xDE000000, 0, "D_8014040"), (0xDE000000, 0, "D_8014078")]:
        raise ArtError("Unexpected all-course setup")
    templates["D_8014008"] = template_words(segment, rows, "D_8014008")
    commands[0] = (0xDE000000, 0, "D_8014008")
    global_rows = metadata((DECOMP / YAML / "machine_custom_gfx.yaml").read_text())
    prelude = race_othermode(rom[0x17B1E0:0x1B8550], global_rows)
    result = resolve(vertices, commands, templates, prelude, spans)
    conditional_faces(result, chunks)
    ic, iv, ig, ispans = capture(inside_bytes.decode())
    if ic != chunks or ig[:2] != [(0xDE000000, 0, "D_8014040"), (0xDE000000, 0, "D_8014078")]:
        raise ArtError("Interior capture source chunk/setup mismatch")
    ig[0] = (0xDE000000, 0, "D_8014008")
    inside = resolve(iv, ig, templates, prelude, ispans)
    interior_identity(result, inside)
    result.update(format="fzx-source-road-v1", chunks=chunks, source_vertices=vertices, source_spans=spans,
                  capture_sha256=hashlib.sha256(exported).hexdigest(),
                  rom_sha256=provenance["normalized_rom_sha256"],
                  presentation=dict(prim_red_clock="255 - ((game_frame * 8) & 255)",
                                    full_detail=True, race_fog="not yet bound",
                                    setup="Course_Draw D_8014008; full-detail geometry from func_800A95B4",
                                    inside_face_selection="classified and source false-pass verified; runtime predicate not yet bound"))
    result["accounting"]["conditional_outer_triangles"] = sum(o["conditional_outer"] for o in result["origins"])
    result["accounting"]["interior_triangles"] = len(inside["triangles"])
    textures = {}
    venue = mio0(rom[0x235130:0x239A80])
    venue_rows = metadata((DECOMP / YAML / "mute_city_textures.yaml").read_text())
    for mat in result["materials"]:
        name = mat["texture"]["name"]
        if name in textures:
            continue
        source_name = name + "_235130" if name.startswith("D_A") else name
        texture_rows = venue_rows if name.startswith("D_A") else rows
        data = venue if name.startswith("D_A") else segment
        texels = ((mat["load"][1] >> 12) & 4095) + 1
        textures[name] = texture_payload(data, texture_rows, source_name, texels)
    result["textures"] = textures
    payload = (json.dumps(result, sort_keys=True, separators=(",", ":")) + "\n").encode()
    (args.output / "road.json").write_bytes(payload)
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    dependencies = {ROOT / name for name in (
        "tools/road_assets.py", "tools/road_bindings.py", "tools/import_source_art.py", "tools/import_course.py",
        "ref/build.sh", "ref/road_bridge.c", "ref/road_main.c", "ref/gen_stubs.py")}
    dependencies.update(DECOMP / YAML / name for name in ("course_track_gfx.yaml", "mute_city_textures.yaml", "machine_custom_gfx.yaml"))
    dependencies.update(DECOMP / name for name in ("src/overlays/ovl_i2/race.c", "src/overlays/ovl_i3/background.c"))
    for pre in host.glob("*.i"):
        for name in re.findall(r'^#\s+\d+\s+"([^"<>]+)"', pre.read_text(), re.M):
            item = Path(name)
            if item.is_absolute() and item.is_relative_to(ROOT) and item.is_file():
                dependencies.add(item)
    receipt = ["# fzx generation receipt v1; regenerate, never edit"]
    receipt += ["generator %s %s" % (digest(p), os.path.relpath(p, ROOT / "tools")) for p in sorted(dependencies)]
    receipt += ["input %s owner-rom" % hashlib.sha256(raw).hexdigest()]
    receipt += ["output %s %s" % (digest(args.output / name), name) for name in ("course.bin", "capture.txt", "capture-interior.txt", "road.json")]
    (args.output / "road.receipt").write_bytes(("\n".join(receipt) + "\n").encode())
    print("source road: %d chunks, %d resolved vertices, %d triangles, %d materials, %d textures" %
          (len(chunks) - 1, len(result["vertices"]), len(result["triangles"]), len(result["materials"]), len(textures)))


if __name__ == "__main__":
    main()
