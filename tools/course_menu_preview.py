#!/usr/bin/env python3
"""Bake the original 24 stock course preview tubes and orbit camera offline.

FZXCPV01 is big endian. Header: magic[8], then 14 u32 values:
version, total bytes, course count, vertex count, quad count, camera count,
descriptor offset, XYZ offset, quad offset, camera offset, payload FNV1a,
maximum vertices, maximum quads, reserved zero.
Each 16-byte descriptor contains first vertex, vertex count, first quad,
quad count. XYZ is s16[3]; quads are four local u16 vertex indices, paired
from the source's six triangles per three-vertex ring. Each camera is 18
s32 Q16.16 words: source row-vector view[4][4], projection XX, projection YY.
Camera index equals yaw in degrees; source pitch20 and model scale0.25.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import os
import re
import shlex
import struct
import subprocess
import sys
from import_source_art import ROOT, DECOMP, ArtError, output_location, verify_source
from import_course import extract_rom_course, normalize_rom
from road_pack import fnv
from camera_assets import decode as decode_viewports, header as viewport_header


def check_source_contract():
    camera=(DECOMP/'src/game/camera.c').read_text()
    block=re.search(r'const CameraOrbitData D_800D50B0 = \{(.*?)\n\};',camera,re.S)
    expected=[0,0,0,0,0,0,0,1,0,0,1,0,1,60,60,1,0,0,0,0,1,1,
              0,0,0,0,0,1,0,1,0,1,0,0,10500,10500,1,20,20,1,0,0,1]
    if not block or [float(v) for v in re.findall(r'(-?\d+\.\d+)f',block[1])]!=expected:
        raise ArtError('Source course camera parameters changed')
    model=(DECOMP/'src/overlays/course_select/course_model.c').read_text()
    if 'guScale(gGfxPool->unk_20308, 0.25f, 0.25f, 0.25f);' not in model:
        raise ArtError('Source preview model scale changed')
    course=(DECOMP/'src/game/course.c').read_text()
    block=course[course.index('Gfx* Course_DrawModel('):course.index('Gfx* func_800A95B4(')]
    commands=re.findall(r'gSP2Triangles\(gfx\+\+, ([^;]+)\);',block)
    expected_commands=['i + 1, i + 0, i + 4, 0, i + 4, i + 0, i + 3, 0',
                       'i + 0, i + 2, i + 3, 0, i + 3, i + 2, i + 5, 0',
                       'i + 2, i + 1, i + 5, 0, i + 5, i + 1, i + 4, 0']
    if commands!=expected_commands*2: raise ArtError('Source preview triangle pairs changed')


def topology(count):
    if count<6 or count>=0xC00 or count%3:
        raise ArtError('Invalid source preview vertex count')
    return [q for i in range(0,count-3,3) for q in
            ((i+1,i,i+3,i+4),(i,i+2,i+5,i+3),(i+2,i+1,i+4,i+5))]


def parse_capture(text):
    lines=iter(text.splitlines())
    if next(lines,None)!='FZXCPVRAW1': raise ArtError('Preview capture magic')
    courses=[]
    for course in range(24):
        row=next(lines,'').split()
        if len(row)!=3 or row[:2]!=['C',str(course)]: raise ArtError('Preview course order')
        count=int(row[2]);quads=topology(count);vertices=[]
        for i in range(count):
            row=next(lines,'').split()
            if len(row)!=5 or row[:2]!=['V',str(i)]: raise ArtError('Preview vertex order')
            xyz=list(map(int,row[2:]))
            if any(v < -32768 or v>32767 for v in xyz): raise ArtError('Preview XYZ range')
            vertices.append(xyz)
        if vertices[:3]!=vertices[-3:]: raise ArtError('Preview tube is not closed')
        courses.append(dict(vertices=vertices,quads=quads))
    cameras=[]
    for i in range(361):
        row=next(lines,'').split()
        if len(row)!=20 or row[:2]!=['M',str(i)]: raise ArtError('Preview camera order')
        values=[float.fromhex(v) for v in row[2:]]
        if any(not math.isfinite(v) for v in values): raise ArtError('Nonfinite preview camera')
        cameras.append(values)
    if next(lines,None) is not None: raise ArtError('Trailing preview capture data')
    if struct.pack('<18f',*cameras[0])!=struct.pack('<18f',*cameras[360]):
        raise ArtError('Source camera full-orbit closure differs')
    return courses,cameras[:360]


def fixed(value):
    v=round(value*65536)
    if not -(1<<31)<=v<(1<<31): raise ArtError('Camera fixed-point overflow')
    return v


def pack(courses,cameras):
    if len(courses)!=24 or len(cameras)!=360: raise ArtError('Preview pack dimensions')
    vertices=[];quads=[];descriptors=[]
    for c in courses:
        if c['quads']!=topology(len(c['vertices'])): raise ArtError('Preview topology changed')
        if c['vertices'][:3]!=c['vertices'][-3:]: raise ArtError('Preview closure changed')
        descriptors.append((len(vertices),len(c['vertices']),len(quads),len(c['quads'])))
        vertices+=c['vertices'];quads+=c['quads']
    desc=b''.join(struct.pack('>4I',*d) for d in descriptors)
    xyz=b''.join(struct.pack('>3h',*v) for v in vertices)
    faces=b''.join(struct.pack('>4H',*q) for q in quads)
    camera=b''.join(struct.pack('>18i',*(fixed(v) for v in c)) for c in cameras)
    # XYZ count is divisible by three; align subsequent u32 camera records.
    padding=b'\0'*((-(64+len(desc)+len(xyz)))%4)
    offsets=[64,64+len(desc),64+len(desc)+len(xyz)+len(padding)]
    offsets.append(offsets[-1]+len(faces))
    payload=desc+xyz+padding+faces+camera
    values=[1,64+len(payload),24,len(vertices),len(quads),360,*offsets,fnv(payload),
            max(d[1] for d in descriptors),max(d[3] for d in descriptors),0]
    data=struct.pack('>8s14I',b'FZXCPV01',*values)+payload
    return data,dict(bytes=len(data),vertices=len(vertices),quads=len(quads),cameras=360,
                     descriptor_offset=offsets[0],vertex_offset=offsets[1],quad_offset=offsets[2],
                     camera_offset=offsets[3],payload_fnv=fnv(payload),pack_fnv=fnv(data),
                     max_vertices=values[-3],max_quads=values[-2],descriptors=descriptors)


def validate(data):
    if len(data)<64: raise ArtError('Truncated preview pack')
    magic,*h=struct.unpack_from('>8s14I',data)
    version,size,nc,nv,nq,nm,do,vo,qo,mo,checksum,mv,mq,reserved=h
    if magic!=b'FZXCPV01' or (version,size,nc,nm,do,reserved)!=(1,len(data),24,360,64,0):
        raise ArtError('Preview header mismatch')
    if vo!=do+nc*16 or qo!=(vo+nv*6+3)&~3 or mo!=qo+nq*8 or size!=mo+nm*72:
        raise ArtError('Preview pack extents')
    if fnv(data[64:])!=checksum: raise ArtError('Preview payload checksum')
    cursors=[0,0];maxima=[0,0]
    for i in range(nc):
        fv,vc,fq,qc=struct.unpack_from('>4I',data,do+i*16)
        expected=topology(vc)
        if [fv,fq]!=cursors or qc!=len(expected) or fv+vc>nv or fq+qc>nq:
            raise ArtError('Preview descriptor bounds')
        if data[vo+fv*6:vo+(fv+3)*6]!=data[vo+(fv+vc-3)*6:vo+(fv+vc)*6]:
            raise ArtError('Preview vertex closure')
        actual=[struct.unpack_from('>4H',data,qo+(fq+j)*8) for j in range(qc)]
        if actual!=expected: raise ArtError('Preview face topology')
        cursors=[fv+vc,fq+qc];maxima=[max(maxima[0],vc),max(maxima[1],qc)]
    if cursors!=[nv,nq] or maxima!=[mv,mq]: raise ArtError('Preview total bounds')
    return True


def scoped_build(output,cc):
    """Fresh private build; never mutate ref/build.sh or shared race outputs."""
    build=output/('build-'+cc);build.mkdir(parents=True,exist_ok=True)
    source=(ROOT/'ref/build.sh').read_text()
    source=source.replace('ROOT=$(cd "$(dirname "$0")/.." && pwd)', 'ROOT='+shlex.quote(linux_path(ROOT)))
    source=source.replace('OUT=$ROOT/ref/build/${CC}${OPT}','OUT='+shlex.quote(linux_path(build)))
    source=source.replace('OUT=$OUT-camera-$ART','OUT=$OUT')
    source=source.replace('ASSETS=$ROOT/generated/camera_$ART','ASSETS='+shlex.quote(linux_path(output)))
    source=source.replace('BRIDGES="bridge hooks"','BRIDGES="bridge hooks course_menu_preview"')
    source=source.replace('DRIVER=main','DRIVER=course_menu_preview')
    source=source.replace('-DFZX_SOURCE_CAMERA=$CAMERA -DFZX_ROAD_PLAN=$PLAN',
                          '-DFZX_PREVIEW_DRIVER=1 -DFZX_SOURCE_CAMERA=$CAMERA -DFZX_ROAD_PLAN=$PLAN')
    script=build/'build.sh';script.write_bytes(source.encode())
    overrides=dict(FZX_SOURCE_CAMERA='1',REF_CAMERA_ART='retail',FZX_ROAD_EXPORT='0',
                   FZX_ROAD_PLAN='0',FZX_BACKDROP_STATE='0',REF_STUB_CALLS='1')
    command=['bash',linux_path(script),cc,'-O2']
    if os.name=='nt': command=['wsl','--','env',*(k+'='+v for k,v in overrides.items()),*command]
    with (build/'build.log').open('w') as log:
        subprocess.run(command,cwd=ROOT,env=dict(os.environ,**overrides),check=True,stdout=log,stderr=subprocess.STDOUT)
    return build/'fzx-ref'


def export(rom,output):
    output=output.resolve();output_location(output);verify_source(DECOMP);check_source_contract()
    _,provenance=extract_rom_course(rom);normalized,_=normalize_rom(rom)
    yaml=DECOMP/'assets/yaml/us/rev0/course_data.yaml'
    records=re.findall(r'^(aCourse\w+):\s+type: FZX:COURSE\s+offset: (0x[0-9A-Fa-f]+)',yaml.read_text(),re.M)
    if len(records)<24 or int(records[0][1],16)!=0x2AD1E0: raise ArtError('Source course inventory changed')
    records=records[:24];output.mkdir(parents=True,exist_ok=True)
    # Standalone generation must not depend on an earlier fixture-camera run.
    # Use the owner's actual viewport records in this scoped reference build.
    from import_source_art import mio0
    viewport_yaml=DECOMP/'assets/yaml/us/rev0/course_track_gfx.yaml'
    viewports=viewport_header(decode_viewports(mio0(normalized[0x16C8A0:0x17B1E0]),viewport_yaml.read_text())).encode()
    (output/'viewports.h').write_bytes(viewports)
    viewport_deps=[Path(__file__),ROOT/'tools/camera_assets.py',ROOT/'tools/import_source_art.py',
                   ROOT/'tools/import_course.py',viewport_yaml]
    viewport_receipt=['# fzx generation receipt v1; regenerate, never edit']
    viewport_receipt+=['generator %s %s'%(hashlib.sha256(p.read_bytes()).hexdigest(),os.path.relpath(p,ROOT/'tools').replace('\\','/')) for p in viewport_deps]
    viewport_receipt+=['input %s owner-rom'%hashlib.sha256(rom).hexdigest(),
                      'output %s viewports.h'%hashlib.sha256(viewports).hexdigest()]
    (output/'viewports.receipt').write_bytes(('\n'.join(viewport_receipt)+'\n').encode())
    if any(int(offset,16)!=0x2AD1E0+i*0x7E0 for i,(_,offset) in enumerate(records)):
        raise ArtError('Source stock course ROM stride changed')
    course_file=output/'course-records.bin'
    course_file.write_bytes(b''.join(normalized[int(offset,16):int(offset,16)+0x7E0] for _,offset in records))
    captures=[];build_hashes={}
    for cc in ('gcc','clang'):
        executable=scoped_build(output,cc)
        stubfile=output/(cc+'-calls.txt')
        command=[linux_path(executable),linux_path(course_file),linux_path(stubfile)]
        if os.name=='nt': command=['wsl','--',*command]
        raw=subprocess.check_output(command,cwd=ROOT)
        (output/(cc+'-capture.txt')).write_bytes(raw)
        calls=stubfile.read_text().splitlines()
        # Original course DMA invalidates the N64 cache twice per record.
        # Host memcpy is coherent; no numerical/geometry/camera stubs may run.
        allowed={'osInvalDCache'}
        unexpected=[line for line in calls if line.split()[0].removeprefix('camera:') not in allowed]
        if unexpected: raise ArtError('Unexpected source preview stub calls: '+','.join(unexpected))
        captures.append(parse_capture(raw.decode()))
        build_hashes[cc]=hashlib.sha256(executable.read_bytes()).hexdigest()
    if captures[0]!=captures[1]: raise ArtError('GCC and Clang preview captures differ')
    courses,cameras=captures[0];data,report=pack(courses,cameras);validate(data)
    report.update(schema=1,tier='HOST',rom_sha256=provenance['normalized_rom_sha256'],
                  build_sha256=build_hashes,course_symbols=[s for s,_ in records],
                  capture_sha256=hashlib.sha256((output/'gcc-capture.txt').read_bytes()).hexdigest(),
                  camera_pitch=20,camera_yaw='index in degrees',source_model_scale=0.25,
                  topology='Course_DrawModel triangle pairs; original order',
                  quantization='XYZ exact s16; camera nearest Q16.16',
                  courses=courses,cameras_source=cameras)
    header='/* Generated source course preview identity; regenerate, never edit. */\n'
    for name,value in [('BYTES',len(data)),('HASH',fnv(data)),('COURSES',24),('CAMERAS',360),
                       ('MAX_VERTICES',report['max_vertices']),('MAX_QUADS',report['max_quads'])]:
        header+='#define FZX_COURSE_PREVIEW_%s %du\n'%(name,value)
    outputs={'CPREVIEW.PAK':data,'course_preview_identity.h':header.encode(),'viewports.h':viewports,
             'course_preview.json':(json.dumps(report,indent=2)+'\n').encode()}
    deps=[Path(__file__),ROOT/'ref/course_menu_preview.c',ROOT/'ref/build.sh',ROOT/'ref/ref.h',
          ROOT/'ref/bridge.c',ROOT/'ref/hooks.c',ROOT/'ref/camera_bridge.c',ROOT/'ref/gen_stubs.py',
          ROOT/'ref/overrides.h',ROOT/'tools/import_source_art.py',ROOT/'tools/import_course.py',
          ROOT/'tools/road_pack.py',ROOT/'tools/camera_assets.py',yaml,viewport_yaml]
    deps+=list((DECOMP/'src/game').glob('*.c'))
    deps+=[DECOMP/'src/sys/math.c',DECOMP/'src/sys/controller.c',
           DECOMP/'src/overlays/course_select/course_model.c',DECOMP/'src/overlays/ovl_i2/course_context.c',
           DECOMP/'src/overlays/ovl_i3/cpu.c']
    receipt=['# fzx generation receipt v1; regenerate, never edit']
    receipt+=['generator %s %s'%(hashlib.sha256(p.read_bytes()).hexdigest(),os.path.relpath(p,ROOT/'tools').replace('\\','/')) for p in deps]
    receipt+=['input %s owner-rom'%hashlib.sha256(rom).hexdigest()]
    for name,content in outputs.items():
        (output/name).write_bytes(content);receipt.append('output %s %s'%(hashlib.sha256(content).hexdigest(),name))
    (output/'course_preview.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    return {k:v for k,v in report.items() if k in ('bytes','vertices','quads','cameras','max_vertices','max_quads')}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'generated/course_menu_preview')
    parser.add_argument('--config',type=Path,help='Private JSON with rom key; contents never logged')
    args=parser.parse_args()
    path=json.loads(args.config.read_text())['rom'] if args.config else os.environ.get('FZX_TEST_ROM')
    if not path: parser.error('Set FZX_TEST_ROM privately or pass --config')
    # A Windows config can be consumed from WSL without printing its path.
    if os.name!='nt' and re_windows_path(path):
        path='/mnt/'+path[0].lower()+'/'+path[3:].replace('\\','/')
    try: print(json.dumps(export(Path(path).read_bytes(),args.output),sort_keys=True))
    except Exception as exc:
        print('Preview export failed: '+(str(exc) if isinstance(exc,ArtError) else type(exc).__name__),file=sys.stderr)
        return 1
    return 0


def re_windows_path(path):
    return len(path)>2 and path[1]==':' and path[2] in '/\\'


def linux_path(path):
    path=str(path)
    if re_windows_path(path): return '/mnt/'+path[0].lower()+'/'+path[3:].replace('\\','/')
    return path


if __name__=='__main__': raise SystemExit(main())
