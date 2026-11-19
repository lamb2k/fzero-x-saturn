#!/usr/bin/env python3
"""Strict selected13 comparison of a candidate image's capture against the recipe image's capture.

Usage: selected13_strict.py BASE_RUN CANDIDATE_RUN MODE OUT.json      MODE: exact | lod
Exit status 0 only for a PASS verdict; 1 for FAIL; 2 for unusable input (a JSON result is written in every case).

Every hold of the 13 must exist on both sides with metadata naming that hold as its target, literal true
checks_pass, and a non-empty guard list of named guards each passing with literal true (same names on both
sides). Every required buffer must exist on both sides; command buffers must be whole 32-byte records.
  exact: road commands, clip command, metadata, images, textures, positions, remap and countdown buffers
         byte-identical; equal command counts; every road shade entry referenced by a command (CMDGRDA)
         equal. Unreferenced shade slots may differ.
  lod:   the same minus the road outputs (commands, shades, packets, projections, work), which change by
         design; every candidate command count must be <= the base count and <= 420, and every referenced
         shade entry must lie inside the captured shade table.
When the comparison passes, the verdict also requires three planted mutations of the candidate to be
rejected (a road command, a metadata byte, a failed hold check); a failing pair keeps its FAIL. Captures are EMULATOR (Ymir); hardware UNRUN.
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
    try:
        report = bytes.fromhex(meta['packets']['_fzx_road_stream_report'])
    except (KeyError, TypeError, ValueError, AttributeError) as e:
        raise Unusable('road report missing or not hex: %s' % e)
    if len(report) < 40:
        raise Unusable('road report too short (%d bytes, need 40)' % len(report))
    address = struct.unpack('>I', report[36:40])[0]
    if not VDP1_VRAM <= address < VDP1_VRAM + 0x80000:
        raise Unusable('shade table address outside VDP1 VRAM: %#x' % address)
    return address - VDP1_VRAM


def referenced(commands, base, shades):
    """Shade entry offsets (8 bytes each) referenced by the commands' CMDGRDA words; -1 if out of range.
    The caller has already required whole 32-byte records."""
    out = []
    for i in range(0, len(commands), 32):
        grda = struct.unpack('>H', commands[i + 28:i + 30])[0]
        if grda:
            offset = grda * 8 - base
            out.append(offset if 0 <= offset and offset + 8 <= len(shades) else -1)
    return out


COMMAND_BUFFERS = ('c3-commands', 'c3-clip-command', 'countdown-commands')


def side_problems(side, h, m):
    """Each side's metadata on its own: the right hold, literal true checks and guards, named guards."""
    out = []
    if not isinstance(m, dict):
        return [side + ' hold metadata is not an object']
    if type(m.get('target')) is not int or m.get('target') != h:
        out.append('%s hold metadata names target %r, expected %d' % (side, m.get('target'), h))
    if m.get('checks_pass') is not True:
        out.append(side + ' checks_pass is not literal true')
    g = m.get('guards')
    if not isinstance(g, list) or not g:
        out.append(side + ' guards missing')
    else:
        for x in g:
            if not isinstance(x, dict) or not isinstance(x.get('name'), str) or not x.get('name'):
                out.append(side + ' guard without a non-empty string name')
                break
        for x in g:
            if not isinstance(x, dict) or x.get('pass') is not True:
                out.append(side + ' guard pass is not literal true')
                break
    return out


def judge(base, cand, mode):
    rows, ok = [], True
    for h in HOLDS:
        reasons = []
        snaps = (base / 'snapshots', cand / 'snapshots')
        metas = []
        for side, s in zip(('base', 'candidate'), snaps):
            p = s / f'hold-{h}.json'
            if not p.exists():
                reasons.append(side + ' hold metadata missing')
                metas.append(None)
                continue
            # Unparseable or non-object metadata is unusable input, not a verdict; a well-formed record
            # whose target, checks or guards are wrong stays a FAIL below.
            try:
                meta = json.loads(p.read_text())
            except ValueError as e:
                raise Unusable('%s hold-%d.json is not valid JSON (%s)' % (side, h, e))
            if not isinstance(meta, dict):
                raise Unusable('%s hold-%d.json is not a JSON object (%s)' % (side, h, type(meta).__name__))
            metas.append(meta)
            reasons += side_problems(side, h, meta)
        if None not in metas and not reasons:
            if [x['name'] for x in metas[0]['guards']] != [x['name'] for x in metas[1]['guards']]:
                reasons.append('guard sets differ')
        data = {}
        for k in BUFFERS:
            a, b = snaps[0] / f'hold-{h}.{k}', snaps[1] / f'hold-{h}.{k}'
            if not a.exists() or not b.exists():
                where = 'both' if not a.exists() and not b.exists() else 'base' if not a.exists() else 'candidate'
                reasons.append('%s missing (%s)' % (k, where))
                continue
            data[k] = (a.read_bytes(), b.read_bytes())
            if k in COMMAND_BUFFERS:
                for side, buf in zip(('base', 'candidate'), data[k]):
                    if len(buf) % 32:
                        reasons.append('%s %s is not whole 32-byte records (%d bytes)' % (side, k, len(buf)))
            if (mode == 'exact' or k not in ROAD_OUTPUTS) and k != 'c3-shades' and data[k][0] != data[k][1]:
                reasons.append(k + ' differs')
        counts = None
        whole = 'c3-commands' in data and all(len(x) % 32 == 0 for x in data['c3-commands'])
        if whole and 'c3-shades' in data and all(isinstance(m, dict) for m in metas):
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


def plant(cand, tmp, name, mode):
    """A copy of the candidate with one planted fault; None if the fault cannot be planted."""
    c = Path(tmp) / name
    shutil.copytree(cand, c)
    s = c / 'snapshots'
    try:
        if name == 'command':
            p = s / 'hold-542.c3-commands'
            b = bytearray(p.read_bytes())
            if mode == 'lod':
                b += bytes(32 * 500)
            else:
                b[len(b) // 2] ^= 1
            p.write_bytes(bytes(b))
        elif name == 'metadata':
            p = s / 'hold-542.c3-metadata'
            b = bytearray(p.read_bytes())
            b[len(b) // 2] ^= 1
            p.write_bytes(bytes(b))
        else:
            p = s / 'hold-542.json'
            m = json.loads(p.read_text())
            m['checks_pass'] = False
            p.write_text(json.dumps(m))
    except (OSError, ValueError, IndexError, TypeError):
        return None
    return c


def write(out, res):
    out.write_text(json.dumps(res, indent=1) + '\n')


def main(argv):
    if len(argv) != 5 or argv[3] not in ('exact', 'lod'):
        print(__doc__)
        return 2
    base, cand, mode, out = Path(argv[1]), Path(argv[2]), argv[3], Path(argv[4])
    head = dict(tier='EMULATOR', hardware='UNRUN', mode=mode, base=str(base), candidate=str(cand))
    try:
        for d in (base, cand):
            r = json.loads((d / 'receipt.json').read_text())
            holds = r.get('holds') if isinstance(r, dict) else None
            # Literal ints only: JSON true would otherwise compare equal to hold 1.
            if not isinstance(holds, list) or not all(type(x) is int for x in holds) or holds != HOLDS:
                raise Unusable('%s does not hold the 13 selected inputs' % d)
        ok, rows = judge(base, cand, mode)
        controls = []
        if ok:
            # Controls only mean something on a passing pair; a failing pair keeps its own verdict.
            with tempfile.TemporaryDirectory() as tmp:
                for name in ('command', 'metadata', 'check'):
                    c = plant(cand, tmp, name, mode)
                    rejected = c is not None and not judge(base, c, mode)[0]
                    controls.append(dict(control=name, planted=c is not None, rejected=rejected))
    except (Unusable, OSError, ValueError, KeyError, IndexError, TypeError, AttributeError, struct.error) as e:
        write(out, dict(head, verdict='UNUSABLE', reason='%s: %s' % (type(e).__name__, e)))
        print('selected13_strict: unusable input: %s' % e)
        return 2
    verdict = 'PASS' if ok and controls and all(c['rejected'] for c in controls) else 'FAIL'
    write(out, dict(head, verdict=verdict, holds=rows, controls=controls,
                    controls_note=None if ok else 'not run: the comparison already failed'))
    print(json.dumps(dict(verdict=verdict, holds_ok=sum(r['ok'] for r in rows), controls=controls,
                          failing=[(r['hold'], r['reasons']) for r in rows if not r['ok']][:4])))
    return 0 if verdict == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
