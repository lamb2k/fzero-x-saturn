#!/usr/bin/env python3
"""Second-lap finish targets for the native rival field (SH2ZAM line-field-2), from the decomp's tables.

From its second lap a paced CPU racer in the source re-targets a finish time (cpu.c func_i3_80116A50):
    course finish (D_i3_8013E328) - remaining share x D_i3_8013E9E0[difficulty] + D_i3_8013E9F0[rank - 1 + difficulty x 30]
evaluated here at the end of lap 1 (remaining share (laps - 1) / laps); rank is the racer's pace rank, read
back from its lap target (D_i3_8013E4A8 base + D_i3_8013E628 step x (rank - 1)). Chasers and the player
get none (0). Where the course's finish time is exactly laps x the lap base (Mute City), the source's two
targets agree and the native field keeps its one pace, as validated: every target is 0 there.
Values are table entries from the pinned decomp source, not game data."""
import re
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import course_catalog  # noqa: E402
from rivals_assets import DECOMP, table  # noqa: E402

LAPS = 3


def values(kind, name, src, cast):
    m = re.search(r"%s %s\[\] = \{(.*?)\};" % (kind, re.escape(name)), src, re.S)
    if not m:
        raise SystemExit(f"cannot find {name} in cpu.c")
    return [cast(v.rstrip('f')) for v in re.findall(r"-?\d+(?:\.\d+)?f?", m.group(1))]


def targets(report, slug):
    """{racer id: finish ms or 0} for a fzx-gp-ref report on course SLUG."""
    src = (DECOMP / "src/overlays/ovl_i3/cpu.c").read_text()
    c, d = course_catalog.course(slug)["index"], int(report["difficulty"])
    finish, base, step = (table(n, src)[c * 4 + d] for n in ("D_i3_8013E328", "D_i3_8013E4A8", "D_i3_8013E628"))
    share, rank_add = values("f32", "D_i3_8013E9E0", src, float)[d], values("s32", "D_i3_8013E9F0", src, int)
    out = {}
    for r in report["racers"]:
        ms, out[r["id"]] = r["target_lap_ms"], 0
        if finish == LAPS * base or r["id"] == 0 or ms <= 0 or r["chase"] != 255 or (ms - base) % step:
            continue
        rank = (ms - base) // step + 1
        out[r["id"]] = int(round(finish - share * (LAPS - 1) / LAPS + rank_add[rank - 1 + d * 30]))
    return out


# A chaser's first lap (cpu.c: racer->lap == 1 sets unk_1E8 to D_i3_8013DFA0, 0 on these courses): the engine
# force has no boost share, so a chaser runs at its machine's own top speed until lap 2. Measured on the
# HOST reference (handling build, R01 driver, zones off, Devil's Forest): the stock machines at the default
# engine setting 0.5 top out at 36.53..37.05 (eight machines across the roster), mean 36.7.
FIRST_LAP_TOP = 36.7


def chase_caps(report, slug, speed_k):
    """{racer id: first-lap chase cap, Q8 line distance per tick (line-field-3), or 0}: chasers only."""
    src = (DECOMP / "src/overlays/ovl_i3/cpu.c").read_text()
    c, d = course_catalog.course(slug)["index"], int(report["difficulty"])
    if values("f32", "D_i3_8013DFA0", src, float)[c * 4 + d] != 0.0:
        return {r["id"]: 0 for r in report["racers"]}   # a boost share on the first lap: no cap
    cap = int(round(FIRST_LAP_TOP * speed_k * 256))
    return {r["id"]: (cap if r["id"] != 0 and r["chase"] != 255 else 0) for r in report["racers"]}


if __name__ == "__main__":
    import json
    t = targets(json.load(open(sys.argv[1])), sys.argv[2])
    print(" ".join("%d:%d" % kv for kv in sorted(t.items())))
