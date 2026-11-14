#!/usr/bin/env python3
"""Price per-tick float operations in SH-2 cycles for SH2ZAM fixed point (card G1.3, part 2).

Input: DIR/float_ops_by_function.tsv and DIR/float_ops_summary.tsv from float_ops.py.
Prices: mul, div and sqrt are the median modelled cycles per call of SH2ZAM's own math model
(third_party/SH2ZAM/saturn/benchmarks/math-model/results.json, `bounded` profile: Yabause SH-2
interpreter, flat RAM, call overhead and result writes included). add/sub, compare, negate and
convert have no API call; they are priced as short inline sequences including one load and one
store (assumptions written below, not measurements).

Everything here is MODELLED. A source-level float operation is not one fixed-point call, and
integer work, memory traffic and control flow are not counted: the report gives the float-only
figure and the same figure times an overhead factor. Real costs come from the emulator image
(G1.3 control) and, finally, hardware.

Usage: cycle_model.py DIR [--overhead 3]
"""
from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[2]
RESULTS = ROOT / "third_party/SH2ZAM/saturn/benchmarks/math-model/results.json"
SH2_HZ = 28_636_360          # NTSC Saturn SH-2 clock
FIELD_S = 1001 / 60000       # NTSC field
MASTER_BUDGET_S = 0.008      # plan section 4.4: master line, proposed until measured
INLINE = {"fadd": 4, "fcmp": 4, "fneg": 2, "fcvt": 4}  # assumed: load, op, store


def sh2zam_prices() -> dict[str, float]:
    data = json.loads(RESULTS.read_text())
    med = {}
    for rec in data["records"]:
        if rec["profile"] == "bounded" and rec["operation"] in ("mul", "div", "sqrt"):
            med[rec["operation"]] = statistics.median(s["after"]["cycles"] for s in rec["samples"]) / data["batchSize"]
    return {"fmul": med["mul"], "fdiv": med["div"], "fsqrt": med["sqrt"], **INLINE}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir", type=Path)
    ap.add_argument("--overhead", type=float, default=3.0)
    args = ap.parse_args()
    price = sh2zam_prices()
    summary = {r[0]: float(r[1]) for r in csv.reader(open(args.dir / "float_ops_summary.tsv"), delimiter="\t")}
    mean_ops = summary["mean_ops_per_tick"]
    mean_cycles = sum(summary[f"mean_{k}_per_tick"] * p for k, p in price.items())
    per_op = mean_cycles / mean_ops
    budget = SH2_HZ * MASTER_BUDGET_S
    field = SH2_HZ * FIELD_S
    rows = []
    with open(args.dir / "float_ops_by_function.tsv") as f:
        for r in csv.DictReader(f, delimiter="\t"):
            cyc = sum(float(r[f"{k}_per_tick"]) * p for k, p in price.items())
            rows.append((r["function"], float(r["calls_per_tick"]), float(r["total_per_tick"]), cyc))
    rows.sort(key=lambda x: -x[3])
    with open(args.dir / "cycle_model.tsv", "w", newline="") as f:
        w = csv.writer(f, delimiter="\t", lineterminator="\n")
        w.writerow(["function", "calls_per_tick", "float_ops_per_tick", "modelled_cycles_per_tick",
                    "share_of_8ms_master_line"])
        for fn, calls, ops, cyc in rows:
            w.writerow([fn, f"{calls:.2f}", f"{ops:.1f}", f"{cyc:.0f}", f"{cyc / budget:.4f}"])
    print("prices (cycles):", {k: round(v, 1) for k, v in price.items()})
    print(f"master line {budget:.0f} cycles (8 ms); field {field:.0f} cycles")
    for label, ops in (("mean", mean_ops), ("p95", summary["p95_ops_per_tick"]), ("max", summary["max_ops_per_tick"])):
        c = ops * per_op
        print(f"{label:4s} {ops:7.1f} ops  {c:8.0f} cycles float-only ({100 * c / budget:5.1f}% of 8 ms)"
              f"  x{args.overhead:g} {c * args.overhead:8.0f} ({100 * c * args.overhead / budget:5.1f}%)")
    print("top functions by modelled cycles per tick:")
    for fn, calls, ops, cyc in rows[:10]:
        print(f"  {fn:36s} {ops:7.1f} ops  {cyc:7.0f} cycles  {100 * cyc / budget:5.2f}%")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
