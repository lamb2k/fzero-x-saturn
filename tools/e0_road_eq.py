#!/usr/bin/env python3
"""E0 HOST = SH-2 equality, road build slice (ENGINE_PLAN section 5, gate 3).

Private stage only: the road packs and the memory dumps are ROM-derived. This
tool prints offsets, counts and hashes, never their contents.

  build    compile tools/e0_road_host.c with the engine image's -D set
  compare  extract the road build's inputs (the pick's plan, the view, the eye,
           the fog constants, the displayed shade bank) from an SH-2 hold dump,
           run the HOST build and compare its VDP1 commands and Gouraud words
           byte for byte with the dumped VDP1 VRAM.

The hold dump is the headless Ymir E0 harness's output after 16 fields held at
one tick (fzx_e0_hold_tick): <dir>/holds.wram-high, holds.vdp1-vram.
"""
import argparse
import json
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WRAM_HIGH = 0x06000000
VIEW_BYTES, PLAN_BYTES, CMD_BYTES = 80, 796, 32
# job_t (saturn/slave_road.c): eight words, then fzx_slave_road_args_t.
JOB_ARGS = 32
ARG = dict(commands=0, capacity=4, view=8, eye=12, plan=24, pa=28, pb=32, pixels=36, shades=40)
SH2ZAM_UNITS = ('sh2zam.c', 'sh2zam_view.c', 'sh2zam_view_round.c', 'sh2zam_view_guard_src.c',
                'sh2zam_view_center.c', 'sh2zam_view_guard_src_center.c', 'sh2zam_view_merge_tier.c',
                'sh2zam_shade.c', 'sh2zam_shade_fused.c', 'sh2zam_view_rails.c', 'sh2zam_divu.c')
SOURCES = ('src/fzx_road_stream_draw.c', 'src/fzx_road_stream.c', 'src/fzx_road_pack.c',
           'src/fzx_view.c', 'src/fzx_view16.c', 'src/fzx_view16_center.c')


def engine_defines(cflags_text):
    """The engine image's -D set, SH-2 only switches replaced by the portable form."""
    out = []
    for flag in cflags_text.split():
        if not flag.startswith('-D'):
            continue
        if flag.startswith('-DSH2Z_REQUIRE_SH2'):
            flag = '-DSH2Z_FORCE_PORTABLE=1'
        if flag not in out:
            out.append(flag)
    if '-DSH2Z_FORCE_PORTABLE=1' not in out:
        sys.exit('engine -D set has no SH2Z_REQUIRE_SH2')
    return out


def cmd_build(a):
    defines = engine_defines(Path(a.cflags).read_text())
    lib = Path(a.sh2zam)
    args = ['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', *defines,
            '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'ref'), '-I' + str(lib / 'saturn/include'),
            '-I' + str(Path(a.identity)), str(ROOT / 'tools/e0_road_host.c'),
            *[str(ROOT / s) for s in SOURCES], *[str(lib / 'saturn/src' / u) for u in SH2ZAM_UNITS],
            '-lm', '-o', a.out]
    r = subprocess.run(args, capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    sys.stderr.write(r.stderr)
    if r.returncode:
        sys.exit(r.returncode)
    print('built', a.out, 'defines', len(defines))


def symbols(path):
    table = {}
    for line in Path(path).read_text().splitlines():
        parts = line.split()
        if len(parts) == 3:
            name = parts[2].split('.lto_priv.')[0]
            table.setdefault(name, int(parts[0], 16))
    return table


def source_constant(path, pattern):
    m = re.search(pattern, (ROOT / path).read_text())
    if not m:
        sys.exit('constant not found in %s' % path)
    return int(m.group(1))


def first_diff(a, b):
    n = min(len(a), len(b))
    for i in range(n):
        if a[i] != b[i]:
            return i
    return None if len(a) == len(b) else n


def cmd_compare(a):
    sym = symbols(a.sym)
    dump = Path(a.dump)
    wram = dump.with_name(dump.name + '.wram-high').read_bytes()
    vram = dump.with_name(dump.name + '.vdp1-vram').read_bytes()

    def at(addr, size):
        off = (addr & 0x0FFFFFFF) - WRAM_HIGH
        if off < 0 or off + size > len(wram):
            sys.exit('address %#x outside high work RAM' % addr)
        return wram[off:off + size]

    def word(addr):
        return struct.unpack('>I', at(addr, 4))[0]

    def sym_word(name, index=0):
        return word(sym['_' + name] + 4 * index)

    tick = sym_word('fzx_input_count')
    hold = sym_word('fzx_e0_hold_tick')
    job = sym['_job'] + JOB_ARGS
    args = {k: word(job + v) for k, v in ARG.items()}
    eye = [struct.unpack('>i', at(job + ARG['eye'] + 4 * i, 4))[0] for i in range(3)]
    pa, pb = struct.unpack('>ii', at(job + ARG['pa'], 8))
    shades = [sym_word('road_shades', i) for i in range(2)]
    pixels = sym_word('road_pixels')
    checks = {
        'hold_tick': hold == a.tick and tick == a.tick,
        'job_view': args['view'] == sym['_fzx_native_view'],
        'job_plan': args['plan'] == sym['_fzx_source_road_plan'],
        'job_pixels': args['pixels'] == pixels,
    }
    view = at(sym['_fzx_native_view'], VIEW_BYTES)
    plan = at(sym['_fzx_source_road_plan'], PLAN_BYTES)
    checks['plan_tick'] = plan[24] == (255 - ((a.tick * 8) & 255))  # primitive[0], set by the pick
    reserve = source_constant('saturn/slave_road.h', r'#define FZX_SLAVE_ROAD_RESERVE (\d+)u')
    first = source_constant('saturn/engine/engine_main.c', r'CMD_FIRST = (\d+)')
    clip_index = first + reserve
    road = (clip_index + 1) * CMD_BYTES
    grda = struct.unpack('>H', vram[road + 28:road + 30])[0]
    shade_offset = grda * 8
    bank = [s & 0x7FFFF for s in shades].index(shade_offset) if shade_offset in [s & 0x7FFFF for s in shades] else -1
    checks['displayed_bank'] = bank in (0, 1)
    texture_offset = pixels & 0x7FFFF
    ltx = Path(a.ltx).read_bytes()
    checks['texture_bank'] = vram[texture_offset:texture_offset + len(ltx)] == ltx

    work = Path(a.work)
    work.mkdir(parents=True, exist_ok=True)
    tag = 'flip' if a.flip_used else 'eq'

    def host(offset, name, flip):
        record = struct.pack('>4sIIIIii3i', b'E0RI', 1, a.tick, texture_offset, offset, pa, pb, *eye) + view + plan
        inp = work / ('input-%d-%s.bin' % (a.tick, name))
        inp.write_bytes(record)
        out_path = work / ('%s-%d-%s.out' % (tag, a.tick, name))
        run = [a.exe, a.lod, a.ltx, str(inp), str(out_path)] + (['--flip-used'] if flip else [])
        r = subprocess.run(run, capture_output=True, text=True)
        if r.returncode:
            sys.exit('HOST build failed: ' + r.stderr.strip())
        out = out_path.read_bytes()
        head = struct.unpack('>4s7I', out[:32])
        n = head[2]
        return head, out[32:64], out[64:64 + n * CMD_BYTES], out[64 + n * CMD_BYTES:]

    # The displayed list: VDP1 VRAM's command table and the shade bank its commands address.
    (_, _, count, passes, coarse, dropped, full, flip), host_clip, host_cmds, host_shades = host(shade_offset, 'shown', a.flip_used)
    sh2_clip = vram[clip_index * CMD_BYTES:road]
    sh2_cmds = vram[road:road + count * CMD_BYTES]
    end = struct.unpack('>H', vram[road + count * CMD_BYTES:road + count * CMD_BYTES + 2])[0]
    sh2_shades = vram[shade_offset:shade_offset + count * 8]
    sections = {
        'user_clip': first_diff(host_clip, sh2_clip),
        'commands': first_diff(host_cmds, sh2_cmds),
        'shades': first_diff(host_shades, sh2_shades),
    }
    checks['end_after_road'] = bool(end & 0x8000)
    equal = all(v is None for v in sections.values())
    # The next list: with the slave idle (job done == seq), the master's list in work RAM is the
    # finished build for the same held tick into the other shade bank. Compared the same way.
    seq, done = struct.unpack('>II', at(sym['_job'], 8))
    checks['slave_idle'] = seq == done
    other = None
    if bank in (0, 1) and not a.flip_used:
        other_offset = shades[1 - bank] & 0x7FFFF
        (_, _, n2, *_), _, cmds2, shades2 = host(other_offset, 'next', False)
        wram_cmds = at(sym['_commands_buf'] + road, n2 * CMD_BYTES)
        other = dict(bank=1 - bank, commands=n2,
                     first_diff=dict(commands=first_diff(cmds2, wram_cmds),
                                     shades=first_diff(shades2, vram[other_offset:other_offset + n2 * 8])))
        other['equal'] = all(v is None for v in other['first_diff'].values())
    result = dict(tick=a.tick, mode=tag, bank=bank, shade_offset=shade_offset, texture_offset=texture_offset,
                  commands=count, shade_bytes=count * 8, passes=passes, coarse=coarse, dropped=dropped, full=full,
                  groups=struct.unpack('>I', plan[4:8])[0], visible_chunks=struct.unpack('>I', plan[8:12])[0],
                  flipped_vertex=None if flip == 0xFFFFFFFF else flip, first_diff=sections, equal=equal,
                  next_bank=other, checks=checks, ok_checks=all(checks.values()))
    print(json.dumps(result, sort_keys=True))
    if a.flip_used:
        return 0 if not equal else 1   # the control passes when the comparison fails
    return 0 if equal and other and other['equal'] and result['ok_checks'] else 1


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    b = sub.add_parser('build')
    b.add_argument('--cflags', required=True, help='file holding the engine image SH_CFLAGS line')
    b.add_argument('--sh2zam', required=True, help='SH2ZAM checkout at the engine pin')
    b.add_argument('--identity', required=True, help='directory of road_lod_identity.h')
    b.add_argument('--out', required=True)
    c = sub.add_parser('compare')
    c.add_argument('--sym', required=True)
    c.add_argument('--dump', required=True, help='dump prefix, e.g. RUN/holds')
    c.add_argument('--tick', type=int, required=True)
    c.add_argument('--exe', required=True)
    c.add_argument('--lod', required=True)
    c.add_argument('--ltx', required=True)
    c.add_argument('--work', required=True)
    c.add_argument('--flip-used', action='store_true')
    a = p.parse_args()
    sys.exit(cmd_build(a) if a.cmd == 'build' else cmd_compare(a))


if __name__ == '__main__':
    main()
