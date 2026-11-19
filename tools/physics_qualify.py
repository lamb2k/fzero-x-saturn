#!/usr/bin/env python3
"""E1 qualification: run the native physics model on the plays-right fixtures and score it.

Runs every fixture of tools/handling_fixtures.py through the native HOST driver
(tools/physics_host, SH2ZAM vehicle-dynamics-1) with the same inputs and test
driver as the reference, reduces the traces with the same analysis, and compares
each metric with the reference report under the agreed tolerances
(generated/E1-PLAYS-RIGHT.md). The physics packs are baked on the fly from the
reference harness (fzx-ref --bake, then tools/physics_pack.py) into --out.

Everything written goes to --out (evidence, never the repository): packs and traces
derive from the owner's ROM and course data. Prints one line per fixture.

    tools/physics_qualify.py --ref REF --native PHYS --reference handling-reference.json \\
        --out DIR --rom ROM --retail-course COURSE [--replay-ref REF2 --replay HEX] [--only P01,...]
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import handling_fixtures as hf  # noqa: E402
import physics_pack  # noqa: E402

# (metric, rule, value): rel = fraction of the reference, abs = absolute, exact = equal.
P04 = [("yaw_rate_steady_deg_per_tick", "rel", 0.01), ("turn_radius", "rel", 0.015),
       ("speed_ratio", "abs", 0.002), ("slip_angle_steady_deg", "abs", 0.1)]
WALL = [("energy_lost_contact", "wall", 0.03), ("speed_ratio_after_contact", "abs", 0.01),
        ("contact_ticks", "exact", 0), ("leave_angle_deg", "abs", 0.5),
        # C-056 direct response (proposed bands): signed wall-normal speed before and after.
        ("normal_speed_before", "rel", 0.02), ("restitution", "abs", 0.02)]
RACE = [("race_time_ms", "rel", 0.005), ("speed_mean", "rel", 0.005), ("speed_max", "rel", 0.01),
        ("dash_plates", "exact", 0), ("damage_ticks", "exact", 0), ("lateral_rms_over_radius", "abs", 0.02),
        ("finished", "exact", 0), ("retired", "exact", 0)]
TOLERANCES = {
    "P01_launch": [("v_plus_10", "rel", 0.02), ("v_plus_30", "rel", 0.02), ("v_plus_60", "rel", 0.02),
                   ("ticks_to_50pct", "abs", 1), ("ticks_to_90pct", "abs", 1), ("v_top", "rel", 0.005),
                   ("hover_height", "abs", 0.05), ("wall_contact_ticks", "exact", 0)],
    "P02_coast": [("v_plus_60", "rel", 0.01), ("v_plus_120", "rel", 0.01), ("v_plus_240", "rel", 0.01),
                  ("ticks_to_half", "abs", 3)],
    "P03_brake": [("v_plus_30", "rel", 0.02), ("v_plus_60", "rel", 0.02), ("v_plus_120", "rel", 0.02),
                  ("ticks_to_half", "abs", 1)],
    "P04_turn_16": P04, "P04_turn_32": P04, "P04_turn_48": P04, "P04_turn_64": P04, "P04_turn_80": P04,
    "P05_turn_from_rest": [("yaw_rate_steady_deg_per_tick", "rel", 0.01)],
    "P06_lean_into": [("yaw_rate_steady_deg_per_tick", "rel", 0.015), ("turn_radius", "rel", 0.02),
                      ("slip_angle_steady_deg", "abs", 0.3), ("speed_ratio", "abs", 0.004)],
    "P06_lean_against": [("yaw_rate_steady_deg_per_tick", "rel", 0.03), ("slip_angle_steady_deg", "abs", 3.0),
                         ("speed_ratio", "abs", 0.005)],
    "P07_side_attack": [("attack_ticks", "exact", 0), ("side_shift_after_60", "rel", 0.02),
                        ("side_speed_peak", "rel", 0.02)],
    "P08_spin_attack": [("attack_ticks", "exact", 0), ("speed_after_60", "rel", 0.01)],
    # C-056 boosted grip turn (proposed bands).
    "P10_boost_turn": [("boosting", "exact", 0), ("grip_share_15", "abs", 0.1), ("side_speed_peak_15", "relf", 0.05),
                       ("yaw_rate_15_deg_per_tick", "rel", 0.02), ("speed_ratio_15", "abs", 0.01)],
    # C-056 machine matrix grip break (proposed bands).
    "P11_grip_break": [("grip_break_speed", "rel", 0.01), ("grip_break_tick", "abs", 2),
                       ("yaw_rate_10_deg_per_tick", "rel", 0.01)],
    "P09_slalom": [("side_swing_peak_to_peak", "rel", 0.02), ("speed_min", "rel", 0.02), ("speed_end", "rel", 0.03)],
    "W01_wall_04": WALL, "W01_wall_08": WALL, "W01_wall_16": WALL, "W01_wall_24": WALL,
    "C01_features": [("dash_boost_ticks", "exact", 0), ("dash_speed_peak", "rel", 0.01),
                     ("dash_energy_used", "exact", 0), ("dirt_ratio", "rel", 0.015), ("air_ticks", "abs", 2),
                     ("air_distance", "rel", 0.03), ("air_height_peak", "rel", 0.03)],
    "C02_pit_after_wall": [("pit_energy_per_tick", "rel", 0.005)],
    "C03_lap_boost": [("unlock_on_second_crossing", "exact", 0), ("boost_energy_per_tick", "rel", 0.005),
                      ("boost_speed_peak", "rel", 0.01)],
    "R01_race_driver": RACE + [("lap_1", "rel", 0.005), ("lap_2", "rel", 0.005), ("lap_3", "rel", 0.005)],
    "R02_race_driver_boost": [m for m in RACE if m[0] not in ("damage_ticks", "dash_plates")] +
                             [("lap_1", "rel", 0.0075), ("lap_2", "rel", 0.0075), ("lap_3", "rel", 0.0075),
                              ("boosts", "exact", 0), ("energy_min", "exact", 0)],
}


def derived(name, rows, metrics):
    """Metrics the table scores that the reducer reports in another form."""
    m = dict(metrics)
    if m.get("dirt_speed_in"):
        m["dirt_ratio"] = m["dirt_speed_out"] / m["dirt_speed_in"]
    if rows and name.startswith("R"):
        last = rows[-1]
        for k in (1, 2, 3):
            m["lap_%d" % k] = last["lap%d" % k]
    if rows and name.startswith("C03"):
        crossings = [r["tick"] for a, r in zip(rows, rows[1:]) if r["lap"] > a["lap"]]
        unlock = next((r["tick"] for r in rows if r["flags"] & (1 << 20)), None)
        m["unlock_on_second_crossing"] = len(crossings) >= 2 and unlock == crossings[1]
    return m


def score(rule, ref, got, tol):
    if ref is None or got is None:
        return ref == got, "n/a"
    if rule == "exact":
        return ref == got, "exact"
    if rule == "rel":
        return abs(got - ref) <= tol * abs(ref), "±%g%%" % (tol * 100)
    if rule == "relf":  # relative, with a 0.01 floor for references near zero
        band = max(tol * abs(ref), 0.01)
        return abs(got - ref) <= band, "±%.3g" % band
    if rule == "wall":
        band = max(tol * abs(ref), 0.1)
        return abs(got - ref) <= band, "±%.3g" % band
    return abs(got - ref) <= tol, "±%g" % tol


def bake(ref, course, rom, selection, work):
    work.mkdir(parents=True, exist_ok=True)
    (work / "one.txt").write_text("1 - 0 0\n")
    cmd = [str(ref), "--course", str(course), "--inputs", str(work / "one.txt"), "--bake", str(work / "source.txt"),
           "--zones", "1"]
    if rom:
        cmd += ["--rom", str(rom)]
    if selection:
        cmd += ["--selection", selection]
    subprocess.run(cmd, check=True, capture_output=True)
    pack = work / "course.fzxp"
    pack.write_bytes(physics_pack.build(physics_pack.parse(work / "source.txt")))
    return pack


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--ref", type=Path, required=True, help="fzx-ref from ref/handling_build.sh (bakes the packs)")
    ap.add_argument("--native", type=Path, required=True, help="fzx-phys from tools/physics_host/build.sh")
    ap.add_argument("--reference", type=Path, required=True, help="handling-reference.json of the reference run")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--rom", type=Path)
    ap.add_argument("--retail-course", type=Path)
    ap.add_argument("--driver", default="0.004,2.5")
    ap.add_argument("--only", default="")
    ap.add_argument("--json", type=Path, help="write the scored table here")
    ap.add_argument("--diff", action="store_true", help="also print each fixture's first diverging tick")
    ap.add_argument("--mutate", help="plant a model mutation in the native build (fzx-phys --mutate)")
    args = ap.parse_args()
    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    reference = json.loads(args.reference.read_text())["fixtures"]
    courses = {"pad": out / "course-pad.bin", "corridor": out / "course-corridor.bin",
               "straight": out / "course-straight.bin"}
    courses["pad"].write_bytes(hf.pad_course())
    courses["straight"].write_bytes(hf.straight_course())
    courses["corridor"].write_bytes(hf.corridor_course())
    if args.retail_course:
        courses["retail"] = args.retail_course
    packs = {}

    def native_run(_ref, course, text, work, rom=None, driver=None, zones=True, selection=None):
        key = (str(course), selection)
        if key not in packs:
            packs[key] = bake(args.ref, course, args.rom, selection, out / "packs" / ("pack%d" % len(packs)))
        work.mkdir(parents=True, exist_ok=True)
        (work / "inputs.txt").write_text(text)
        cmd = [str(args.native), "--pack", str(packs[key]), "--inputs", str(work / "inputs.txt"),
               "--out", str(work / "handling.tsv"), "--zones", "1" if zones else "0"]
        if driver:
            cmd += ["--driver", driver]
        if args.mutate:
            cmd += ["--mutate", args.mutate]
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError("fzx-phys failed: " + result.stderr.strip())
        rows, head = [], None
        for line in (work / "handling.tsv").read_text().splitlines():
            if line.startswith("# tick"):
                head = line[2:].split("\t")
            elif not line.startswith("#"):
                row = {}
                for k, value in zip(head, line.split("\t")):
                    row[k] = int(value, 16) if k in ("button", "flags") else (
                        int(value) if value.lstrip("-").isdigit() else float(value))
                rows.append(row)
        return rows, result.stdout.split()[-1]

    only = [p for p in args.only.split(",") if p]
    want = lambda name: not only or any(name.startswith(p) for p in only)
    table, fails = {}, 0
    runs = []
    for name, (course, lines, action, kind, driver) in hf.fixtures().items():
        if want(name):
            runs.append((name, courses[course], hf.script(lines), driver, kind, action))
    if args.retail_course:
        race = hf.script([(hf.INTRO, "-", 0), (9000, "A", 0)])
        for name, driver in (("R01_race_driver", args.driver), ("R02_race_driver_boost", args.driver + ",240")):
            if want(name):
                runs.append((name, courses["retail"], race, driver, "race", None))
    for name, course, text, driver, kind, action in runs:
        rows, digest = native_run(None, course, text, out / name, driver=driver)
        metrics = hf.race_metrics(rows) if kind == "race" else hf.analyse(kind, rows, action)
        rmetrics = reference[name]["metrics"]
        ref_rows = None
        if args.diff or name.startswith("R"):
            ref_rows, _ = hf.run(args.ref, course, text, out / (name + "-ref"), args.rom, driver)
            if args.diff:
                print("  " + first_divergence(ref_rows, rows))
        got = derived(name, rows, metrics)
        refm = derived(name, ref_rows or [], rmetrics)
        if name.startswith("C03"):
            refm["unlock_on_second_crossing"] = True
        checks = []
        for metric, rule, tol in TOLERANCES.get(name, []):
            ok, band = score(rule, refm.get(metric), got.get(metric), tol)
            checks.append({"metric": metric, "reference": refm.get(metric), "native": got.get(metric),
                           "band": band, "pass": bool(ok)})
        if name == "P04_turn_80" and "P04_turn_64" in table:
            same = table["P04_turn_64"]["metrics"] == metrics
            checks.append({"metric": "identical_to_stick_64", "reference": True, "native": same, "band": "exact",
                           "pass": same})
        passed = all(c["pass"] for c in checks)
        fails += not passed
        table[name] = {"pass": passed, "checks": checks, "metrics": metrics, "trace_hash": digest}
        bad = [c for c in checks if not c["pass"]]
        print("%-24s %s  %s" % (name, "PASS" if passed else "FAIL", "; ".join(
            "%s ref %s native %s (%s)" % (c["metric"], fmt(c["reference"]), fmt(c["native"]), c["band"])
            for c in (bad or checks[:2]))))
    if args.json:
        args.json.write_text(json.dumps(table, indent=1, sort_keys=True, default=str) + "\n")
    print("physics qualify: %d fixtures, %d fail" % (len(table), fails))
    return 1 if fails else 0


DIFF = {"speed": 0.01, "lateral": 0.5, "height": 0.5, "energy": 0.01, "heading_side": 0.001, "lap_distance": 1.0,
        "accel_force": 0.001, "slide_boost": 0.001}
EXACT = ("flags", "boost", "spin", "attack", "grip_a", "segment", "laps_completed", "intro")


def first_divergence(ref_rows, rows):
    for a, b in zip(ref_rows, rows):
        for k, tol in DIFF.items():
            if abs(a[k] - b[k]) > tol:
                return "first divergence tick %d %s ref %.6g native %.6g" % (a["tick"], k, a[k], b[k])
        for k in EXACT:
            mask = ~(1 << 13) if k == "flags" else -1  # recoil is a drawing flag; the model clears it each tick
            if (a[k] & mask) != (b[k] & mask):
                return "first divergence tick %d %s ref %s native %s" % (a["tick"], k, a[k], b[k])
    return "no divergence over %d ticks" % min(len(ref_rows), len(rows))


def fmt(v):
    return "%.4g" % v if isinstance(v, float) else str(v)


if __name__ == "__main__":
    sys.exit(main())
