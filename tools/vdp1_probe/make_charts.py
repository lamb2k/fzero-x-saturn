#!/usr/bin/env python3
"""Bake bounded diagnostic charts from a receipted local model probe pack."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--art',choices=['fixture','retail'],default='fixture')
    args=p.parse_args()
    source=ROOT/'generated'/('model_fixture' if args.art=='fixture' else 'source_art')
    out=ROOT/'generated'/('charts_'+args.art)
    subprocess.run(['sh',str(ROOT/'tools/check_receipt.sh'),str(source/'source_art.receipt')],check=True)
    out.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory() as temp:
        outputs=[]
        for cc in ('gcc','clang'):
            exe=Path(temp)/cc
            subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include'),
                            '-I'+str(source),str(ROOT/'tools/vdp1_probe/bake.c'),
                            str(ROOT/'src/fzx_material.c'),'-lm','-o',str(exe)],check=True)
            result=subprocess.run([str(exe)],check=True,capture_output=True)
            outputs.append(result.stdout)
        if outputs[0]!=outputs[1]:
            raise RuntimeError('Chart bake differs between GCC and Clang')
    (out/'charts.h').write_bytes(outputs[0])
    (out/'lineage.txt').write_bytes(result.stderr)
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    receipt=['# fzx generation receipt v1']
    for name in ('vdp1_probe/bake.c','vdp1_probe/make_charts.py','../src/fzx_material.c','../include/fzx_material.h'):
        receipt.append(f'generator {digest(ROOT/"tools"/name)} {name}')
    for name in ('charts.h','lineage.txt','../'+source.name+'/model_probe.h','../'+source.name+'/source_art.receipt'):
        receipt.append(f'output {digest(out/name)} {name}')
    (out/'charts.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    print(result.stderr.decode(),end='')
    print('GCC/Clang chart identity PASS; fixed poses only, adaptation review pending')

if __name__=='__main__':
    main()
