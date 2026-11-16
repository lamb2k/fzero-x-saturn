#!/usr/bin/env python3
"""Package the existing original HUD atlas for boot-time Saturn CD loading."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
from import_source_art import ROOT,ArtError,output_location
import hud_assets

def payload(sheets):
    # Reuse the original atlas layout and its dimension/padding validation.
    embedded=hud_assets.header(sheets)
    metadata,separator,_=embedded.partition(b'static const uint16_t fzx_hud_pixels[] = {')
    if not separator:raise ArtError('HUD header boundary changed')
    pixels=b''.join(sheets[name] for name,_,_ in hud_assets.HUD_SHEETS)
    digest=2166136261
    for byte in pixels:digest=((digest^byte)*16777619)&0xffffffff
    metadata+=('#define FZX_HUD_PACK_BYTES %du\n#define FZX_HUD_PACK_HASH 0x%08xu\n'%
               (len(pixels),digest)).encode()
    return pixels,metadata,embedded

def generate(source,out):
    output_location(out)
    subprocess.run(['bash',str(ROOT/'tools/check_receipt.sh'),str(source/'source_art.receipt')],check=True)
    manifest=json.loads((source/'source_art.json').read_text(encoding='utf-8'))
    assets={r['symbol']:r for r in manifest['textures']}
    dependencies=[Path(__file__),ROOT/'tools/hud_assets.py',ROOT/'tools/import_source_art.py',
                  source/'source_art.receipt',source/'source_art.json',source/'hud.h']
    sheets={}
    for name,width,height in hud_assets.HUD_SHEETS:
        row=assets[name]
        if (row['width'],row['height'],row['native_stride'])!=(width,height,(width+7)&~7):
            raise ArtError('Unexpected source HUD sheet layout')
        file=source/row['native_file'];sheets[name]=file.read_bytes();dependencies.append(file)
    pixels,metadata,embedded=payload(sheets)
    if embedded!=(source/'hud.h').read_bytes():raise ArtError('HUD pack differs from embedded atlas')
    out.mkdir(parents=True,exist_ok=True)
    (out/'HUD.PAK').write_bytes(pixels)
    (out/'hud_identity.h').write_bytes(metadata)
    description={'textures':len(sheets),'bytes':len(pixels),'adaptation':'none; identical existing native HUD atlas',
                 'native_sha256':hashlib.sha256(pixels).hexdigest()}
    (out/'hud_cd.json').write_bytes((json.dumps(description,indent=2)+'\n').encode())
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    receipt=['# fzx generation receipt v1; regenerate, never edit']
    receipt+=['generator %s %s'%(digest(p),os.path.relpath(p,ROOT/'tools')) for p in dependencies]
    receipt+=['output %s %s'%(digest(out/name),name) for name in ('HUD.PAK','hud_identity.h','hud_cd.json')]
    (out/'hud_cd.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    print('native HUD CD pack: %d original sheets, %d identical atlas bytes'%(len(sheets),len(pixels)))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=ROOT/'generated/source_art')
    parser.add_argument('--output',type=Path,default=ROOT/'generated/hud_cd')
    args=parser.parse_args();generate(args.source,args.output)
