#!/usr/bin/env python3
"""Pack the receipted source road into bounded, big-endian cartridge records."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import subprocess
from import_source_art import output_location

ROOT=Path(__file__).resolve().parents[1]
STRIDES=(16,12,64,80,16,1)

def fnv(data):
    h=2166136261
    for b in data: h=((h^b)*16777619)&0xffffffff
    return h

def pack(mesh,course,fixture=False):
    if mesh.get('format')!='fzx-source-road-v1' or len(course)!=0x7e0:
        raise ValueError('Expected a verified source road and course record')
    vertices=mesh['vertices']; triangles=mesh['triangles'];origins=mesh['origins']
    chunks=mesh['chunks'][:-1];materials=mesh['materials'];textures=mesh['textures']
    if len(triangles)!=len(origins):raise ValueError('Missing source triangle provenance')
    names=sorted(textures)
    sections=[bytearray() for _ in STRIDES]
    for v in vertices:sections[0]+=struct.pack('>5h4B2x',*v)
    for tri,origin in zip(triangles,origins):
        sections[1]+=struct.pack('>6H',*tri,origin['chunk'],int(origin['conditional_outer']))
    first=0
    for i,c in enumerate(chunks):
        end=first
        while end<len(origins) and origins[end]['chunk']==i:end+=1
        sections[2]+=struct.pack('>IHHII24h',c['flags'],c['segment'],0,first,end-first,*c['points'])
        first=end
    if first!=len(triangles):raise ValueError('Source triangles are not in chunk order')
    for m in materials:
        tile=m['tile']; tex=textures[m['texture']['name']]
        sections[3]+=struct.pack('>10I8H3I12x',m['geometry'],m['other_h'],m['other_l'],
                                 *m['combine'],m['prim'],*m['tiles'][str(tile)],*m['sizes'][str(tile)],
                                 names.index(m['texture']['name']),tex['width'],tex['height'],int(m['enabled']),
                                 *m['scale'],tile,0,m['texture']['image'],*m['load'])
    for name in names:
        tex=textures[name];rgba=bytes.fromhex(tex['rgba_hex']);raw=bytearray()
        if len(rgba)!=tex['width']*tex['height']*4:raise ValueError('Truncated source texture')
        for i in range(0,len(rgba),4):
            r,g,b,a=rgba[i:i+4]
            if a not in (0,255) or any(c!=(c>>3)*255//31 for c in (r,g,b)):
                raise ValueError('Texture is not lossless RGBA16')
            raw+=struct.pack('>H',(r>>3)<<11|(g>>3)<<6|(b>>3)<<1|(a>>7))
        if hashlib.sha256(raw).hexdigest()!=tex['source_sha256']:
            raise ValueError('Texture differs from original RGBA16 bytes')
        sections[4]+=struct.pack('>IIHHI',len(sections[5]),len(raw),tex['width'],tex['height'],fnv(raw))
        sections[5]+=raw
    header=bytearray(128);header[:8]=b'FZXROAD1'
    payload=bytearray();counts=[];offsets=[]
    for stride,data in zip(STRIDES,sections):
        assert len(data)%stride==0
        counts.append(len(data)//stride);offsets.append(128+len(payload))
        payload+=data;payload+=bytes(-len(payload)%4)
    total=128+len(payload)
    if total>4*1024*1024:raise ValueError('Road pack exceeds 4 MiB cartridge')
    struct.pack_into('>16I',header,8,1,total,int(fixture),0,*counts,*offsets)
    header[72:104]=hashlib.sha256(course).digest()
    struct.pack_into('>I',header,104,fnv(course))
    result=header+payload
    identity=fnv(result);struct.pack_into('>I',result,20,identity)
    return bytes(result),dict(bytes=total,hash=identity,counts=counts,offsets=offsets,
                              fixture=fixture,course_hash=fnv(course))

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source',type=Path,default=ROOT/'generated/road')
    ap.add_argument('--output',type=Path,default=ROOT/'generated/road_pack')
    args=ap.parse_args();output_location(args.output)
    subprocess.run(['bash',str(ROOT/'tools/check_receipt.sh'),str(args.source/'road.receipt')],check=True)
    mesh=json.loads((args.source/'road.json').read_text());course=(args.source/'course.bin').read_bytes()
    data,info=pack(mesh,course)
    args.output.mkdir(parents=True,exist_ok=True)
    (args.output/'ROAD.PAK').write_bytes(data)
    header=('/* Generated source-road identity; data remains on disc. */\n'
            '#define FZX_ROAD_PACK_BYTES %du\n#define FZX_ROAD_PACK_HASH 0x%08Xu\n'
            '#define FZX_ROAD_PACK_FIXTURE %du\n')%(info['bytes'],info['hash'],info['fixture'])
    (args.output/'road_identity.h').write_bytes(header.encode())
    info.update(source_sha256=hashlib.sha256((args.source/'road.json').read_bytes()).hexdigest(),
                pack_sha256=hashlib.sha256(data).hexdigest())
    (args.output/'manifest.json').write_bytes((json.dumps(info,sort_keys=True,indent=2)+'\n').encode())
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    dependencies=[ROOT/'tools/road_pack.py',ROOT/'include/fzx_road_pack.h',ROOT/'src/fzx_road_pack.c',
                  args.source/'road.receipt',args.source/'road.json',args.source/'course.bin']
    import os
    receipt=['# fzx generation receipt v1; regenerate, never edit']
    receipt+=['generator %s %s'%(digest(p),os.path.relpath(p,ROOT/'tools')) for p in dependencies]
    receipt+=['output %s %s'%(digest(args.output/n),n) for n in ('ROAD.PAK','road_identity.h','manifest.json')]
    (args.output/'road_pack.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    print('native road pack:',info['bytes'],'bytes; vertices/triangles/chunks/materials/textures/texel bytes',info['counts'])

if __name__=='__main__':main()
