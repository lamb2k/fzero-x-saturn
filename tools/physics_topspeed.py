#!/usr/bin/env python3
"""Stock top-speed signed residual (C-142 item 3, force-precision card), HOST.

For each stock machine (30) at engine 0, 0.5 and 1 the same script (intro, then A held on the straight
fixture course) runs on the reference (original code, fzx-ref) and on the native model (fzx-phys from a
pack baked from the same reference state). The metric is the SIGNED speed residual, native minus
reference, in Q16 steps (q = 2^-16 of a speed unit):

    end    the residual on the last tick of the run
    tail   the mean residual over the last TAIL ticks (smooths the one-tick rounding of the trace)
    slope  the least-squares slope of the residual over the last TAIL ticks, q per tick

and, over the 90 configurations, the mean, the standard error, the min and the max of `tail`. A force
constant stored Q16 shifts the equilibrium speed by (stored - exact) / (1 - damp), so the sign and size
differ per machine; the aggregate mean over stock machines is the systematic part. The pass condition of
the card is fixed in its header (generated/ is local; the numbers are in the evidence README), not here:
this tool only measures. Controls: --mutate NAME is passed to the native driver (fzx-phys --mutate), e.g.
damp0.9968, which must move the aggregate far outside the band.

Output is derived from the owner's ROM and course data: write it outside the repository.

    tools/physics_topspeed.py --ref FZX_REF --native FZX_PHYS --rom ROM --out DIR [--ticks 900] [--tail 100]
                              [--machines 0,1,...] [--engines 0,0.5,1] [--json OUT.json] [--mutate NAME]
"""
import argparse
import json
import math
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import handling_fixtures as hf  # noqa: E402
import physics_pack  # noqa: E402

Q = 1.0 / 65536
DAMP_FWD = 0.997   # the forward damping of the race code (saturn/physics_native.c .damp_fwd): equilibrium speed = force / (1 - damp), and a run of N ticks builds the shift (1 - damp^N) / (1 - damp)


def rows_of(path):
    rows, head = [], None
    for line in Path(path).read_text().splitlines():
        if line.startswith("# tick"):
            head = line[2:].split("\t")
        elif not line.startswith("#"):
            rows.append(dict(zip(head, line.split("\t"))))
    return rows


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise SystemExit("failed: %s\n%s" % (" ".join(cmd), r.stderr[-1500:]))
    return r.stdout


def residual_stats(res, tail):
    t = res[-tail:]
    mean = sum(t) / len(t)
    xs = list(range(len(t)))
    xm = sum(xs) / len(xs)
    slope = sum((x - xm) * (y - mean) for x, y in zip(xs, t)) / sum((x - xm) ** 2 for x in xs)
    return dict(end=res[-1], tail=mean, slope=slope)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--ref", type=Path, required=True)
    ap.add_argument("--native", type=Path, required=True)
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--ticks", type=int, default=600, help="ticks of A held after the intro (the P01 launch length: the speed is still rising, so the residual is a rate of drift, not an equilibrium)")
    ap.add_argument("--force-scale", type=float, default=1.0, help="divide the native trace's accel_force column by this (a native build that carries the force with more fraction bits prints it scaled)")
    ap.add_argument("--tail", type=int, default=100)
    ap.add_argument("--machines", default=",".join(str(i) for i in range(30)))
    ap.add_argument("--engines", default="0,0.5,1")
    ap.add_argument("--json", type=Path)
    ap.add_argument("--mutate", default="")
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    course = a.out / "course-straight.bin"
    course.write_bytes(hf.straight_course())
    script = a.out / "in.txt"
    script.write_text(hf.script([(hf.INTRO, "-", 0), (a.ticks, "A", 0)]))
    out = {}
    for c in (int(x) for x in a.machines.split(",")):
        for e in (float(x) for x in a.engines.split(",")):
            cfg = "%d-%g" % (c, e)
            w = a.out / cfg
            w.mkdir(exist_ok=True)
            base = [str(a.ref), "--course", str(course), "--inputs", str(script), "--rom", str(a.rom), "--machine", "%d,%g" % (c, e)]
            run(base + ["--out", str(w / "ref.tsv"), "--bake", str(w / "source.txt")])
            pack = w / "course.fzxp"
            pack.write_bytes(physics_pack.build(physics_pack.parse(w / "source.txt")))
            cmd = [str(a.native), "--pack", str(pack), "--inputs", str(script), "--out", str(w / "native.tsv"), "--zones", "1"]
            if a.mutate:
                cmd += ["--mutate", a.mutate]
            run(cmd)
            r, n = rows_of(w / "ref.tsv"), rows_of(w / "native.tsv")
            m = min(len(r), len(n))
            res = [(float(n[i]["speed"]) - float(r[i]["speed"])) / Q for i in range(hf.INTRO, m)]
            st = residual_stats(res, a.tail)
            F = physics_pack.parse(w / "source.txt")["machine"][2][2]          # the reference's force_high (binary32, exact)
            pred = (physics_pack.fx(F, 16) - F * 65536.0) * (1.0 - DAMP_FWD ** a.ticks) / (1.0 - DAMP_FWD)  # the Q16 rounding of the constant, as the speed shift it builds over the run, q
            st["force_residual_q"] = (float(n[m - 1]["accel_force"]) / a.force_scale - float(r[m - 1]["accel_force"])) / Q
            st.update(ref_end=float(r[m - 1]["speed"]), native_end=float(n[m - 1]["speed"]), force_high=F, stored_minus_exact_q=physics_pack.fx(F, 16) - F * 65536.0,
                      predicted_shift_q=pred)
            out[cfg] = st
    tails = [v["tail"] for v in out.values()]
    mean = sum(tails) / len(tails)
    sd = math.sqrt(sum((x - mean) ** 2 for x in tails) / max(1, len(tails) - 1))
    summary = dict(configs=len(tails), tail_mean_q=mean, tail_se_q=sd / math.sqrt(len(tails)), tail_sd_q=sd, tail_min_q=min(tails),
                   tail_max_q=max(tails), slope_mean_q_per_tick=sum(v["slope"] for v in out.values()) / len(out),
                   negative=sum(1 for x in tails if x < 0), mutate=a.mutate)
    summary.update(tail_rms_q=math.sqrt(sum(x * x for x in tails) / len(tails)), tail_mean_abs_q=sum(abs(x) for x in tails) / len(tails))
    pr = [v["predicted_shift_q"] for v in out.values()]
    pm = sum(pr) / len(pr)
    summary["corr_with_predicted_rounding_shift"] = (sum((x - mean) * (y - pm) for x, y in zip(tails, pr)) /
                                                    math.sqrt(sum((x - mean) ** 2 for x in tails) * sum((y - pm) ** 2 for y in pr)) if sd > 0 else None)
    fr = [v["force_residual_q"] for v in out.values()]
    summary.update(force_residual_mean_q=sum(fr) / len(fr), force_residual_mean_abs_q=sum(abs(x) for x in fr) / len(fr), force_residual_max_abs_q=max(abs(x) for x in fr))
    summary["predicted_shift_mean_abs_q"] = sum(abs(y) for y in pr) / len(pr)
    print(json.dumps(summary, sort_keys=True))
    if a.json:
        a.json.write_text(json.dumps(dict(summary=summary, configs=out), indent=1, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
