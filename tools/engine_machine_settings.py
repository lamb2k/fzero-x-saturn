"""Bake one native settings background with four paint schemes and a movable slider per stock machine."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

import numpy as np

from road_pack import fnv


def digest(data):
    return hashlib.sha256(data).hexdigest()


def qualified_views(folder, names, inputs):
    proof = (folder / "state-command-qualification.json").read_bytes()
    check = json.loads(proof)
    if check["status"] != "PASS" or digest((folder / "receipt.json").read_bytes()) != check["capture_receipt_sha256"]:
        raise ValueError("Unqualified settings capture")
    inputs.append((folder.name + "/state-command-qualification.json", digest(proof)))
    rows = {r["view"]: r for r in check["rows"]}
    for name in names:
        source = folder / "snapshots" / name
        row = rows[name]
        if row["screen"] != 1 or digest(source.with_suffix(".source-frame").read_bytes()) != row["source_snapshot_sha256"]:
            raise ValueError(f"Wrong source state for {name}")
        rgba = source.with_suffix(".rgba").read_bytes()
        inputs.append((folder.name + "/" + name + ".rgba", digest(rgba)))
        rgb = np.frombuffer(rgba, np.uint8).reshape(224, 320, 4).astype(np.uint16) >> 3
        yield row, 0x8000 | rgb[:,:,0] | (rgb[:,:,1] << 5) | (rgb[:,:,2] << 10)


def split(view, middle, bare):
    board = view.copy()
    board[middle] = bare
    arrow = np.zeros((11, 16), dtype=np.uint16)
    for y in range(11):
        radius = min(5, y // 2 + 1)
        arrow[y, 6-radius:7+radius] = view[middle][y, 6-radius:7+radius]
    replay = np.where(arrow, arrow, bare)
    if not np.array_equal(replay, view[middle]):
        raise ValueError("Slider does not reconstruct the settings view")
    return board, arrow


def pack_skins(views):
    base = views[0][0]
    bank = bytearray(base.astype(">u2").tobytes())
    for _, arrow in views:
        bank.extend(arrow.astype(">u2").tobytes())
    descriptors = bytearray()
    first = [0, 0]
    for board, _ in views[1:]:
        replay = base.copy()
        for y in range(0, 224, 8):
            active = (base[y:y+8] != board[y:y+8]).reshape(8, 40, 8).any(axis=(0, 2))
            edges = np.flatnonzero(np.diff(active, prepend=False, append=False))
            for left, right in edges.reshape(-1, 2):
                rect = np.s_[y:y+8, 8*left:8*right]
                descriptors.extend(struct.pack(">HHHBB", len(bank)//8, 8*left, y, right-left, 8))
                pixels = board[rect].astype(">u2").tobytes()
                bank.extend(pixels)
                replay[rect] = np.frombuffer(pixels, ">u2").reshape(8, -1)
        if not np.array_equal(replay, board):
            raise ValueError("Skin patches do not reconstruct the source view")
        first.append(len(descriptors)//8)
    return bank + descriptors, len(bank), first


def bake(captures, output, models):
    output.mkdir(parents=True, exist_ok=True)
    inputs, outputs, pages = [], {}, {}
    endpoints = list(qualified_views(captures / "route1", ["engine-min", "engine-max"], inputs))
    middle = np.s_[78:89, 218:234]
    bare = endpoints[0][1][middle]
    if not np.array_equal(bare, endpoints[1][1][middle]):
        raise ValueError("Endpoint views disagree on the uncovered track")
    names = [f"slot{i}-settings-entry" for i in range(30)]
    variants = qualified_views(captures / "stock-skins",
                               [f"slot{i}-skin{j}" for i in range(30) for j in (1, 2, 3)], inputs)
    for row, view in qualified_views(captures / "all30-2", names, inputs):
        identity = row["character"]
        if identity in pages or identity not in range(30) or row["skin"] != 0:
            raise ValueError("Unexpected base machine or paint scheme")
        views = [split(view, middle, bare)]
        for skin in (1, 2, 3):
            other, image = next(variants)
            if (other["character"], other["skin"]) != (identity, skin):
                raise ValueError("Source machine or paint scheme mismatch")
            views.append(split(image, middle, bare))
        pages[identity] = pack_skins(views)
    atlas, info = bytearray(), []
    maximum = commands = 0
    for identity in range(30):
        bank, descriptors, first = pages[identity]
        info.append("{%du,%du,%du,%du,{%s}}" % (len(atlas), len(bank), fnv(bank), descriptors,
                                                   ",".join(map(str, first))))
        atlas.extend(bank)
        maximum = max(maximum, len(bank))
        commands = max(commands, 2 + max(b-a for a,b in zip(first, first[1:])))
    header = (f"#pragma once\n#define SETTINGS_FILE_BYTES {len(atlas)}u\n"
              f"#define SETTINGS_MAX_BYTES {maximum}u\n#define SETTINGS_MAX_COMMANDS {commands}u\n"
              "typedef struct { uint32_t offset, bytes, hash, descriptors; uint16_t first[5]; } settings_bank_t;\n"
              "static const settings_bank_t settings_banks[] = {\n" + ",\n".join(info) + "};\n")
    source = (models / "machine_models_source.json").read_bytes()
    if f"output {digest(source)} machine_models_source.json" not in (models / "machine_models.receipt").read_text().splitlines():
        raise ValueError("Model paint scheme manifest differs from receipt")
    inputs.append(("machine_models_source.json", digest(source)))
    colors = {m["character"]: np.asarray(m["skins"]) for m in json.loads(source)["machines"]}
    body = []
    for identity in range(30):
        values = colors[identity]
        if values.shape != (4,3) or values.dtype.kind not in "iu" or values.min() < 0 or values.max() > 255:
            raise ValueError("Invalid machine body colours")
        body.append("{" + ",".join("{%d,%d,%d}" % tuple(rgb) for rgb in values) + "}")
    header += "static const uint8_t settings_rgb[30][4][3] = {\n" + ",\n".join(body) + "};\n"
    for name, data in [("SETTINGS.PAK", atlas), ("machine_settings.h", header.encode())]:
        (output / name).write_bytes(data)
        outputs[name] = digest(data)
    lines = ["# fzx generation receipt v1", f"generator {digest(Path(__file__).read_bytes())} engine_machine_settings.py"]
    lines.extend(f"input {sha} {name}" for name, sha in inputs)
    lines.extend(f"output {sha} {name}" for name, sha in outputs.items())
    (output / "machine_settings.receipt").write_bytes(("\n".join(lines)+"\n").encode())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--models", type=Path, default=Path(__file__).resolve().parents[1] / "generated/machine_menu_models")
    args = parser.parse_args()
    bake(args.captures, args.output, args.models)
