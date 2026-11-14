#!/usr/bin/env python3
"""Write the reference's synthetic course record (card G1.2). Authored data, not game data.

A closed walled-road loop: an ellipse of control points at constant height, no bank, no
features, explicit non-derived flags only (the game derives joins, forms and continuity at
init). Segment 0 is WALLED_ROAD_1 and the rest WALLED_ROAD_0: the chunk builder needs at
least one shape or type change around the loop, as every real course has. One pit area
(PIT_BOTH) at point 3 and one dash plate (DASH_MIDDLE) at point 0. The bot never registers
this pit (pit recharge is covered on Mute City 1 in the ROM lane).
The dash plate sits where the road runs along z: its detection vectors are axis-aligned
(as built by Course_EffectsViewInteractDataInit), so on a diagonal stretch of this ellipse
the game never registers them. 
Written big-endian, as a CourseDataDisk record is stored in the ROM, with the
original checksum computed by tools/import_course.py and verified by parsing it back.

Usage: make_fixture.py OUT.bin [--points 12] [--a 12000] [--b 8000]
"""
from __future__ import annotations

import argparse
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import import_course as imp  # noqa: E402

TRACK_SHAPE_WALLED_ROAD = 0x40
WALLED_ROAD_0, WALLED_ROAD_1 = 0, 1
PIT_POINT, PIT_BOTH = 3, 0
DASH_POINT, DASH_MIDDLE = 0, 0
RADIUS = 150


def record(points: int, a: float, b: float) -> bytes:
    buf = bytearray(imp.RECORD_SIZE)
    struct.pack_into(">Bbbb", buf, 0, 0, points, 0, 0)  # creator, count, venue, skybox
    buf[9:9 + 13] = b"REF FIXTURE 2"
    parsed_points = []
    for i in range(points):
        angle = 2 * math.pi * i / points
        position = (imp.f32(a * math.cos(angle)), 0.0, imp.f32(b * math.sin(angle)))
        info = TRACK_SHAPE_WALLED_ROAD | (WALLED_ROAD_1 if i == 0 else WALLED_ROAD_0)
        struct.pack_into(">fffhhI", buf, 0x20 + i * 0x14, *position, RADIUS, RADIUS, info)
        struct.pack_into(">h", buf, 0x520 + i * 2, 0)
        parsed_points.append({"position": position, "radius_left": RADIUS, "radius_right": RADIUS,
                              "track_segment_info": info, "bank_degrees": 0})
    for offset, _ in imp.FEATURES.values():
        buf[offset:offset + 64] = b"\xff" * 64
    features = {name: [-1] * points for name in imp.FEATURES}
    features["pit"][PIT_POINT] = PIT_BOTH
    features["dash"][DASH_POINT] = DASH_MIDDLE
    for name, values in features.items():
        offset = imp.FEATURES[name][0]
        for i, value in enumerate(values):
            buf[offset + i] = value & 0xFF
    struct.pack_into(">I", buf, 4, imp.course_checksum(parsed_points, features))
    data = bytes(buf)
    imp.parse_record(data, "big")  # raises if the record or its checksum is not valid
    return data


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("out", type=Path)
    parser.add_argument("--points", type=int, default=12)
    parser.add_argument("--a", type=float, default=12000.0)
    parser.add_argument("--b", type=float, default=8000.0)
    args = parser.parse_args()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(record(args.points, args.a, args.b))
    print(f"fixture: {args.points}-point walled-road loop -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
