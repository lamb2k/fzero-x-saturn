"""Bake one native settings background and independently movable slider per stock machine."""
import argparse
import hashlib
import json
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


def bake(captures, output):
    output.mkdir(parents=True, exist_ok=True)
    inputs, outputs, hashes, pages = [], {}, {}, {}
    # The two endpoint views uncover the background behind the middle arrow.
    endpoints = list(qualified_views(captures / "route1", ["engine-min", "engine-max"], inputs))
    middle = np.s_[78:89, 218:234]
    bare = endpoints[0][1][middle]
    if not np.array_equal(bare, endpoints[1][1][middle]):
        raise ValueError("Endpoint views disagree on the uncovered track")
    names = [f"slot{i}-settings-entry" for i in range(30)]
    for row, view in qualified_views(captures / "all30-2", names, inputs):
        identity = row["character"]
        if identity in hashes or identity not in range(30):
            raise ValueError("Duplicate or invalid machine")
        board = view.copy()
        board[middle] = bare
        # Include the black outline even where it matches the black track.
        arrow = np.zeros((11, 16), dtype=np.uint16)
        for y in range(11):
            radius = min(5, y // 2 + 1)
            arrow[y, 6-radius:7+radius] = view[middle][y, 6-radius:7+radius]
        mask = arrow != 0
        rebuilt = board[middle].copy()
        rebuilt[mask] = arrow[mask]
        if not np.array_equal(rebuilt, view[middle]):
            raise ValueError(f"Slider shape does not reconstruct slot {identity}")
        data = board.astype(">u2").tobytes() + arrow.astype(">u2").tobytes()
        pages[identity] = data
        hashes[identity] = fnv(data)
    atlas = b"".join(pages[i] for i in range(30))
    (output / "SETTINGS.PAK").write_bytes(atlas)
    outputs["SETTINGS.PAK"] = digest(atlas)
    header = ("#pragma once\n#define SETTINGS_BYTES (320u*224u*2u + 16u*11u*2u)\n"
              "static const uint32_t settings_hash[] = {" +
              ",".join(str(hashes[i])+"u" for i in range(30)) + "};\n").encode()
    (output / "machine_settings.h").write_bytes(header)
    outputs["machine_settings.h"] = digest(header)
    lines = ["# fzx generation receipt v1", f"generator {digest(Path(__file__).read_bytes())} engine_machine_settings.py"]
    lines.extend(f"input {sha} {name}" for name, sha in inputs)
    lines.extend(f"output {sha} {name}" for name, sha in outputs.items())
    (output / "machine_settings.receipt").write_bytes(("\n".join(lines)+"\n").encode())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    bake(args.captures, args.output)
