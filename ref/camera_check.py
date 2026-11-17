#!/usr/bin/env python3
"""HOST source-camera gate: two cameras, exact compiler/restart traces, controls.

All inputs here are synthetic. Retail input/trace checks are local evidence only.
"""
from pathlib import Path
import argparse
import hashlib
import math
import os
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "ref/build/camera-check"
ALLOWED_CAMERA_STUBS = {"Audio_SetEnemyEnginePan"}


def inspect(path):
    rows = []
    for line in path.read_text().splitlines():
        parts = line.split()
        if len(parts) != 65:
            raise ValueError("camera trace schema")
        tick, index = map(int, parts[:2])
        words = [int(x, 16) for x in parts[2:]]
        if (tick, index) != (len(rows)//2, len(rows)%2) or words[-1]:
            raise ValueError("camera trace order or mapping fault")
        floats = [struct.unpack("<f", struct.pack("<I", w))[0] for w in words[4:62]]
        if not all(math.isfinite(x) for x in floats):
            raise ValueError("nonfinite camera")
        f = lambda n: floats[n-4]
        if not 0 < f(20) < f(21) or not 0 < f(19) < 180:
            raise ValueError("camera projection range")
        if f(46) <= 0 or f(51) <= 0 or f(57) != -1 or f(61) != 0:
            raise ValueError("camera projection convention")
        if index == 0 and (f(22) <= 0 or f(23) <= 0):
            raise ValueError("empty race camera viewport")
        # Stock mode 12 initializes matrices but no viewport. Records-entry UI
        # installs its own viewport when drawn; this race harness does not draw it.
        if index == 1 and (words[0] != 12 or any(f(j) != 0 for j in range(22,30))):
            raise ValueError("dormant records camera changed")
        rows.append(words)
    if not rows or len(rows)%2:
        raise ValueError("empty or partial camera trace")
    return rows


def calls(path):
    for line in path.read_text().splitlines():
        name, count = line.split()
        if name.startswith("camera:") and name[7:] not in ALLOWED_CAMERA_STUBS and int(count):
            raise ValueError("unimplemented camera dependency: " + name)


def run(binary, name, prefix=None):
    suffix = "-restart" if prefix else ""
    trace = OUT / (binary.parent.name + "-" + name + suffix + ".txt")
    report = trace.with_suffix(".calls")
    env = dict(os.environ, REF_STUB_CALL_REPORT=str(report))
    cmd = [str(binary), "--course", str(OUT/"course.bin"), "--byte-order", "big",
           "--inputs", str(ROOT/"ref/inputs"/(name+".txt")), "--camera-trace", str(trace)]
    if prefix:
        cmd += ["--prefix", str(ROOT/"ref/inputs"/(prefix+".txt"))]
    result = subprocess.check_output(cmd, env=env, text=True).split()
    rows = inspect(trace)
    calls(report)
    return result[3], hashlib.sha256(trace.read_bytes()).hexdigest(), rows


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--record", action="store_true", help="bootstrap synthetic expected hashes")
    ap.add_argument("--no-build", action="store_true")
    args = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    subprocess.run(["python3", "tools/camera_assets.py", "--art", "fixture"], cwd=ROOT, check=True)
    subprocess.run(["python3", "ref/fixture_course.py", str(OUT/"course.bin")], cwd=ROOT, check=True)
    expected_race = dict(x.split() for x in (ROOT/"ref/expected/fixture.hashes").read_text().splitlines())
    names = list(expected_race) + ["camera_controls"]
    records = {}
    for cc, opt in (("gcc", "-O0"), ("gcc", "-O2"), ("clang", "-O0"), ("clang", "-O2")):
        binary = ROOT/"ref/build"/(cc+opt+"-camera-fixture")/"fzx-ref"
        if not args.no_build:
            with (OUT/(cc+opt+"-build.log")).open("w") as log:
                subprocess.run(["bash", "ref/build.sh", cc, opt], cwd=ROOT,
                               env=dict(os.environ, FZX_SOURCE_CAMERA="1", REF_CAMERA_ART="fixture", FZX_SOURCE_MACHINE_MENU="0"),
                               stdout=log, stderr=subprocess.STDOUT, check=True)
        for name in names:
            race, camera, rows = run(binary, name)
            if name in expected_race and race != expected_race[name]:
                raise ValueError("simulation changed: " + name)
            if name in records and records[name] != camera:
                raise ValueError("compiler camera divergence: " + name)
            records[name] = camera
            if name == "camera_controls":
                primary = rows[::2]
                if primary[299][0] != 4 or primary[300][0] != 6:
                    raise ValueError("intro/chase transition changed")
                if {r[1] for r in primary[500:]} != {0,1,2,3} or max(r[3] for r in primary) != 8:
                    raise ValueError("camera controls did not exercise every setting/lookback")
        print(cc, opt, "11 two-camera traces PASS", flush=True)
    binary = ROOT/"ref/build/gcc-O2-camera-fixture/fzx-ref"
    for name in ("T02_full_throttle", "T05_slalom", "T10_random"):
        race, camera, _ = run(binary, name, "T06_boost")
        if camera != records[name] or race != expected_race[name]:
            raise ValueError("camera restart divergence: " + name)
    path = ROOT/"ref/expected/camera.hashes"
    rendered = "".join(name+" "+records[name]+"\n" for name in names)
    if args.record:
        path.write_bytes(rendered.encode())
    elif path.read_text() != rendered:
        raise ValueError("source camera regression")
    # Nonfinite and missing-dependency controls must fail the same validators.
    trace = OUT/"gcc-O2-camera-fixture-T02_full_throttle.txt"
    bad = OUT/"control.txt"
    lines = trace.read_text().splitlines(); fields = lines[0].split(); fields[6] = "7fc00000"
    lines[0] = " ".join(fields); bad.write_text("\n".join(lines)+"\n")
    try:
        inspect(bad)
    except ValueError:
        pass
    else:
        raise ValueError("nonfinite control survived")
    bad.write_text("camera:Camera_Update 1\n")
    try:
        calls(bad)
    except ValueError:
        pass
    else:
        raise ValueError("missing dependency control survived")
    # The unchanged controls schedule contains CR; removing it must move the hash.
    script = ROOT/"ref/inputs/camera_controls.txt"
    mutant = OUT/"without-camera-controls.txt"
    mutant.write_text(script.read_text().replace("CR", "-"))
    altered = OUT/"control-camera.txt"
    subprocess.run([str(binary), "--course", str(OUT/"course.bin"), "--byte-order", "big",
                    "--inputs", str(mutant), "--camera-trace", str(altered)], check=True,
                   stdout=subprocess.DEVNULL)
    if hashlib.sha256(altered.read_bytes()).hexdigest() == records["camera_controls"]:
        raise ValueError("camera input control survived")
    print("camera HOST gate PASS: compiler identity, simulation identity, restart and 3 failure controls")


if __name__ == "__main__":
    main()
