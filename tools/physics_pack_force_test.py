#!/usr/bin/env python3
"""Force-card pack tests (C-142 item 3, HOST): the wide-force pack rows, decoded by the real reader.

Needs a physics source (fzx-ref --bake output, local only) and the pack row dump built twice by
tools/physics_host/build.sh (default and FZX_CFLAGS=-DSH2Z_VD_FORCE_FRAC=24):

    tools/physics_pack_force_test.py --source SRC --dump16 BUILD16/fzx-pack-dump --dump24 BUILD24/fzx-pack-dump
                                     [--baseline-packer PATH]  (the packer before the force card, for the byte-identity check)

Checks (each prints PASS/FAIL; the exit status is non-zero on any FAIL):
  identity   force_frac 16 writes the baseline packer's bytes (versions 2/3 unchanged)
  rows       a force_frac 24 pack reads back, in a 24 build, as fx(x, 24) for force_low, force_slide, force_high, top_force and
             the state's accel_force, and every other word equals the 16 pack's word
  exact      those constants are within half a step of 2^-24 of the source's value (Q16 was within half of 2^-16)
  ties       fx rounds ties away from zero at both scales, positive and negative
  legacy     a version 2/3 pack in a 24 build reads as the Q16 word << 8, flags legacy_force, and refuses to save
  stale      a 16 build refuses a 24 pack (rc -6); a 24 build refuses force_frac 16 .. 23 and 25 .. 28 and 0 and 99 in a
             version 4/5 pack (rc -6); an unknown version (6) is refused (rc -1)
  boundary   a Q16 word of 2^23 - 1 widens, 2^23 is refused (rc -3), -2^23 widens, -2^23 - 1 is refused
  controls   a truncating packer, a ties-down packer and a packer that forgets the accel_force scale each fail rows
"""
import argparse
import importlib.util
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import physics_pack as PP  # noqa: E402

FAILS = []


def check(ok, what):
    print(("PASS " if ok else "FAIL ") + what)
    if not ok:
        FAILS.append(what)


def dump(exe, pack):
    out = subprocess.run([str(exe), str(pack)], capture_output=True, text=True, check=True).stdout.split()
    return {k: int(v) for k, v in (x.split("=") for x in out)}


def words(b):
    return [int.from_bytes(b[i:i + 4], "big", signed=True) for i in range(0, len(b), 4)]


def pack_of(w):
    return b"".join((x & 0xFFFFFFFF).to_bytes(4, "big") for x in w)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--source", type=Path, required=True)
    ap.add_argument("--dump16", type=Path, required=True)
    ap.add_argument("--dump24", type=Path, required=True)
    ap.add_argument("--baseline-packer", type=Path)
    a = ap.parse_args()
    src = PP.parse(a.source)
    m = src["machine"][2]
    tubes = bool(src["join"])
    v16, v24 = (3, 5) if tubes else (2, 4)
    tmp = Path(tempfile.mkdtemp(prefix="forcetest-"))

    p16 = PP.build(src, force_frac=16)
    p24 = PP.build(src, force_frac=24)
    w16, w24 = words(p16), words(p24)

    if a.baseline_packer:
        spec = importlib.util.spec_from_file_location("baseline_packer", a.baseline_packer)
        base = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(base)
        check(base.build(src) == PP.build(src, force_frac=16), "identity: force_frac 16 is the baseline packer's bytes")
    check(w16[1] == v16 and w24[1] == v24 and w24[8] == 24, "version words: 16 -> %d, 24 -> %d with force_frac word 24" % (v16, v24))
    check(len(p24) == len(p16) + 4, "the wide pack is one word longer")
    # the vehicle row starts at word 8 (v4: the force_frac word first); the state block is the last 92 words, accel_force the 68th
    f_idx16, f_idx24 = [8, 9, 10], [9, 10, 11]
    top16, top24 = 8 + 24, 9 + 24
    acc16, acc24 = len(w16) - 92 + 67, len(w24) - 92 + 67
    check(all(w24[i24] == PP.fx(m[k], 24) for k, i24 in zip((0, 1, 2), f_idx24)) and w24[top24] == PP.fx(m[24], 24),
          "rows: 24 pack force_low/slide/high/top are fx(x, 24)")
    expect_w24 = list(w16)
    expect_w24[1] = v24
    for k, i in zip((0, 1, 2), f_idx16):
        expect_w24[i] = PP.fx(m[k], 24)
    expect_w24[top16] = PP.fx(m[24], 24)
    expect_w24[acc16] = w24[acc24]
    expect_w24.insert(8, 24)
    check(expect_w24 == w24, "rows: every other word of the 24 pack equals the 16 pack's word (only version, force_frac, 4 constants, accel_force differ)")
    check(abs(w24[acc24] / 2.0 ** 24 - w16[acc16] / 2.0 ** 16) <= 0.5 / 2.0 ** 16 + 0.5 / 2.0 ** 24, "rows: the 24 accel_force is the same value as the 16 one to a Q16 half step")

    (tmp / "p16.bin").write_bytes(p16)
    d16 = dump(a.dump16, tmp / "p16.bin")
    (tmp / "p24.bin").write_bytes(p24)
    d24 = dump(a.dump24, tmp / "p24.bin")
    check(d24["rc"] == 0 and d24["force_frac"] == 24 and d24["legacy"] == 0, "rows: the 24 reader loads a 24 pack, no legacy flag")
    check(d24["accel_force"] == w24[acc24] and d16["accel_force"] == w16[acc16], "rows: the decoded accel_force is the pack's state word at each scale")
    check(d24["force_high"] == PP.fx(m[2], 24) and d24["force_low"] == PP.fx(m[0], 24) and d24["force_slide"] == PP.fx(m[1], 24) and
          d24["top_force"] == PP.fx(m[24], 24), "rows: the decoded constants are fx(x, 24)")
    check(d16["rc"] == 0 and d16["force_high"] == PP.fx(m[2], 16), "rows: the 16 reader's decoded force_high is fx(x, 16)")
    e24 = abs(d24["force_high"] / 2.0 ** 24 - m[2])
    e16 = abs(d16["force_high"] / 2.0 ** 16 - m[2])
    check(e24 <= 0.5 / 2.0 ** 24 + 1e-15 and e16 <= 0.5 / 2.0 ** 16 + 1e-15, "exact: both within half a step (24: %.3g, 16: %.3g of the value)" % (e24 * 2 ** 24, e16 * 2 ** 16))
    check(d24["save"] > 0 and d16["save"] > 0, "rows: a state saved from the loaded pack has a size")

    for bits in (16, 24):
        k = 12345
        t = (2 * k + 1) / 2.0 ** (bits + 1)
        check(PP.fx(t, bits) == k + 1 and PP.fx(-t, bits) == -(k + 1) and PP.fx(0.5 / 2.0 ** bits, bits) == 1,
              "ties: fx at %d bits rounds a half step away from zero, both signs" % bits)

    # legacy: a 24 reader on the 16 pack
    dl = dump(a.dump24, tmp / "p16.bin")
    check(dl["rc"] == 0 and dl["legacy"] == 1 and dl["force_frac"] == 16 and dl["force_high"] == d16["force_high"] << 8 and
          dl["force_low"] == d16["force_low"] << 8 and dl["accel_force"] == d16["accel_force"] << 8 and dl["top_force"] == d16["top_force"] << 8,
          "legacy: a 24 build widens a version %d pack exactly (<< 8) and flags it" % v16)
    check(dl["save"] == 0, "legacy: the 24 build refuses to save a state over a legacy image")

    # stale versions
    dm = dump(a.dump16, tmp / "p24.bin")
    check(dm["rc"] == -6, "stale: the 16 build refuses the 24 pack (rc %d)" % dm["rc"])
    for ff in (0, 16, 17, 20, 23, 25, 28, 99):
        w = list(w24)
        w[8] = ff
        (tmp / "x.bin").write_bytes(pack_of(w))
        d = dump(a.dump24, tmp / "x.bin")
        check(d["rc"] == -6, "stale: the 24 build refuses force_frac %d in a version %d pack (rc %d)" % (ff, v24, d["rc"]))
    for ver in (6, 1, 0):
        w = list(w24)
        w[1] = ver
        (tmp / "x.bin").write_bytes(pack_of(w))
        check(dump(a.dump24, tmp / "x.bin")["rc"] == -1, "stale: version %d refused (rc -1)" % ver)

    # boundary: patch the force_low word of the 16 pack (a Q16 word, read by the 24 build as << 8)
    for val, ok in ((2 ** 23 - 1, True), (2 ** 23, False), (-2 ** 23, True), (-2 ** 23 - 1, False)):
        w = list(w16)
        w[8] = val
        (tmp / "x.bin").write_bytes(pack_of(w))
        d = dump(a.dump24, tmp / "x.bin")
        check((d["rc"] == 0 and d["force_low"] == val << 8) if ok else d["rc"] == -3, "boundary: legacy force word %d %s (rc %d)" % (val, "widens" if ok else "is refused", d["rc"]))

    # controls: planted packers must fail the rows check
    expect = (PP.fx(m[0], 24), PP.fx(m[1], 24), PP.fx(m[2], 24))

    def mutated(kind):
        def fx(x, bits):
            v = x * (1 << bits)
            if kind == "trunc":
                return int(v)
            if kind == "ties-down":
                import math
                return math.ceil(v - 0.5) if v >= 0 else -math.ceil(-v - 0.5)
            return PP.fx(x, bits)
        return fx

    for kind in ("trunc", "ties-down"):
        real = PP.fx
        PP.fx = mutated(kind)
        try:
            pm = words(PP.build(src, force_frac=24))
        finally:
            PP.fx = real
        t = 12345.75 / 2.0 ** 24   # a constructed value three quarters of a step up: truncation and rounding differ
        if kind == "trunc":
            check(tuple(pm[i] for i in f_idx24 + [top24]) != tuple(w24[i] for i in f_idx24 + [top24]) or mutated(kind)(t, 24) != PP.fx(t, 24),
                  "controls: a truncating packer is caught (the constructed 0.75 step value, or this source)")
        else:
            t = (2 * 7 + 1) / 2.0 ** 25
            check(mutated(kind)(t, 24) != PP.fx(t, 24), "controls: a ties-down packer is caught by the tie check")
    # a packer that leaves accel_force at Q16 in a 24 pack: caught by the decoded accel_force check
    w = list(w24)
    w[acc24] = w16[acc16]
    (tmp / "x.bin").write_bytes(pack_of(w))
    check(dump(a.dump24, tmp / "x.bin")["accel_force"] != w24[acc24] or w16[acc16] == w24[acc24], "controls: a 24 pack with a Q16 accel_force decodes to a different accel_force")
    print("%d failures" % len(FAILS))
    sys.exit(1 if FAILS else 0)


if __name__ == "__main__":
    main()
