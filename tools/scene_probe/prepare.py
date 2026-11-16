#!/usr/bin/env python3
"""Prepare synthetic source-oracle hashes; no ROM or game-derived inputs."""
from pathlib import Path
import hashlib
import os
import subprocess
ROOT=Path(__file__).resolve().parents[2]
OUT=Path(__file__).resolve().parent/'build'
OUT.mkdir(exist_ok=True)
results=[]
for cc in ('gcc','clang','gcc'):
    binary=ROOT/'ref/build'/f'{cc}-O2-camera-fixture-backdrop/fzx-ref'
    subprocess.run(['sha256sum','--check','--status',str(binary.parent/'scene-build.sha256')],check=True)
    subprocess.run(['bash',str(ROOT/'tools/check_receipt.sh'),str(binary.parent/'background.receipt')],
                   check=True,capture_output=True)
    result=subprocess.run([str(binary),'--scene-probe'],check=True,capture_output=True,text=True)
    rows=[line.split() for line in result.stdout.splitlines()]
    if len(rows)!=256 or any(len(r)!=3 or int(r[0])!=i for i,r in enumerate(rows)):
        raise ValueError('Missing or invalid source scene cases')
    results.append(result.stdout)
if len(set(results))!=1:raise ValueError('Scene probe host compiler/repeat divergence')
header='/* Synthetic source scene state gate. Regenerate; never edit. */\n'
header+='static const unsigned int expected_scene[256][2] = {\n'
header+=''.join('    {0x%su,0x%su},\n'%(r[1],r[2]) for r in rows)+'};\n'
(OUT/'expected.h').write_text(header)
(OUT/'host.txt').write_text(results[0])
inputs=[Path(__file__),ROOT/'ref/scene_probe.c',ROOT/'ref/scene_probe.h',
        ROOT/'tools/backdrop_state.py',ROOT/'tools/skyline_state.py',ROOT/'ref/backdrop_state.h',
        ROOT/'ref/skyline_state.h',ROOT/'ref/backdrop_oracle.inc',ROOT/'ref/skyline_oracle.inc',
        ROOT/'upstream/G-Diffuser/decomp/src/overlays/ovl_i3/background.c',
        ROOT/'upstream/G-Diffuser/decomp/src/overlays/ovl_i3/background.h',
        ROOT/'upstream/G-Diffuser/decomp/src/sys/math.c']
inputs += [Path(__file__).with_name(name) for name in ('main.c','state.c','Makefile','inner.sh')]
inputs += [ROOT/'ref/sat/soft_sqrtf.c',ROOT/'src/sh2float/fzx_float.c']
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
receipt=['# fzx generation receipt v1; regenerate, never edit']
receipt+=['generator %s %s'%(digest(p),os.path.relpath(p,ROOT/'tools')) for p in inputs]
receipt+=['output %s %s'%(digest(OUT/name),name) for name in ('expected.h','host.txt')]
(OUT/'expected.receipt').write_text('\n'.join(receipt)+'\n')
print('HOST scene oracle: 256 cases x GCC/Clang/repeat PASS; '+digest(OUT/'host.txt'))
