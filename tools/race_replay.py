#!/usr/bin/env python3
"""Replay a Saturn race (tools/emu/race_log.ps1 output) through the host reference.

  tools/race_replay.py EVIDENCE_DIR --course RECORD --rom ROM [--ref ref/build/gcc-O0/fzx-ref]

Turns EVIDENCE_DIR/inputs.hex (one N64 input word per tick) into a host input script, runs the
reference with a trace, and prints:
  - the race time at which the host sees racer 0 finish, and the Saturn's frozen race time;
  - the largest distance between the Saturn's pose samples and the host's position at the same
    tick (the samples are two debugger reads apart, so a tick or two of travel is expected).
Exit status 1 when the finish times differ. Local only with the owner's ROM: nothing it writes
belongs in the repository.
"""
import argparse
import math
import os
import subprocess
import sys

NAMES = [("A", 0x8000), ("B", 0x4000), ("Z", 0x2000), ("S", 0x1000), ("L", 0x20), ("R", 0x10),
         ("CU", 8), ("CD", 4), ("CL", 2), ("CR", 1)]
FINISHED = 1 << 25


def token(word):
    buttons = word >> 16
    names = "+".join(n for n, m in NAMES if buttons & m) or "-"
    sx, sy = (word >> 8) & 0xFF, word & 0xFF
    sx = sx - 256 if sx > 127 else sx
    sy = sy - 256 if sy > 127 else sy
    return f"{names} {sx} {sy}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("evidence")
    ap.add_argument("--course", required=True)
    ap.add_argument("--rom", required=True)
    ap.add_argument("--ref", default=os.path.join(os.path.dirname(__file__), "..", "ref", "build", "gcc-O0", "fzx-ref"))
    a = ap.parse_args()

    words = [int(line, 16) for line in open(os.path.join(a.evidence, "inputs.hex")) if line.strip()]
    lines, prev, run = [], None, 0
    for w in words:
        t = token(w)
        if t == prev:
            run += 1
        else:
            if prev is not None:
                lines.append(f"{run} {prev}")
            prev, run = t, 1
    if prev is not None:
        lines.append(f"{run} {prev}")
    script = os.path.join(a.evidence, "replay.txt")
    trace = os.path.join(a.evidence, "replay.tr")
    with open(script, "w") as f:
        f.write("\n".join(lines) + "\n")
    subprocess.run([a.ref, "--course", a.course, "--byte-order", "big", "--rom", a.rom, "--inputs", script,
                    "--trace", trace], check=True, stdout=subprocess.DEVNULL)

    rows, host_finish = {}, None
    for line in open(trace):
        if line.startswith("#") or line.startswith("hash"):
            continue
        f = line.rstrip("\n").split("\t")
        tick = int(f[0])
        rows[tick] = (float(f[22]), float(f[23]), float(f[24]))
        if host_finish is None and int(f[20], 16) & FINISHED:
            host_finish = (tick, int(f[19]))

    worst, worst_tick, saturn_time = 0.0, None, None
    for line in open(os.path.join(a.evidence, "samples.txt")):
        parts = line.split()
        n, pos = int(parts[0]), tuple(float(v) for v in parts[1:4])
        fields = dict(p.split("=") for p in parts[5:])
        if int(fields["flags"], 16) & FINISHED:
            saturn_time = int(fields["time"])
        if n - 1 in rows:
            d = math.dist(pos, rows[n - 1])
            if d > worst:
                worst, worst_tick = d, n
    print(f"ticks replayed {len(words)}; largest sample gap {worst:.1f} units at tick {worst_tick}")
    print(f"host finish: {host_finish}; saturn finish time: {saturn_time}")
    if host_finish is None and saturn_time is None:
        return 0
    if host_finish is None or saturn_time != host_finish[1]:
        print("FINISH TIMES DIFFER")
        return 1
    print("finish times match")
    return 0


if __name__ == "__main__":
    sys.exit(main())
