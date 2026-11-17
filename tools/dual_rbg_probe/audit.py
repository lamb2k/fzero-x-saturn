#!/usr/bin/env python3
"""Generate source-only oracle/receipts; optionally audit captured VRAM/registers.

The PPM is a coordinate-model reference, not emulator/hardware evidence. Do not
use its exact pixels as a hardware pass tolerance (VDP2 rounding needs review).
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent
Q = 65536
MODES = ['dual', 'missing-rbg1', 'wrong-parameter-b', 'constant-both',
         'unrolled', 'constant-b', 'constant-a']
COLORS = [(0, 0, 0)] * 32
for i, rgb in {1:(2,8,20), 2:(2,16,26), 3:(8,28,31),
               17:(31,10,1), 18:(31,24,3), 19:(31,2,17)}.items():
    COLORS[i] = rgb

def fnv(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xffffffff
    return f'{h:08x}'

def floor(x, y):
    return 3 if x % 32 == 0 or y % 32 == 0 else 1 if ((x//32) ^ (y//32)) & 1 else 2

def cloud(t, x, y):
    if t == 0:
        return 0
    if t == 1:
        return 1 if x == 3 or y == 3 else 0
    if t == 2:
        return 0 if (x ^ y) & 1 else 2
    return 3 if x == y or x+y == 7 else 0

def tile(x, y):
    return 0 if (x//4+y//4) & 1 else 1 + (x//8+y//8) % 3

def params(mode):
    s, c = (0, Q) if mode == 4 else (16384, 63488)
    a = [0] * 24
    a[:7] = [-160*c+112*s, -160*s-112*c, 96*Q, -s, c, c, s]
    a[7], a[12] = Q, Q
    a[17:24] = [256*Q,64*Q,Q,Q,0x81000000+a[1],c,s]
    b = a.copy()
    b[7], b[2], b[17], b[18] = 49152,64*Q,192*Q,96*Q
    if mode == 2:
        b = a.copy()
    return a, b

def assets(mode):
    bitmap = bytes((floor(x,y)<<4) | floor(x+1,y)
                   for y in range(256) for x in range(0,512,2))
    coeff = b''.join(struct.pack('>I',256*Q//(i+128)) for i in range(32768))
    chars = bytes((cloud(t,x,y)<<4) | cloud(t,x+1,y)
                  for t in range(4) for y in range(8) for x in range(0,8,2))
    pmap = b''.join(struct.pack('>I',0x12000+tile(x,y))
                    for y in range(64) for x in range(64))
    rpt = b''.join(struct.pack('>24I',*[v & 0xffffffff for v in p])+bytes(32)
                   for p in params(mode))
    pal = b''.join(struct.pack('>H',0 if rgb == (0,0,0) else
                0x8000 | rgb[0] | rgb[1]<<5 | rgb[2]<<10) for rgb in COLORS)
    return [('bitmap',0x25e00000,bitmap), ('coefficient',0x25e20000,coeff),
            ('characters',0x25e40000,chars), ('map',0x25e60000,pmap),
            ('rpt',0x25e64000,rpt), ('cram',0x25f00000,pal)]

def coords(p, x, y, coeff):
    sx = p[0] + p[3]*y + p[5]*x
    idx = ((p[21] + p[22]*y + p[23]*x) >> 16) - 32768
    assert 0 <= idx < 32768
    k = 256*Q//(idx+128) if coeff else Q
    u = ((sx*p[7]//Q)*k//Q + p[17])//Q
    v = (p[2]*k//Q + p[18])//Q
    return u,v,idx

def picture(mode):
    a,b = params(mode)
    result = bytearray()
    counts = {'rbg0':0,'rbg1':0}
    bounds = [32768,0]
    for y in range(224):
        for x in range(320):
            u,v,k = coords(a,x,y,mode not in (3,6))
            bounds = [min(bounds[0],k),max(bounds[1],k)]
            color = floor(u%512,v%256)
            if mode != 1:
                u,v,_ = coords(b,x,y,mode not in (3,5))
                u,v=u%512,v%512
                c=cloud(tile(u//8,v//8),u%8,v%8)
                if c:
                    color=16+c
            counts['rbg1' if color>16 else 'rbg0'] += 1
            result.extend(ch*8 for ch in COLORS[color])
    return result,counts,bounds

def register_expectations(mode):
    # Only stable configured registers. Unspecified cycle slots are intentionally
    # excluded: rotation reads use RAMCTL ownership, not normal cycle patterns.
    r = {0x00:0x8000,0x0e:0x1307,0x20:0x10 if mode==1 else 0x30,
         0x28:0,0x2a:0x200,0x2e:0,0x30:0,0x38:0,0x3a:0,0x3e:0,
         0xb0:0,0xb2:0,0xb4:0 if mode==3 else 1 if mode==5 else 0x100 if mode==6 else 0x101,
         0xb6:0,0xbc:3,0xbe:0x2000,0xd0:0,0xd2:0,0xd4:0,0xd6:0,
         0xe4:0,0xe6:0,0xe8:0,0xea:0,0xec:0,0xee:0,0xf0:0,
         0xf2:0,0xf4:0,0xf6:0,0xf8:5,0xfa:0,0xfc:3,0x110:0}
    r.update({offset:0x1818 for offset in range(0x60,0x70,2)})
    return r

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,default=ROOT/'out')
    p.add_argument('--mode',type=int,choices=range(7),default=0)
    p.add_argument('--vram',type=Path,help='raw 512 KiB VDP2 VRAM dump')
    p.add_argument('--cram',type=Path,help='raw CRAM dump, big endian words')
    p.add_argument('--registers',type=Path,help='raw >=288 bytes VDP2 registers, big endian')
    args=p.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    reference,_,_=picture(0)
    report={'tier':'HOST coordinate model; EMULATOR/HARDWARE UNRUN',
            'sources':{},'modes':[]}
    for name in ('probe.c','Makefile','audit.py','build-all.sh'):
        report['sources'][name]=hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
    for mode,name in enumerate(MODES):
        pixels,counts,bounds=picture(mode)
        different=sum(pixels[i:i+3] != reference[i:i+3] for i in range(0,len(pixels),3))
        assert different > 5000 if mode else counts['rbg0']>5000 and counts['rbg1']>5000
        data=assets(mode)
        rec={'mode':mode,'name':name,'visible_pixels':counts,
             'different_pixels_vs_dual':different,'coefficient_index_range':bounds,
             'memory':[{'name':n,'address':f'{a:08x}','bytes':len(b),'fnv1a':fnv(b),
                        'sha256':hashlib.sha256(b).hexdigest()} for n,a,b in data],
             'registers':{f'{a:03x}':f'{v:04x}' for a,v in register_expectations(mode).items()}}
        iso=ROOT/f'dual-rbg-{mode}.iso'
        if iso.exists():
            rec['iso_sha256']=hashlib.sha256(iso.read_bytes()).hexdigest()
        (args.out/f'mode-{mode}.ppm').write_bytes(b'P6\n320 224\n255\n'+pixels)
        report['modes'].append(rec)
    failures=[]
    if args.vram:
        dump=args.vram.read_bytes()
        if len(dump)!=524288:
            failures.append('VDP2 VRAM must be exactly 524288 bytes')
        for name,addr,data in assets(args.mode)[:-1]:
            offset=addr-0x25e00000
            if dump[offset:offset+len(data)]!=data:
                failures.append(f'VRAM {name} differs')
    if args.cram and args.cram.read_bytes()[:64]!=assets(args.mode)[-1][2]:
        failures.append('CRAM palette differs')
    if args.registers:
        dump=args.registers.read_bytes()
        if len(dump)<288:
            failures.append('VDP2 register dump must contain at least 288 bytes')
        for addr,want in register_expectations(args.mode).items():
            got=int.from_bytes(dump[addr:addr+2],'big')
            if got!=want:
                failures.append(f'register {addr:03x}: expected {want:04x}, got {got:04x}')
    report['capture_audit']={'requested':bool(args.vram or args.cram or args.registers),
                              'failures':failures}
    (args.out/'audit.json').write_text(json.dumps(report,indent=2)+'\n')
    (args.out/'audit.json.receipt').write_text(hashlib.sha256((args.out/'audit.json').read_bytes()).hexdigest()+'\n')
    for row in report['modes']:
        print(row['mode'],row['name'],row['visible_pixels'],row['different_pixels_vs_dual'])
    if failures:
        raise SystemExit('\n'.join(failures))

if __name__=='__main__':
    main()
