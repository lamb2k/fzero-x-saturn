#!/usr/bin/env python3
"""Export original single-player machine select/settings camera transforms.

MACHINECAM.BIN contains two big-endian 18-word Q16.16 packets: row-vector
view[4][4], projection XX/YY. The reference runs the original Camera_StartInit,
Camera_Init and Camera_Update; no camera math is duplicated in the converter.
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
from import_source_art import ROOT, DECOMP, ArtError, output_location, verify_source, mio0
from import_course import extract_rom_course, normalize_rom
from camera_assets import decode as decode_viewports, header as viewport_header
from course_menu_preview import linux_path, re_windows_path, fixed
from road_pack import fnv


def parse_capture(text):
    lines=text.splitlines()
    if len(lines)!=33 or lines[0]!='FZXMCRAW1':raise ArtError('Machine camera capture extent')
    captures=[];matrices=[None,None]
    for index,line in enumerate(lines[1:]):
        run=index//8;frame=index%8;scene=(0,1,1,0)[run]
        fields=line.split()
        if len(fields)!=67 or fields[:4]!=['R',str(run),str(scene),str(frame)]:
            raise ArtError('Machine camera capture order')
        try:raw=struct.pack('<63I',*(int(value,16)for value in fields[4:]))
        except (ValueError,struct.error)as exc:raise ArtError('Machine camera binary packet')from exc
        state=struct.unpack('<4i58fI',raw)
        if state[-1] or any(not math.isfinite(v)for v in state[4:-1]):raise ArtError('Machine camera source fault')
        floats=state[4:-1]
        view=list(floats[26:42]);projection=list(floats[42:58])
        matrix=view+[projection[0],projection[5]]
        if matrices[scene] is None:matrices[scene]=matrix
        elif struct.pack('<18f',*matrix)!=struct.pack('<18f',*matrices[scene]):
            raise ArtError('Machine camera is not fixed across frames and transitions')
        if tuple(floats[15:18])!=(60.0,16.0,8192.0):raise ArtError('Machine camera source lens changed')
        captures.append(dict(run=run,scene=scene,frame=frame,mode=state[0],setting=state[1],timer=state[2],
                             look_back=state[3],eye=list(floats[:3]),at=list(floats[3:6]),basis=list(floats[6:15]),
                             fov=floats[15],near=floats[16],far=floats[17],viewport=list(floats[18:22]),
                             scissor=list(floats[22:26]),view=view,projection=projection,raw_le_hex=raw.hex()))
    if matrices[0]==matrices[1]:raise ArtError('Machine camera modes unexpectedly coincide')
    return matrices,captures


def matrix_gate(matrices,data):
    if len(matrices)!=2 or len(data)!=144:raise ArtError('Machine camera packet dimensions')
    quantized=[list(struct.unpack_from('>18i',data,i*72))for i in range(2)]
    worst=0.0
    def project(matrix,point):
        view=[sum(point[k]*matrix[k*4+j]for k in range(3))+matrix[12+j]for j in range(3)]
        if view[2]>=-16:raise ArtError('Machine camera projection probe crosses near plane')
        return (160+160*matrix[16]*view[0]/-view[2],120-120*matrix[17]*view[1]/-view[2])
    for source,words in zip(matrices,quantized):
        target=[word/65536 for word in words]
        for x in (-100,0,100):
            for y in (-100,0,100):
                for z in (-100,0,100):
                    left=project(source,(x,y,z));right=project(target,(x,y,z))
                    worst=max(worst,*(abs(a-b)for a,b in zip(left,right)))
    if worst>0.01:raise ArtError('Machine camera matrix projection exceeds0.01px')
    return worst


def scoped_build(output,cc):
    build=output/('build-'+cc);build.mkdir(parents=True,exist_ok=True)
    source=(ROOT/'ref/build.sh').read_text()
    changes={
        'ROOT=$(cd "$(dirname "$0")/.." && pwd)':'ROOT='+shlex.quote(linux_path(ROOT)),
        'OUT=$ROOT/ref/build/${CC}${OPT}':'OUT='+shlex.quote(linux_path(build)),
        'OUT=$OUT-camera-$ART':'OUT=$OUT',
        'ASSETS=$ROOT/generated/camera_$ART':'ASSETS='+shlex.quote(linux_path(output)),
        'BRIDGES="bridge hooks"':'BRIDGES="bridge hooks machine_menu_camera"',
        'DRIVER=main':'DRIVER=machine_menu_camera',
        '-DFZX_SOURCE_CAMERA=$CAMERA -DFZX_ROAD_PLAN=$PLAN':'-DFZX_MACHINE_CAMERA_DRIVER=1 -DFZX_SOURCE_CAMERA=$CAMERA -DFZX_ROAD_PLAN=$PLAN',
    }
    for before,after in changes.items():
        if source.count(before)!=1:raise ArtError('Scoped reference build hook changed: '+before)
        source=source.replace(before,after)
    script=build/'build.sh';script.write_bytes(source.encode())
    overrides=dict(FZX_SOURCE_CAMERA='1',REF_CAMERA_ART='retail',FZX_ROAD_EXPORT='0',FZX_ROAD_PLAN='0',
                   FZX_BACKDROP_STATE='0',FZX_SOURCE_MACHINE_MENU='0',REF_STUB_CALLS='1')
    command=['bash',linux_path(script),cc,'-O2']
    if os.name=='nt':command=['wsl','--','env',*(k+'='+v for k,v in overrides.items()),*command]
    with (build/'build.log').open('w')as log:
        subprocess.run(command,cwd=ROOT,env=dict(os.environ,**overrides),check=True,stdout=log,stderr=subprocess.STDOUT)
    return build/'fzx-ref'


def receipt(output,name,outputs,deps,rom):
    rows=['# fzx generation receipt v1; regenerate, never edit']
    rows+=['generator %s %s'%(hashlib.sha256(p.read_bytes()).hexdigest(),os.path.relpath(p,ROOT/'tools').replace('\\','/'))for p in deps]
    rows+=['input %s owner-rom'%hashlib.sha256(rom).hexdigest()]
    for filename,data in outputs.items():
        (output/filename).write_bytes(data);rows.append('output %s %s'%(hashlib.sha256(data).hexdigest(),filename))
    (output/name).write_bytes(('\n'.join(rows)+'\n').encode())


def export(rom,output):
    output=output.resolve();output_location(output);verify_source(DECOMP)
    _,provenance=extract_rom_course(rom);normalized,_=normalize_rom(rom)
    output.mkdir(parents=True,exist_ok=True)
    yaml=DECOMP/'assets/yaml/us/rev0/course_track_gfx.yaml'
    viewports=viewport_header(decode_viewports(mio0(normalized[0x16C8A0:0x17B1E0]),yaml.read_text())).encode()
    deps=[Path(__file__),ROOT/'tools/camera_assets.py',ROOT/'tools/course_menu_preview.py',
          ROOT/'tools/import_source_art.py',ROOT/'tools/import_course.py',ROOT/'tools/road_pack.py',yaml]
    receipt(output,'viewports.receipt',{'viewports.h':viewports},deps,rom)
    raw=[];builds={};build_source=(ROOT/'ref/build.sh').read_bytes()
    for cc in ('gcc','clang'):
        executable=scoped_build(output,cc);calls=output/(cc+'-calls.txt')
        command=[linux_path(executable),linux_path(calls)]
        if os.name=='nt':command=['wsl','--',*command]
        capture=subprocess.check_output(command,cwd=ROOT)
        (output/(cc+'-capture.txt')).write_bytes(capture)
        if calls.read_text().strip():raise ArtError('Machine camera invoked a reference stub')
        raw.append(capture);builds[cc]=hashlib.sha256(executable.read_bytes()).hexdigest()
    if raw[0]!=raw[1]:raise ArtError('GCC/Clang camera source packets differ')
    if build_source!=(ROOT/'ref/build.sh').read_bytes():raise ArtError('Reference build changed during export; regenerate')
    matrices,captures=parse_capture(raw[0].decode())
    data=b''.join(struct.pack('>18i',*(fixed(v)for v in matrix))for matrix in matrices)
    error=matrix_gate(matrices,data)
    bad=bytearray(data);struct.pack_into('>i',bad,0,struct.unpack_from('>i',bad)[0]+16384)
    try:matrix_gate(matrices,bad)
    except ArtError:pass
    else:raise ArtError('Wrong matrix control survived')
    header='/* Generated source menu camera matrices; regenerate, never edit. */\n'
    header+='#define FZX_MACHINE_MENU_CAMERA_BYTES 144u\n#define FZX_MACHINE_MENU_CAMERA_HASH %du\n'%fnv(data)
    header+='static const int fzx_machine_menu_cameras[2][18]={\n'
    header+=',\n'.join('{'+','.join(str(fixed(v))for v in matrix)+'}'for matrix in matrices)+'\n};\n'
    report=dict(schema=1,tier='HOST',rom_sha256=provenance['normalized_rom_sha256'],camera_order=['machine_select','machine_settings'],
                build_sha256=builds,captures=captures,matrices_source=matrices,frames_per_run=8,runs=4,
                matrix_max_pixel_error=error,wrong_matrix_control='rejected',payload_fnv1a32=fnv(data),
                source_contract='Original single-player cameras; model basis and model viewports are separate')
    deps+=[ROOT/'ref/machine_menu_camera.c',ROOT/'ref/build.sh',ROOT/'ref/ref.h',ROOT/'ref/bridge.c',
           ROOT/'ref/hooks.c',ROOT/'ref/camera_bridge.c',ROOT/'ref/gen_stubs.py',ROOT/'ref/overrides.h']
    deps+=list((DECOMP/'src/game').glob('*.c'))
    deps+=[DECOMP/'src/sys/math.c',DECOMP/'src/sys/controller.c',DECOMP/'src/overlays/ovl_i2/course_context.c',
           DECOMP/'src/overlays/ovl_i3/cpu.c',DECOMP/'include/fzx_camera.h',DECOMP/'include/fzx_game.h']
    outputs={'MACHINECAM.BIN':data,'machine_menu_camera_identity.h':header.encode(),'viewports.h':viewports,
             'machine_menu_camera.json':(json.dumps(report,indent=2)+'\n').encode(),'camera-capture.txt':raw[0],
             'camera-source.bin':b''.join(bytes.fromhex(c['raw_le_hex'])for c in captures)}
    receipt(output,'machine_menu_camera.receipt',outputs,deps,rom)
    return dict(cameras=2,packets=len(captures),bytes=len(data),max_pixel_error=error)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'generated/machine_menu_camera')
    parser.add_argument('--config',type=Path,help='Private JSON with rom key; contents never logged')
    args=parser.parse_args()
    path=json.loads(args.config.read_text())['rom']if args.config else os.environ.get('FZX_TEST_ROM')
    if not path:parser.error('Set FZX_TEST_ROM privately or pass --config')
    if os.name!='nt'and re_windows_path(path):path='/mnt/'+path[0].lower()+'/'+path[3:].replace('\\','/')
    try:print(json.dumps(export(Path(path).read_bytes(),args.output)))
    except Exception as exc:
        print('Machine camera export failed: '+(str(exc)if isinstance(exc,ArtError)else type(exc).__name__),file=sys.stderr)
        return 1
    return 0


if __name__=='__main__':raise SystemExit(main())
