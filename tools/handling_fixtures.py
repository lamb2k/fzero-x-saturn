#!/usr/bin/env python3
"""E1 plays-right fixtures: run the HOST reference on scripted inputs and measure handling.

The judge is the original race code (ref/handling_build.sh). This tool
authors two test courses (not game data), writes input scripts, runs the harness and
reduces each run to a few numbers a player can feel: launch, coast, brake, steady
turns, lean, attacks, slalom, wall contact, dash plate, dirt, jump, pit, and a full
race under a closed-loop test driver. Every fixture is run again with its action one
tick later, which gives the reference's own sensitivity next to each value.

Machine constants come from the owner's ROM (--rom, local only). Without it the CI
fixture machine is used: the numbers then carry no meaning, but the tool still runs.
Outputs go to --out (the evidence folder), never to the repository.

  ref/handling_build.sh gcc -O2
  python3 tools/handling_fixtures.py --ref ref/build/gcc-O2-handling/fzx-ref --out DIR \\
      [--rom ROM] [--retail-course RECORD] [--replay inputs.hex --replay-ref MACHINE_MENU_BUILD] [--only P01,W01]

Units: positions in source units (0.1 m), speeds in units per tick (x 21.6 = km/h),
angles in degrees, time in ticks of 1/60 s, energy in source energy units.
MIT; see ../LICENSE.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from import_course import FEATURES, course_checksum  # noqa: E402

RECORD_SIZE = 0x7E0
WALLED = 0x40
INTRO = 460            # ticks before the race starts
KMH = 21.6
FEATURE_OFFSETS = {"pit": 0x5A0, "dash": 0x5E0, "dirt": 0x620, "ice": 0x660, "jump": 0x6A0, "landmine": 0x6E0,
                   "gate": 0x720, "building": 0x760, "sign": 0x7A0}
# stateFlags bits used here (fzx_racer.h).
EFFECT_MASK, EFFECT_PIT, EFFECT_DIRT, EFFECT_DASH = 0xF, 1, 2, 3
F_DAMAGE, F_RETIRED, F_CAN_BOOST, F_JUMP, F_DASH, F_FINISHED, F_AIRBORNE, F_CRASHED = (
    1 << 17, 1 << 18, 1 << 20, 1 << 21, 1 << 24, 1 << 25, 1 << 26, 1 << 27)
F_GRIP = 1 << 29


def course_record(points, features=None, name=b"E1 HANDLING") -> bytes:
    """points: (x, y, z, radius_left, radius_right, bank_degrees). One segment gets another road type."""
    buf = bytearray(RECORD_SIZE)
    struct.pack_into(">Bbbb", buf, 0, 0, len(points), 0, 0)
    buf[9:9 + len(name)] = name
    odd = len(points) - 3
    for i, (x, y, z, left, right, bank) in enumerate(points):
        struct.pack_into(">fffhhI", buf, 0x20 + i * 20, x, y, z, left, right, WALLED | (1 if i == odd else 0))
        struct.pack_into(">h", buf, 0x520 + i * 2, bank)
    for offset in range(0x5A0, RECORD_SIZE, 0x40):
        buf[offset:offset + 64] = b"\xff" * 64
    for key, values in (features or {}).items():
        for index, value in values.items():
            struct.pack_into(">b", buf, FEATURE_OFFSETS[key] + index, value)
    # The original checksum, computed directly: the importer's bounds are the old renderer's,
    # and the skid pad is deliberately wider than any road the game ships.
    parsed = [{"position": (x, y, z), "radius_left": left, "radius_right": right, "bank_degrees": bank,
               "track_segment_info": WALLED | (1 if i == odd else 0)}
              for i, (x, y, z, left, right, bank) in enumerate(points)]
    listed = {key: [(features or {}).get(key, {}).get(i, -1) for i in range(len(points))] for key in FEATURES}
    struct.pack_into(">I", buf, 4, course_checksum(parsed, listed))
    return bytes(buf)


def pad_course() -> bytes:
    """A flat ring 28 km across with an 18 km wide road: an open skid pad. No wall is reachable."""
    n, radius = 16, 14000.0
    return course_record([(radius * math.cos(2 * math.pi * i / n), 0.0, radius * math.sin(2 * math.pi * i / n),
                           9000, 9000, 0) for i in range(n)], name=b"E1 PAD")


def corridor_course() -> bytes:
    """A stadium with two 4 km straights, 60 m wide. After the line: dash plate (segment 1),
    pit strips at both edges (2), dirt down the middle (3) and a jump (5)."""
    half, r, step = 20000.0, 6000.0, 5000.0
    pts = [(-half + step * i, 0.0, -r) for i in range(9)]
    pts += [(half + r * math.cos(a), 0.0, r * math.sin(a)) for a in (-math.pi / 4, 0.0, math.pi / 4)]
    pts += [(half - step * i, 0.0, r) for i in range(9)]
    pts += [(-half - r * math.cos(a), 0.0, -r * math.sin(a)) for a in (-math.pi / 4, 0.0, math.pi / 4)]
    pts = pts[1:] + pts[:1]  # the grid sits just before point 0: keep it on the straight
    feats = {"dash": {1: 0}, "pit": {2: 0}, "dirt": {3: 3}, "jump": {5: 0}}
    return course_record([(x, y, z, 300, 300, 0) for x, y, z in pts], feats, name=b"E1 CORRIDOR")


def script(lines) -> str:
    return "".join("%d %s %d %d\n" % (ticks, buttons, sx, 0) for ticks, buttons, sx in lines if ticks > 0)


def fixtures():
    """name -> (course, lines, action tick, analysis). Stick +63 is full right."""
    fx = {}
    fx["P01_launch"] = ("pad", [(INTRO, "-", 0), (600, "A", 0)], INTRO, "launch", None)
    fx["P02_coast"] = ("pad", [(INTRO, "-", 0), (300, "A", 0), (600, "-", 0)], INTRO + 300, "decay", None)
    fx["P03_brake"] = ("pad", [(INTRO, "-", 0), (300, "A", 0), (240, "CD", 0)], INTRO + 300, "decay", None)
    for stick in (16, 32, 48, 64, 80):
        fx["P04_turn_%02d" % stick] = ("pad", [(INTRO, "-", 0), (300, "A", 0), (300, "A", stick)], INTRO + 300, "turn", None)
    fx["P05_turn_from_rest"] = ("pad", [(INTRO, "-", 0), (240, "A", 64)], INTRO, "turn", None)
    fx["P06_lean_into"] = ("pad", [(INTRO, "-", 0), (300, "A", 0), (300, "A+R", 64)], INTRO + 300, "turn", None)
    fx["P06_lean_against"] = ("pad", [(INTRO, "-", 0), (300, "A", 0), (300, "A+Z", 64)], INTRO + 300, "turn", None)
    fx["P07_side_attack"] = ("pad", [(INTRO, "-", 0), (300, "A", 0), (1, "A+Z", 0), (3, "A", 0), (1, "A+Z", 0),
                                     (200, "A", 0)], INTRO + 300, "attack", None)
    fx["P08_spin_attack"] = ("pad", [(INTRO, "-", 0), (300, "A", 0), (2, "A+R", 0), (1, "A+R+Z", 0), (3, "A+R", 0),
                                     (1, "A+R+Z", 0), (300, "A", 0)], INTRO + 300, "attack", None)
    slalom = [(INTRO, "-", 0), (300, "A", 0)]
    for _ in range(6):
        slalom += [(30, "A", 64), (30, "A", -64)]
    fx["P09_slalom"] = ("pad", slalom + [(60, "A", 0)], INTRO + 300, "slalom", None)
    for hold in (4, 8, 16, 24):  # ticks of full right stick, then straight into the right wall
        fx["W01_wall_%02d" % hold] = ("corridor", [(INTRO, "-", 0), (60, "A", 0), (hold, "A", 64), (140, "A", 0)],
                                      INTRO + 60, "wall", None)
    fx["C01_features"] = ("corridor", [(INTRO, "-", 0), (1100, "A", 0)], INTRO, "features", None)
    # One wall touch on the right, then the test driver holds a line inside the right-hand pit strip.
    fx["C02_pit_after_wall"] = ("corridor", [(INTRO, "-", 0), (60, "A", 0), (8, "A", 64), (700, "A", 0)],
                                INTRO + 60, "pit", "0.004,2.5,0,%d,-250" % (INTRO + 100))
    # Two laps under the test driver, B pressed every 400 ticks: the boost unlocks on lap 2.
    fx["C03_lap_boost"] = ("corridor", [(INTRO, "-", 0), (7600, "A", 0)], INTRO, "boost", "0.004,2.5,400")
    return fx


def delayed(lines, action):
    """The same script with the step that starts at the action tick one tick later."""
    out, tick = [], 0
    for ticks, buttons, sx in lines:
        if tick == action:
            previous = out[-1] if out else (0, "-", 0)
            out.append((1, previous[1], previous[2]))
        out.append((ticks, buttons, sx))
        tick += ticks
    if action not in [sum(t for t, _, _ in lines[:i]) for i in range(len(lines))]:
        raise ValueError("the action tick must start a script step")
    return out


def run(ref, course, text, work, rom=None, driver=None, zones=True, selection=None):
    work.mkdir(parents=True, exist_ok=True)
    (work / "inputs.txt").write_text(text)
    cmd = [str(ref), "--course", str(course), "--inputs", str(work / "inputs.txt"), "--out", str(work / "handling.tsv"),
           "--zones", "1" if zones else "0"]
    if rom:
        cmd += ["--rom", str(rom)]
    if driver:
        cmd += ["--driver", driver]
    if selection:
        cmd += ["--selection", selection]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError("fzx-ref failed: " + result.stderr.strip())
    rows, head = [], None
    for line in (work / "handling.tsv").read_text().splitlines():
        if line.startswith("# tick"):
            head = line[2:].split("\t")
        elif not line.startswith("#"):
            row = {}
            for key, value in zip(head, line.split("\t")):
                row[key] = int(value, 16) if key in ("button", "flags") else (
                    int(value) if value.lstrip("-").isdigit() else float(value))
            rows.append(row)
    return rows, result.stdout.split()[-1]


def yaw(row):
    return math.degrees(math.atan2(row["fz"], row["fx"]))


def unwrap(delta):
    return (delta + 180.0) % 360.0 - 180.0


def first(rows, start, test):
    for index in range(start, len(rows)):
        if test(rows[index]):
            return index
    return None


def analyse(kind, rows, action):
    at = lambda offset: rows[min(action + offset, len(rows) - 1)]
    m = {}
    if kind == "launch":
        top = rows[-1]["speed"]
        m["v_plus_10"], m["v_plus_30"], m["v_plus_60"], m["v_plus_120"], m["v_plus_300"] = (
            at(o)["speed"] for o in (10, 30, 60, 120, 300))
        m["v_top"] = top
        m["v_top_kmh"] = top * KMH
        m["ticks_to_50pct"] = first(rows, action, lambda r: r["speed"] >= 0.5 * top) - action
        m["ticks_to_90pct"] = first(rows, action, lambda r: r["speed"] >= 0.9 * top) - action
        m["accel_peak"] = max(r["accel_force"] for r in rows[action:])
        m["hover_height"] = rows[-1]["height"]
    elif kind == "decay":
        m["v_at_release"] = at(-1)["speed"]
        for o in (30, 60, 120, 240):
            if action + o < len(rows):
                m["v_plus_%d" % o] = at(o)["speed"]
        m["ticks_to_half"] = first(rows, action, lambda r: r["speed"] <= 0.5 * at(-1)["speed"])
        m["ticks_to_half"] = None if m["ticks_to_half"] is None else m["ticks_to_half"] - action
    elif kind == "turn":
        end = len(rows) - 1
        rate = lambda a, b: sum(unwrap(yaw(rows[i + 1]) - yaw(rows[i])) for i in range(a, b)) / (b - a)
        steady = rate(end - 120, end)
        m["yaw_rate_steady_deg_per_tick"] = steady
        m["yaw_rate_first_10_deg_per_tick"] = rate(action, action + 10)
        m["yaw_after_60_deg"] = sum(unwrap(yaw(rows[i + 1]) - yaw(rows[i])) for i in range(action, action + 60))
        m["speed_before"], m["speed_steady"] = at(-1)["speed"], rows[end]["speed"]
        m["speed_ratio"] = rows[end]["speed"] / at(-1)["speed"] if at(-1)["speed"] else None
        m["turn_radius"] = abs(rows[end]["speed"] / math.radians(steady)) if steady else None
        m["slip_side_speed_steady"] = sum(r["v_side"] for r in rows[end - 120:end]) / 120
        m["slip_angle_steady_deg"] = math.degrees(math.atan2(m["slip_side_speed_steady"], rows[end]["v_forward"]))
        m["grip_state_share"] = sum(1 for r in rows[action:] if r["flags"] & F_GRIP) / (len(rows) - action)
        m["slide_boost_peak"] = max(r["slide_boost"] for r in rows[action:])
    elif kind == "attack":
        active = [i for i in range(action, len(rows)) if rows[i]["attack"]]
        m["attack_ticks"] = len(active)
        m["attack_kind"] = rows[active[0]]["attack"] if active else 0
        m["side_speed_peak"] = max(abs(r["v_side"]) for r in rows[action:])
        heading = math.radians(yaw(rows[action - 1]))
        base = rows[action - 1]
        cross = lambda r: -(r["x"] - base["x"]) * math.sin(heading) + (r["z"] - base["z"]) * math.cos(heading)
        m["side_shift_after_60"] = cross(at(60))
        m["side_shift_after_150"] = cross(at(150))
        m["speed_before"], m["speed_after_60"] = base["speed"], at(60)["speed"]
        m["yaw_after_150_deg"] = sum(unwrap(yaw(rows[i + 1]) - yaw(rows[i])) for i in range(action - 1, action + 149))
    elif kind == "slalom":
        heading = math.radians(yaw(rows[action - 1]))
        base = rows[action - 1]
        cross = [-(r["x"] - base["x"]) * math.sin(heading) + (r["z"] - base["z"]) * math.cos(heading)
                 for r in rows[action:]]
        m["side_swing_peak_to_peak"] = max(cross) - min(cross)
        m["speed_before"], m["speed_min"], m["speed_end"] = base["speed"], min(r["speed"] for r in rows[action:]), rows[-1]["speed"]
        m["yaw_swing_deg"] = max(abs(unwrap(yaw(r) - yaw(base))) for r in rows[action:])
    elif kind == "wall":
        hit = first(rows, action, lambda r: r["energy"] < rows[action]["energy"])
        m["hit"] = hit is not None
        if hit is not None:
            before = rows[hit - 1]
            end = hit
            while end + 1 < len(rows) and any(rows[i]["energy"] < rows[i - 1]["energy"]
                                              for i in range(end + 1, min(end + 11, len(rows)))):
                end += 1
            after = rows[min(end + 1, len(rows) - 1)]
            m["ticks_to_wall"] = hit - action
            m["approach_angle_deg"] = abs(math.degrees(math.asin(max(-1.0, min(1.0, before["heading_side"])))))
            m["speed_before"] = before["speed"]
            m["speed_into_wall"] = abs(before["speed"] * before["heading_side"])
            m["contact_ticks"] = end - hit + 1
            m["energy_lost_contact"] = before["energy"] - after["energy"]
            m["speed_ratio_after_contact"] = after["speed"] / before["speed"]
            m["speed_ratio_after_60"] = rows[min(hit + 60, len(rows) - 1)]["speed"] / before["speed"]
            m["leave_angle_deg"] = abs(math.degrees(math.asin(max(-1.0, min(1.0, after["heading_side"])))))
            m["energy_lost_total"] = before["energy"] - rows[-1]["energy"]
            m["retired"] = bool(rows[-1]["flags"] & (F_RETIRED | F_CRASHED))
    elif kind == "features":
        def span(test):
            ticks = [i for i in range(action, len(rows)) if test(rows[i])]
            return (ticks[0], ticks[-1]) if ticks else None
        dash = span(lambda r: r["flags"] & F_DASH)
        if dash:
            m["dash_boost_ticks"] = dash[1] - dash[0] + 1
            m["dash_speed_before"] = rows[dash[0] - 1]["speed"]
            m["dash_speed_peak"] = max(r["speed"] for r in rows[dash[0]:dash[1] + 60])
            m["dash_speed_peak_kmh"] = m["dash_speed_peak"] * KMH
            m["dash_energy_used"] = rows[dash[0] - 1]["energy"] - rows[dash[1]]["energy"]
        dirt = span(lambda r: (r["flags"] & EFFECT_MASK) == EFFECT_DIRT and not r["boost"])
        if dirt:
            m["dirt_ticks"] = dirt[1] - dirt[0] + 1
            m["dirt_speed_in"], m["dirt_speed_out"] = rows[dirt[0] - 1]["speed"], rows[dirt[1]]["speed"]
        air = span(lambda r: r["flags"] & F_AIRBORNE)
        jump = span(lambda r: r["flags"] & F_JUMP)
        if jump:
            m["jump_boost_ticks"] = jump[1] - jump[0] + 1
        if air:
            m["air_ticks"] = air[1] - air[0] + 1
            m["air_height_peak"] = max(r["height"] for r in rows[air[0]:air[1] + 1])
            m["air_distance"] = math.dist((rows[air[0]]["x"], rows[air[0]]["z"]), (rows[air[1]]["x"], rows[air[1]]["z"]))
            m["air_speed_in"], m["air_speed_out"] = rows[air[0] - 1]["speed"], rows[min(air[1] + 1, len(rows) - 1)]["speed"]
        m["lateral_drift_end"] = rows[-1]["lateral"]
        m["can_boost_tick"] = first(rows, 0, lambda r: r["flags"] & F_CAN_BOOST)
    elif kind == "pit":
        pit = [i for i in range(action, len(rows)) if (rows[i]["flags"] & EFFECT_MASK) == EFFECT_PIT]
        m["energy_min"] = min(r["energy"] for r in rows)
        if pit:
            gains = [rows[i]["energy"] - rows[i - 1]["energy"] for i in pit if rows[i]["energy"] > rows[i - 1]["energy"]]
            m["pit_ticks"] = len(pit)
            m["pit_energy_per_tick"] = max(gains) if gains else 0.0
            m["pit_energy_gain"] = sum(gains)
            m["pit_inner_edge_over_radius"] = min(abs(rows[i]["lateral"]) / rows[i]["radius_left"] for i in pit)
            m["max_energy"] = rows[0]["max_energy"]
    elif kind == "boost":
        m.update(race_metrics(rows))
        start = first(rows, action + 1, lambda r: r["boost"] >= 99 and not r["flags"] & F_DASH)
        m["boost_seen"] = start is not None
        if start is not None:
            m["boost_first_tick"] = start
            m["boost_ticks"] = first(rows, start, lambda r: r["boost"] == 0) - start
            m["boost_energy_used"] = rows[start - 1]["energy"] - rows[start + m["boost_ticks"]]["energy"]
            m["boost_energy_per_tick"] = rows[start + 10]["energy"] - rows[start + 11]["energy"]
            m["boost_speed_before"] = rows[start - 1]["speed"]
            m["boost_speed_peak"] = max(r["speed"] for r in rows[start:start + 130])
            m["boost_speed_peak_kmh"] = m["boost_speed_peak"] * KMH
            m["boost_speed_after_200"] = rows[min(start + 200, len(rows) - 1)]["speed"]
    elif kind == "race":
        m.update(race_metrics(rows))
    return m


def race_metrics(rows):
    m = {}
    done = first(rows, 0, lambda r: r["flags"] & F_FINISHED)
    m["finished"] = done is not None
    m["finish_tick"] = done
    last = rows[done if done is not None else -1]
    m["race_time_ms"] = last["race_time"]
    m["lap_times_ms"] = [last["lap1"], last["lap2"], last["lap3"]]
    racing = [r for r in rows[INTRO:(done or len(rows))]]
    m["speed_mean"] = sum(r["speed"] for r in racing) / len(racing)
    m["speed_max"] = max(r["speed"] for r in racing)
    m["speed_max_kmh"] = m["speed_max"] * KMH
    m["energy_min"] = min(r["energy"] for r in racing)
    m["damage_ticks"] = sum(1 for i in range(1, len(racing)) if racing[i]["energy"] < racing[i - 1]["energy"]
                            and not racing[i]["boost"])
    m["boosts"] = sum(1 for i in range(1, len(racing)) if racing[i]["boost"] > racing[i - 1]["boost"]
                      and not racing[i]["flags"] & F_DASH)
    m["dash_plates"] = sum(1 for i in range(1, len(racing)) if racing[i]["flags"] & F_DASH
                           and not racing[i - 1]["flags"] & F_DASH)
    m["air_ticks"] = sum(1 for r in racing if r["flags"] & F_AIRBORNE)
    m["lateral_rms_over_radius"] = math.sqrt(sum((r["lateral"] / r["radius_left"]) ** 2 for r in racing) / len(racing))
    m["retired"] = bool(rows[-1]["flags"] & (F_RETIRED | F_CRASHED)) and done is None
    laps = [i for i in range(1, len(rows)) if rows[i]["laps_completed"] > rows[i - 1]["laps_completed"]]
    m["lap_cross_ticks"] = laps
    m["can_boost_tick"] = first(rows, 0, lambda r: r["flags"] & F_CAN_BOOST)
    return m


def numeric_delta(a, b):
    out = {}
    for key, value in a.items():
        other = b.get(key)
        if isinstance(value, bool) or isinstance(other, bool):
            if value != other:
                out[key] = "changed"
        elif isinstance(value, (int, float)) and isinstance(other, (int, float)):
            out[key] = other - value
        elif isinstance(value, list) and isinstance(other, list) and len(value) == len(other):
            out[key] = [y - x for x, y in zip(value, other)]
    return out


def replay_script(path, shift_at=None):
    """inputs.hex: one N64 pad word per tick (buttons << 16 | stick x << 8 | stick y)."""
    words = [int(line, 16) for line in Path(path).read_text().split()]
    if shift_at is not None:
        words = words[:shift_at] + [words[shift_at]] + words[shift_at:]
    names = [("A", 0x8000), ("B", 0x4000), ("Z", 0x2000), ("S", 0x1000), ("L", 0x20), ("R", 0x10),
             ("CU", 8), ("CD", 4), ("CL", 2), ("CR", 1)]
    lines = []
    for word in words:
        buttons = "+".join(n for n, mask in names if (word >> 16) & mask) or "-"
        sx, sy = (word >> 8) & 0xFF, word & 0xFF
        lines.append("1 %s %d %d\n" % (buttons, sx - 256 if sx > 127 else sx, sy - 256 if sy > 127 else sy))
    return "".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--ref", type=Path, required=True, help="fzx-ref from ref/handling_build.sh")
    ap.add_argument("--out", type=Path, required=True, help="evidence folder (never the repository)")
    ap.add_argument("--rom", type=Path, help="owner's ROM, local only; machine constants")
    ap.add_argument("--retail-course", type=Path, help="local 0x7E0 course record for the race fixtures")
    ap.add_argument("--replay", type=Path, help="local inputs.hex of a recorded race")
    ap.add_argument("--driver", default="0.004,2.5", help="test driver gains kl,kh for the race fixtures")
    ap.add_argument("--replay-ref", type=Path, help="the same harness built with FZX_SOURCE_MACHINE_MENU=1")
    ap.add_argument("--selection", default="1,0.5078125", help="skin,engine the replay was recorded with")
    ap.add_argument("--only", default="", help="comma-separated fixture name prefixes")
    args = ap.parse_args()
    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    courses = {"pad": out / "course-pad.bin", "corridor": out / "course-corridor.bin"}
    courses["pad"].write_bytes(pad_course())
    courses["corridor"].write_bytes(corridor_course())
    only = [p for p in args.only.split(",") if p]
    want = lambda name: not only or any(name.startswith(p) for p in only)
    report = {"units": {"length": "source unit (0.1 m)", "speed": "units per tick (x21.6 = km/h)",
                        "time": "tick (1/60 s)", "angle": "degree"},
              "machine": "owner ROM" if args.rom else "CI fixture machine (values carry no meaning)", "fixtures": {}}
    previous = out / "handling-reference.json"
    if only and previous.exists():  # a partial run updates the earlier report
        report["fixtures"] = json.loads(previous.read_text())["fixtures"]
    for name, (course, lines, action, kind, driver) in fixtures().items():
        if not want(name):
            continue
        rows, digest = run(args.ref, courses[course], script(lines), out / name, args.rom, driver)
        base = analyse(kind, rows, action)
        later = driver
        if driver and driver.count(",") >= 3:  # the driver start moves with the action
            parts = driver.split(",")
            parts[3] = str(int(parts[3]) + 1)
            later = ",".join(parts)
        rows2, _ = run(args.ref, courses[course], script(delayed(lines, action)), out / (name + "+1"), args.rom, later)
        late = analyse(kind, rows2, action + 1)
        report["fixtures"][name] = {"course": course, "kind": kind, "action_tick": action, "trace_hash": digest,
                                    "metrics": base, "one_tick_later_delta": numeric_delta(base, late)}
    if args.retail_course:
        race = [(INTRO, "-", 0), (9000, "A", 0)]
        for name, driver in (("R01_race_driver", args.driver), ("R02_race_driver_boost", args.driver + ",240")):
            if not want(name):
                continue
            rows, digest = run(args.ref, args.retail_course, script(race), out / name, args.rom, driver)
            base = race_metrics(rows)
            rows2, _ = run(args.ref, args.retail_course, script([(INTRO + 1, "-", 0), (9000, "A", 0)]),
                           out / (name + "+1"), args.rom, driver)
            report["fixtures"][name] = {"course": "retail (local)", "kind": "race", "driver": driver,
                                        "trace_hash": digest, "metrics": base,
                                        "one_tick_later_delta": numeric_delta(base, race_metrics(rows2))}
        if args.replay and args.replay_ref and want("R03"):
            # A recorded race replays only on the machine and the bridge it was driven with: the
            # legacy bridge has no surface zones. Open loop: one tick of shift sends it off the road.
            rows, digest = run(args.replay_ref, args.retail_course, replay_script(args.replay), out / "R03_replay",
                               args.rom, zones=False, selection=args.selection)
            base = race_metrics(rows)
            rows2, _ = run(args.replay_ref, args.retail_course, replay_script(args.replay, 1000), out / "R03_replay+1",
                           args.rom, zones=False, selection=args.selection)
            report["fixtures"]["R03_replay"] = {"course": "retail (local)", "kind": "race", "trace_hash": digest,
                                                "selection": args.selection, "zones": "legacy bridge (none)",
                                                "metrics": base,
                                                "one_tick_later_delta": numeric_delta(base, race_metrics(rows2))}
    (out / "handling-reference.json").write_text(json.dumps(report, indent=1, sort_keys=True) + "\n")
    print("handling fixtures: %d run, report %s" % (len(report["fixtures"]), out / "handling-reference.json"))


if __name__ == "__main__":
    main()
