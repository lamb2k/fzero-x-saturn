#!/usr/bin/env python3
"""Offline original-floor conversion to a bounded-error VDP2 256-colour bitmap.

Index zero is opaque art. Native use MUST disable RBG0 transparency in BGON.
This prepares C5 assets; it does not establish native display or cost acceptance.
"""
from collections import Counter
from pathlib import Path
import argparse
import hashlib
import json
import os
import struct

from import_source_art import ROOT, ArtError, output_location


def rgb(word):
    return tuple((word >> shift) & 31 for shift in (0, 5, 10))


def bounded_palette(pixels):
    """Merge existing representatives; every original channel stays within one.

    Choose the smallest increase in weighted squared RGB5 error, then changed
    pixel count and numeric representative IDs. Compare all original members
    on every merge so chained merges cannot accumulate error. No new art colours
    are invented, and inputs that this bounded greedy method cannot fit fail.
    """
    counts = Counter(pixels)
    groups = {word: {word} for word in sorted(counts)}
    channels = {word: rgb(word) for word in counts}

    def cost(members, representative):
        differences = [tuple(abs(a-b) for a, b in zip(channels[word], channels[representative]))
                       for word in members]
        if any(max(d) > 1 for d in differences):
            return None
        error = sum(counts[word] * sum(c*c for c in d) for word, d in zip(members, differences))
        changed = sum(counts[word] for word in members if word != representative)
        return error, changed

    while len(groups) > 256:
        best = None
        for source in sorted(groups):
            before_source = cost(groups[source], source)
            r, g, b = channels[source]
            # A surviving representative is itself a member, hence must be nearby.
            neighbours = (0x8000 | rr | gg << 5 | bb << 10
                          for rr in range(max(0, r-1), min(31, r+1)+1)
                          for gg in range(max(0, g-1), min(31, g+1)+1)
                          for bb in range(max(0, b-1), min(31, b+1)+1))
            for target in neighbours:
                if source == target or target not in groups:
                    continue
                after = cost(groups[source], target)
                if after is None:
                    continue
                candidate = (after[0]-before_source[0], after[1]-before_source[1], source, target)
                if best is None or candidate < best:
                    best = candidate
        if best is None:
            raise ArtError('Cannot reduce floor to 256 representatives within one RGB5 step/channel')
        _, _, source, target = best
        groups[target].update(groups.pop(source))
    palette = sorted(groups)
    remap = {original: index for index, representative in enumerate(palette)
             for original in groups[representative]}
    return palette, remap


def validate_reconstruction(floor, bitmap, palette):
    """Independently check every repeated pixel, opacity, and RGB5 error bound."""
    if len(floor) != 4096 or len(bitmap) != 512*256 or len(palette) != 512:
        raise ArtError('Invalid floor, bitmap or palette extent')
    source = struct.unpack('>2048H', floor)
    colours = struct.unpack('>256H', palette)
    if any(not word & 0x8000 for word in (*source, *colours)):
        raise ArtError('Floor and palette must be opaque RGB1555')
    maximum = [0, 0, 0]
    changed = error = 0
    for y in range(256):
        for x in range(512):
            old = source[(y % 32)*64+x % 64]
            new = colours[bitmap[y*512+x]]
            delta = tuple(abs(a-b) for a, b in zip(rgb(old), rgb(new)))
            if max(delta) > 1:
                raise ArtError('Floor reconstruction exceeds one RGB5 step/channel')
            maximum = [max(a, b) for a, b in zip(maximum, delta)]
            if x < 64 and y < 32:
                changed += old != new
                error += sum(d*d for d in delta)
    return dict(source_colours=len(set(source)), used_colours=len(set(bitmap)),
                changed_source_pixels=changed, source_pixels=2048,
                max_rgb5_channel_error=maximum, source_squared_rgb5_error=error)


def build(floor):
    if len(floor) != 4096:
        raise ArtError('Expected original 64x32 RGB1555 floor')
    pixels = struct.unpack('>2048H', floor)
    if any(not word & 0x8000 for word in pixels):
        raise ArtError('Expected opaque original floor')
    colours, remap = bounded_palette(pixels)
    tile = bytes(remap[word] for word in pixels)
    bitmap = b''.join(tile[y*64:(y+1)*64]*8 for y in range(32))*8
    palette = struct.pack('>256H', *(colours+[colours[0]]*(256-len(colours))))
    return bitmap, palette, validate_reconstruction(floor, bitmap, palette)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_receipt(receipt):
    """Portable equivalent of check_receipt.sh, also returning covered outputs."""
    lines = receipt.read_text(encoding='utf-8').splitlines()
    if not lines or not lines[0].startswith('# fzx generation receipt v1'):
        raise ArtError('Unknown generation receipt format')
    generators, outputs = 0, set()
    for line in lines[1:]:
        if not line.strip() or line.startswith('#'):
            continue
        parts = line.split(maxsplit=2)
        if len(parts) != 3:
            raise ArtError('Malformed generation receipt')
        kind, expected, name = parts
        if kind == 'input':
            continue
        if kind not in ('generator', 'output'):
            raise ArtError('Unknown generation receipt entry')
        path = (ROOT/'tools' if kind == 'generator' else receipt.parent)/name
        if not path.is_file() or digest(path) != expected:
            raise ArtError('Stale generation receipt: '+name)
        if kind == 'generator':
            generators += 1
        else:
            outputs.add(path.resolve())
    if not generators or not outputs:
        raise ArtError('Incomplete generation receipt')
    return outputs


def generate(source, out):
    output_location(out)
    receipt = source/'backdrop.receipt'
    covered = check_receipt(receipt)
    manifest_path = source/'backdrop.json'
    if manifest_path.resolve() not in covered:
        raise ArtError('Backdrop manifest is not receipt-bound')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    assets = [a for a in manifest['assets'] if a['symbol'] == manifest['bindings']['floor']['texture']]
    if len(assets) != 1:
        raise ArtError('Expected exactly one source floor binding')
    floor = assets[0]
    if (floor['format'], floor['width'], floor['height']) != ('RGBA16', 64, 32):
        raise ArtError('Unexpected source floor binding')
    path = source/floor['native_file']
    if path.resolve() not in covered or digest(path) != floor['native_sha256']:
        raise ArtError('Original floor identity mismatch')
    bitmap, palette, metrics = build(path.read_bytes())
    files = {'FLOORIDX.BIN': bitmap, 'FLOORPAL.BIN': palette}
    header = ['/* Generated original floor indexed bitmap; regenerate, never edit. */',
              '#define FZX_FLOOR_INDEXED_FIXTURE 0',
              '#define FZX_FLOOR_INDEXED_WIDTH 512u', '#define FZX_FLOOR_INDEXED_HEIGHT 256u',
              '/* Index 0 contains opaque art: disable RBG0 transparency in BGON. */',
              '#define FZX_FLOOR_INDEXED_OPAQUE_ZERO 1']
    for name, data in files.items():
        checksum = 2166136261
        for byte in data:
            checksum = ((checksum ^ byte)*16777619) & 0xffffffff
        key = name.split('.')[0]
        header += [f'#define FZX_{key}_BYTES {len(data)}u', f'#define FZX_{key}_HASH 0x{checksum:08x}u']
    description = dict(schema=1, source_texture=floor['symbol'],
                       source_offset=floor.get('source_offset'),
                       source_rom_sha256=manifest.get('rom_sha256'),
                       source_course_sha256=manifest.get('course_sha256'),
                       source_commit=manifest.get('source_commit'),
                       decomp_commit=manifest.get('decomp_commit'),
                       source_native_sha256=floor['native_sha256'],
                       source_manifest_sha256=digest(manifest_path),
                       source_receipt_sha256=digest(receipt),
                       width=512, height=256, palette_format='big-endian RGB1555',
                       palette_entries=256, index_zero='opaque; disable RBG0 transparency in BGON',
                       metrics=metrics,
                       banks={name: dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
                              for name, data in files.items()},
                       adaptations=['Deterministic representative merging, maximum one RGB5 step per channel',
                                    'Original 64x32 floor repeated 8x8 without resizing or filtering'],
                       acceptance='Offline preparation only; native display, motion and cost unrun')
    files['floor_indexed_identity.h'] = ('\n'.join(header)+'\n').encode()
    files['floor_indexed.json'] = (json.dumps(description, indent=2)+'\n').encode()
    out.mkdir(parents=True, exist_ok=True)
    for name, data in files.items():
        (out/name).write_bytes(data)
    dependencies = [Path(__file__), ROOT/'tools/import_source_art.py', receipt, manifest_path, path]
    lines = ['# fzx generation receipt v1; regenerate, never edit']
    lines += ['generator %s %s' % (digest(p), os.path.relpath(p, ROOT/'tools').replace('\\', '/'))
              for p in dependencies]
    lines += ['output %s %s' % (digest(out/name), name) for name in files]
    (out/'floor_indexed.receipt').write_bytes(('\n'.join(lines)+'\n').encode())
    return description


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT/'generated/backdrop')
    parser.add_argument('--output', type=Path, default=ROOT/'generated/floor_indexed')
    args = parser.parse_args()
    print(json.dumps(generate(args.source, args.output)['metrics'], sort_keys=True))
