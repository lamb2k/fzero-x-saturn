#!/usr/bin/env python3
"""Convert original machine geometry and texture charts into guarded native CD packs."""
from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess,tempfile
from import_source_art import ROOT,ArtError,output_location
from model_assets import source_probe_state
from model_probe.make_fixture import fixture_data
from road_pack import fnv

LODS=(0,3,4)
def chart_records(data):
    at=0;result=[]
    def words(n):
        nonlocal at
        v=struct.unpack_from('>'+str(n)+'H',data,at);at+=n*2;return v
    for lod in LODS:
        charts=[]
        for k in range(words(1)[0]):
            w,h,count,source,material=words(5)
            if not w or w>16 or w%8 or h<2 or h>16 or h%2 or not 1<=count<=16 or source!=k:
                raise ArtError('Invalid native machine chart')
            palette=data[at:at+count*8];at+=count*8
            pixels=data[at:at+w*h//2];at+=w*h//2
            if len(pixels)!=w*h//2 or any((p>>4)>=count or (p&15)>=count for p in pixels):raise ArtError('Invalid palette index')
            charts.append(dict(width=w,height=h,count=count,source=source,material=material,palette=palette,pixels=pixels))
        result.append(charts)
    if at!=len(data):raise ArtError('Trailing chart data')
    return result

def convert(models,charts,lut,fog,fixture):
    data=bytearray(64+3*32);data[:8]=b'FZXMCH01';bank=bytearray();descriptors=[];lineage=[]
    lut_at=len(data);data+=bytes(lut);fog_at=len(data);data+=bytes(fog)+bytes(5)
    def align():data.extend(bytes((-len(data))%4))
    for index,lod in enumerate(LODS):
        model=models[lod];vc=len(model['vertices']);tc=len(model['triangles']);mc=len(model['materials'])
        if vc>512 or tc>256 or mc>64 or tc!=len(charts[index]):raise ArtError('Machine capacity exceeded')
        unique=[{},{}];maps=[]
        for v in model['vertices']:
            ids=[]
            for u,key in zip(unique,('position','normal')):
                q=tuple(v[key]);ids.append(u.setdefault(q,len(u)))
            maps.append(ids)
        align();va=len(data)
        for v,ids in zip(model['vertices'],maps):
            if any(abs(p)>1024 for p in v['position']):raise ArtError('Machine position outside bounded extent')
            data+=struct.pack('>5h3bB3H',*v['position'],*v['uv'],*v['normal'],0,*ids,0)
        ma=len(data)
        for m in model['materials']:
            body=m['environment']=='racer.bodyRGB';env=[0,0,0] if body else m['environment'][:3]
            primitive=(m['primitive'] or [0,0,0])[:3];alpha=m['combine'][0]=='TEXEL0'
            data+=struct.pack('>8B2H',*primitive,*env,int(alpha),int(body),m['tile'],0)
        ta=len(data);data+=bytes(tc*24)
        for k,(tri,c) in enumerate(zip(model['triangles'],charts[index])):
            if tri['material']!=c['material']:raise ArtError('Machine material binding drift')
            align();pa=len(data);data+=c['palette'];pixel_at=len(bank);bank+=c['pixels']
            if pixel_at%8:raise ArtError('Machine texture alignment')
            struct.pack_into('>8H2I',data,ta+k*24,*tri['vertices'],tri['material'],c['width'],c['height'],c['count'],0,pixel_at,pa)
        align();struct.pack_into('>4H3I2H8x',data,64+index*32,lod+1,vc,tc,mc,va,ta,ma,len(unique[0]),len(unique[1]))
        lineage.append(dict(source_lod=lod+1,vertices=vc,triangles=tc,materials=mc,positions=len(unique[0]),normals=len(unique[1])))
    if len(bank)+max(x['triangles'] for x in lineage)*40>29952:raise ArtError('Machine VDP1 allocation exceeded')
    struct.pack_into('>8I',data,8,len(data),len(bank),fnv(bank),int(fixture),3,64,lut_at,fog_at)
    info=dict(metadata_bytes=len(data),texture_bytes=len(bank),metadata_hash=fnv(data),texture_hash=fnv(bank),fixture=fixture,lods=lineage,
        shade_bytes=max(x['triangles'] for x in lineage)*40,source_lod_mapping=[1,1,1,4,5,5],max_chart_dimension=16,
        adaptations=['original LOD 1 used for source LODs 1-3; original LOD 4 retained; original LOD 5 used for source LODs 5-6','source UV triangle charts point sampled offline, capped at 16x16','dynamic raw-texel palettes retain live body/environment color; native Gouraud lighting approximation'])
    return bytes(data),bytes(bank),info

def generate(source,out,fixture=False):
    output_location(out);subprocess.run(['bash',str(ROOT/'tools/check_receipt.sh'),str(source/'source_art.receipt')],check=True)
    inputs=[source/'source_art.receipt',source/'model_probe.h']
    if fixture:models,_,_,_,lut,body,fog=fixture_data()
    else:
        manifest=source/'source_art.json';inputs.append(manifest);models=json.loads(manifest.read_text())['models']
        racer=ROOT/'upstream/G-Diffuser/decomp/src/game/racer.c';background=ROOT/'upstream/G-Diffuser/decomp/src/overlays/ovl_i3/background.c';inputs.extend([racer,background])
        lut,body,fog=source_probe_state(racer.read_text(),background.read_text())
        # Mute City 1 uses purple racer fog; the immutable source record is receipt-bound.
    results=[]
    with tempfile.TemporaryDirectory() as tmp:
        for cc in ('gcc','clang'):
            exe=Path(tmp)/cc
            subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include'),'-I'+str(source),str(ROOT/'tools/machine_pack_bake.c'),str(ROOT/'src/fzx_material.c'),'-lm','-o',str(exe)],check=True)
            results.append(subprocess.run([str(exe)],check=True,capture_output=True).stdout)
    if results[0]!=results[1]:raise ArtError('Machine charts GCC/Clang mismatch')
    meta,bank,info=convert(models,chart_records(results[0]),lut,fog,fixture)
    out.mkdir(parents=True,exist_ok=True);(out/'MACHINE.PAK').write_bytes(meta);(out/'MACHTX.PAK').write_bytes(bank)
    header='/* Generated machine pack identity; regenerate. */\n'
    for key in ('metadata_bytes','texture_bytes','metadata_hash','texture_hash','shade_bytes'):
        header+='#define FZX_MACHINE_%s %du\n'%(key.upper(),info[key])
    header+='#define FZX_MACHINE_FIXTURE %du\n'%int(fixture)
    (out/'machine_identity.h').write_bytes(header.encode());(out/'machine.json').write_bytes((json.dumps(info,indent=2)+'\n').encode())
    dependencies=[Path(__file__),ROOT/'tools/machine_pack_bake.c',ROOT/'tools/vdp1_probe/bake.c',ROOT/'tools/model_assets.py',ROOT/'tools/model_probe/make_fixture.py',ROOT/'tools/import_source_art.py',ROOT/'tools/road_pack.py',ROOT/'src/fzx_material.c',ROOT/'include/fzx_material.h',ROOT/'include/fzx_model.h',*inputs]
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();receipt=['# fzx generation receipt v1; regenerate, never edit']
    receipt+=['generator %s %s'%(digest(p),os.path.relpath(p,ROOT/'tools')) for p in dependencies]
    receipt+=['output %s %s'%(digest(out/n),n) for n in ('MACHINE.PAK','MACHTX.PAK','machine_identity.h','machine.json')]
    (out/'machine.receipt').write_bytes(('\n'.join(receipt)+'\n').encode());print(json.dumps(info))
if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--fixture',action='store_true');ap.add_argument('--output',type=Path);a=ap.parse_args()
    generate(ROOT/'generated'/('model_fixture' if a.fixture else 'source_art'),a.output or ROOT/'generated'/('machine_fixture' if a.fixture else 'machine'),a.fixture)
