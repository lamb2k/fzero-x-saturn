#!/usr/bin/env python3
"""Prebake the original floor into a Saturn rotation-plane character bank."""
from pathlib import Path
import argparse, hashlib, json, os, struct, subprocess
from import_source_art import ROOT, ArtError, output_location


def coefficient(index):
    if not 0 <= index < 32768:
        return 0x80000000
    denominator = 2 * index - 32767
    if abs(denominator) <= 32:
        return 0x80000000
    value = (268435456 + abs(denominator) // 2) // abs(denominator)
    return (-value if denominator < 0 else value) & 0xffffff


def build(floor):
    if len(floor) != 4096 or any(not word & 0x8000 for word in struct.unpack('>2048H', floor)):
        raise ArtError('Expected opaque original 64x32 RGB1555 floor')
    chars = bytearray()
    for cy in range(4):
        for cx in range(8):
            for y in range(8):
                offset = ((cy * 8 + y) * 64 + cx * 8) * 2
                chars += floor[offset:offset + 16]
    # Two-word names: no flip/palette attributes; character address in 32-byte units.
    names = b''.join(struct.pack('>HH', 0, ((y % 4) * 8 + x % 8) * 4)
                     for y in range(64) for x in range(64))
    coefficients = b''.join(struct.pack('>I', coefficient(i)) for i in range(32768))
    return bytes(chars), names, coefficients


def generate(source, out):
    output_location(out)
    subprocess.run(['bash', str(ROOT / 'tools/check_receipt.sh'), str(source / 'backdrop.receipt')], check=True)
    manifest = json.loads((source / 'backdrop.json').read_text(encoding='utf-8'))
    floor = next(a for a in manifest['assets'] if a['symbol'] == manifest['bindings']['floor']['texture'])
    if (floor['format'], floor['width'], floor['height']) != ('RGBA16', 64, 32):
        raise ArtError('Unexpected source floor binding')
    path = source / floor['native_file']
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    if digest(path) != floor['native_sha256']:
        raise ArtError('Original floor identity mismatch')
    files = dict(zip(('FLOORCHR.BIN', 'FLOORMAP.BIN', 'FLOORKOF.BIN'), build(path.read_bytes())))
    out.mkdir(parents=True, exist_ok=True)
    header = ['/* Generated original floor rotation banks; regenerate, never edit. */',
              '#define FZX_FLOOR_FIXTURE 0']
    for name, data in files.items():
        (out / name).write_bytes(data)
        checksum = 2166136261
        for byte in data:
            checksum = ((checksum ^ byte) * 16777619) & 0xffffffff
        key = name.split('.')[0]
        header += [f'#define FZX_{key}_BYTES {len(data)}u', f'#define FZX_{key}_HASH 0x{checksum:08x}u']
    fog = [(c * 31 + 127) // 255 for c in manifest['bindings']['sky']['course_fog']]
    header += ['#define FZX_FLOOR_FOG 0x%04xu' % (0x8000 | fog[0] | fog[1] << 5 | fog[2] << 10)]
    (out / 'floor_rbg_identity.h').write_bytes(('\n'.join(header) + '\n').encode())
    description = dict(source_texture=floor['symbol'], source_native_sha256=floor['native_sha256'],
                       banks={name: dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest()) for name, data in files.items()},
                       adaptations=['VDP2 reciprocal perspective with ten-bit parameter fractions',
                                    'finite source floor window', 'eight source fog bands via sprite/background color calculation'])
    (out / 'floor_rbg.json').write_bytes((json.dumps(description, indent=2) + '\n').encode())
    dependencies = [Path(__file__), ROOT / 'tools/import_source_art.py', ROOT / 'src/fzx_floor_rbg.c',
                    source / 'backdrop.receipt', source / 'backdrop.json', path]
    receipt = ['# fzx generation receipt v1; regenerate, never edit']
    receipt += ['generator %s %s' % (digest(p), os.path.relpath(p, ROOT / 'tools')) for p in dependencies]
    receipt += ['output %s %s' % (digest(out / name), name) for name in (*files, 'floor_rbg_identity.h', 'floor_rbg.json')]
    (out / 'floor_rbg.receipt').write_bytes(('\n'.join(receipt) + '\n').encode())
    print('original floor RBG: 4096 character + 16384 map + 131072 coefficient bytes')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'generated/backdrop')
    parser.add_argument('--output', type=Path, default=ROOT / 'generated/floor_rbg')
    args = parser.parse_args()
    generate(args.source, args.output)
