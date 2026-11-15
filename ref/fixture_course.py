#!/usr/bin/env python3
"""Write the synthetic reference course: an authored 8-point oval, not game data.

One 0x7E0-byte CourseDataDisk record, big-endian like a ROM record. Walled
road throughout, with one segment of a different road type: the original
chunk builder needs at least one change of shape or type around the loop
(with none, every segment is "continuous" and Course_Init never returns).
Feature arrays are empty (-1). The derived flags (form, joins, continuity)
are left for the original init code to compute.

Usage: fixture_course.py OUT.bin
"""
import math
import struct
import sys

RECORD_SIZE = 0x7E0
TRACK_SHAPE_WALLED_ROAD = 0x40
POINTS = 8


def record() -> bytes:
    buf = bytearray(RECORD_SIZE)
    struct.pack_into(">Bbbb", buf, 0, 0, POINTS, 0, 0)  # creator, count, venue 0, skybox 0
    buf[9:9 + 13] = b"REF FIXTURE 1"
    for i in range(POINTS):
        a = 2 * math.pi * i / POINTS
        x, z = 6000.0 * math.cos(a), 3500.0 * math.sin(a)
        info = TRACK_SHAPE_WALLED_ROAD | (1 if i == 5 else 0)
        struct.pack_into(">fffhhI", buf, 0x20 + i * 20, x, 40.0 * math.sin(2 * a), z, 160, 160, info)
        struct.pack_into(">h", buf, 0x520 + i * 2, (0, 4, 8, 4, 0, -4, -8, -4)[i])
    for offset in range(0x5A0, RECORD_SIZE, 0x40):
        buf[offset:offset + 64] = b"\xff" * 64
    return bytes(buf)


if __name__ == "__main__":
    with open(sys.argv[1], "wb") as fh:
        fh.write(record())
