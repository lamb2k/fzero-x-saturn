#!/usr/bin/env python3
"""Prepare the original opaque sky samples and bounded source geometry helper."""
from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess
from import_source_art import ROOT,ArtError,output_location
import backdrop_state

def breakpoints(colors, tolerance=1):
    """Keep original endpoints; bound RGB5 error throughout each linear span."""
    result=[0]
    while result[-1]<63:
        start=result[-1];end=start+1
        for stop in range(start+2,64):
            width=stop-start
            if all(abs(((colors[i]>>shift)&31)*width-
                       (((colors[start]>>shift)&31)*(stop-i)+
                        ((colors[stop]>>shift)&31)*(i-start)))<=tolerance*width
                   for i in range(start+1,stop) for shift in (0,5,10)):
                end=stop
        result.append(end)
    return result

def header(data):
    if len(data)!=128:raise ArtError('Sky requires exactly 64 RGB1555 samples')
    colors=struct.unpack('>64H',data)
    if any(not c&0x8000 for c in colors):raise ArtError('Native sky requires opaque source samples')
    return ('/* Generated local source sky. Regenerate; never edit. */\n'
            '#define FZX_SKY_FIXTURE 0\n'
            'static const uint16_t fzx_sky_colors[64] = {\n'+
            ''.join('    '+','.join('0x%04x'%c for c in colors[i:i+8])+',\n' for i in range(0,64,8))+'};\n'+
            '/* Similar-quality adaptation: at most 1 RGB5 unit per channel in texture space. */\n'+
            'static const uint8_t fzx_sky_breaks[] = {'+','.join(map(str,breakpoints(colors)))+'};\n')

def generate(source,out):
    output_location(out)
    subprocess.run(['bash',str(ROOT/'tools/check_receipt.sh'),str(source/'backdrop.receipt')],check=True)
    manifest=json.loads((source/'backdrop.json').read_text())
    if manifest['bindings']['sky']['texture']!='aSkyboxPurpleTex':raise ArtError('Unexpected source sky binding')
    records=[r for r in manifest['assets'] if r['symbol']=='aSkyboxPurpleTex']
    if len(records)!=1:raise ArtError('Missing/ambiguous source sky')
    row=records[0]
    if (row['format'],row['width'],row['height'],row['native_stride'])!=('RGBA16',64,1,64):
        raise ArtError('Unexpected sky format or dimensions')
    if row['native_file']!='aSkyboxPurpleTex.rgb1555':raise ArtError('Unexpected native sky path')
    pixels=source/row['native_file'];data=pixels.read_bytes()
    if hashlib.sha256(data).hexdigest()!=row['native_sha256']:raise ArtError('Native sky hash mismatch')
    state=('#include "global.h"\n#include "fzx_camera.h"\n#include "background.h"\n'
           'extern Camera gCameras[];\nextern s32 gNumPlayers;\n')
    state+=backdrop_state.native(backdrop_state.SOURCE.read_text())
    out.mkdir(parents=True,exist_ok=True)
    (out/'sky_colors.h').write_bytes(header(data).encode())
    (out/'sky_state.c').write_bytes(state.encode())
    dependencies=[Path(__file__),ROOT/'tools/backdrop_state.py',ROOT/'tools/import_source_art.py',
                  backdrop_state.SOURCE,backdrop_state.SOURCE.with_suffix('.h'),ROOT/'ref/backdrop_state.h',
                  source/'backdrop.receipt',source/'backdrop.json',pixels]
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    receipt=['# fzx generation receipt v1; regenerate, never edit']
    receipt+=['generator %s %s'%(digest(p),os.path.relpath(p,ROOT/'tools')) for p in dependencies]
    receipt+=['output %s %s'%(digest(out/name),name) for name in ('sky_colors.h','sky_state.c')]
    (out/'sky.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    print('native sky: 64 original samples, 128 texture bytes; source backdrop state unchanged')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=ROOT/'generated/backdrop')
    parser.add_argument('--output',type=Path,default=ROOT/'generated/sky')
    args=parser.parse_args();generate(args.source,args.output)
