#!/usr/bin/env python3
"""Class terms for the native rival field (SH2ZAM line-field-6), from the decomp's cpu.c tables (claude/rival-class).

Per racer (by character, class D and chase target), the source's attack chances per CPU decision:
  side attack on the chased vehicle when alongside (cpu.c ~1462): chance unk_360 = D_i3_8013EBD0[group * 4 + D], or
      0.15 when the chased vehicle is a CPU racer and the character's group is below 2 (awareness flag 0x200);
  spin attack beside it (cpu.c ~1475, the else branch on the same draw): up to unk_364 = D_i3_8013EC30[group * 4 + D]
      + unk_360, or 0.2 under the same 0x200 rule; it waits unless the player is behind the racer (Expert and Master)
      or 138 units behind (Novice and Standard);
  rear attack (cpu.c ~1795, Expert and Master, group below 2): 0.01 a decision on a racer within 23 units behind and
      138 across, with the player behind.
Per race: the drive once the player has finished (cpu.c ~1404: full throttle, no boost share, one machine's constants:
the stock machines' top speed without boost, rivals_finish.FIRST_LAP_TOP); the finish target's slip a lap at a time
(cpu.c 874: the remaining share x D_i3_8013E9E0[D], laps evenly); the player chasers' lead (cpu.c 1509: 80 units more
per class than Novice's rand * 400 - 10).
Values are table entries from the pinned decomp source, not game data.

  rivals_class.py racers REFERENCE.json   -> "id aggression spin_q16 rear_q16" per racer (aggression in 1/256)
  rivals_class.py race CLASS SPEED_K      -> tuning for ref/rivals_gp_main.c --tune (and engine_field.c)
"""
import re
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from rivals_assets import DECOMP  # noqa: E402
import rivals_finish  # noqa: E402

LAPS = 3
# racer.c ~4700 and ~5598: a spin turns at 0x100 a tick, slowing by 2 a tick, until under 50: (256 - 50) / 2 = 103
# ticks. A drift attack (cpu.c driftingCounter 5 decisions, presses at 3 and 1) steers for about 5 x 4 = 20 ticks.
SPIN_TICKS = (256 - 50) // 2
REAR_TICKS = 20


def _src():
    return (DECOMP / "src/overlays/ovl_i3/cpu.c").read_text()


def groups(src=None):
    """Character group (D_i3_8013E7A8[character].unk_02) in character order."""
    src = src or _src()
    m = re.search(r"D_i3_8013E7A8\[\] = \{(.*?)\n\};", src, re.S)
    if not m:
        raise SystemExit("cannot find D_i3_8013E7A8 in cpu.c")
    rows = re.findall(r"\{\s*(\d+),\s*(\d+),\s*\{", m.group(1))
    if len(rows) != 30:
        raise SystemExit("D_i3_8013E7A8: %d rows, expected 30" % len(rows))
    return [int(g) for _, g in rows]


def q16(x):
    return max(0, min(65535, int(round(x * 65536))))


def racers(report):
    src = _src()
    d = int(report["difficulty"])
    g = groups(src)
    ebd0 = rivals_finish.values("f32", "D_i3_8013EBD0", src, float)
    ec30 = rivals_finish.values("f32", "D_i3_8013EC30", src, float)
    out = []
    for r in report["racers"]:
        if r["id"] == 0:
            out.append((0, 0, 0, 0))
            continue
        grp = g[r["character"]]
        u360 = ebd0[grp * 4 + d]
        u364 = ec30[grp * 4 + d] + u360
        aware = grp < 2
        chase = r["chase"]
        side = spin = 0.0
        if chase != 255:
            cpu_chased = chase != 0
            side = max(0.15 if (cpu_chased and aware) else 0.0, u360)
            spin = max(0.0, max(0.2 if (cpu_chased and aware) else 0.0, u364) - side)
        rear = 0.01 if (d >= 2 and aware) else 0.0
        out.append((r["id"], int(round(side * 256)), q16(spin), q16(rear)))
    return out


def race(d, speed_k):
    src = _src()
    e9e0 = rivals_finish.values("f32", "D_i3_8013E9E0", src, float)
    after = int(round(rivals_finish.FIRST_LAP_TOP * speed_k * 256))
    slip = int(round(e9e0[d] / LAPS * 60 / 1000))
    focus_lead = 0 if d >= 2 else 138 * 256
    return {"after_speed": after, "finish_slip": slip, "focus_lead": focus_lead, "rear_long": 23 * 256,
            "rear_lat": 138 * 256, "spin_ticks": SPIN_TICKS, "rear_ticks": REAR_TICKS, "chase_lead_add": 80 * d}


if __name__ == "__main__":
    import json
    if sys.argv[1] == "racers":
        for row in racers(json.load(open(sys.argv[2]))):
            print(*row)
    elif sys.argv[1] == "race":
        print(",".join("%s=%d" % kv for kv in race(int(sys.argv[2]), float(sys.argv[3])).items()))
    else:
        raise SystemExit(__doc__)
