#!/usr/bin/env python3
"""E1 bounded machine check (C-056 item 7): other stock machines and engine settings.

Picks, from the baked stats of the 30 stock machines (all at engine 0.5), the lightest
and heaviest by machine weight and the best grip by the baked grip limit, dropping
overlaps in that order. For each pick at engine 0, 0.5 and 1 it runs launch (P01),
coast (P02), plain steering (P04 stick 64), the boosted grip-loading turn (P10) and the
grip break (P11), and the long grip break (P12: eight boosts, so the heaviest
machine at engine 0 and 0.5 reaches its limit) on
the reference (gcc and clang, which must agree; their values are frozen to
machines-reference.json before any native run) and on the native model, and scores
native against each configuration's own reference with the P01/P02/P04/P10 bands.
Controls: grip +5% and turn rate +2% must each be rejected in every configuration.

    tools/physics_machines.py --ref GCC_REF --ref2 CLANG_REF --native PHYS --rom ROM --out DIR
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import handling_fixtures as hf  # noqa: E402
import physics_pack  # noqa: E402
import physics_qualify as pq  # noqa: E402

# Stock machine weights (sDefaultMachines, the last field), by character index.
WEIGHTS = [1260, 1420, 1620, 1960, 1150, 1330, 1870, 1780, 990, 780, 1010, 1780, 1390, 910, 2060, 2340, 2110,
           1050, 860, 1140, 900, 1460, 1620, 2220, 1530, 1520, 950, 1490, 1010, 1170]
ENGINES = (0.0, 0.5, 1.0)
FIXTURES = ("P01_launch", "P02_coast", "P04_turn_64", "P10_boost_turn", "P11_grip_break",
            "P12_grip_break_long")
CONTROLS = ("grip+5%", "turn+2%")


def rows_of(path):
    rows, head = [], None
    for line in Path(path).read_text().splitlines():
        if line.startswith("# tick"):
            head = line[2:].split("\t")
        elif not line.startswith("#"):
            row = {}
            for k, v in zip(head, line.split("\t")):
                row[k] = int(v, 16) if k in ("button", "flags") else (int(v) if v.lstrip("-").isdigit() else float(v))
            rows.append(row)
    return rows


def run(cmd, work):
    work.mkdir(parents=True, exist_ok=True)
    r = subprocess.run(cmd + ["--out", str(work / "t.tsv")], capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(" ".join(cmd) + ": " + r.stderr.strip())
    return rows_of(work / "t.tsv"), r.stdout.split()[-1]


def machine_stats(ref, rom, course, out):
    names = ("force_low force_slide force_high crossover boost_mul plate_mul slide_max slide_decay slew_base "
             "turn_free turn_grip top_mix accel_cutoff grip_limit slide_limit").split()
    stats = {}
    (out / "one.txt").write_text("1 - 0 0\n")
    for c in range(30):
        src = out / ("stats-%d.txt" % c)
        subprocess.run([str(ref), "--course", str(course), "--inputs", str(out / "one.txt"), "--rom", str(rom),
                        "--machine", "%d,0.5" % c, "--bake", str(src)], check=True, capture_output=True)
        stats[c] = dict(zip(names, physics_pack.parse(src)["machine"][2]))
    return stats


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--ref", type=Path, required=True)
    ap.add_argument("--ref2", type=Path, required=True, help="the clang build, which must agree")
    ap.add_argument("--native", type=Path, required=True)
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    courses = {"pad": out / "course-pad.bin", "corridor": out / "course-corridor.bin",
               "straight": out / "course-straight.bin"}
    courses["pad"].write_bytes(hf.pad_course())
    courses["corridor"].write_bytes(hf.corridor_course())
    courses["straight"].write_bytes(hf.straight_course())
    stats = machine_stats(args.ref, args.rom, courses["pad"], out)
    lightest = min(range(30), key=lambda c: (WEIGHTS[c], c))
    heaviest = max(range(30), key=lambda c: (WEIGHTS[c], -c))
    ranked = sorted(range(30), key=lambda c: (-stats[c]["grip_limit"], c))
    grip = next(c for c in ranked if c not in (lightest, heaviest))
    picks = {"lightest": lightest, "heaviest": heaviest, "best_grip": grip}
    print("picks: lightest %d (weight %d), heaviest %d (weight %d), best grip %d (grip limit %.4f at 0.5)" % (
        lightest, WEIGHTS[lightest], heaviest, WEIGHTS[heaviest], grip, stats[grip]["grip_limit"]))
    fx = hf.fixtures()
    frozen, table, fails = {}, {}, 0
    for role, c in picks.items():
        for e in ENGINES:
            cfg = "%s-%d-%.1f" % (role, c, e)
            sel = ["--machine", "%d,%g" % (c, e)]
            frozen[cfg], table[cfg] = {}, {}
            for name in FIXTURES:
                course, lines, action, kind, driver = fx[name]
                work = out / cfg / name
                work.mkdir(parents=True, exist_ok=True)
                (work / "in.txt").write_text(hf.script(lines))
                base = ["--course", str(courses[course]), "--inputs", str(work / "in.txt"), "--rom", str(args.rom)] + sel
                if driver:
                    base += ["--driver", driver]
                r1, h1 = run([str(args.ref)] + base, work / "gcc")
                r2, h2 = run([str(args.ref2)] + base, work / "clang")
                if h1 != h2:
                    raise SystemExit("%s %s: gcc and clang references differ" % (cfg, name))
                ref_m = hf.analyse(kind, r1, action)
                frozen[cfg][name] = {"metrics": ref_m, "trace_hash": h1}
            (out / "machines-reference.json").write_text(json.dumps(frozen, indent=1, sort_keys=True) + "\n")
    for role, c in picks.items():
        for e in ENGINES:
            cfg = "%s-%d-%.1f" % (role, c, e)
            src = out / cfg / "src"
            src.mkdir(parents=True, exist_ok=True)
            packs = {}
            for course in ("pad", "corridor", "straight"):
                subprocess.run([str(args.ref), "--course", str(courses[course]), "--inputs", str(out / "one.txt"),
                                "--rom", str(args.rom), "--machine", "%d,%g" % (c, e), "--bake",
                                str(src / (course + ".txt"))], check=True, capture_output=True)
                packs[course] = src / (course + ".fzxp")
                packs[course].write_bytes(physics_pack.build(physics_pack.parse(src / (course + ".txt"))))
            for mutate in (None,) + CONTROLS:
                rejected = False
                for name in FIXTURES:
                    course, lines, action, kind, driver = fx[name]
                    work = out / cfg / name
                    cmd = [str(args.native), "--pack", str(packs[course]), "--inputs", str(work / "in.txt")]
                    if driver:
                        cmd += ["--driver", driver]
                    if mutate:
                        cmd += ["--mutate", mutate]
                    rows, _ = run(cmd, work / ("native" if not mutate else "native-" + mutate))
                    got = hf.analyse(kind, rows, action)
                    ref_m = frozen[cfg][name]["metrics"]
                    checks = []
                    for metric, rule, tol in pq.TOLERANCES[name]:
                        ok, band = pq.score(rule, ref_m.get(metric), got.get(metric), tol)
                        checks.append({"metric": metric, "reference": ref_m.get(metric), "native": got.get(metric),
                                       "band": band, "pass": bool(ok)})
                    passed = all(ch["pass"] for ch in checks)
                    if mutate is None:
                        table[cfg][name] = {"pass": passed, "checks": checks}
                        fails += not passed
                    elif not passed:
                        rejected = True
                if mutate:
                    table[cfg]["control " + mutate] = {"rejected": rejected}
                    fails += not rejected
            line = " ".join("%s %s" % (n.split("_")[0], "PASS" if table[cfg][n]["pass"] else "FAIL") for n in FIXTURES)
            ctl = " ".join("%s %s" % (m, "caught" if table[cfg]["control " + m]["rejected"] else "MISSED")
                           for m in CONTROLS)
            print("%-22s %s | %s" % (cfg, line, ctl))
    (out / "machines-native.json").write_text(json.dumps(table, indent=1, sort_keys=True, default=str) + "\n")
    print("machine check: %d configurations, %d failures" % (len(table), fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
