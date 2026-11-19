#!/usr/bin/env python3
"""E0 HOST = SH-2 equality, VDP2 tables slice (ENGINE_PLAN section 5, gate 3).

Private stage only: the sky colours and the dumps are ROM-derived. This tool
prints offsets, counts and booleans, never their contents.

  build    compile tools/e0_vdp2_host.c with the engine image's -D set
  compare  take the view and the backdrop from an SH-2 hold dump, run the HOST
           floor, cloud and sky-line preparation, and compare byte for byte
           with the presented tables in VDP2 VRAM (saturn/engine/VDP2_MAP.md),
           CRAM entries 0..65 and the presented floor record in work RAM.

The dump is claude-eq's headless Ymir hold harness output: <dir>/holds.* taken
with the slave idle after 64 held fields, and <dir>/holds2.* one slave job
later (the held inputs must be identical in both).
"""
import argparse
import json
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from e0_road_eq import engine_defines, first_diff, symbols  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
VIEW_BYTES, BACKDROP_BYTES, FLOOR_BYTES, CLOUDS_BYTES = 80, 424, 1016, 1020
RP, WINDOW, SKY = 96, 896, 448
# VDP2 VRAM offsets of the presented tables (VDP2_MAP.md).
VDP2 = dict(floor_rp=(0x20000, RP), floor_window=(0x60000, WINDOW), cloud_rp=(0x20080, RP),
            cloud_window=(0x2c000, WINDOW), sky_back=(0x2c400, SKY))
SOURCES = ('src/fzx_floor_rbg.c', 'src/fzx_clouds_rbg.c', 'src/fzx_rbg_fast.c', 'src/fzx_view.c', 'src/fzx_sky.c')


def cmd_build(a):
    defines = engine_defines(Path(a.cflags).read_text())
    args = ['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', *defines,
            # FZX_RBG_FAST leaves the exact window() helpers unused (as on the SH-2).
            '-Wno-unused-function', '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'ref'),
            *['-I' + str(Path(d)) for d in a.generated], str(ROOT / 'tools/e0_vdp2_host.c'),
            *[str(ROOT / s) for s in SOURCES], '-lm', '-o', a.out]
    r = subprocess.run(args, capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    sys.stderr.write(r.stderr)
    if r.returncode:
        sys.exit(r.returncode)
    print('built', a.out, 'defines', len(defines))


class Dump:
    def __init__(self, prefix):
        p = Path(prefix)
        self.wram = p.with_name(p.name + '.wram-high').read_bytes()
        self.vdp1 = p.with_name(p.name + '.vdp1-vram').read_bytes()
        self.vdp2 = p.with_name(p.name + '.vdp2-vram').read_bytes()
        self.cram = p.with_name(p.name + '.vdp2-cram').read_bytes()

    def at(self, addr, size):
        """Bytes at an SH-2 address in high work RAM or VDP1 VRAM (cached or cache-through)."""
        a = addr & 0x0FFFFFFF
        if 0x06000000 <= a and a + size <= 0x06000000 + len(self.wram):
            return self.wram[a - 0x06000000:a - 0x06000000 + size]
        if 0x05C00000 <= a and a + size <= 0x05C80000:
            return self.vdp1[a - 0x05C00000:a - 0x05C00000 + size]
        sys.exit('address %#x outside the dumped memory' % addr)

    def word(self, addr):
        return struct.unpack('>I', self.at(addr, 4))[0]


def cmd_compare(a):
    sym = symbols(a.sym)
    d, d2 = Dump(a.dump), Dump(a.dump + '2')
    view = d.at(sym['_fzx_native_view'], VIEW_BYTES)
    backdrop = d.at(sym['_fzx_source_backdrop'], BACKDROP_BYTES)
    seq, done = struct.unpack('>II', d.at(sym['_job'], 8))
    checks = {
        'hold_tick': d.word(sym['_fzx_input_count']) == a.tick and d.word(sym['_fzx_e0_hold_tick']) == a.tick,
        'slave_idle': seq == done,
        'inputs_stable': view == d2.at(sym['_fzx_native_view'], VIEW_BYTES) and
                         backdrop == d2.at(sym['_fzx_source_backdrop'], BACKDROP_BYTES),
        'backdrop_ok': struct.unpack('>I', backdrop[:4])[0] == 0,
    }
    work = Path(a.work)
    work.mkdir(parents=True, exist_ok=True)
    tag = 'flip' if a.flip_view else 'eq'
    inp = work / ('vdp2-input-%d.bin' % a.tick)
    inp.write_bytes(struct.pack('>4sII', b'E0VI', 1, a.tick) + view + backdrop)
    out_path = work / ('vdp2-%s-%d.out' % (tag, a.tick))
    r = subprocess.run([a.exe, str(inp), str(out_path)] + (['--flip-view'] if a.flip_view else []),
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit('HOST build failed: ' + r.stderr.strip())
    out = out_path.read_bytes()
    _, _, floor_bands, cloud_bands, sky_set = struct.unpack('>4s4I', out[:20])
    o = 20
    floor = out[o:o + FLOOR_BYTES]; o += FLOOR_BYTES
    clouds = out[o:o + CLOUDS_BYTES]; o += CLOUDS_BYTES
    sky = out[o:o + SKY]; o += SKY
    cram = out[o:o + 132]
    host = dict(floor_rp=floor[:RP], floor_window=floor[RP:RP + WINDOW], cloud_rp=clouds[:RP],
                cloud_window=clouds[RP:RP + WINDOW], sky_back=sky)
    sections = {k: first_diff(host[k], d.vdp2[off:off + n]) for k, (off, n) in VDP2.items()}
    sections['cram_0_65'] = first_diff(cram, d.cram[:132])
    # The presented floor record (fzx_floor_present copies the uploaded bank into it): tables and counters.
    sections['floor_record'] = first_diff(floor, d.at(sym['_fzx_floor_rbg'], FLOOR_BYTES))
    # Which staged bank the presented tables came from (both match while the tick is held).
    banks = {}
    floor_stage = [d.word(sym['_floor_stage'] + 4 * i) for i in range(2)]
    cloud_stage = [d.word(sym['_clouds_stage'] + 4 * i) for i in range(2)]
    sky_stage = [d.word(sym['_sky_stage'] + 4 * i) for i in range(2)]
    for i in range(2):
        banks[i] = dict(
            floor=d.at(floor_stage[i], RP + WINDOW) == d.vdp2[0x20000:0x20000 + RP] + d.vdp2[0x60000:0x60000 + WINDOW],
            clouds=d.at(cloud_stage[i], RP + WINDOW) == d.vdp2[0x20080:0x20080 + RP] + d.vdp2[0x2c000:0x2c000 + WINDOW],
            sky=d.at(sky_stage[i], SKY) == d.vdp2[0x2c400:0x2c400 + SKY])
    equal = all(v is None for v in sections.values())
    result = dict(tick=a.tick, mode=tag, equal=equal, first_diff=sections, floor_bands=floor_bands,
                  cloud_bands=cloud_bands, sky_lines_set=sky_set,
                  floor_visible_lines=struct.unpack('>I', floor[RP + WINDOW + 4:RP + WINDOW + 8])[0],
                  cloud_visible_lines=struct.unpack('>I', clouds[RP + WINDOW + 4:RP + WINDOW + 8])[0],
                  staged_banks_matching_vdp2=banks, checks=checks, ok_checks=all(checks.values()))
    print(json.dumps(result, sort_keys=True))
    if a.flip_view:
        return 0 if not equal else 1   # the control passes when the comparison fails
    return 0 if equal and result['ok_checks'] else 1


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    b = sub.add_parser('build')
    b.add_argument('--cflags', required=True, help='file holding the engine image SH_CFLAGS line')
    b.add_argument('--generated', nargs='+', required=True, help='generated/sky and generated/floor_rbg')
    b.add_argument('--out', required=True)
    c = sub.add_parser('compare')
    c.add_argument('--sym', required=True)
    c.add_argument('--dump', required=True, help='dump prefix, e.g. RUN/holds (RUN/holds2 is read too)')
    c.add_argument('--tick', type=int, required=True)
    c.add_argument('--exe', required=True)
    c.add_argument('--work', required=True)
    c.add_argument('--flip-view', action='store_true')
    a = p.parse_args()
    sys.exit(cmd_build(a) if a.cmd == 'build' else cmd_compare(a))


if __name__ == '__main__':
    main()
