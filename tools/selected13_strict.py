#!/usr/bin/env python3
"""Strict selected13 comparison of a candidate image's capture against the recipe image's capture.

Usage: selected13_strict.py BASE_RUN CANDIDATE_RUN MODE OUT.json      MODE: exact | lod
Exit status 0 only for a PASS verdict; 1 for FAIL; 2 for unusable input.

Every hold of the 13 must exist on both sides with its metadata, every required buffer and a non-empty,
all-passing guard list with the same guard names. A buffer missing on either side fails.
  exact: road commands, clip command, metadata, images, textures, positions, remap and countdown buffers
         byte-identical; equal command counts; every road shade entry referenced by a command (CMDGRDA)
         equal. Unreferenced shade slots may differ.
  lod:   the same minus the road outputs (commands, shades, packets, projections, work), which change by
         design; every candidate command count must be <= the base count and <= 420, and every referenced
         shade entry must lie inside the captured shade table.
The verdict also requires three planted mutations of the candidate to be rejected (a road command, a
metadata byte, a failed hold check). Captures are EMULATOR (Ymir); hardware UNRUN.
"""
import json
import shutil
import struct
import sys
import tempfile
from pathlib import Path

HOLDS = [1, 300, 301, 500, 501, 542, 583, 624, 664, 665, 674, 684, 685]
BUFFERS = ['c3-commands', 'c3-clip-command', 'c3-metadata', 'c3-images', 'c3-textures', 'c3-positions', 'c3-remap',
           'c3-packets-capacity', 'c3-shades', 'countdown-commands', 'countdown-metadata', 'countdown-textures']
ROAD_OUTPUTS = {'c3-commands', 'c3-shades'}
BUDGET = 420
VDP1_VRAM = 0x25C00000


class Unusable(Exception):
    pass


def shade_base(meta):
    """VDP1 VRAM offset of the road shade table: report word 9 (fzx_road_stream_report)."""
    report = bytes.fromhex(meta['packets']['_fzx_road_stream_report'])
    address = struct.unpack('>I', report[36:40])[0]
    if not VDP1_VRAM <= address < VDP1_VRAM + 0x80000:
        raise Unusable('shade table address outside VDP1 VRAM: %#x' % address)
    return address - VDP1_VRAM


def referenced(commands, base, shades):
    """Shade entry offsets (8 bytes each) referenced by the commands' CMDGRDA words; -1 if out of range."""
    out = []
    for i in range(0, len(commands), 32):
        grda = struct.unpack('>H', commands[i + 28:i + 30])[0]
        if grda:
            offset = grda * 8 - base
            out.append(offset if 0 <= offset and offset + 8 <= len(shades) else -1)
    return out


def judge(base, cand, mode):
    rows, ok = [], True
    for h in HOLDS:
        reasons = []
        snaps = (base / 'snapshots', cand / 'snapshots')
        metas = []
        for s in snaps:
            p = s / f'hold-{h}.json'
            metas.append(json.loads(p.read_text()) if p.exists() else None)
        if None in metas:
            reasons.append('hold metadata missing')
        else:
            for side, m in zip(('base', 'candidate'), metas):
                if not m.get('checks_pass'):
                    reasons.append(side + ' hold checks fail')
                g = m.get('guards') or []
                if not g or not all(x.get('pass') for x in g):
                    reasons.append(side + ' guards missing or failing')
            if [x.get('name') for x in metas[0].get('guards') or []] != [x.get('name') for x in metas[1].get('guards') or []]:
                reasons.append('guard sets differ')
        data = {}
        for k in BUFFERS:
            a, b = snaps[0] / f'hold-{h}.{k}', snaps[1] / f'hold-{h}.{k}'
            if not a.exists() or not b.exists():
                reasons.append('%s missing (%s)' % (k, 'both' if not a.exists() and not b.exists() else 'base' if not a.exists() else 'candidate'))
                continue
            data[k] = (a.read_bytes(), b.read_bytes())
            if (mode == 'exact' or k not in ROAD_OUTPUTS) and k != 'c3-shades' and data[k][0] != data[k][1]:
                reasons.append(k + ' differs')
        counts = None
        if 'c3-commands' in data and 'c3-shades' in data and None not in metas:
            (ca, cb), (sa, sb) = data['c3-commands'], data['c3-shades']
            counts = (len(ca) // 32, len(cb) // 32)
            ra, rb = referenced(ca, shade_base(metas[0]), sa), referenced(cb, shade_base(metas[1]), sb)
            if -1 in ra or -1 in rb:
                reasons.append('a command references a shade outside the captured table')
            if mode == 'exact':
                if counts[0] != counts[1]:
                    reasons.append('command counts differ')
                if ra != rb or any(sa[o:o + 8] != sb[o:o + 8] for o in ra if o >= 0):
                    reasons.append('referenced shade entries differ')
            elif counts[1] > counts[0] or counts[1] > BUDGET:
                reasons.append('candidate commands over the base count or the budget')
        row_ok = not reasons
        ok &= row_ok
        rows.append(dict(hold=h, ok=row_ok, commands=counts, reasons=reasons))
    return ok, rows


def plant(cand, tmp, name):
    c = Path(tmp) / name
    shutil.copytree(cand, c)
    s = c / 'snapshots'
    if name == 'command':
        p = s / 'hold-542.c3-commands'
        b = bytearray(p.read_bytes()); b[len(b) // 2] ^= 1; p.write_bytes(bytes(b))
    elif name == 'metadata':
        p = s / 'hold-542.c3-metadata'
        b = bytearray(p.read_bytes()); b[len(b) // 2] ^= 1; p.write_bytes(bytes(b))
    else:
        p = s / 'hold-542.json'
        m = json.loads(p.read_text()); m['checks_pass'] = False; p.write_text(json.dumps(m))
    return c


def main(argv):
    if len(argv) != 5 or argv[3] not in ('exact', 'lod'):
        print(__doc__); return 2
    base, cand, mode, out = Path(argv[1]), Path(argv[2]), argv[3], Path(argv[4])
    try:
        for d in (base, cand):
            r = json.loads((d / 'receipt.json').read_text())
            if r.get('holds') != HOLDS:
                raise Unusable('%s does not hold the 13 selected inputs' % d)
        ok, rows = judge(base, cand, mode)
        controls = []
        with tempfile.TemporaryDirectory() as tmp:
            for name in ('command', 'metadata', 'check'):
                c = plant(cand, tmp, name)
                if mode == 'lod' and name == 'command':
                    p = c / 'snapshots' / 'hold-542.c3-commands'
                    p.write_bytes(p.read_bytes() + bytes(32 * 500))
                controls.append(dict(control=name, rejected=not judge(base, c, mode)[0]))
    except (Unusable, OSError, ValueError, KeyError, IndexError) as e:
        print('selected13_strict: unusable input: %s' % e); return 2
    verdict = 'PASS' if ok and all(c['rejected'] for c in controls) else 'FAIL'
    res = dict(tier='EMULATOR', hardware='UNRUN', mode=mode, base=str(base), candidate=str(cand),
               verdict=verdict, holds=rows, controls=controls)
    out.write_text(json.dumps(res, indent=1) + '\n')
    print(json.dumps(dict(verdict=verdict, holds_ok=sum(r['ok'] for r in rows), controls=controls,
                          failing=[(r['hold'], r['reasons']) for r in rows if not r['ok']][:4])))
    return 0 if verdict == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
