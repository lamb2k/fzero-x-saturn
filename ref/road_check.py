#!/usr/bin/env python3
"""HOST source road export: synthetic shapes, compiler identity, failure controls."""
from pathlib import Path
import concurrent.futures
import hashlib
import os
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from import_course import record_checksum
from road_assets import capture
from fixture_course import record


def fixtures():
    result = {"oval": record()}
    # Authored synthetic oval, with source shape flags and empty feature tables.
    # Includes both ends of an inside tunnel and an air gap with rails.
    for name, flags in (("mixed", [2, 3, 0x40, 0x20000143, 0x20000143, 2, 0x1BF, 1]),
                        ("shapes", [0x80, 0x20000080, 0xC0, 0x100, 0x1C0, 0x40, 2, 3])):
        data = bytearray(record())
        for i, flag in enumerate(flags):
            struct.pack_into(">I", data, 0x20 + 20 * i + 16, flag | 0x10000000)
            struct.pack_into(">hh", data, 0x20 + 20 * i + 12, 130 + i * 11, 190 - i * 6)
        struct.pack_into(">I", data, 4, record_checksum(bytes(data), "big"))
        result[name] = bytes(data)
    return result


def main():
    out = ROOT / "ref/build/road-check"
    out.mkdir(parents=True, exist_ok=True)
    inputs = fixtures()
    for name, data in inputs.items():
        (out / (name + ".bin")).write_bytes(data)

    def run(arm):
        cc, opt = arm
        label = cc + opt
        with (out / (label + ".log")).open("w") as log:
            subprocess.run(["bash", str(ROOT / "ref/build.sh"), cc, opt], cwd=ROOT,
                           env=dict(os.environ, FZX_ROAD_EXPORT="1", FZX_SOURCE_CAMERA="0", FZX_SOURCE_MACHINE_MENU="0"),
                           check=True, stdout=log, stderr=subprocess.STDOUT)
        exe = ROOT / "ref/build" / (label + "-road/fzx-ref")
        results = {}
        for name in inputs:
            data = subprocess.check_output([str(exe), str(out / (name + ".bin"))], timeout=30)
            capture(data.decode())
            (out / (name + "-" + label + ".txt")).write_bytes(data)
            repeated = subprocess.check_output([str(exe), str(out / (name + ".bin"))], timeout=30)
            if data != repeated:
                raise ValueError("Non-repeatable source road export")
            results[name] = data
        return results

    arms = [(cc, opt) for cc in ("gcc", "clang") for opt in ("-O0", "-O2")]
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
        results = list(executor.map(run, arms))
    for name in inputs:
        if any(result[name] != results[0][name] for result in results[1:]):
            raise ValueError("Source road compiler mismatch: " + name)
        print(name, hashlib.sha256(results[0][name]).hexdigest(), results[0][name].splitlines()[0].decode())
    subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", "tests", "-p", "test_road_assets.py", "-v"], cwd=ROOT, check=True)
    print("source road HOST: 3 synthetic shape courses x 4 compiler arms, repeats and decoder controls PASS")


if __name__ == "__main__": main()
