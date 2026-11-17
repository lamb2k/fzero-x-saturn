#!/usr/bin/env python3
"""Offline original stock Records previews, sampled as transparent Saturn sprites.

Runs only on privately extracted source geometry/textures and a receipt-checked
original-source matrix capture. This bounded rasterizer is an authorized Saturn
adaptation: pixel-centre coverage, nearest UV texels, interpolated directional
lighting and RGB555 output. It does not reproduce N64 subpixel coverage/filtering.
No retail data is checked in and no runtime display-list interpreter is added.

RMODELS.PAK has a32-byte big-endian header (>8s6I: magic,version,count,
descriptor stride,bank offset,bank bytes,bank FNV). The120 character-major
descriptors are40bytes (>6H2h6I): character,skin,width,height,VDP1 format,
palette count,x,y,pixel offset/bytes,palette offset/bytes,combined FNV,body RGB24.
Offsets are bank-relative; x/y are relative to the source viewport centre.
The generated C identity table is intended for bounded native selection/copy.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import os
import struct
import sys

from import_source_art import ROOT, DECOMP, ArtError, output_location, verify_source
from machine_menu_models import census, address
from road_pack import fnv

MAGIC = b'FZXRCM1\0'
HEADER = 32
DESCRIPTOR = 40
LIMIT = 128


def sha(data):
    return hashlib.sha256(data).hexdigest()


def vector(v, m, translate=False):
    return [sum(v[k]*m[k*4+j] for k in range(3))+(m[12+j] if translate else 0) for j in range(3)]


def cross(a, b):
    return a[0]*b[1]-a[1]*b[0]


def sub(a, b):
    return [x-y for x, y in zip(a, b)]


def validate_capture(c):
    for key, count in (('model',16), ('view',16), ('projection',2), ('light',9), ('viewport_scale',2)):
        if key not in c or len(c[key]) != count or any(not isinstance(v,(int,float)) or not math.isfinite(v) for v in c[key]):
            raise ArtError('Invalid Records source matrix capture: '+key)
    if c['viewport_scale'] != [160,120]:
        raise ArtError('Unsupported original Records viewport scale')
    if any(v <= 0 for v in c['projection']) or sum(v*v for v in c['light'][6:]) <= 0:
        raise ArtError('Invalid original Records projection/light')
    if any(not 0 <= v <= 255 for v in c['light'][:6]):
        raise ArtError('Original Records light color range')
    for m in (c['model'], c['view']):
        if any(abs(m[i]) > 1e-6 for i in (3,7,11)) or m[15] != 1:
            raise ArtError('Records capture is not an affine row matrix')
    if 'matrix_q16' in c:
        q=c['matrix_q16']
        combined=[sum(c['model'][i*4+k]*c['view'][k*4+j] for k in range(4)) for i in range(4) for j in range(4)]+c['projection']
        if len(q)!=18 or any(not isinstance(v,int) or not -(1<<31)<=v<(1<<31) for v in q):
            raise ArtError('Invalid source Records matrix identity')
        if max(abs(a-b/65536) for a,b in zip(combined,q))>2/65536:
            raise ArtError('Original Records float and packed matrices disagree')
    return c


def read_capture(path, receipt):
    """Require a current source-generator receipt, including the actual capture."""
    path=path.resolve();receipt=receipt.resolve()
    if path.parent!=receipt.parent or not receipt.is_file():
        raise ArtError('Missing original Records capture receipt')
    lines=receipt.read_text().splitlines()
    if not lines or not lines[0].startswith('# fzx generation receipt v1'):
        raise ArtError('Unknown original Records capture receipt')
    found=False;source=False;dependencies=[path,receipt]
    for line in lines[1:]:
        if not line or line.startswith('#'):continue
        parts=line.split(' ',2)
        if len(parts)!=3 or parts[0] not in ('generator','input','output'):
            raise ArtError('Invalid original Records capture receipt line')
        kind,digest,name=parts
        if kind=='input':continue
        target=((ROOT/'tools') if kind=='generator' else receipt.parent)/name
        if not target.is_file() or sha(target.read_bytes())!=digest:
            raise ArtError('Stale original Records capture dependency: '+name)
        if kind=='generator':
            dependencies.append(target.resolve())
            if name=='records_draw_source.py':source=True
        elif target.resolve()==path:found=True
    if not found or not source:raise ArtError('Capture is not owned by original Records source generator')
    return validate_capture(json.loads(path.read_text())),dependencies


def project(vertices, c):
    points=[];depth=[];shades=[]
    light=c['light'];direction=light[6:];length=math.sqrt(sum(v*v for v in direction))
    direction=[v/length for v in direction]
    # Uniform model scale is removed for normal lighting, preserving orientation.
    scales=[math.sqrt(sum(c['model'][i*4+j]**2 for j in range(3))) for i in range(3)]
    if min(scales) <= 0 or max(scales)-min(scales)>1e-6:
        raise ArtError('Records normal transform requires original uniform model scale')
    normal_matrix=[v/scales[0] for v in c['model']]
    for vertex in vertices:
        eye=vector(vector(vertex['position'],c['model'],True),c['view'],True)
        z=-eye[2]
        if not 16 < z < 8192:raise ArtError('Records model requires source near/far clipping')
        points.append([c['viewport_scale'][0]*c['projection'][0]*eye[0]/z,
                       -c['viewport_scale'][1]*c['projection'][1]*eye[1]/z])
        depth.append(z)
        normal=vector(vertex['normal'],normal_matrix)
        intensity=max(0,min(1,sum(a*b for a,b in zip(normal,direction))/127))
        shades.append([min(255,(light[i]+intensity*light[3+i]))/255 for i in range(3)])
    return points,depth,shades


def texel(texture, uv, sampler):
    if sampler.get('s_shift',0) or sampler.get('t_shift',0):
        raise ArtError('Unsupported shifted Records source sampler')
    w,h=texture['width'],texture['height']
    if len(texture['rgba']) != w*h*4:raise ArtError('Records texture extent')
    xy=[address(math.floor((v+16)/32),n,sampler[a+'_mode'],sampler[a+'_mask'])
        for v,n,a in zip(uv,(w,h),('s','t'))]
    if not 0<=xy[0]<w or not 0<=xy[1]<h:raise ArtError('Records texel outside source image')
    at=(xy[1]*w+xy[0])*4
    return texture['rgba'][at:at+4]


def color(material, sampled, body, shade):
    env=body if material['environment']=='body' else material['environment'][:3]
    mode=material['combine']
    if mode not in (0,1,2):raise ArtError('Unsupported original Records combiner')
    channels=[]
    for i in range(3):
        if mode==2:value=env[i]
        elif mode==0:value=(sampled[i]*sampled[3]+env[i]*(255-sampled[3]))/255
        else:value=(material['primitive'][i]*sampled[i]+env[i]*(255-sampled[i]))/255
        channels.append(max(0,min(31,int(value*shade[i])/8)))
    return 0x8000 | sum(int(v)<<(i*5) for i,v in enumerate(channels))


def raster(machine, textures, capture, skin, sample_scale=1):
    """Return integer-pixel sprite relative to original viewport centre."""
    validate_capture(capture)
    if skin not in range(4) or len(machine['skins'])!=4 or sample_scale not in (1,2):
        raise ArtError('Unsupported Records skin/sampling')
    geometry=machine['geometry'];vertices=geometry['vertices']
    if not 0<len(vertices)<=512 or not 0<len(geometry['triangles'])<=256:
        raise ArtError('Records source model bounds')
    points,depth,shades=project(vertices,capture)
    points=[[x*sample_scale,y*sample_scale] for x,y in points]
    left=math.floor(min(p[0] for p in points));top=math.floor(min(p[1] for p in points))
    right=math.ceil(max(p[0] for p in points));bottom=math.ceil(max(p[1] for p in points))
    if right-left>LIMIT*sample_scale or bottom-top>LIMIT*sample_scale:
        raise ArtError('Records source preview exceeds bounded raster')
    w=right-left;h=bottom-top;pixels=[0]*(w*h);zbuffer=[math.inf]*(w*h)
    for triangle in geometry['triangles']:
        indices=triangle['vertices']
        if len(indices)!=3 or any(i not in range(len(vertices)) for i in indices):
            raise ArtError('Records triangle source index')
        p=[points[i] for i in indices];area=cross(sub(p[1],p[0]),sub(p[2],p[0]))
        if area>=-1e-10:continue
        material=geometry['materials'][triangle['material']]
        binding=machine['bindings'][material['tile']];texture=textures[binding['symbol']]
        uv=[[u*65535/65536 for u in vertices[i]['uv']] for i in indices]
        for y in range(max(top,math.floor(min(a[1] for a in p))),min(bottom,math.ceil(max(a[1] for a in p)))):
            for x in range(max(left,math.floor(min(a[0] for a in p))),min(right,math.ceil(max(a[0] for a in p)))):
                point=[x+.5,y+.5]
                b=cross(sub(point,p[0]),sub(p[2],p[0]))/area
                c=cross(sub(p[1],p[0]),sub(point,p[0]))/area
                weights=[1-b-c,b,c]
                if min(weights)<-1e-10:continue
                recip=[weights[k]/depth[i] for k,i in enumerate(indices)];z=1/sum(recip)
                at=(y-top)*w+x-left
                if z>=zbuffer[at]:continue
                zbuffer[at]=z
                coords=[sum(recip[k]*uv[k][j] for k in range(3))*z for j in range(2)]
                sample=texel(texture,coords,binding['sampler']) if material['combine']!=2 else [0,0,0,0]
                shade=[sum(weights[k]*shades[i][j] for k,i in enumerate(indices)) for j in range(3)]
                pixels[at]=color(material,sample,machine['skins'][skin],shade)
    if not any(pixels):raise ArtError('Original Records preview is empty')
    return dict(x=left,y=top,width=w,height=h,pixels=pixels)


def encode(sprite):
    """Exact RGB555: choose CLUT16 only when it saves bytes; no color reduction."""
    w,h=sprite['width'],sprite['height'];source=sprite['pixels']
    if not 0<w<=LIMIT or not 0<h<=LIMIT or len(source)!=w*h or any(p and not 0x8000<=p<=0xffff for p in source):
        raise ArtError('Invalid Records sprite extent/color')
    # Tight crop to visible coverage, preserve original viewport-relative origin.
    occupied=[(i%w,i//w) for i,p in enumerate(source) if p]
    if not occupied:raise ArtError('Empty Records sprite')
    x0=min(p[0] for p in occupied);x1=max(p[0] for p in occupied)+1
    y0=min(p[1] for p in occupied);y1=max(p[1] for p in occupied)+1
    width=(x1-x0+7)//8*8;height=y1-y0
    pixels=[source[(y+y0)*w+x+x0] if x<x1-x0 else 0 for y in range(height) for x in range(width)]
    colors=sorted(set(pixels)-{0})
    palette=b'';fmt=5
    if len(colors)<=15 and width*height//2+32<width*height*2:
        fmt=1;colors=[0]+colors;mapping={p:i for i,p in enumerate(colors)}
        palette=struct.pack('>16H',*(colors+[0]*(16-len(colors))))
        packed=bytes((mapping[pixels[i]]<<4)|mapping[pixels[i+1]] for i in range(0,len(pixels),2))
    else:packed=struct.pack('>%dH'%len(pixels),*pixels)
    packed+=b'\0'*((-len(packed))%8)
    return dict(x=sprite['x']+x0,y=sprite['y']+y0,width=width,height=height,format=fmt,
                pixels=packed,palette=palette,vram_bytes=len(packed)+len(palette),opaque=sum(bool(p) for p in pixels))


def select(entries, character, custom_type, body):
    if custom_type!=0 or character not in range(30) or len(body)!=3:
        raise ArtError('Unsupported custom Records machine')
    matches=[r for r in entries if r['character']==character and r['body']==list(body)]
    if not matches:raise ArtError('Unrecognized original Records body color')
    # Source skins may alias the same RGB. Their baked output must then coincide.
    if len({(r['pixel_sha256'],r['palette_sha256']) for r in matches})!=1:
        raise ArtError('Ambiguous original Records body color')
    return matches[0]


def pack(sprites):
    if len(sprites)!=120 or [(r['character'],r['skin']) for r in sprites]!=[(c,s) for c in range(30) for s in range(4)]:
        raise ArtError('Records preview stock inventory must contain30x4')
    bank=bytearray();descriptors=bytearray();report=[]
    for r in sprites:
        e=encode(r['sprite']);offset=len(bank);bank.extend(e['pixels']);palette_offset=len(bank);bank.extend(e['palette'])
        body=r['body']
        if len(body)!=3 or any(not isinstance(v,int) or not 0<=v<=255 for v in body):raise ArtError('Records body color')
        body_word=(body[0]<<16)|(body[1]<<8)|body[2]
        descriptors.extend(struct.pack('>6H2h6I',r['character'],r['skin'],e['width'],e['height'],e['format'],len(e['palette'])//2,
                                       e['x'],e['y'],offset,len(e['pixels']),palette_offset,len(e['palette']),fnv(e['pixels']+e['palette']),body_word))
        report.append(dict(character=r['character'],skin=r['skin'],body=body,x=e['x'],y=e['y'],width=e['width'],height=e['height'],
                           format=e['format'],pixel_offset=offset,pixel_bytes=len(e['pixels']),palette_offset=palette_offset,
                           palette_bytes=len(e['palette']),vram_bytes=e['vram_bytes'],opaque=e['opaque'],
                           fnv=fnv(e['pixels']+e['palette']),body_rgb24=body_word,
                           pixel_sha256=sha(e['pixels']),palette_sha256=sha(e['palette'])))
    header=struct.pack('>8s6I',MAGIC,1,120,DESCRIPTOR,HEADER+len(descriptors),len(bank),fnv(bank))
    data=header+descriptors+bank
    validate_pack(data)
    return data,report


def validate_pack(data, expected_sha=None):
    if expected_sha and sha(data)!=expected_sha:raise ArtError('Records preview pack identity')
    if len(data)<HEADER:raise ArtError('Records preview header missing')
    magic,version,count,stride,start,size,hash_value=struct.unpack_from('>8s6I',data)
    if (magic,version,count,stride,start)!=(MAGIC,1,120,DESCRIPTOR,HEADER+120*DESCRIPTOR) or len(data)!=start+size or fnv(data[start:])!=hash_value:
        raise ArtError('Records preview header/extent/checksum')
    end=0
    for i in range(count):
        c,s,w,h,fmt,pal,x,y,offset,n,po,pn,pixelhash,body=struct.unpack_from('>6H2h6I',data,HEADER+i*stride)
        raw=w*h*(2 if fmt==5 else .5)
        expected=(int(raw)+7)//8*8
        if (c,s)!=(i//4,i%4) or w%8 or not 0<w<=LIMIT or not 0<h<=LIMIT or fmt not in (1,5) or abs(x)>LIMIT or abs(y)>LIMIT:
            raise ArtError('Records preview descriptor bounds')
        if n!=expected or offset!=end or po!=offset+n or pn!=(32 if fmt==1 else 0) or pal!=pn//2 or po+pn>size or body>0xffffff:
            raise ArtError('Records preview sprite span')
        if fnv(data[start+offset:start+po+pn])!=pixelhash:raise ArtError('Records preview sprite checksum')
        end=po+pn
    if end!=size:raise ArtError('Records preview unowned tail')
    return True


def identity_header(data, entries, capture):
    maximum=max(r['vram_bytes'] for r in entries)
    text='/* Generated Records source previews; regenerate, never edit. */\n'
    text+='#define FZX_RECORDS_MODELS_BYTES %du\n#define FZX_RECORDS_MODELS_HASH %du\n'%(len(data),fnv(data))
    text+='#define FZX_RECORDS_MODELS_BANK_OFFSET %du\n'%(HEADER+120*DESCRIPTOR)
    text+='#define FZX_RECORDS_MODELS_MAX_VRAM_BYTES %du\n#define FZX_RECORDS_MODELS_SIX_VRAM_BYTES %du\n'%(maximum,maximum*6)
    text+='typedef struct { unsigned pixel_offset,pixel_bytes,palette_offset,palette_bytes,hash,body_rgb24; unsigned short width,height,format; short x,y; } FzxRecordsModelAsset;\n'
    text+='static const FzxRecordsModelAsset fzx_records_models[30][4]={\n'
    for character in range(30):
        text+=' {'+','.join('{%du,%du,%du,%du,%du,%du,%du,%du,%du,%d,%d}'%tuple(r[k] for k in
                         ('pixel_offset','pixel_bytes','palette_offset','palette_bytes','fnv','body_rgb24','width','height','format','x','y'))
                         for r in entries[character*4:character*4+4])+'},\n'
    text+='};\n'
    if 'matrix_q16' in capture:
        values=capture['matrix_q16']
        if len(values)!=18 or any(not isinstance(v,int) or not -(1<<31)<=v<(1<<31) for v in values):
            raise ArtError('Invalid source Records matrix identity')
        text+='static const int fzx_records_models_matrix_q16[18]={'+','.join(map(str,values))+'};\n'
    return text.encode()


def decode(data, entry):
    start=HEADER+120*DESCRIPTOR
    offset=start+entry['pixel_offset'];pixels=data[offset:offset+entry['pixel_bytes']]
    count=entry['width']*entry['height']
    if entry['format']==5:return list(struct.unpack('>%dH'%count,pixels[:count*2]))
    palette=struct.unpack_from('>16H',data,start+entry['palette_offset'])
    result=[]
    for value in pixels:result.extend([palette[value>>4],palette[value&15]])
    return result[:count]


def local_review(data, entries, sprites, output):
    """Original point raster beside decoded Saturn sprite, privately retained."""
    from PIL import Image,ImageDraw
    sheet=Image.new('RGB',(4*192,30*64),(30,30,30));draw=ImageDraw.Draw(sheet)
    changed=0
    def rgba(value):return tuple(((value>>(i*5))&31)*255//31 for i in range(3))+(255 if value else 0,)
    for row,entry in zip(sprites,entries):
        original=row['sprite'];decoded=decode(data,entry)
        image=Image.new('RGBA',(original['width'],original['height']));image.putdata([rgba(v) for v in original['pixels']])
        baked=Image.new('RGBA',(entry['width'],entry['height']));baked.putdata([rgba(v) for v in decoded])
        # Compare original occupied pixels and transparent/padded output in world pixel coordinates.
        a={(original['x']+i%original['width'],original['y']+i//original['width']):v for i,v in enumerate(original['pixels']) if v}
        b={(entry['x']+i%entry['width'],entry['y']+i//entry['width']):v for i,v in enumerate(decoded) if v}
        changed+=sum(a.get(p,0)!=b.get(p,0) for p in set(a)|set(b))
        x=entry['skin']*192;y=entry['character']*64
        draw.text((x+2,y+2),'%02d/%d source / sprite'%(entry['character'],entry['skin']),fill='white')
        sheet.paste(image,(x+48+original['x'],y+40+original['y']),image)
        sheet.paste(baked,(x+144+entry['x'],y+40+entry['y']),baked)
    if changed:raise ArtError('Records cropped/packed sprite differs from source raster')
    path=output/'records-models-source-vs-sprite.png';sheet.save(path)
    return dict(tier='HOST',images=120,changed_opaque_pixels=changed,
                comparison='Original point-sampled sourceUV raster vs decoded cropped Saturn RGB555/CLUT16; same raster, exact transport',
                image=path.name,image_sha256=sha(path.read_bytes()),native='UNRUN',exact_n64=False)


def export(rom, output, capture, capture_dependencies=()):
    output=output.resolve();output_location(output);verify_source(DECOMP);output.mkdir(parents=True,exist_ok=True)
    capture=validate_capture(capture)
    if 'matrix_q16' not in capture:raise ArtError('Original Records packed matrix identity is required')
    machines,textures,source=census(rom)
    sprites=[dict(character=m['character'],skin=s,body=m['skins'][s],sprite=raster(m,textures,capture,s)) for m in machines for s in range(4)]
    data,entries=pack(sprites)
    for m in machines:
        for body in m['skins']:select(entries,m['character'],0,body)
    maximum=max(r['vram_bytes'] for r in entries)
    report=dict(schema=1,tier='HOST',source=source,capture=capture,entries=entries,pack_bytes=len(data),pack_sha256=sha(data),
                max_sprite_bytes=maximum,six_preview_worst_bytes=maximum*6,budget_bytes=8640,budget_fits=maximum*6<=8640,
                adaptation='Pixel-centre coverage; perspective originalUV nearest texels; affine Gouraud shade; RGB555; exact CLUT16 when smaller',
                exact_n64=False,native='UNRUN',hardware='UNRUN')
    report['source_models']=[dict(character=m['character'],vertices=len(m['geometry']['vertices']),triangles=len(m['geometry']['triangles']),
                                 geometry_sha256=sha(json.dumps(m['geometry'],sort_keys=True).encode())) for m in machines]
    report['source_textures']={name:sha(t['rgba']) for name,t in textures.items()}
    report['local_comparison']=local_review(data,entries,sprites,output)
    outputs={'RMODELS.PAK':data,'records_models.json':(json.dumps(report,indent=2)+'\n').encode(),
             'records_models_identity.h':identity_header(data,entries,capture),
             report['local_comparison']['image']:(output/report['local_comparison']['image']).read_bytes()}
    deps=[Path(__file__),ROOT/'tools/machine_menu_models.py',ROOT/'tools/import_source_art.py',ROOT/'tools/import_course.py',
          ROOT/'tools/model_assets.py',ROOT/'tools/machine_rebake.py',ROOT/'tools/road_pack.py',ROOT/'tools/countdown_pack.py',
          ROOT/'tools/menu_source.py',ROOT/'tools/course_menu_preview.py',
          DECOMP/'src/overlays/ovl_i3/records_entry.c',DECOMP/'src/sys/math.c',DECOMP/'src/game/racer.c',
          DECOMP/'src/overlays/ovl_i9/machine_draw.c',DECOMP/'src/overlays/ovl_i4/machine.c',
          DECOMP/'src/game/common.c',DECOMP/'include/fzx_game.h']
    deps+=sorted((DECOMP/'assets/yaml/us/rev0').glob('machine*.yaml'))
    deps+=list(capture_dependencies)
    deps=list(dict.fromkeys(deps))
    receipt=['# fzx generation receipt v1; regenerate, never edit']
    receipt+=['generator %s %s'%(sha(p.read_bytes()),os.path.relpath(p,ROOT/'tools').replace('\\','/')) for p in deps]
    receipt+=['input %s owner-rom'%sha(rom)]
    for name,content in outputs.items():
        (output/name).write_bytes(content);receipt.append('output %s %s'%(sha(content),name))
    (output/'records_models.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'generated/records_models')
    parser.add_argument('--config',type=Path)
    parser.add_argument('--capture',type=Path,required=True)
    parser.add_argument('--capture-receipt',type=Path)
    args=parser.parse_args();rom_path=json.loads(args.config.read_text())['rom'] if args.config else os.environ.get('FZX_TEST_ROM')
    if not rom_path:parser.error('Set FZX_TEST_ROM privately or pass --config')
    if os.name!='nt' and len(rom_path)>2 and rom_path[1]==':' and rom_path[2] in ('/','\\'):
        rom_path='/mnt/'+rom_path[0].lower()+'/'+rom_path[3:].replace('\\','/')
    try:
        capture,dependencies=read_capture(args.capture,args.capture_receipt or args.capture.parent/'records_draw_source.receipt')
        report=export(Path(rom_path).read_bytes(),args.output,capture,dependencies)
        print(json.dumps({k:report[k] for k in ('pack_bytes','max_sprite_bytes','six_preview_worst_bytes','budget_fits')}))
    except Exception as exc:
        print('Records model export failed: '+(str(exc) if isinstance(exc,ArtError) else type(exc).__name__),file=sys.stderr);return 1
    return 0


if __name__=='__main__':raise SystemExit(main())
