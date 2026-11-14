#!/usr/bin/env python3
"""Dynamic float-operation counts per tick of the original racer code (card G1.3, part 1).

Runs the reference's profiling build (make -C ref PROFILE=1 OPT=-O0) on input scripts, then
weights every source line's static float operations (docs/census/float_ops_by_line.tsv, from
tools/census/closure.py) by its gcov execution count. Per-tick distributions come from the
per-tick call counts: ops in tick t = sum over functions of calls(t) x ops per call.

Tiers: the per-trace totals and means are exact counts of source-level operations in the
reference (reference tier). Per-tick p95 and max use ops per call averaged over the run
(MODELLED). Source operations are not SH-2 instructions; part 2 prices them.

Usage: float_ops.py --course REC.bin --scripts T01.txt ... [--rom ROM] --out DIR
Writes DIR/float_ops_by_function.tsv and DIR/float_ops_summary.tsv; prints the summary.
"""
from __future__ import annotations

import argparse
import collections
import csv
import json
from pathlib import Path
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build/ref/gcc-O0-prof"
KINDS = ["fadd", "fmul", "fdiv", "fsqrt", "fcmp", "fneg", "fcvt"]


def line_ops() -> dict[tuple[str, int], tuple[str, str, collections.Counter]]:
    table = {}
    with (ROOT / "docs/census/float_ops_by_line.tsv").open(encoding="utf-8") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            ops = collections.Counter({k: int(row[k]) for k in KINDS if int(row[k])})
            table[(Path(row["file"]).name, int(row["line"]))] = (row["function"], row["class"], ops)
    return table


def gcov_counts() -> tuple[dict[tuple[str, int], int], dict[str, int]]:
    lines, calls = {}, {}
    for gcda in sorted(BUILD.glob("*.gcda")):
        out = subprocess.run(["gcov", "--json-format", "--stdout", gcda.name], cwd=BUILD,
                             capture_output=True, text=True, check=True).stdout
        for doc in out.splitlines():
            if not doc.strip():
                continue
            for f in json.loads(doc)["files"]:
                name = Path(f["file"]).name
                for ln in f["lines"]:
                    lines[(name, ln["line_number"])] = lines.get((name, ln["line_number"]), 0) + ln["count"]
                for fn in f["functions"]:
                    calls[fn["name"]] = calls.get(fn["name"], 0) + fn["execution_count"]
    return lines, calls


def symbols(binary: Path) -> dict[int, str]:
    out = subprocess.run(["nm", str(binary)], capture_output=True, text=True, check=True).stdout
    result = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in "Tt":
            result[int(parts[0], 16)] = parts[2]
    return result


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--course", required=True)
    ap.add_argument("--scripts", nargs="+", required=True)
    ap.add_argument("--rom")
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    subprocess.run(["make", "-s", "-C", str(ROOT / "ref"), "PROFILE=1", "OPT=-O0"], check=True,
                   stdout=subprocess.DEVNULL)
    for gcda in BUILD.glob("*.gcda"):
        gcda.unlink()
    args.out.mkdir(parents=True, exist_ok=True)
    binary = BUILD / "ref"
    names = symbols(binary)
    per_tick_calls: list[collections.Counter] = []
    for script in args.scripts:
        prof = args.out / (Path(script).stem + "-calls.tsv")
        cmd = [str(binary), "--course", args.course, "--script", script, "--trace",
               str(args.out / (Path(script).stem + "-prof-trace.tsv")), "--profile", str(prof),
               "--stop-at-finish", "1"]
        if args.rom:
            cmd += ["--rom", args.rom]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        ticks: dict[int, collections.Counter] = collections.defaultdict(collections.Counter)
        with prof.open() as f:
            for line in f:
                tick, addr, n = line.split("\t")
                ticks[int(tick)][names.get(int(addr, 16), addr)] += int(n)
        n_ticks = sum(1 for _ in open(args.out / (Path(script).stem + "-prof-trace.tsv"))) - 3
        per_tick_calls += [ticks.get(t, collections.Counter()) for t in range(n_ticks)]

    table = line_ops()
    lines, calls = gcov_counts()
    total_ticks = len(per_tick_calls)
    by_fn: dict[str, collections.Counter] = collections.defaultdict(collections.Counter)
    cls_of = {}
    for key, (fn, cls, ops) in table.items():
        count = lines.get(key, 0)
        cls_of[fn] = cls
        for k, v in ops.items():
            by_fn[fn][k] += v * count
    # Tick share of each function: calls during ticks over all calls (the rest ran in init).
    tick_calls = collections.Counter()
    for c in per_tick_calls:
        tick_calls.update(c)
    rows, per_call = [], {}
    for fn, ops in by_fn.items():
        if cls_of[fn] != "TICK" or not calls.get(fn):
            continue
        share = tick_calls[fn] / calls[fn]
        tick_ops = collections.Counter({k: v * share for k, v in ops.items()})
        per_call[fn] = sum(ops.values()) / calls[fn]
        rows.append((fn, tick_calls[fn] / total_ticks, tick_ops))
    rows.sort(key=lambda r: -sum(r[2].values()))
    with (args.out / "float_ops_by_function.tsv").open("w", newline="") as f:
        w = csv.writer(f, delimiter="\t", lineterminator="\n")
        w.writerow(["function", "calls_per_tick", *[f"{k}_per_tick" for k in KINDS], "total_per_tick"])
        for fn, cpt, ops in rows:
            w.writerow([fn, f"{cpt:.3f}", *[f"{ops[k] / total_ticks:.1f}" for k in KINDS],
                        f"{sum(ops.values()) / total_ticks:.1f}"])
    totals = [sum(c[fn] * per_call.get(fn, 0.0) for fn in c) for c in per_tick_calls]
    racing = [t for t in totals if t > 0]
    kinds_mean = {k: sum(r[2][k] for r in rows) / total_ticks for k in KINDS}
    summary = {
        "ticks": total_ticks,
        "mean_ops_per_tick": statistics.fmean(totals),
        "p95_ops_per_tick": sorted(totals)[int(0.95 * (len(totals) - 1))],
        "max_ops_per_tick": max(totals),
        "mean_ops_per_racing_tick": statistics.fmean(racing) if racing else 0.0,
        **{f"mean_{k}_per_tick": v for k, v in kinds_mean.items()},
    }
    with (args.out / "float_ops_summary.tsv").open("w", newline="") as f:
        w = csv.writer(f, delimiter="\t", lineterminator="\n")
        for k, v in summary.items():
            w.writerow([k, f"{v:.1f}" if isinstance(v, float) else v])
    for k, v in summary.items():
        print(f"{k}\t{v:.1f}" if isinstance(v, float) else f"{k}\t{v}")
    print("top functions (ops per tick):")
    for fn, cpt, ops in rows[:12]:
        print(f"  {fn:36s} calls/tick {cpt:6.2f}  ops/tick {sum(ops.values()) / total_ticks:8.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
