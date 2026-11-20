#!/usr/bin/env python3
"""Physics pack baker: physics source (fzx-ref --bake) to the E1 pack (saturn/physics_native.c).

The source is what the original race set-up builds from a course record and the
machine table: the course spline after Course_Init (control points, up vectors,
tension, half widths, arc lengths from Course_Init's lengthFromStart, quarter
marks), the surface zones, jumps and mines Race_Init adds, the machine's derived
constants and racer 0's starting state. Every value is a C99 hex float, so the
conversion to fixed point is exact up to the one rounding below.

Tube courses (pipe, cylinder or half-pipe spans; the source has "join" lines) get format
version 3: after the mines, one join record per node (the t where the join with the previous
span ends, where the one with the next starts, Q30, and the join scale, Q30; a node without
joins gets 0, 1, 1). The source marks a side without a join with t -99 or 99 and a span
without joins with scale -1: those become -1 and 1.5 (never reached by t in [0, 1)) and
scale 1. Every other course keeps version 2 byte for byte.

Output: big-endian 32-bit words, round to nearest (ties away from zero):
    Q16 positions, widths, speeds, forces, energies;  Q30 unit vectors, tension, t;
    Q12 arc lengths along the line (node start, span length, mark lengths, lap distance).
A line-space user (the rivals' field) converts Q12 to its Q8 by a rounded shift of 4.

The pack is derived from the owner's ROM and course data: write it outside the
repository and never commit or print it. A receipt (--receipt) records the source
and pack hashes and the format version, not the data.
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

MAGIC = 0x465A5850  # "FZXP"
VERSION = 2
VERSION_TUBES = 3
ZONES_BY_SIDE = 0x8000000
SHAPE_MASK = 0x1C0


def fx(value, bits):
    v = value * (1 << bits)
    r = int(v + 0.5) if v >= 0 else -int(-v + 0.5)
    if not -(1 << 31) <= r < (1 << 31):
        raise ValueError("value %r does not fit Q%d" % (value, bits))
    return r


def parse(path):
    src = {"seg": [], "zone": [], "jump": [], "mine": [], "join": []}
    for line in Path(path).read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        f = line.split()
        key, a, b, vals = f[0], int(f[1]), int(f[2]), [float.fromhex(x) for x in f[3:]]
        if key.startswith("error"):
            raise ValueError("source reports %s at %d" % (key, a))
        if key in src:
            src[key].append((a, b, vals))
        else:
            src[key] = (a, b, vals)
    return src


def build(src, laps=3):
    count, _, (length,) = src["course"]
    segs = src["seg"]
    if len(segs) != count or [s[0] for s in segs] != list(range(count)):
        raise ValueError("segments out of order")
    per = lambda kind: [[x for x in src[kind] if x[0] == i] for i in range(count)]
    zones, jumps, mines = per("zone"), per("jump"), per("mine")
    tubes = {}
    for a, _, v in src.get("join", []):
        if not 0 <= a < count or len(v) != 3:
            raise ValueError("bad join record")
        prev_end, next_start = max(v[0], -1.0), min(v[1], 1.5)
        if not v[2] > 0 and (prev_end > 0 or next_start < 1):
            raise ValueError("join without a scale at %d" % a)
        tubes[a] = (prev_end, next_start, v[2] if v[2] > 0 else 1.0)
    w = [MAGIC, VERSION_TUBES if tubes else VERSION, count, len(src["zone"]), len(src["jump"]), len(src["mine"]), fx(length, 12), laps]
    m = src["machine"][2]
    q16 = lambda *k: [fx(m[i], 16) for i in k]
    q30 = lambda *k: [fx(m[i], 30) for i in k]
    # force_low force_slide force_high crossover boost_mul plate_mul slide_max slide_decay slew_base
    w += q16(0, 1, 2, 3, 4, 5, 6, 7, 8)
    w += q30(9, 10)                     # turn_free turn_grip
    w += q16(11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24)
    zf = rf = mf = 0
    for i, (_, info, v) in enumerate(segs):
        info &= 0xFFFFFFFF
        w += [fx(x, 16) for x in v[0:3]] + [fx(x, 30) for x in v[3:6]] + [fx(v[8], 30)]
        w += [fx(v[6], 16), fx(v[7], 16), fx(v[9], 12), fx(v[10], 12)]
        w += [fx(x, 16) for x in v[11:20]] + [fx(x, 12) for x in v[20:23]]
        w += [((info & SHAPE_MASK) >> 6) | ((1 if info & ZONES_BY_SIDE else 0) << 8)]
        w += [zf | (len(zones[i]) << 16), rf | (len(jumps[i]) << 16), mf | (len(mines[i]) << 16)]
        zf, rf, mf = zf + len(zones[i]), rf + len(jumps[i]), mf + len(mines[i])
    for group in zones:
        for _, kind, v in group:
            w += [kind, fx(v[0], 30), fx(v[1], 30)] + [fx(x, 16) for x in v[2:8]]
    for group in jumps:
        for _, _, v in group:
            w += [fx(x, 16) for x in v[0:3]] + [fx(x, 30) for x in v[3:12]] + [fx(x, 16) for x in v[12:15]]
    for group in mines:
        for _, _, v in group:
            w += [fx(x, 16) for x in v]
    if tubes:
        for i in range(count):
            prev_end, next_start, scale = tubes.get(i, (0.0, 1.0, 1.0))
            w += [fx(prev_end, 30), fx(next_start, 30), fx(scale, 30)]
    flags, node, st = src["state"]
    lap, laps_done, ticks, race_time, intro, _boost = (int(x) for x in st)
    w += [flags & 0xFFFFFFFF, node, intro, lap, laps_done, ticks, race_time]
    w += [fx(x, 16) for x in src["pos"][2] + src["vel"][2]]
    w += [fx(x, 30) for x in src["body"][2] + src["road"][2] + src["gravity_up"][2] + src["tilt_up"][2] +
          src["ground_up"][2]]
    w += [fx(x, 16) for x in src["fwd"][2] + src["spos"][2] + src["disp"][2] + src["ground"][2] + src["bvel"][2]]
    s = src["scalars"][2]
    # t prop fwd_mag height half_left half_right lap_dist laps_dist race_dist speed energy hover accel spin_steer
    w += [fx(s[0], 30), fx(s[1], 30)] + [fx(x, 16) for x in s[2:6]] + [fx(x, 12) for x in s[6:9]]
    w += [fx(x, 16) for x in s[9:14]]
    w += [int(_boost)]
    d = src.get("dynamics", (0, 0, [0.0] * 21))[2]
    # spin, attack, attack direction (left +1, right -1), attack angle and rate, tap
    # timers L and R, free, hit, slide count, tap window, lap times x3, done time
    w += [int(d[0]), int(d[1]), 1 if int(d[2]) == 0 else -1] + [int(x) for x in d[3:15]]
    # slide boost, lean force, ramp boost, spring, wall bounce (Q16); pitch input (Q30)
    w += [fx(x, 16) for x in d[15:20]] + [fx(d[20], 30)]
    # N64 buttons applied on the tick the state follows (0 at the start): restart edges.
    w += [src["input"][0] if "input" in src else 0]
    return struct.pack(">%di" % len(w), *[x - (1 << 32) if x >= (1 << 31) else x for x in w])


def line_hash(src):
    """course-line-1 agreement hash (ref/course_line.h): FNV-1a 64 over big-endian int32 words:
    lap length Q12, segment count, each segment start Q12, the same rounding as the pack."""
    count, _, (length,) = src["course"]
    words = [fx(length, 12), count] + [fx(v[10], 12) for _, _, v in src["seg"]]
    h = 0xCBF29CE484222325
    for w in words:
        for b in struct.pack(">I", w & 0xFFFFFFFF):
            h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("source", type=Path)
    ap.add_argument("pack", type=Path)
    ap.add_argument("--laps", type=int, default=3)
    ap.add_argument("--receipt", type=Path)
    args = ap.parse_args()
    data = build(parse(args.source), args.laps)
    args.pack.write_bytes(data)
    src = parse(args.source)
    if args.receipt:
        args.receipt.write_text(json.dumps({
            "tool": "tools/physics_pack.py", "format": "FZXP v%d" % (VERSION_TUBES if src["join"] else VERSION), "laps": args.laps,
            "source_sha256": hashlib.sha256(args.source.read_bytes()).hexdigest(),
            "pack_sha256": hashlib.sha256(data).hexdigest(), "pack_bytes": len(data),
            "course_line_hash": "%016x" % line_hash(src)}, indent=1) + "\n")
    print("physics pack: %d bytes, course-line-1 hash %016x" % (len(data), line_hash(src)))


if __name__ == "__main__":
    sys.exit(main())
