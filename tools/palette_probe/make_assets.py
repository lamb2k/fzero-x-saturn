#!/usr/bin/env python3
"""Private native machine pack: original geometry, indexed charts and probe poses."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from model_assets import rotate,source_probe_state,unit
from model_probe.make_fixture import fixture_data

def geometry(models,lut):
    lines=['#include "fzx_machine.h"','#include "indexed.h"']
    lines+=['static const uint8_t machine_reflection_lut[]={'+','.join(map(str,lut))+'};']
    for i,m in enumerate(models):
        lines+=[f'static const fzx_machine_vertex_t machine_vertices_{i}[]={{']
        for v in m['vertices']:
            lines+=['{{'+','.join(map(str,v['position']))+'},{'+','.join(map(str,v['uv']))+'},{'+','.join(map(str,v['normal']))+'}},']
        lines+=['};',f'static const fzx_model_triangle_t machine_triangles_{i}[]={{']
        for t in m['triangles']:lines+=['{{'+','.join(map(str,t['vertices']))+'},'+str(t['material'])+'},']
        lines+=['};']
        plans=[]
        for field in ('position','normal'):
            unique={};sources=[];mapping=[]
            for index,vertex in enumerate(m['vertices']):
                key=tuple(vertex[field])
                if key not in unique:unique[key]=len(sources);sources.append(index)
                mapping.append(unique[key])
            for suffix,data in (('sources',sources),('map',mapping)):
                lines+=[f'static const uint16_t machine_{field}_{suffix}_{i}[]={{'+','.join(map(str,data))+'};']
            plans.append(len(sources))
        lines+=[f'static const fzx_machine_plan_t machine_plan_{i}={{machine_position_sources_{i},machine_normal_sources_{i},machine_position_map_{i},machine_normal_map_{i},{len(m["vertices"])},{plans[0]},{plans[1]}}};']
    lines+=['typedef struct { const fzx_machine_vertex_t *vertices; const fzx_model_triangle_t *triangles; const indexed_chart_t *charts; const fzx_machine_plan_t *plan; uint16_t vertex_count,triangle_count; } machine_model_t;',
            'static const machine_model_t machine_models[]={']
    for i,m in enumerate(models):
        lines+=[f'{{machine_vertices_{i},machine_triangles_{i},indexed_charts_{i},&machine_plan_{i},{len(m["vertices"])},{len(m["triangles"])}}},']
    lines+=['};']
    poses=[(0,-25,0),(40,-25,0),(-40,-25,0),(180,-25,0)]+[(20,-25,0)]*4
    poses += [(i*360/64,-25,15*math.sin(i*2*math.pi/64)) for i in range(64)]
    light=unit([69]*3)
    half=unit([int((value-(i==2))*16383+(0.5 if value-(i==2)>=0 else -0.5)) for i,value in enumerate([.57735]*3)])
    lines+=['#define MACHINE_POSES 72u','static const fzx_machine_pose_t machine_poses[]={']
    for yaw,pitch,bank in poses:
        basis=[rotate([int(i==j) for i in range(3)],math.radians(yaw),math.radians(pitch)) for j in range(3)]
        roll=math.radians(bank);c,s=math.cos(roll),math.sin(roll)
        basis=[[c*v[0]-s*v[1],s*v[0]+c*v[1],v[2]] for v in basis]
        matrix=[[round(basis[j][i]*16384) for j in range(3)] for i in range(3)]
        lines+=['{{'+','.join('{'+','.join(map(str,row))+'}' for row in matrix)+'},{'+','.join(str(round(v*16384)) for v in light)+'},{'+','.join(str(round(v*16384)) for v in half)+'},1200,75},']
    lines+=['};','']
    return '\n'.join(lines).encode()

def main():
    p=argparse.ArgumentParser();p.add_argument('--art',choices=['fixture','retail'],default='fixture');args=p.parse_args()
    source=ROOT/'generated'/('model_fixture' if args.art=='fixture' else 'source_art')
    subprocess.run(['sh',str(ROOT/'tools/check_receipt.sh'),str(source/'source_art.receipt')],check=True)
    inputs=[source/'model_probe.h',source/'source_art.receipt']
    if args.art=='fixture':models,_,_,_,lut,body,fog=fixture_data()
    else:
        manifest=source/'source_art.json';inputs.append(manifest)
        m=json.loads(manifest.read_text());models=m['models']
        decomp=ROOT/'upstream/G-Diffuser/decomp'
        racer=decomp/'src/game/racer.c';background=decomp/'src/overlays/ovl_i3/background.c'
        inputs.extend([racer,background]);lut,body,fog=source_probe_state(racer.read_text(),background.read_text())
    out=ROOT/'generated'/('palette_'+args.art);out.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory() as temp:
        results=[]
        for cc in ('gcc','clang'):
            exe=Path(temp)/cc
            subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include'),'-I'+str(source),
                            str(ROOT/'tools/palette_probe/bake.c'),str(ROOT/'src/fzx_material.c'),'-lm','-o',str(exe)],check=True)
            result=subprocess.run([str(exe)],check=True,capture_output=True);results.append(result.stdout)
        if results[0]!=results[1]:raise RuntimeError('Indexed chart compiler mismatch')
    (out/'indexed.h').write_bytes(results[0]);(out/'lineage.txt').write_bytes(result.stderr)
    generated=geometry(models,lut)+('static const uint8_t machine_fog[3]={'+','.join(map(str,fog))+'};\n').encode()
    (out/'machine.h').write_bytes(generated)
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    receipt=['# fzx generation receipt v1']
    for name in ('palette_probe/make_assets.py','palette_probe/bake.c','vdp1_probe/bake.c','model_assets.py','model_probe/make_fixture.py','../src/fzx_material.c','../include/fzx_material.h','../include/fzx_machine.h'):
        receipt.append(f'generator {digest(ROOT/"tools"/name)} {name}')
    for file in inputs:
        if file.is_relative_to(ROOT/'generated'):receipt.append(f'output {digest(file)} ../{file.parent.name}/{file.name}')
        else:receipt.append(f'generator {digest(file)} ../{file.relative_to(ROOT).as_posix()}')
    for name in ('indexed.h','machine.h','lineage.txt'):receipt.append(f'output {digest(out/name)} {name}')
    (out/'machine.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    print(result.stderr.decode(),end='');print('Native indexed charts: GCC/Clang identity PASS; point sampling and lighting adaptation unreviewed')

if __name__=='__main__':main()
