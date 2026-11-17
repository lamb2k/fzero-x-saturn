#!/usr/bin/env python3
"""Prepare source vehicle faces, shared palettes and eight native draw orders.

The original mesh remains private. Only coplanar, convex, material/UV/normal
compatible triangles are paired. Unpaired triangles remain original geometry.
This pack is not runtime admission or an appearance/performance certificate.
"""
from collections import defaultdict
from fractions import Fraction
from pathlib import Path
import argparse
import hashlib
import json
import math
import os
import struct
import subprocess

from import_source_art import ROOT, DECOMP, ArtError, output_location, decode_texture
from model_assets import sampler, source_probe_state
from countdown_pack import chart_geometry, uv_at
from road_pack import fnv

LODS=(0,3,4)
HEADER=96
DESCRIPTOR=48
VERTEX=20
POSITION=8
NORMAL=4
FACE=56
PALETTE=112
PACK_LIMIT=65536
TEXTURE_LIMIT=32768


def sub(a,b):return tuple(x-y for x,y in zip(a,b))
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def vertex_key(v):return tuple(v['position']+v['uv']+v['normal'])


def pair_order(model,first,second):
    """Return an oriented boundary whose 0-2 diagonal is the original edge."""
    a,b=(model['triangles'][i] for i in (first,second))
    if a['material']!=b['material']:return None
    keys=[vertex_key(v) for v in model['vertices']]
    ak=[keys[i] for i in a['vertices']];bk=[keys[i] for i in b['vertices']]
    if len(set(ak+bk))!=4:return None
    for shift in range(3):
        ids=a['vertices'][shift:]+a['vertices'][:shift]
        x,y,z=[keys[i] for i in ids]
        for other in range(3):
            bi=b['vertices'][other:]+b['vertices'][:other]
            if keys[bi[0]]!=x or keys[bi[1]]!=z:continue
            order=ids+[bi[2]]
            try:chart_geometry([model['vertices'][i]['position'] for i in order])
            except ArtError:continue
            return order
    return None


def pairing(model):
    keys=[vertex_key(v) for v in model['vertices']];edges=defaultdict(list)
    for i,t in enumerate(model['triangles']):
        ids=t['vertices']
        if len(ids)!=3 or len(set(ids))!=3 or any(j<0 or j>=len(keys) for j in ids):
            raise ArtError('Invalid vehicle triangle')
        for j in range(3):edges[tuple(sorted((keys[ids[j]],keys[ids[(j+1)%3]])))].append(i)
    candidates={}
    for members in edges.values():
        if len(members)==2:
            a,b=sorted(members);order=pair_order(model,a,b)
            if order is not None:candidates[a,b]=order
    # Deterministic source-order matching. It does not alter the mesh to find
    # extra matches or claim that a maximum matching was computed.
    used=set();faces=[]
    for (a,b),vertices in sorted(candidates.items()):
        if a in used or b in used:continue
        used.update((a,b));faces.append(dict(vertices=vertices,triangles=[a,b],material=model['triangles'][a]['material']))
    for i,t in enumerate(model['triangles']):
        if i not in used:faces.append(dict(vertices=list(t['vertices']),triangles=[i],material=t['material']))
    return sorted(faces,key=lambda f:min(f['triangles']))


def extent(length,multiple,limit):
    root=math.isqrt(length);root+=root*root!=length
    n=(root+31)//32+1
    return min(limit,max(multiple,((n+multiple-1)//multiple)*multiple))


def address(index,extent,mode,mask):
    if mode&2:return max(0,min(extent-1,index))
    period=(1<<mask) if mask else extent
    index%=period*(2 if mode&1 else 1)
    return 2*period-1-index if mode&1 and index>=period else index


def chart(model,face,texture,sampling,pixels):
    vertices=[model['vertices'][i] for i in face['vertices']]
    v=[[*p['position'],*((u*65535)//65536 for u in p['uv'])] for p in vertices]
    if sampling['s_shift'] or sampling['t_shift']:
        raise ArtError('Vehicle rebake requires the source unshifted sampler')
    tw,th=texture['width'],texture['height']
    if not tw or not th or len(pixels)!=tw*th*4:raise ArtError('Invalid vehicle source image')
    for axis,n in (('s',tw),('t',th)):
        if sampling[axis+'_mode'] not in range(4) or sampling[axis+'_mask'] not in range(9):raise ArtError('Invalid vehicle sampler')
        if sampling[axis+'_mask'] and 1<<sampling[axis+'_mask']!=n:raise ArtError('Vehicle sampler mask mismatch')
    def distance(a,b):return sum((v[a][i]-v[b][i])**2 for i in (3,4))
    if len(v)==4:
        points=chart_geometry(v)
        w=extent(max(distance(0,1),distance(3,2)),8,32)
        h=extent(max(distance(0,3),distance(1,2)),2,32)
    else:
        w=extent(distance(0,1),8,16)
        h=extent(max(distance(0,2),distance(1,2)),2,16)
    colors=[]
    for y in range(h):
        for x in range(w):
            if len(v)==4:uv,den=uv_at(v,points,x,y,w,h)
            else:
                weights=[(w-1-x)*(h-1-y),x*(h-1-y),y*(w-1)];den=(w-1)*(h-1)
                uv=[sum(weights[k]*v[k][i] for k in range(3)) for i in (3,4)]
            # Point-sampled original UV, after the original texture scale.
            indices=[address((n+16*den)//(32*den),size,sampling[axis+'_mode'],sampling[axis+'_mask'])
                     for n,size,axis in zip(uv,(tw,th),('s','t'))]
            at=(indices[1]*tw+indices[0])*4;colors.append(tuple(pixels[at:at+4]))
    palette=sorted(set(colors))
    if len(palette)>16:raise ArtError('Vehicle chart exceeds 16 source colors')
    mapping={c:i for i,c in enumerate(palette)};indices=[mapping[c] for c in colors]
    packed=bytes((indices[i]<<4)|indices[i+1] for i in range(0,len(indices),2))
    return w,h,palette,packed


def material_key(m):
    if not m.get('combine') or m['combine'][0] not in ('PRIMITIVE','TEXEL0'):
        raise ArtError('Unsupported vehicle material equation')
    primitive=tuple((m['primitive'] or [0,0,0])[:3]);is_body=m['environment']=='racer.bodyRGB'
    environment=(0,0,0) if is_body else tuple(m['environment'][:3])
    if len(primitive)!=3 or len(environment)!=3 or any(not isinstance(x,int) or not 0<=x<=255 for x in (*primitive,*environment)):
        raise ArtError('Invalid vehicle material color')
    return primitive,environment,int(is_body),int(m['combine'][0]=='TEXEL0')


def modulate(a,b):
    p=a*b+32768
    return (p+(p>>16))>>16


def base_color(key,texel,body):
    primitive,environment,is_body,alpha=key
    if is_body:environment=body
    result=0x8000
    for i in range(3):
        t=texel[i]*257;e=environment[i]*257
        value=(modulate(t,texel[3]*257)+modulate(e,65535-texel[3]*257)) if alpha else (
            modulate(primitive[i]*257,t)+modulate(e,65535-t))
        result|=(min(255,(value+128)//257)>>3)<<(i*5)
    return result


def face_plane(vertices):
    a,b,c=[v['position'] for v in vertices[:3]]
    n=cross(sub(b,a),sub(c,a));length=math.isqrt(dot(n,n))
    if not length:raise ArtError('Degenerate vehicle face')
    # Each component differs by less than one from the exact scaled normal.
    # DO NOT use the packed dot sign alone near silhouettes: a consumer must
    # retain faces within sum(abs(eye - first_vertex)) of the plane, plus
    # sum(abs(normal))*eye_error for its model-space eye rounding bound.
    # This keeps potentially front-facing faces; it is not exact-sign parity.
    scale=max(abs(x) for x in n)
    normal=tuple((abs(x)*16384//scale)*(1 if x>=0 else -1) for x in n)
    return normal,dot(normal,a)


def build(models,bindings,textures,raw,lut,body,fog,fixture=False):
    if len(models)<5 or len(lut)!=256 or any(not 0<=x<=255 for x in [*lut,*body,*fog]) or len(body)!=3 or len(fog)!=3:
        raise ArtError('Invalid vehicle source setup')
    by_name={t['symbol']:t for t in textures};by_tile={b['tile']:b for b in bindings}
    image_cache={};palette_ids={};palettes=[];bank=bytearray();prepared=[]
    for lod in LODS:
        model=models[lod]
        if not 0<len(model['vertices'])<=512 or not 0<len(model['triangles'])<=256:raise ArtError('Vehicle model capacity exceeded')
        unique_positions={};unique_normals={};maps=[]
        for v in model['vertices']:
            if len(v['position'])!=3 or len(v['uv'])!=2 or len(v['normal'])!=3 or any(abs(x)>1024 for x in v['position']):
                raise ArtError('Invalid vehicle vertex')
            if any(not -32768<=x<=32767 for x in v['uv']) or any(not -128<=x<=127 for x in v['normal']):raise ArtError('Vehicle vertex range exceeded')
            maps.append((unique_positions.setdefault(tuple(v['position']),len(unique_positions)),unique_normals.setdefault(tuple(v['normal']),len(unique_normals))))
        faces=[];rejected_pairs=0
        queue=pairing(model)
        while queue:
            face=queue.pop(0);mat=model['materials'][face['material']];binding=by_tile[mat['tile']];tex=by_name[binding['symbol']]
            if tex['symbol'] not in image_cache:image_cache[tex['symbol']]=decode_texture(raw[tex['symbol']],tex['format'],tex['width'],tex['height'])
            try:w,h,colors,pixels=chart(model,face,tex,sampler(binding,tex),image_cache[tex['symbol']])
            except ArtError as exc:
                if len(face['triangles'])!=2 or str(exc)!='Vehicle chart exceeds 16 source colors':raise
                rejected_pairs+=1
                for i in reversed(face['triangles']):
                    tri=model['triangles'][i];queue.insert(0,dict(vertices=list(tri['vertices']),triangles=[i],material=tri['material']))
                continue
            key=material_key(mat);identity=(key,tuple(colors))
            if identity not in palette_ids:
                palette_ids[identity]=len(palettes);palettes.append((key,colors))
            normal,plane=face_plane([model['vertices'][i] for i in face['vertices']])
            center=[sum(model['vertices'][j]['position'][i] for j in face['vertices']) for i in range(3)]
            faces.append(dict(**face,palette=palette_ids[identity],width=w,height=h,offset=len(bank),normal=normal,plane=plane,center=center))
            bank+=pixels
        faces.sort(key=lambda f:min(f['triangles']))
        covered=sorted(i for f in faces for i in f['triangles'])
        if covered!=list(range(len(model['triangles']))):raise ArtError('Vehicle face coverage changed')
        orders=[]
        for octant in range(8):
            direction=[1 if octant&(1<<i) else -1 for i in range(3)]
            orders.append(sorted(range(len(faces)),key=lambda i:(Fraction(dot(faces[i]['center'],direction),len(faces[i]['vertices'])),i)))
        prepared.append(dict(lod=lod+1,model=model,maps=maps,positions=list(unique_positions),normals=list(unique_normals),faces=faces,orders=orders,rejected_pairs=rejected_pairs))
    data=bytearray(HEADER+len(prepared)*DESCRIPTOR);data[:8]=b'FZXMCH02'
    def align():data.extend(bytes((-len(data))%8))
    for index,p in enumerate(prepared):
        align();va=len(data)
        for v,ids in zip(p['model']['vertices'],p['maps']):
            data+=struct.pack('>5h3bB2H2x',*v['position'],*v['uv'],*v['normal'],v.get('alpha',255),*ids)
        align();pa=len(data)
        for pos in p['positions']:data+=struct.pack('>3hH',*pos,0)
        align();na=len(data)
        for n in p['normals']:data+=struct.pack('>3bB',*n,0)
        align();fa=len(data)
        for f in p['faces']:
            indices=f['vertices'] if len(f['vertices'])==4 else f['vertices']+[f['vertices'][-1]]
            triangle_ids=f['triangles'] if len(f['triangles'])==2 else f['triangles']+[65535]
            data+=struct.pack('>8HI3hHi4H3i4x',*indices,f['palette'],f['width'],f['height'],len(f['vertices']),f['offset'],
                              *f['normal'],0,f['plane'],*triangle_ids,f['material'],0,*f['center'])
        align();oa=len(data)
        for order in p['orders']:data+=struct.pack('>'+str(len(order))+'H',*order)
        struct.pack_into('>12I',data,HEADER+index*DESCRIPTOR,p['lod'],len(p['model']['vertices']),len(p['positions']),len(p['normals']),
                         len(p['faces']),va,pa,na,fa,oa,len(p['model']['triangles']),0)
    align();palette_at=len(data)
    for key,colors in palettes:
        primitive,environment,is_body,alpha=key
        data+=struct.pack('>9B7x',*primitive,*environment,is_body,alpha,len(colors))
        data+=b''.join(bytes(c) for c in colors)+bytes((16-len(colors))*4)
        native=[base_color(key,c,body) for c in colors];native+=native[:1]*(16-len(native))
        data+=struct.pack('>16H',*native)
    align();lut_at=len(data);data+=bytes(lut)
    if len(data)>PACK_LIMIT or len(bank)>TEXTURE_LIMIT:raise ArtError('Prepared vehicle exceeds pack budget')
    struct.pack_into('>10I',data,8,len(data),len(bank),fnv(bank),0,int(fixture),3,HEADER,len(palettes),palette_at,lut_at)
    data[48:54]=bytes([*body,*fog]);struct.pack_into('>I',data,20,fnv(data))
    report=dict(format='FZXMCH02',metadata_bytes=len(data),texture_bytes=len(bank),palette_groups=len(palettes),palette_vram_bytes=len(palettes)*32,
                gouraud_bytes=max(len(p['faces']) for p in prepared)*8,
                lods=[dict(lod=p['lod'],source_vertices=len(p['model']['vertices']),source_triangles=len(p['model']['triangles']),positions=len(p['positions']),
                           normals=len(p['normals']),faces=len(p['faces']),pairs=sum(len(f['triangles'])==2 for f in p['faces']),palette_rejected_pairs=p['rejected_pairs']) for p in prepared],
                runtime='UNRUN',adaptations=['original coplanar convex pairs only; UV/normal/material seams retained',
                'nearest original texels; triangle chart cap16 and quad chart cap32','eight offline centroid orders; appearance qualification pending',
                'unlit default-body palette prebaked; source colors retained for shared live body changes; lighting is pending'])
    return bytes(data),bytes(bank),report


def generate(source,output):
    output_location(output)
    subprocess.run(['bash',str(ROOT/'tools/check_receipt.sh'),str(source/'source_art.receipt')],check=True,stdout=subprocess.DEVNULL)
    manifest=json.loads((source/'source_art.json').read_text())
    racer=DECOMP/'src/game/racer.c';background=DECOMP/'src/overlays/ovl_i3/background.c'
    lut,body,fog=source_probe_state(racer.read_text(),background.read_text())
    raw={t['symbol']:(source/t['raw_file']).read_bytes() for t in manifest['textures']}
    data,bank,report=build(manifest['models'],manifest['texture_bindings'],manifest['textures'],raw,lut,body,fog)
    output.mkdir(parents=True,exist_ok=True)
    outputs={'MACHRLD.PAK':data,'MACHRTX.PAK':bank,'machine_rebake.json':(json.dumps(report,indent=2)+'\n').encode()}
    header='/* Generated prepared vehicle identity; regenerate. */\n'
    for name,value in [('METADATA_BYTES',len(data)),('TEXTURE_BYTES',len(bank)),('METADATA_HASH',fnv(data)),('TEXTURE_HASH',fnv(bank)),('PALETTE_BYTES',report['palette_vram_bytes']),('GOURAUD_BYTES',report['gouraud_bytes'])]:
        header+='#define FZX_MACHINE_REBAKE_%s %du\n'%(name,value)
    outputs['machine_rebake_identity.h']=header.encode()
    inputs=[Path(__file__),ROOT/'tools/import_source_art.py',ROOT/'tools/model_assets.py',ROOT/'tools/countdown_pack.py',ROOT/'tools/road_pack.py',
            source/'source_art.receipt',source/'source_art.json',racer,background]
    inputs+=[source/t['raw_file'] for t in manifest['textures']]
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    receipt=['# fzx generation receipt v1; regenerate, never edit']
    receipt+=['generator %s %s'%(digest(p),os.path.relpath(p,ROOT/'tools').replace('\\','/')) for p in inputs]
    for name,content in outputs.items():
        (output/name).write_bytes(content);receipt.append('output %s %s'%(hashlib.sha256(content).hexdigest(),name))
    (output/'machine_rebake.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    print('Prepared vehicle pack generated locally; native drawing and appearance remain unqualified')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=ROOT/'generated/source_art')
    parser.add_argument('--output',type=Path,default=ROOT/'generated/machine_rebake')
    args=parser.parse_args();generate(args.source,args.output)
