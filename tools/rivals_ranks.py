#!/usr/bin/env python3
"""Pace-rank tables for the native rival field (SH2ZAM line-field-12), from the decomp's cpu.c tables.

The source (cpu.c func_i3_80116A50) targets each paced racer's finish at
    D_i3_8013E328[course * 4 + class] - remaining share x D_i3_8013E9E0[class] + D_i3_8013E9F0[rank - 1 + class * 30]
with its pace rank (unk_368) renumbered whenever a chaser joins the ranking (func_i3_801175A4). This prints the
tables in ticks (60 a second) for HOST runs and the engine bundle:

  rivals_ranks.py race CLASS COURSE_SLUG   -> "base=T,share=T" tuning and the 30 rank steps, space separated:
                                              BASE SHARE STEP1 ... STEP30
Values are table entries from the pinned decomp source, not game data.
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import course_catalog  # noqa: E402
import rivals_finish  # noqa: E402
from rivals_assets import DECOMP, table  # noqa: E402


def tables(d, slug):
    src = (DECOMP / "src/overlays/ovl_i3/cpu.c").read_text()
    c = course_catalog.course(slug)["index"]
    finish_ms = table("D_i3_8013E328", src)[c * 4 + d]
    share_ms = rivals_finish.values("f32", "D_i3_8013E9E0", src, float)[d]
    steps = rivals_finish.values("s32", "D_i3_8013E9F0", src, int)[d * 30:(d + 1) * 30]
    t = lambda ms: int(round(ms * 60 / 1000))
    return t(finish_ms), t(share_ms), [t(x) for x in steps]


if __name__ == "__main__":
    if len(sys.argv) != 4 or sys.argv[1] != "race":
        raise SystemExit(__doc__)
    base, share, steps = tables(int(sys.argv[2]), sys.argv[3])
    print(base, share, *steps)
