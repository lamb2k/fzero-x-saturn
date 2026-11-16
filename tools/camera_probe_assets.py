#!/usr/bin/env python3
"""Size camera-enabled reference stand-ins, verify them, prepare synthetic SH-2 probes."""
from pathlib import Path
import argparse
import hashlib
import os
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
HOST=ROOT/"ref/build/gcc-O0-camera-fixture"
OUT=ROOT/"ref/sat/build/camera"


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("scripts", nargs="*")
    a=ap.parse_args()
    expected=dict(x.split() for x in (ROOT/"ref/expected/fixture.hashes").read_text().splitlines())
    names=list(expected)+["camera_controls"]
    requested=a.scripts or names
    if any(x not in names for x in requested): raise SystemExit("unknown synthetic camera script")
    # Never derive a new receipt from stale host objects.
    subprocess.run([sys.executable,str(ROOT/"tools/camera_assets.py"),"--art","fixture"],cwd=ROOT,check=True)
    OUT.mkdir(parents=True,exist_ok=True)
    with (OUT/"host-build.log").open("w") as log:
        subprocess.run(["bash",str(ROOT/"ref/build.sh"),"gcc","-O0"],cwd=ROOT,
                       env=dict(os.environ,FZX_SOURCE_CAMERA="1",REF_CAMERA_ART="fixture"),
                       stdout=log,stderr=subprocess.STDOUT,check=True)
    camera_expected=dict(x.split() for x in (ROOT/"ref/expected/camera.hashes").read_text().splitlines())
    small=OUT/"host-small";small.mkdir(parents=True,exist_ok=True)
    course=small/"course.bin"
    subprocess.run([sys.executable,str(ROOT/"ref/fixture_course.py"),str(course)],check=True)
    extents=small/"extents.txt";extents.write_text("")
    rows={}
    for name in names:
        trace=small/(name+".camera")
        cmd=[str(HOST/"fzx-ref"),"--course",str(course),"--byte-order","big",
             "--inputs",str(ROOT/"ref/inputs"/(name+".txt")),"--camera-trace",str(trace)]
        result=subprocess.check_output(cmd,env=dict(os.environ,REF_STUB_REPORT=str(extents)),text=True).split()
        rows[name]=(result[3],trace.read_bytes())
        if hashlib.sha256(rows[name][1]).hexdigest()!=camera_expected[name]:
            raise ValueError("source camera regression: "+name)
        if name in expected and result[3]!=expected[name]: raise ValueError("source simulation mismatch")
    sizes={}
    for line in extents.read_text().splitlines():
        name,n=line.split();sizes[name]=max(sizes.get(name,0),int(n))
    sizefile=small/"sizes.txt"
    sizefile.write_text("".join("%s 0x%X\n" % (name,max(64,(2*n+63)&~63)) for name,n in sorted(sizes.items())))
    objects=sorted(p for p in HOST.glob("*.o") if p.name!="stubs.o")
    preprocessed=sorted(HOST.glob("*.i"))
    env=dict(os.environ,REF_STUB_SIZES=str(sizefile),REF_STUB_CALLS="1",FZX_SOURCE_CAMERA="1")
    subprocess.run([sys.executable,str(ROOT/"ref/gen_stubs.py"),str(small/"stubs.c"),
                    *map(str,preprocessed),"--",*map(str,objects)],env=env,check=True,stdout=subprocess.DEVNULL)
    subprocess.run(["gcc","-m32","-fno-pic","-O0","-w","-c",str(small/"stubs.c"),"-o",str(small/"stubs.o")],check=True)
    subprocess.run(["gcc","-m32","-no-pie",*map(str,objects),str(small/"stubs.o"),"-lm","-o",str(small/"fzx-ref")],check=True)
    for name in names:
        trace=small/(name+"-small.camera")
        result=subprocess.check_output([str(small/"fzx-ref"),"--course",str(course),"--byte-order","big",
            "--inputs",str(ROOT/"ref/inputs"/(name+".txt")),"--camera-trace",str(trace)],text=True).split()
        if (result[3],trace.read_bytes())!=rows[name]: raise ValueError("sized camera stand-ins changed "+name)
    # The target does not link the host main or its FILE-based diagnostic counters.
    env.update(REF_STUB_CALLS="0",FZX_SOURCE_CAMERA="0")
    source=small/"sat-stubs-source.c"
    subprocess.run([sys.executable,str(ROOT/"ref/gen_stubs.py"),str(source),*map(str,preprocessed),"--",
                    *[str(p) for p in objects if p.name!="ref_main.o"]],env=env,check=True,stdout=subprocess.DEVNULL)
    for name in requested:
        out=OUT/name;out.mkdir(exist_ok=True)
        subprocess.run([sys.executable,str(ROOT/"ref/sat/gen_sat.py"),str(out),
                        str(ROOT/"ref/inputs"/(name+".txt")),str(source)],check=True)
        h=2166136261;blocks=[]
        for line in rows[name][1].decode().splitlines():
            parts=line.split();values=[int(parts[0]),int(parts[1])]+[int(x,16) for x in parts[2:]]
            for word in values:
                for shift in range(0,32,8): h=((h^((word>>shift)&255))*16777619)&0xffffffff
            if int(parts[1])==1 and int(parts[0])%64==63: blocks.append(h)
        (out/"expected_hash.txt").write_text(rows[name][0]+"\n")
        (out/"expected_camera.txt").write_text("%08x\n" % h)
        (out/"expected_blocks.txt").write_text("".join("%08x\n" % x for x in blocks))
        files=("sat_data.c","sat_stubs.c","sat_stubs_data.s","expected_hash.txt","expected_camera.txt","expected_blocks.txt")
        digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
        generators=("tools/camera_probe_assets.py","ref/sat/gen_sat.py","ref/camera_bridge.c","ref/bridge.c","ref/ref.h",
                    "upstream/G-Diffuser/decomp/src/game/camera.c", "ref/build.sh", "ref/gen_stubs.py",
                    "ref/fixture_course.py", "ref/expected/camera.hashes", "ref/expected/fixture.hashes",
                    "ref/inputs/"+name+".txt")
        receipt=["# fzx generation receipt v1; regenerate, never edit"]
        dependencies={ROOT/path for path in generators}
        dependencies.update(ROOT/"ref/inputs"/(script+".txt") for script in names)
        dependencies.update((ROOT/"ref/main.c",ROOT/"ref/sat/closure.sh",ROOT/"src/sh2float/fzx_float.c"))
        # Compiler line markers identify the actual source/header inputs, including
        # generated fixture viewports. All paths remain local to this checkout.
        for pre in preprocessed:
            for path in re.findall(r'^#\s+\d+\s+"([^"<>]+)"',pre.read_text(),re.M):
                item=Path(path)
                if item.is_absolute() and item.is_relative_to(ROOT) and item.is_file():
                    dependencies.add(item)
        for item in sorted(dependencies):
            relative=os.path.relpath(item,ROOT/"tools")
            receipt.append("generator %s %s" % (digest(item),relative))
        receipt += ["output %s %s" % (digest(out/f),f) for f in files]
        (out/"camera.receipt").write_bytes(("\n".join(receipt)+"\n").encode())
    print("camera stand-ins: 11 exact host traces; probes prepared:"," ".join(requested))


if __name__=="__main__": main()
