#!/usr/bin/env python3
"""Bake the engine's countdown charts (generated/countdown_engine): the original quads, with the frame's charts at a reduced size.

tools/countdown_pack.py bakes every planar quad of the original sign into a resident chart at one source texel per chart step (70,400
bytes). The engine keeps its charts in VDP1 VRAM below the 3D player's region, where the worst course (Space Plant) leaves 66,048 bytes,
so the engine's pack scales the 20 frame quads' charts by FRAME_NUM / FRAME_DEN in both directions (widths stay multiples of 8, heights
even) and keeps the two mouth quads' and the digit quad's charts at the source's own step. The art is the original's: the same
triangle-exact UV baking, nearest texels, RGB5 channels and binary transparency, only fewer chart steps on the faces that are small on
screen. It is not a display-list interpreter or a runtime rasterizer. Inputs, receipts and the pack layout are tools/countdown_pack.py's.
Output: COUNTDN.PAK, COUNTDTX.PAK, countdown_identity.h, countdown_pack.json and countdown_pack.receipt in generated/countdown_engine.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess

import countdown_pack as cp
from import_source_art import ROOT, output_location
from road_pack import fnv

FRAME_NUM, FRAME_DEN = 1, 2
FRAME_TEXTURES = ('D_80189F8', 'D_801A6F8', 'aCountdownSignGratesTex')
_orig_chart_size = cp.chart_size
_orig_bake = cp.bake_chart
_state = {'frame': False}


def chart_size(vertices):
    w, h = _orig_chart_size(vertices)
    if not _state['frame']:
        return w, h
    w2 = max(8, (-(-w * FRAME_NUM // FRAME_DEN) + 7) // 8 * 8)
    h2 = max(2, (-(-h * FRAME_NUM // FRAME_DEN) + 1) // 2 * 2)
    return min(w, w2), min(h, h2)


def bake_chart(vertices, texture, pixels):
    _state['frame'] = texture['symbol'] in FRAME_TEXTURES
    try:
        return _orig_bake(vertices, texture, pixels)
    finally:
        _state['frame'] = False


cp.chart_size = chart_size
cp.bake_chart = bake_chart


def generate(source, output):
    output_location(output)
    # the source art is checked through the original pack's receipt, which holds the hash of every input file and is current
    subprocess.run(['sh', str(ROOT/'tools/check_receipt.sh'), str(ROOT/'generated/countdown_pack/countdown_pack.receipt')], check=True)
    manifest = json.loads((source/'countdown.json').read_text())
    inputs = [source/t['file'] for t in manifest['textures']]
    metadata, textures, report = cp.build(manifest, {p.name: p.read_bytes() for p in inputs})
    report['adaptations'].append('Frame charts at %d/%d of the source step (engine VRAM budget); mouth and digit charts at the source step' %
                                 (FRAME_NUM, FRAME_DEN))
    output.mkdir(parents=True, exist_ok=True)
    outputs = {'COUNTDN.PAK': metadata, 'COUNTDTX.PAK': textures,
               'countdown_pack.json': (json.dumps(report, indent=2)+'\n').encode()}
    header = '/* Generated engine countdown pack identity; regenerate. */\n'
    for name, value in (('BYTES', len(metadata)), ('TEXTURE_BYTES', len(textures)),
                        ('HASH', fnv(metadata)), ('TEXTURE_HASH', fnv(textures))):
        header += '#define FZX_COUNTDOWN_%s %du\n' % (name, value)
    outputs['countdown_identity.h'] = header.encode()
    dependencies = [Path(__file__), ROOT/'tools/countdown_pack.py', ROOT/'tools/import_source_art.py', ROOT/'tools/road_pack.py',
                    source/'countdown.receipt', source/'countdown.json', *inputs]
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    receipt = ['# fzx generation receipt v1; regenerate, never edit']
    receipt += ['generator %s %s' % (digest(p), os.path.relpath(p, ROOT/'tools').replace('\\', '/')) for p in dependencies]
    for name, value in outputs.items():
        (output/name).write_bytes(value)
        receipt.append('output %s %s' % (hashlib.sha256(value).hexdigest(), name))
    (output/'countdown_pack.receipt').write_bytes(('\n'.join(receipt)+'\n').encode())
    print(json.dumps({k: v for k, v in report.items() if k != 'images'}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT/'generated/countdown')
    parser.add_argument('--output', type=Path, default=ROOT/'generated/countdown_engine')
    args = parser.parse_args()
    generate(args.source, args.output)
