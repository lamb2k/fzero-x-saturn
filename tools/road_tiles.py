#!/usr/bin/env python3
"""Prebake shared source-road strips and paired-quad provenance for Saturn."""
from pathlib import Path
import argparse,hashlib,json,math,os,struct,subprocess
from import_source_art import ROOT,ArtError,output_location
from road_pack import fnv

def pair(mesh):
    vertices=mesh['vertices'];triangles=mesh['triangles'];origins=mesh['origins'];result=[]
    if len(triangles)%2 or len(triangles)!=len(origins):raise ArtError('Unpaired source road triangles')
    for first in range(0,len(triangles),2):
        a,b=triangles[first:first+2];oa,ob=origins[first:first+2]
        if a[3]!=b[3] or any(oa[k]!=ob[k] for k in ('chunk','conditional_outer')):
            raise ArtError('Source pair crosses a material/chunk/outer boundary')
        edges=[]
        for tri in (a,b):
            for i in range(3):
                edge=(tri[i],tri[(i+1)%3]);reverse=edge[::-1]
                if reverse in edges:edges.remove(reverse)
                else:edges.append(edge)
        if len(edges)!=4 or len({x for x,y in edges})!=4:raise ArtError('Source triangles do not form one quad')
        ids=[edges[0][0]]
        for i in range(3):ids.append(next(y for x,y in edges if x==ids[-1]))
        if next(y for x,y in edges if x==ids[-1])!=ids[0]:raise ArtError('Noncyclic source pair')
        material=mesh['materials'][a[3]];primitive=material['combine'][0]==0xfc357e6a
        if not primitive:
            for i in range(4):
                q=ids[i:]+ids[:i]
                if vertices[q[0]][3]==vertices[q[1]][3] and vertices[q[2]][3]==vertices[q[3]][3]:break
            else:
                # A cylinder join (course.c Course_ChunkJoinCylinder) tapers its quads to the line, so a
                # few pairs have no two edges of constant S. Take the turn whose cross edges are nearest
                # constant S and give the pair its own copies of the second and fourth corners with the
                # S of the first and third (approximate texturing on those quads only; the mesh keeps
                # the change, so a second pass sees an ordinary pair).
                if not any(vertices[i][3]!=vertices[ids[0]][3] for i in ids):raise ArtError('Source road pair has unsupported S topology')
                q=min((ids[i:]+ids[:i] for i in range(4)),key=lambda r:abs(vertices[r[0]][3]-vertices[r[1]][3])+abs(vertices[r[2]][3]-vertices[r[3]][3]))
                for src,dst in ((q[0],1),(q[2],3)):
                    copy=list(vertices[q[dst]]);copy[3]=vertices[src][3];vertices.append(copy)
                    for tri in (a,b):
                        for k in range(3):
                            if tri[k]==q[dst]:tri[k]=len(vertices)-1
                    q[dst]=len(vertices)-1
                mesh.setdefault('approximate_pairs',[]).append(first)
            ids=q
        if any(vertices[i][5:]!=[0,0,0,255] for i in ids):raise ArtError('Road vertex combiner requires a new color path')
        result.append(dict(vertices=ids,material=a[3],chunk=oa['chunk'],outer=oa['conditional_outer'],primitive=primitive,first=first))
    return result

def strip(mesh,material,u0,u1,fog):
    m=mesh['materials'][material];tile=str(m['tile']);a,b=m['tiles'][tile];_,size=m['sizes'][tile]
    tex=mesh['textures'][m['texture']['name']];width=((size>>12)&4095)//4+1;height=(size&4095)//4+1
    if m['combine']!=[0xfc147e28,0x44fe793c] or ((b>>8)&3)!=0 or (b&15) or ((b>>10)&15):
        raise ArtError('Unsupported source strip material')
    mirror=((b>>18)&3)==1
    if ((b>>18)&3)>1 or width!=tex['width']:raise ArtError('Unsupported source strip addressing')
    start=(a&511)*8//(width*2);rgba=bytes.fromhex(tex['rgba_hex'])
    period=height*(2 if mirror else 1)
    native_width=max(8,8*math.ceil((u1-u0)/256))
    if native_width>504 or u0>=u1:raise ArtError('Unsupported source strip width')
    def pixel(x,y):
        x%=width;y%=period
        if mirror and y>=height:y=period-1-y
        at=((start+y)*width+x)*4;r,g,blue,alpha=rgba[at:at+4]
        # Source shade is black; texture alpha mixes with that black, not holes.
        return [c*alpha/255 for c in (r,g,blue)]
    rows=[]
    for y in range(period):
        row=[]
        for x in range(native_width):
            u=(u0+(u1-u0)*(x+.5)/native_width)/32
            left=math.floor(u);f=u-left;a0=pixel(left,y);b0=pixel(left+1,y)
            row.append([a0[c]+(b0[c]-a0[c])*f for c in range(3)])
        rows.append(row)
    mean=[round(sum(rows[y][x][c] for y in range(period) for x in range(native_width))/(period*native_width)*31/255) for c in range(3)]
    average=[[sum(rows[y][x][c] for y in range(period))/period for c in range(3)] for x in range(native_width)]
    data=bytearray();variant_stride=native_width*(period+1)*2
    for fade in (0,128,255):
        for row in rows+[average]:
            for rgb in row:
                word=0x8000
                for c in range(3):word|=min(31,max(0,round((rgb[c]*(255-fade)+fog[c]*fade)*31/(255*255))))<<(5*c)
                data+=struct.pack('>H',word)
    return bytes(data),dict(width=native_width,period=period,mean=mean,stride=variant_stride)

def convert(mesh,road_hash,fog):
    quads=pair(mesh);bank=bytearray();strips=[];lookup={}
    for q in quads:
        if q['primitive']:q['strip']=65535;continue
        vertices=[mesh['vertices'][i] for i in q['vertices']];lo,hi=sorted((vertices[0][3],vertices[2][3]))
        key=(q['material'],lo,hi)
        if key not in lookup:
            pixels,info=strip(mesh,*key,fog);lookup[key]=len(strips)
            strips.append(dict(info,offset=len(bank),material=key[0],u0=lo,u1=hi));bank+=pixels
        q['strip']=lookup[key]
    chunks=len(mesh['chunks'])-1
    head=bytearray(64);head[:8]=b'FZXRTL01'
    chunk_data=bytearray();quad_data=bytearray();strip_data=bytearray();at=0
    for chunk in range(chunks):
        end=at
        while end<len(quads) and quads[end]['chunk']==chunk:end+=1
        chunk_data+=struct.pack('>II',at,end-at);at=end
    if at!=len(quads):raise ArtError('Noncontiguous source chunk order')
    for q in quads:
        quad_data+=struct.pack('>8H',*q['vertices'],q['strip'],int(q['outer'])|int(q['primitive'])*2,q['first'],q['chunk'])
    for s in strips:
        strip_data+=struct.pack('>4I6H4x',s['offset'],s['stride'],s['offset']+s['width']*s['period']*2,0,
                               s['width'],s['period'],*s['mean'],s['material'])
    total=64+len(chunk_data)+len(quad_data)+len(strip_data)
    struct.pack_into('>10I',head,8,total,road_hash,fnv(bank),len(bank),chunks,len(quads),len(strips),64,64+len(chunk_data),64+len(chunk_data)+len(quad_data))
    meta=head+chunk_data+quad_data+strip_data
    return bytes(meta),bytes(bank),dict(quads=len(quads),triangles=len(mesh['triangles']),strips=len(strips),chunks=chunks,
        metadata_bytes=len(meta),texture_bytes=len(bank),metadata_hash=fnv(meta),texture_hash=fnv(bank),road_hash=road_hash)

def generate(source,out):
    output_location(out)
    pack=ROOT/'generated/road_pack';subprocess.run(['bash',str(ROOT/'tools/check_receipt.sh'),str(source/'road.receipt')],check=True)
    subprocess.run(['bash',str(ROOT/'tools/check_receipt.sh'),str(pack/'road_pack.receipt')],check=True)
    mesh=json.loads((source/'road.json').read_text(encoding='utf-8'));manifest=json.loads((pack/'manifest.json').read_text(encoding='utf-8'))
    meta,bank,info=convert(mesh,manifest['hash'],manifest['environment']['course_fog'])
    if info['quads']*2!=manifest['counts'][1]:raise ArtError('Road coverage mismatch')
    out.mkdir(parents=True,exist_ok=True);(out/'ROADTL.PAK').write_bytes(meta);(out/'ROADTX.PAK').write_bytes(bank)
    header='/* Generated source-road native tile identity; regenerate. */\n'
    for key in ('metadata_bytes','texture_bytes','metadata_hash','texture_hash'):
        header+='#define FZX_ROAD_TILE_%s %du\n'%(key.upper(),info[key])
    (out/'road_tile_identity.h').write_bytes(header.encode())
    info['adaptations']=['paired source triangles become native quads','shared horizontal strip resampling','longitudinal mean mip for minified repetition','three fog levels plus native Gouraud correction']
    (out/'road_tiles.json').write_bytes((json.dumps(info,indent=2)+'\n').encode())
    dependencies=[Path(__file__),ROOT/'tools/import_source_art.py',ROOT/'tools/road_pack.py',source/'road.receipt',source/'road.json',pack/'road_pack.receipt',pack/'manifest.json']
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    receipt=['# fzx generation receipt v1; regenerate, never edit']
    receipt+=['generator %s %s'%(digest(p),os.path.relpath(p,ROOT/'tools')) for p in dependencies]
    receipt+=['output %s %s'%(digest(out/n),n) for n in ('ROADTL.PAK','ROADTX.PAK','road_tile_identity.h','road_tiles.json')]
    (out/'road_tiles.receipt').write_bytes(('\n'.join(receipt)+'\n').encode());print(json.dumps(info))

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--source',type=Path,default=ROOT/'generated/road');ap.add_argument('--output',type=Path,default=ROOT/'generated/road_tiles');args=ap.parse_args();generate(args.source,args.output)
