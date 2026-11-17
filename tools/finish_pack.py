#!/usr/bin/env python3
"""Bounded original finish-sign pack: exact solid sides and CLUT4 face charts.

Source triangles are neither merged nor sorted. The chart convention is A/B
at the top corners, C at both bottom corners. All source colors are opaque;
native VDP1 SPD=1 is required even when a face chart contains palette index 0.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import struct

from import_source_art import ROOT, ArtError, output_location
from finish_assets import GEOMETRY, OPA_SURF, TEXTURES
from road_pack import fnv
from floor_indexed import check_receipt

HEADER=64
VERTICES=256
TRIANGLES=222
IMAGES=98
STRIDE=16
WIDTH=HEIGHT=16
PALETTE_BYTES=32
CHART_BYTES=128
MAX_TEXTURE_BYTES=PALETTE_BYTES+IMAGES*CHART_BYTES
METADATA_BYTES=HEADER+(VERTICES+TRIANGLES+IMAGES)*STRIDE


def triangle_chart(vertices,pixels,palette):
    """Integer-rational original Q5 UV sampling; no host float rounding.

    vertices already contain the source 0xffff texture-scale result. Source
    face texture clamps at 16x16. Nearest replaces source three-point filtering
    explicitly, as in the existing countdown converter.
    """
    if len(vertices)!=3 or len(pixels)!=512 or not 1<=len(palette)<=16:
        raise ArtError('Invalid finish chart input')
    indices={v:i for i,v in enumerate(palette)}
    colors=struct.unpack('>256H',pixels)
    output=[];den=(WIDTH-1)*(HEIGHT-1)
    for y in range(HEIGHT):
        for x in range(WIDTH):
            weights=((WIDTH-1-x)*(HEIGHT-1-y),x*(HEIGHT-1-y),y*(WIDTH-1))
            uv=[sum(weights[k]*vertices[k][axis] for k in range(3)) for axis in (3,4)]
            u,v=[max(0,min(15,(q+16*den)//(32*den))) for q in uv]
            try:output.append(indices[colors[v*16+u]])
            except KeyError as exc:raise ArtError('Finish chart loses a source palette color') from exc
    return bytes((output[i]<<4)|output[i+1] for i in range(0,len(output),2))


def build(manifest,files):
    if manifest.get('schema')!=1:
        raise ArtError('Unknown finish manifest')
    model=manifest['model'];vertices=model['vertices'];triangles=model['triangles']
    materials=model['materials'];textures=manifest['textures']
    if (len(vertices),len(triangles),len(materials),len(textures))!=(VERTICES,TRIANGLES,2,2):
        raise ArtError('Finish coverage changed')
    if not model.get('source_order') or model.get('source_loaded_rows')!=VERTICES:
        raise ArtError('Finish source load/order identity lost')
    for i,m in enumerate(materials):
        if (m['texture']['name']!=TEXTURES[i] or m['geometry']!=GEOMETRY or
            m['combine']!=[0xfcffffff,0xfffcf279] or m['scale']!=[65535,65535] or
            m['tile']!=0 or m['other_l']!=OPA_SURF or m['other_h']!=0x82c00 or
            m['tiles'].get('0')!=([0xf5100400,0x4c130] if i==0 else [0xf5100800,0x90240])):
            raise ArtError('Finish material changed')
    source=[]
    for i,t in enumerate(textures):
        extent=(8,16)[i]
        data=files[t['file']]
        if (t['symbol'],t['width'],t['height'])!=(TEXTURES[i],extent,extent) or len(data)!=extent*extent*2:
            raise ArtError('Invalid finish texture extent')
        colors=struct.unpack('>'+'H'*(extent*extent),data)
        if any(c<0x8000 for c in colors) or len(set(colors))!=(1,13)[i]:
            raise ArtError('Finish source palette/opacity changed')
        source.append(data)
    solid=struct.unpack_from('>H',source[0])[0]
    palette=sorted(set(struct.unpack('>256H',source[1])))
    # Fill unused CLUT entries with an opaque original color, not transparent 0.
    blob=bytearray(struct.pack('>16H',*(palette+[palette[0]]*(16-len(palette)))))
    data=bytearray(HEADER);data[:8]=b'FZXFIN01'
    for v in vertices:
        if (len(v)!=9 or any(not isinstance(q,int) or not -32768<=q<=32767 for q in v[:5]) or
            any(not isinstance(q,int) or not 0<=q<=255 for q in v[5:])):
            raise ArtError('Invalid finish vertex')
        data+=struct.pack('>5h4B2x',*v)
    triangle_at=len(data);data+=bytes(TRIANGLES*STRIDE);image_at=len(data)
    images=[];cache={};report_images=[];used=set();side_count=0
    for ti,t in enumerate(triangles):
        if (len(t)!=4 or any(not isinstance(k,int) for k in t) or
            any(k<0 or k>=VERTICES for k in t[:3]) or len(set(t[:3]))!=3 or t[3] not in (0,1)):
            raise ArtError('Invalid finish triangle')
        points=[vertices[k][:3] for k in t[:3]]
        ab=[points[1][i]-points[0][i] for i in range(3)]
        ac=[points[2][i]-points[0][i] for i in range(3)]
        if not any(ab[(i+1)%3]*ac[(i+2)%3]-ab[(i+2)%3]*ac[(i+1)%3] for i in range(3)):
            raise ArtError('Degenerate finish triangle')
        used.update(t[:3]);image=0xffff
        if t[3]==0:side_count+=1
        else:
            image=len(images)
            chart=triangle_chart([vertices[k] for k in t[:3]],source[1],palette)
            if chart not in cache:
                cache[chart]=len(blob);blob+=chart
            offset=cache[chart]
            images.append(struct.pack('>2HI4H',WIDTH,HEIGHT,offset,ti,0,0,0))
            report_images.append(dict(triangle=ti,width=WIDTH,height=HEIGHT,offset=offset))
        struct.pack_into('>8H',data,triangle_at+ti*STRIDE,*t[:3],t[3],image,ti,0,0)
    if side_count!=124 or len(images)!=IMAGES or len(used)!=VERTICES:
        raise ArtError('Finish triangle/material/vertex coverage changed')
    data+=b''.join(images)
    if len(data)!=METADATA_BYTES or len(blob)>MAX_TEXTURE_BYTES:
        raise ArtError('Finish pack bounds exceeded')
    struct.pack_into('>14I',data,8,len(data),len(blob),fnv(blob),VERTICES,TRIANGLES,IMAGES,
                     HEADER,triangle_at,image_at,0,solid,GEOMETRY,0,0)
    struct.pack_into('>I',data,44,fnv(data))
    report=dict(metadata_bytes=len(data),texture_bytes=len(blob),source_vertices=VERTICES,
                source_triangles=TRIANGLES,side_triangles=124,face_triangles=IMAGES,
                images=report_images,unique_charts=len(cache),source_face_colors=len(palette),
                palette_bytes=32,chart_size=[WIDTH,HEIGHT],geometry_flags=GEOMETRY,
                source_order=True,lighting=False,depth_sort=False,opaque_palette_zero=True,
                source_projection=manifest.get('projection'),
                adaptations=['Nearest source texels replace three-point filtering',
                             'Original triangle UVs prebaked into collapsed-bottom rectangular charts',
                             'Constant opaque side texture represented by its exact RGB5 solid color'],
                native_rendering='UNRUN',appearance='UNRUN',hardware='UNRUN')
    return bytes(data),bytes(blob),report


def generate(source,output):
    output_location(output)
    covered=check_receipt(source/'finish.receipt')
    if (source/'finish.json').resolve() not in covered:
        raise ArtError('Finish manifest is not covered by its receipt')
    manifest=json.loads((source/'finish.json').read_text())
    inputs=[source/t['file'] for t in manifest['textures']]
    if any(p.resolve() not in covered for p in inputs):
        raise ArtError('Finish texture is not covered by its receipt')
    metadata,textures,report=build(manifest,{p.name:p.read_bytes() for p in inputs})
    outputs={'FINISH.PAK':metadata,'FINISHTX.PAK':textures,
             'finish_pack.json':(json.dumps(report,indent=2)+'\n').encode()}
    header='/* Generated original finish pack identity; regenerate. */\n'
    for name,value in (('BYTES',len(metadata)),('TEXTURE_BYTES',len(textures)),
                       ('HASH',fnv(metadata)),('TEXTURE_HASH',fnv(textures))):
        header+='#define FZX_FINISH_%s %du\n'%(name,value)
    outputs['finish_identity.h']=header.encode()
    deps=[Path(__file__),ROOT/'tools/finish_assets.py',ROOT/'tools/import_source_art.py',
          ROOT/'tools/road_pack.py',ROOT/'tools/floor_indexed.py',
          source/'finish.receipt',source/'finish.json',*inputs]
    receipt=['# fzx generation receipt v1; regenerate, never edit']
    receipt+=['generator %s %s'%(hashlib.sha256(p.read_bytes()).hexdigest(),os.path.relpath(p,ROOT/'tools').replace('\\','/')) for p in deps]
    output.mkdir(parents=True,exist_ok=True)
    for name,value in outputs.items():
        (output/name).write_bytes(value)
        receipt.append('output %s %s'%(hashlib.sha256(value).hexdigest(),name))
    (output/'finish_pack.receipt').write_text('\n'.join(receipt)+'\n',newline='\n')
    return {k:v for k,v in report.items() if k!='images'}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=ROOT/'generated/finish')
    parser.add_argument('--output',type=Path,default=ROOT/'generated/finish/pack')
    args=parser.parse_args()
    print(json.dumps(generate(args.source,args.output),indent=2))
