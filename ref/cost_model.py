#!/usr/bin/env python3
"""G1.3 tick cost model: turn ref/cost.sh's callgrind dumps into the ranked table.

    cost_model.py OUT_DIR CLOSURE_TSV SH2ZAM_RESULTS_JSON

Counts are exact executed x86 instructions from callgrind (HOST tier). Each binary32 SSE
instruction is classed by kind (add, mul, div, sqrt, cmp, cvt, neg); calls into libm are
counted per call. The gcc -O0 build gives the per-function attribution (no inlining); the
gcc -O2 build gives a second total. Cycles are MODELLED: op counts times a per-op SH-2
cost (see COSTS), plus the non-float instructions at -O2 as a one-cycle-each proxy.
Writes OUT_DIR/cost_table.tsv and OUT_DIR/SUMMARY.md.
"""
import collections
import glob
import json
import os
import re
import statistics
import sys

MASTER_HZ = 28_636_360  # NTSC Saturn SH-2 clock
BUDGET_MS = 8.0         # plan section 4.4: master simulation line
BUDGET_CYCLES = MASTER_HZ * BUDGET_MS / 1000

KIND = {
    "addss": "add", "subss": "add", "addsd": "add", "subsd": "add",
    "mulss": "mul", "mulsd": "mul",
    "divss": "div", "divsd": "div",
    "sqrtss": "sqrt", "sqrtsd": "sqrt",
    "comiss": "cmp", "ucomiss": "cmp", "comisd": "cmp", "ucomisd": "cmp",
    "cmpss": "cmp", "minss": "cmp", "maxss": "cmp",
    "cvtsi2ss": "cvt", "cvttss2si": "cvt", "cvtss2si": "cvt", "cvtss2sd": "cvt", "cvtsd2ss": "cvt",
    "cvtsi2sd": "cvt", "cvttsd2si": "cvt",
    "xorps": "neg", "andps": "neg", "xorpd": "neg", "andpd": "neg",
}
DOUBLE = {"addsd", "subsd", "mulsd", "divsd", "sqrtsd", "comisd", "ucomisd", "cvtss2sd", "cvtsd2ss",
          "cvtsi2sd", "cvttsd2si", "xorpd", "andpd"}
KINDS = ["add", "mul", "div", "sqrt", "cmp", "cvt", "neg", "libm"]

# Modelled SH-2 cycles per operation in Q16.16 fixed point.
#   inline: hand-inlined SH-2 sequences from the SH-2 manual's issue/latency tables
#           (add 1; dmuls.l+sts+sts+xtrct ~6; cmp/xx+bt 2; shift 2; neg 1; hardware DIVU 39 + setup).
#   sh2zam: SH2ZAM's modelled per-call cost (math-model results.json, optimized "after"),
#           which includes call and range-wrapper overhead; kinds the suite lacks use inline.
INLINE = {"add": 1, "mul": 6, "div": 45, "sqrt": None, "cmp": 2, "cvt": 2, "neg": 1}
SUITE_OP = {"mul": "mul", "div": "div", "sqrt": "sqrt"}
LIBM_OP = {"sqrtf": "sqrt", "sqrt": "sqrt", "sinf": "sin", "cosf": "cos", "atan2f": "atan2",
           "acosf": "acos", "asinf": "asin", "tanf": "tan", "atanf": "atan"}


def load_suite(path):
    data = json.load(open(path))
    out = {}
    for rec in data["records"]:
        if rec.get("profile") == "bounded":
            out[rec["operation"]] = rec["cyclesPerCall"]["after"]
    return out


def costs(suite):
    inline = dict(INLINE)
    inline["sqrt"] = suite["sqrt"]  # no cheaper inline square root on the SH-2
    sh2zam = dict(inline)
    for kind, op in SUITE_OP.items():
        sh2zam[kind] = suite[op]
    return {"inline": inline, "sh2zam": sh2zam}


def load_dis(path):
    """address -> (function, mnemonic) from objdump -d."""
    addr = {}
    fn = None
    head = re.compile(r"^[0-9a-f]+ <([^>]+)>:")
    line = re.compile(r"^\s+([0-9a-f]+):\s+(\S+)\s*(.*)$")
    for text in open(path):
        m = head.match(text)
        if m:
            fn = m.group(1)
            continue
        m = line.match(text)
        if m and fn:
            mn, ops = m.group(2), m.group(3)
            if mn in ("xorps", "xorpd") and len(set(o.strip() for o in ops.split(","))) == 1:
                mn = "zero"  # xorps %xmm0,%xmm0 clears a register; not an operation
            addr[int(m.group(1), 16)] = (fn, mn)
    return addr


def parse_dump(path, binary, dis):
    """One callgrind dump -> {function: Counter(kind -> count, 'ir' -> x86 instrs, 'dbl' -> double ops)}."""
    per = collections.defaultdict(collections.Counter)
    ob = fn = cfn = None
    skip = False
    with open(path) as f:
        for text in f:
            if skip:
                skip = False
                continue
            c = text[0]
            if c == "0":
                if ob != binary:
                    continue
                a, _, cost = text.split()
                a, cost = int(a, 16), int(cost)
                fname, mn = dis.get(a, (fn, "?"))
                ctr = per[fname]
                ctr["ir"] += cost
                k = KIND.get(mn)
                if k:
                    ctr[k] += cost
                    if mn in DOUBLE:
                        ctr["dbl"] += cost
            elif text.startswith("ob="):
                ob = text[3:].strip()
            elif text.startswith("fn="):
                fn = text[3:].strip()
            elif text.startswith("cfn="):
                cfn = text[4:].strip()
            elif text.startswith("calls="):
                skip = True  # next line is the inclusive cost of the call
                if ob == binary and cfn in LIBM_OP:
                    n = int(text.split()[0][6:])
                    per[fn]["libm"] += n
                    per[fn]["libm:" + cfn] += n
    return per


def load_trace(path):
    rows = []
    for text in open(path):
        if text.startswith("#") or text.startswith("hash"):
            continue
        f = text.rstrip("\n").split("\t")
        rows.append({"intro": int(f[1]), "energy": int(f[12], 16) & 0x7FFFFFFF})
    return rows


def load_census(path):
    tiers = {}
    for i, text in enumerate(open(path)):
        f = text.rstrip("\n").split("\t")
        if i and len(f) > 6:
            tiers[f[0]] = f[6]
    return tiers


def pct(values, q):
    v = sorted(values)
    return v[min(len(v) - 1, int(q * len(v)))] if v else 0


def model_cycles(ctr, table, suite):
    c = sum(ctr[k] * table[k] for k in KINDS if k != "libm")
    for key, n in ctr.items():
        if key.startswith("libm:"):
            c += n * suite[LIBM_OP[key[5:]]]
    return c


def collect(out, opt, dis):
    binary = None
    ticks = []  # list of (script, tick, per-function counters, trace row)
    for d in sorted(glob.glob(os.path.join(out, "gcc" + opt, "T*"))):
        trace = load_trace(os.path.join(d, "trace.tsv"))
        dumps = glob.glob(os.path.join(d, "cg.*.*"))
        if binary is None:
            for text in open(dumps[0]):
                if text.startswith("cmd:"):
                    binary = text.split()[1]
                    break
        for p in dumps:
            part = int(p.rsplit(".", 1)[1])
            if part < 2 or part > len(trace):  # part 1 also holds start-up and race init
                continue
            ticks.append((os.path.basename(d), part - 1, parse_dump(p, binary, dis), trace[part - 1]))
    return ticks


def main():
    out, census_path, suite_path = sys.argv[1:4]
    suite = load_suite(suite_path)
    table = costs(suite)
    tiers = load_census(census_path)

    t0 = collect(out, "-O0", load_dis(os.path.join(out, "gcc-O0.dis")))
    t2 = collect(out, "-O2", load_dis(os.path.join(out, "gcc-O2.dis")))
    race = [t for t in t0 if t[3]["intro"] == 0]
    live = [t for t in race if t[3]["energy"] != 0]
    sample = live if live else race
    n = len(sample)

    fns = sorted({f for t in sample for f in t[2]})
    rows = []
    for f in fns:
        vals = [t[2].get(f, collections.Counter()) for t in sample]
        ops = [sum(v[k] for k in KINDS) for v in vals]
        inl = [model_cycles(v, table["inline"], suite) for v in vals]
        shz = [model_cycles(v, table["sh2zam"], suite) for v in vals]
        kinds = {k: sum(v[k] for v in vals) / n for k in KINDS}
        rows.append({
            "function": f, "tier": tiers.get(f, "STUB" if f.startswith("stub") else "-"),
            "ops_mean": sum(ops) / n, "ops_p95": pct(ops, 0.95), "ops_max": max(ops),
            "ir_mean": sum(v["ir"] for v in vals) / n, "dbl": sum(v["dbl"] for v in vals),
            "inline_mean": sum(inl) / n, "inline_p95": pct(inl, 0.95),
            "sh2zam_mean": sum(shz) / n, "sh2zam_p95": pct(shz, 0.95), "sh2zam_max": max(shz),
            **{"k_" + k: kinds[k] for k in KINDS},
        })
    rows.sort(key=lambda r: -r["sh2zam_mean"])

    def totals(ticks):
        float_ops, ir, inl, shz = [], [], [], []
        for t in ticks:
            c = collections.Counter()
            for v in t[2].values():
                c.update(v)
            float_ops.append(sum(c[k] for k in KINDS))
            ir.append(c["ir"])
            inl.append(model_cycles(c, table["inline"], suite))
            shz.append(model_cycles(c, table["sh2zam"], suite))
        return float_ops, ir, inl, shz

    f0, ir0, inl0, shz0 = totals(sample)
    race2 = [t for t in t2 if t[3]["intro"] == 0 and (t[3]["energy"] != 0 or not live)]
    f2, ir2, inl2, shz2 = totals(race2)
    other2 = [i - f for i, f in zip(ir2, f2)]  # non-float x86 instructions at -O2

    cols = ["function", "tier", "ops_mean", "ops_p95", "ops_max", "ir_mean", "inline_mean", "inline_p95",
            "sh2zam_mean", "sh2zam_p95", "sh2zam_max"] + ["k_" + k for k in KINDS] + ["dbl"]
    with open(os.path.join(out, "cost_table.tsv"), "w") as f:
        f.write("\t".join(cols) + "\n")
        for r in rows:
            f.write("\t".join(r[c] if isinstance(r[c], str) else "%.1f" % r[c] for c in cols) + "\n")

    def line(name, v):
        return "| %s | %.0f | %d | %d |" % (name, statistics.mean(v), pct(v, 0.95), max(v))

    def share(c):
        return "%.1f%%" % (100 * c / BUDGET_CYCLES)

    md = []
    md.append("# G1.3 tick cost model (synthetic fixture)\n")
    md.append("Generated by `ref/cost.sh` / `ref/cost_model.py`. HOST counts, MODELLED cycles. "
              "Ticks sampled: %d race ticks with energy left (%d race ticks, %d scripts T01 to T10). "
              "Fixture course and fixture machine constants, not Mute City 1: rerun on the owner's ROM "
              "at G1.6.\n" % (n, len(race), len({t[0] for t in t0})))
    md.append("## Per tick, whole closure\n")
    md.append("| Quantity | mean | p95 | max |\n| --- | --- | --- | --- |")
    md.append(line("float ops (gcc -O0)", f0))
    md.append(line("float ops (gcc -O2)", f2))
    md.append(line("x86 instructions (gcc -O0)", ir0))
    md.append(line("x86 instructions (gcc -O2)", ir2))
    md.append(line("MODELLED float cycles, inline", inl0))
    md.append(line("MODELLED float cycles, SH2ZAM calls", shz0))
    md.append(line("non-float x86 instructions (gcc -O2), 1 cycle each", other2))
    fa, ira, ina, sha = totals(race)
    md.append(line("all race ticks, incl. no energy: float ops (gcc -O0)", fa))
    md.append(line("all race ticks, incl. no energy: MODELLED float cycles, SH2ZAM calls", sha))
    libm = collections.Counter()
    for t in sample:
        for v in t[2].values():
            libm.update({k[5:]: c for k, c in v.items() if k.startswith("libm:")})
    md.append("\nlibm calls per tick (mean): " + (", ".join("%s %.2f" % (k, c / n) for k, c in sorted(libm.items())) or "none") + ".")
    tot_mean = statistics.mean(shz0) + statistics.mean(other2)
    tot_p95 = pct(shz0, 0.95) + pct(other2, 0.95)
    tot_max = max(shz0) + max(other2)
    md.append("\nMODELLED master tick (SH2ZAM calls + integer proxy): mean %.0f, p95 %.0f, max %.0f cycles; "
              "**%s / %s / %s** of the %.0f-cycle (%.0f ms) master line.\n"
              % (tot_mean, tot_p95, tot_max, share(tot_mean), share(tot_p95), share(tot_max),
                 BUDGET_CYCLES, BUDGET_MS))
    md.append("## Ranked by modelled SH2ZAM cycles per tick (top 25)\n")
    md.append("| # | function | tier | float ops mean / p95 / max | add | mul | div | sqrt | cmp | cvt | neg | libm "
              "| cycles inline mean | cycles SH2ZAM mean / p95 | share of 8 ms (mean) |")
    md.append("| " + " | ".join(["---"] * 15) + " |")
    for i, r in enumerate(rows[:25], 1):
        md.append("| %d | `%s` | %s | %.1f / %d / %d | %s | %.0f | %.0f / %d | %s |" % (
            i, r["function"], r["tier"], r["ops_mean"], r["ops_p95"], r["ops_max"],
            " | ".join("%.1f" % r["k_" + k] for k in KINDS), r["inline_mean"], r["sh2zam_mean"],
            r["sh2zam_p95"], share(r["sh2zam_mean"])))
    md.append("\n## Cost table (MODELLED cycles per op)\n")
    md.append("| kind | inline | SH2ZAM call | source |\n| --- | --- | --- | --- |")
    src = {"add": "SH-2 manual: add/sub 1", "mul": "dmuls.l + sts mach/macl + xtrct",
           "div": "inline: SH-2 DIVU unit (39) + setup; SH2ZAM: software div", "sqrt": "SH2ZAM sqrt (both)",
           "cmp": "cmp/xx + bt", "cvt": "shift", "neg": "neg"}
    for k in ["add", "mul", "div", "sqrt", "cmp", "cvt", "neg"]:
        md.append("| %s | %s | %s | %s |" % (k, table["inline"][k], table["sh2zam"][k], src[k]))
    md.append("| libm | per call | per call | SH2ZAM %s |" % ", ".join(
        "%s %.0f" % (op, suite[op]) for op in sorted(set(LIBM_OP.values()))))
    dbl = sum(r["dbl"] for r in rows)
    md.append("\nDouble-precision instructions seen in the sample: %d." % dbl)
    with open(os.path.join(out, "SUMMARY.md"), "w") as f:
        f.write("\n".join(md) + "\n")
    print("\n".join(md))


if __name__ == "__main__":
    main()
