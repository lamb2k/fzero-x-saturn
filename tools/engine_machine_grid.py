"""Bake the thirty qualified C8 machine-grid views into a shared board and CLUT sprites."""
import argparse
import hashlib
import itertools
import json
import struct
from pathlib import Path

import numpy as np
from road_pack import fnv


def digest(data):
    return hashlib.sha256(data).hexdigest()


def bake(capture, output):
    qualification = (capture / "state-command-qualification.json").read_bytes()
    check = json.loads(qualification)
    if check["status"] != "PASS" or digest((capture / "receipt.json").read_bytes()) != check["capture_receipt_sha256"]:
        raise ValueError("Unqualified machine grid")
    rows = {r["view"]: r for r in check["rows"]}
    inputs = {"state-command-qualification.json": qualification}
    views, ids = [], []
    for i in range(30):
        key = f"slot{i}-select"
        row = rows[key]
        source = capture / "snapshots" / key
        if row["screen"] != 0 or digest(source.with_suffix(".source-frame").read_bytes()) != row["source_snapshot_sha256"]:
            raise ValueError(f"Incorrect C8 source frame for {key}")
        rgba = source.with_suffix(".rgba").read_bytes()
        inputs[key + ".rgba"] = rgba
        rgb = np.frombuffer(rgba, np.uint8).reshape(224, 320, 4).astype(np.uint16) >> 3
        views.append(0x8000 | rgb[:,:,0] | (rgb[:,:,1] << 5) | (rgb[:,:,2] << 10))
        ids.append(row["character"])
    if sorted(ids) != list(range(30)):
        raise ValueError("Incomplete C8 roster")
    # For each pixel choose its most frequent value. This removes the selection
    # marker and leaves the stationary original miniatures on a shared board.
    stack = np.sort(views, axis=0)
    base = stack[0].copy()
    longest = np.ones_like(base)
    run = longest.copy()
    for a, b in zip(stack, stack[1:]):
        run = np.where(a == b, run + 1, 1)
        base = np.where(run > longest, b, base)
        longest = np.maximum(longest, run)
    bank = bytearray()
    sprites, starts, cache = [], [], {}
    for view in views:
        starts.append(len(sprites))
        delta = np.where(view == base, 0, view)
        replay = base.copy()
        def patch(x, y, w, h):
            block = delta[y:y+h, x:x+w]
            if not block.any():
                return
            colors = np.unique(block[block != 0])
            if len(colors) > 15:
                if w > 8:
                    cut = max(8, (w // 16) * 8)
                    patch(x, y, cut, h)
                    patch(x+cut, y, w-cut, h)
                else:
                    cut = h // 2
                    patch(x, y, w, cut)
                    patch(x, y+cut, w, h-cut)
                return
            palette = np.zeros(16, dtype=">u2")
            palette[1:1+len(colors)] = colors
            idx = np.where(block, 1 + np.searchsorted(colors, block), 0)
            packed = ((idx[:,::2] << 4) | idx[:,1::2]).astype(np.uint8)
            data = palette.tobytes() + packed.tobytes()
            if data not in cache:
                cache[data] = len(bank)
                bank.extend(data)
                bank.extend(bytes(-len(bank) % 8))
            sprites.append((cache[data], x, y, w, h))
            dst = replay[y:y+h, x:x+w]
            dst[block != 0] = palette[idx[block != 0]]
        for y in range(0, 224, 8):
            active = delta[y:y+8].reshape(8,40,8).any(axis=(0,2))
            x = 0
            while x < 40:
                if not active[x]:
                    x += 1
                    continue
                end = x + 1
                while end < 40 and active[end]:
                    end += 1
                patch(8*x, y, 8*(end-x), 8)
                x = end
        if not np.array_equal(replay, view):
            raise ValueError("Grid replay mismatch")
    starts.append(len(sprites))
    # Descriptors are cold menu data too. Keep them in the checked cartridge
    # bank, leaving the SH-2 executable with just the 31 offsets and slot IDs.
    art_bytes = len(bank)
    for at, x, y, w, h in sprites:
        bank.extend(struct.pack(">HHHBB", at//8, x, y, w//8, h))
    background_at = len(bank)
    for color, group in itertools.groupby(base.flat):
        count = sum(1 for _ in group)
        bank.extend(struct.pack(">HH", count, color))
    maximum = max(b-a for a,b in zip(starts, starts[1:]))
    header = (f"#pragma once\n#define GRID_BYTES {len(bank)}u\n#define GRID_HASH {fnv(bank)}u\n"
              f"#define GRID_BACKGROUND_AT {background_at}u\n"
              f"#define GRID_ART_BYTES {art_bytes}u\n#define GRID_MAX_SPRITES {maximum}u\n"
              "static const uint16_t grid_first[] = {" + ",".join(map(str,starts)) + "};\n"
              "static const uint8_t grid_identity[] = {" + ",".join(map(str,ids)) + "};\n").encode()
    output.mkdir(parents=True, exist_ok=True)
    outputs = {"MACHGRID.PAK": bank, "machine_grid.h": header}
    lines = ["# fzx generation receipt v1", f"generator {digest(Path(__file__).read_bytes())} engine_machine_grid.py"]
    lines.extend(f"input {digest(data)} {name}" for name,data in inputs.items())
    for name, data in outputs.items():
        (output / name).write_bytes(data)
        lines.append(f"output {digest(data)} {name}")
    (output / "machine_grid.receipt").write_bytes(("\n".join(lines)+"\n").encode())
    print(f"30 boards, {len(bank)} cartridge bytes, {art_bytes + 143360} VRAM bytes, {maximum+1} commands")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    bake(args.capture, args.output)
