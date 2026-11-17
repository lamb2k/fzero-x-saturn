#!/usr/bin/env python3
"""Prebake original floor fog and cloud coverage into native Saturn tile banks."""
from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess
from import_source_art import ROOT,ArtError,output_location
from skyline_assets import BAYER

def atlas(floor,cloud,fog,*,cloud_only=False):
    if len(floor)!=4096 or len(cloud)!=2048 or len(fog)!=3:raise ArtError('Expected original 64x32 floor/cloud')
    words=struct.unpack('>2048H',floor)
    if any(not c&0x8000 for c in words):raise ArtError('Floor must be opaque')
    fog5=[(c*31+127)//255 for c in fog]
    mean=[sum((c>>(5*j))&31 for c in words)//len(words) for j in range(3)]
    data=bytearray()
    for layer in range(2):
        for variant in range(8):
            shade=variant*255//7;pixels=[]
            for i in range(2048):
                if layer==0:
                    value=0x8000|sum(((((words[i]>>(5*j))&31)*(255-shade)+fog5[j]*shade+127)//255)<<(5*j) for j in range(3))
                else:
                    alpha=(cloud[i]&15)*17*(255-shade)
                    covered=alpha*32>(2*BAYER[(i//64)&3][i&3]+1)*255*255
                    intensity=((cloud[i]>>4)*31+7)//15
                    value=(0x8000|intensity|(intensity<<5)|(intensity<<10)) if covered else 0
                pixels.append(value)
            for level in range(4):
                width,height=64>>level,32>>level
                for ty in range(1<<level):
                    for tx in range(1<<level):
                        for y in range(height):
                            start=(ty*height+y)*64+tx*width
                            data+=struct.pack('>%dH'%width,*pixels[start:start+width])
    if len(data)!=262144:raise ArtError('Native plane atlas extent')
    # Preserve the established atlas; compact mode retains its exact cloud half.
    return bytes(data[131072:] if cloud_only else data),fog5,mean

def generate(source,out,cloud_only=False):
    output_location(out)
    subprocess.run(['bash',str(ROOT/'tools/check_receipt.sh'),str(source/'backdrop.receipt')],check=True)
    manifest=json.loads((source/'backdrop.json').read_text(encoding='utf-8'))
    assets={r['symbol']:r for r in manifest['assets']};bindings=manifest['bindings']
    floor=assets[bindings['floor']['texture']];cloud=assets[bindings['cloud']['texture']]
    if (floor['format'],cloud['format'],floor['width'],floor['height'],cloud['width'],cloud['height'])!=('RGBA16','IA8',64,32,64,32):
        raise ArtError('Unexpected source plane bindings')
    fp=source/floor['native_file'];cp=source/cloud['raw_file']
    if hashlib.sha256(fp.read_bytes()).hexdigest()!=floor['native_sha256'] or hashlib.sha256(cp.read_bytes()).hexdigest()!=cloud['source_sha256']:
        raise ArtError('Source plane identity mismatch')
    data,fog,mean=atlas(fp.read_bytes(),cp.read_bytes(),bindings['sky']['course_fog'],cloud_only=cloud_only)
    checksum=2166136261
    for byte in data:checksum=((checksum^byte)*16777619)&0xffffffff
    header=('/* Generated original floor/cloud bank; regenerate, never edit. */\n'
            '#define FZX_PLANE_FIXTURE 0\n#define FZX_PLANE_EXPECTED_BYTES 262144u\n'
            '#define FZX_PLANE_HASH 0x%08xu\n'
            'static const uint16_t fzx_plane_fog[3]={%s};\n'
            'static const uint16_t fzx_plane_mean[3]={%s};\n')%(checksum,','.join(map(str,fog)),','.join(map(str,mean)))
    if cloud_only:
        header=header.replace('#define FZX_PLANE_EXPECTED_BYTES 262144u',
            '#define FZX_PLANE_EXPECTED_BYTES 131072u\n#define FZX_PLANE_CLOUD_ONLY_PACK 1')
    out.mkdir(parents=True,exist_ok=True);(out/'PLANES.PAK').write_bytes(data);(out/'planes_identity.h').write_bytes(header.encode())
    description={'source_textures':[floor['symbol'],cloud['symbol']],'bytes':len(data),'variants':8,'subdivision_levels':4,
        'adaptations':['native affine tile interpolation','eight fog levels with mean-color Gouraud correction','eight cloud fade levels with 4x4 ordered coverage'],
        'native_sha256':hashlib.sha256(data).hexdigest()}
    if cloud_only:
        description.update(cloud_only=True,combined_source_offset=131072,
            adaptations=['original cloud half retained byte-for-byte; floor rendered by RBG0',
                         'eight cloud fade levels with 4x4 ordered coverage'])
    (out/'planes.json').write_bytes((json.dumps(description,indent=2)+'\n').encode())
    dependencies=[Path(__file__),ROOT/'tools/skyline_assets.py',ROOT/'tools/import_source_art.py',source/'backdrop.receipt',source/'backdrop.json',fp,cp]
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    receipt=['# fzx generation receipt v1; regenerate, never edit']
    receipt+=['generator %s %s'%(digest(p),os.path.relpath(p,ROOT/'tools')) for p in dependencies]
    receipt+=['output %s %s'%(digest(out/name),name) for name in ('PLANES.PAK','planes_identity.h','planes.json')]
    (out/'planes.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    print('original %s atlas: %d native bytes; no runtime pixel conversion'%
          ('cloud' if cloud_only else 'floor/cloud',len(data)))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=ROOT/'generated/backdrop')
    parser.add_argument('--output',type=Path)
    parser.add_argument('--cloud-only',action='store_true',help='retain only the original cloud half for an RBG0-floor build')
    args=parser.parse_args()
    generate(args.source,args.output or ROOT/('generated/planes_cloud' if args.cloud_only else 'generated/planes'),args.cloud_only)
