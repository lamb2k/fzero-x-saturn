#!/usr/bin/env python3
"""Engine 3D vehicle packs, one per stock machine: MACH3Dnn.PAK (nn = two-digit family).

Each pack is laid out as MACHAB.PAK: the prepared vehicle metadata (tools/machine_rebake.py
build, format FZXMCH02, LODs 1/4/5, eight centroid orders) then its texels. The models are
the family's own race models (Machine_Draw<Stem>Lod1..5) and texture loads
(Machine_DrawLoad<Stem>Textures) from the pinned decomp, read through
tools/import_source_art.py. Family 0's pack is byte for byte MACHRLD.PAK + MACHRTX.PAK.

Writes, under the git-ignored output (default generated/mach3d): MACH3Dnn.PAK per family,
mach3d_identity.h (a table indexed by family: pack bytes and FNV-1a 32, metadata and texel
bytes, palette and Gouraud bytes, and the body colour of the sprite sets' skin),
mach3d.json (per-family counts) and mach3d.receipt. Nothing is printed from the ROM.

    tools/mach3d_packs.py [--families 0,1,3] [--skin 1] [--output generated/mach3d] [--config ROM.json]
"""
import argparse
import hashlib
import json
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from import_source_art import ArtError, DECOMP, SourceArt, output_location, verify_source  # noqa: E402
from import_course import normalize_rom  # noqa: E402
from model_assets import source_probe_state  # noqa: E402
import machine_rebake  # noqa: E402
import racer_sprites  # noqa: E402

STOCK = 30
GUESTS = (33, 34)  # reserved guest identities: never baked here
GENERATORS = ('tools/mach3d_packs.py', 'tools/machine_rebake.py', 'tools/import_source_art.py', 'tools/model_assets.py',
              'tools/countdown_pack.py', 'tools/road_pack.py', 'tools/racer_sprites.py')


def fnv32(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def sha(data):
    return hashlib.sha256(data).hexdigest()


def family_pack(source, row, lut, probe_body, fog, skin):
    """(pack bytes, metadata bytes, texel bytes, report, body) for one family."""
    stem = row['source_family']
    bindings = source.texture_bindings(stem)
    models = [source.model(lod, stem) for lod in range(1, 6)]
    textures, raw = [], {}
    for symbol in dict.fromkeys(b['symbol'] for b in bindings if b['symbol']):
        t = source.texture(symbol)
        t.pop('rgba')
        raw[symbol] = t.pop('texels')
        textures.append(t)
    # The prebaked palette uses the probe's body for family 0 (MACHRLD.PAK as baked); the
    # engine re-tints body palettes at run time from the identity table's body colour.
    body = list(row['skins'][skin][:3])
    data, bank, report = machine_rebake.build(models, bindings, textures, raw, lut,
                                              probe_body if row['family'] == 0 else body, fog, quantize=True)
    return data + bank, data, bank, report, body


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--families', default=','.join(str(f) for f in range(STOCK)))
    ap.add_argument('--skin', type=int, default=racer_sprites.DEFAULT_VIEWS['skin'])
    ap.add_argument('--output', type=Path, default=ROOT / 'generated/mach3d')
    ap.add_argument('--config', type=Path, help='JSON with {"rom": path}; else FZX_TEST_ROM')
    args = ap.parse_args()
    families = [int(f) for f in args.families.split(',') if f.strip()]
    if any(f in GUESTS or not 0 <= f < STOCK for f in families):
        ap.error('stock families are 0..29 (guests 33/34 are not baked here)')
    rom_path = json.loads(args.config.read_text())['rom'] if args.config else os.environ.get('FZX_TEST_ROM')
    if not rom_path:
        ap.error('Set FZX_TEST_ROM privately or pass --config')
    if os.name != 'nt' and len(rom_path) > 2 and rom_path[1] == ':' and rom_path[2] in ('/', '\\'):
        rom_path = '/mnt/' + rom_path[0].lower() + '/' + rom_path[3:].replace('\\', '/')
    out = args.output.resolve()
    output_location(out)
    verify_source(DECOMP)
    rom = Path(rom_path).read_bytes()
    normalized, _ = normalize_rom(rom)
    source = SourceArt(normalized, DECOMP)
    lut, probe_body, fog = source_probe_state((DECOMP / 'src/game/racer.c').read_text(),
                                              (DECOMP / 'src/overlays/ovl_i3/background.c').read_text())
    rows = racer_sprites.source_families()
    out.mkdir(parents=True, exist_ok=True)
    table, outputs, failed = {}, {}, {}
    for f in families:
        row = rows[f]
        try:
            pak, data, bank, report, body = family_pack(source, row, lut, probe_body, fog, args.skin)
        except ArtError as exc:
            failed[f] = str(exc)
            continue
        name = 'MACH3D%02d.PAK' % f
        outputs[name] = pak
        lod1 = report['lods'][0]
        table[f] = dict(family=f, source_family=row['source_family'], pack=name, pack_bytes=len(pak), pack_fnv=fnv32(pak),
                        metadata_bytes=len(data), texel_bytes=len(bank), metadata_fnv=fnv32(data),
                        palette_bytes=report['palette_vram_bytes'], gouraud_bytes=report['gouraud_bytes'],
                        palettes=report['palette_groups'], lods=report['lods'], body=body,
                        lod1_faces=lod1['faces'], lod1_positions=lod1['positions'], lod1_normals=lod1['normals'])
    lines = ['/* Generated by tools/mach3d_packs.py; regenerate, never edit. */',
             '#ifndef FZX_MACH3D_IDENTITY_H', '#define FZX_MACH3D_IDENTITY_H', '#include <stdint.h>',
             '/* Per stock family (index = family): pack bytes 0 = no pack. */',
             'typedef struct { uint32_t pack_bytes, pack_fnv, metadata_bytes, texel_bytes, metadata_fnv;',
             '                 uint16_t palette_bytes, gouraud_bytes; uint8_t body[3], present; } fzx_mach3d_identity_t;',
             '#define FZX_MACH3D_FAMILIES %du' % STOCK,
             'static const fzx_mach3d_identity_t fzx_mach3d_identity[FZX_MACH3D_FAMILIES] = {']
    for f in range(STOCK):
        t = table.get(f)
        if t:
            lines.append('    [%d] = { %du, 0x%08Xu, %du, %du, 0x%08Xu, %du, %du, { %d, %d, %d }, 1 },' % (
                f, t['pack_bytes'], t['pack_fnv'], t['metadata_bytes'], t['texel_bytes'], t['metadata_fnv'],
                t['palette_bytes'], t['gouraud_bytes'], *t['body']))
    lines += ['};', '#endif', '']
    outputs['mach3d_identity.h'] = '\n'.join(lines).encode()
    outputs['mach3d.json'] = (json.dumps(dict(skin=args.skin, families=table, failed=failed), indent=1) + '\n').encode()
    receipt = ['# fzx generation receipt v1; regenerate, never edit']
    receipt += ['generator %s %s' % (sha((ROOT / g).read_bytes()), os.path.relpath(ROOT / g, ROOT / 'tools').replace('\\', '/'))
                for g in GENERATORS]
    receipt.append('input %s owner-rom' % sha(rom))
    receipt.append('input %s families-%s-skin-%d' % (sha(args.families.encode()), args.families.replace(',', '-'), args.skin))
    for name, content in outputs.items():
        (out / name).write_bytes(content)
        receipt.append('output %s %s' % (sha(content), name))
    (out / 'mach3d.receipt').write_bytes(('\n'.join(receipt) + '\n').encode())
    print(json.dumps({f: dict(faces=t['lod1_faces'], pack_bytes=t['pack_bytes'], texel_bytes=t['texel_bytes']) for f, t in table.items()}))
    if failed:
        print('failed: ' + json.dumps(failed))
    return 0 if not failed else 1


if __name__ == '__main__':
    raise SystemExit(main())
