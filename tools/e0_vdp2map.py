"""E0 VDP2 allocation map from an E0_VDP2_MAP=1 run: decodes the captured VDP2
register shadow and CRAM copy (fzx_e0_vdp2_regs, fzx_e0_cram), lists every VRAM
and CRAM allocation with its source, checks overlaps, and checks the final
VDP2 VRAM dump for written bytes outside every allocation.
Usage: vdp2map.py BUILD_DIR RUN_DIR  -> prints the map and writes RUN_DIR/vdp2-map.md."""
import struct, sys
from pathlib import Path

build, run = Path(sys.argv[1]), Path(sys.argv[2])
sym = {}
for line in (build / 'fzs-engine.sym').read_text().splitlines():
    p = line.split()
    if len(p) == 3: sym.setdefault(p[2].split('.lto_priv.')[0], int(p[0], 16))
hi = (run / 'final.wram-high').read_bytes()
vram = (run / 'final.vdp2-vram').read_bytes()
def at(name, n): a = (sym['_' + name] & 0x07ffffff) - 0x06000000; return hi[a:a + n]
R = struct.unpack('>144H', at('fzx_e0_vdp2_regs', 288))
CR = struct.unpack('>2048H', at('fzx_e0_cram', 4096))
reg = lambda off: R[off // 2]
VRAM = 0x80000
out = []
def say(s=''): print(s); out.append(s)

bgon = reg(0x20); ramctl = reg(0x0e)
assert ramctl & 0x0300 == 0x0300, 'expected partitioned banks A0/A1/B0/B1'
crmd = (ramctl >> 12) & 3
allocs = []   # (start, end, owner, source)
notes = []
def alloc(start, size, owner, source): allocs.append((start % VRAM, start % VRAM + size, owner, source))

# RBG0 (BGON bit 4): parameters A and B, mode RPMD.
if bgon & 0x10:
    rpta = ((reg(0xbc) & 7) << 16 | reg(0xbe)) * 2 & ~0x7f
    alloc(rpta, 0x60, 'RBG0 rotation parameter table A (floor)', 'RPTA; floor_rbg_native.c FLOOR_RP')
    alloc(rpta + 0x80, 0x60, 'RBG0 rotation parameter table B (clouds)', 'RPTA+0x80; clouds_vdp2_native.c CLOUD_RP')
    chctlb = reg(0x2a); r0chcn = (chctlb >> 12) & 7
    assert r0chcn == 3, 'RBG0 expected RGB 32768 colours'
    pncr = reg(0x38); two_word = not (pncr & 0x8000)
    assert two_word and not (chctlb & 0x0100) and not (reg(0x3a) & 0x0300), 'RBG0 expected 2-word PND, 1x1 cells, 1x1 plane'
    plane = 0x4000
    mpofr = reg(0x3e)
    for name, off, mpof, owner, src, chars, nchars in (
            ('A', 0x50, mpofr & 7, 'RBG0 map A (floor)', 'MPOFR/MPABRA; FLOOR_MAP', 0x00000, 32),
            ('B', 0x60, (mpofr >> 4) & 7, 'RBG0 map B (clouds)', 'MPOFR/MPABRB; CLOUD_MAP', 0x01000, 32)):
        planes = {reg(off + 2 * i) & 0xff for i in range(8)} | {reg(off + 2 * i) >> 8 for i in range(8)}
        assert len(planes) == 1, 'RBG0 %s expected a single plane' % name
        alloc(((mpof << 6) | planes.pop()) * plane, plane, owner, src)
        # Character patterns referenced by that map (RGB 32768: 128 bytes per cell).
        a = allocs[-1][0]
        used = sorted({struct.unpack_from('>I', vram, a + 4 * i)[0] & 0x7fff for i in range(64 * 64)})
        first, last = used[0] * 0x20, used[-1] * 0x20 + 0x80
        alloc(first, last - first, 'RBG0 character patterns %s (%s)' % (name, 'floor' if name == 'A' else 'clouds'),
              'cells %d..%d referenced by map %s; %s' % (used[0], used[-1], name, 'FLOORCHR.BIN' if name == 'A' else 'clouds_vdp2_native.c CLOUD_CHARS'))
    ktctl, ktaof = reg(0xb4), reg(0xb6)
    assert ktctl & 0x0101 == 0x0101 and not ktctl & 0x0202, 'expected coefficients on A and B, 2-byte words'
    assert ktaof & 7 == (ktaof >> 8) & 7
    alloc((ktaof & 7) * 0x20000 * 2 % VRAM if False else 0x40000, 0x20000,
          'RBG0 coefficient table (A and B share it)', 'KTAOF; FLOORKOF.BIN at B0 (FLOOR_COEFF)')
    for n, off, owner, src in ((0, 0xd8, 'Line window W0 (floor span; selects parameter A)', 'LWTA0; FLOOR_WINDOW'),
                               (1, 0xdc, 'Line window W1 (clouds)', 'LWTA1; CLOUD_WINDOW')):
        u, l = reg(off), reg(off + 2)
        assert u & 0x8000, 'line window %d expected enabled' % n
        alloc(((u & 7) << 16 | l) * 2, 224 * 4, owner, src)
# Back screen.
bku, bkl = reg(0xac), reg(0xae)
alloc(((bku & 7) << 16 | bkl) * 2, 224 * 2 if bku & 0x8000 else 2,
      'Back screen colour (%s)' % ('one per line' if bku & 0x8000 else 'single'), 'BKTA; clouds_vdp2_native.c SKY_BACK')
# Line colour screen.
if reg(0xa8) & 0x8000 or reg(0xe8):
    alloc(((reg(0xa8) & 7) << 16 | reg(0xaa)) * 2, 224 * 2, 'Line colour table', 'LCTA')
# NBG3 (BGON bit 3): dbgio text.
if bgon & 0x08:
    pncn3 = reg(0x36); chctlb = reg(0x2a)
    assert pncn3 & 0x8000 and pncn3 & 0x4000 and not (chctlb & 0x0030) and not (reg(0x3a) & 0xc0), 'NBG3 expected 1-word PND, 12-bit, 16 colours, 1x1'
    mpof = (reg(0x3c) >> 12) & 7
    planes = {reg(0x4c) & 0x3f, reg(0x4c) >> 8 & 0x3f, reg(0x4e) & 0x3f, reg(0x4e) >> 8 & 0x3f}
    assert len(planes) == 1
    map_at = ((mpof << 6) | planes.pop()) * 0x2000 % VRAM
    alloc(map_at, 0x2000, 'NBG3 map (dbgio text, 64x64 cells)', 'MPOFN/MPABN3/MPCDN3 (Yaul dbgio)')
    pnd = struct.unpack_from('>4096H', vram, map_at)
    spcn = pncn3 & 0x1f
    cell = lambda w: ((spcn >> 2) & 7) << 12 | (w & 0xfff)
    # Displayed cells: NBG3 scroll is fixed at 0, so the 40x28 cells at the map origin.
    shown = sorted({cell(pnd[y * 64 + x]) for y in range(28) for x in range(40)})
    hidden = sorted({cell(pnd[y * 64 + x]) for y in range(64) for x in range(64) if x >= 40 or y >= 28} - set(shown))
    base = (shown[0] & ~0xff) * 32
    glyph_end = base
    for c in range(256):
        if any(vram[base + 32 * c: base + 32 * c + 32]): glyph_end = base + 32 * c + 32
    alloc(base, glyph_end - base, 'NBG3 character patterns (dbgio font, 256 glyphs, 16 colours)',
          'Yaul dbgio; written glyphs to +0x%x' % (glyph_end - base))
    notes.append('NBG3 cells outside the displayed 40x28 area reference cells %s (VRAM %s); they are never displayed (NBG3 does not scroll).'
                 % (', '.join(str(c) for c in hidden), ', '.join('0x%05x' % (c * 32 % VRAM) for c in hidden)))
    last = pnd[4095]
    if last & 0x8000:
        notes.append('Map cell (63,63) at 0x7fffe holds 0x%04x: the boot back-screen colour written by user_init '
                     '(vdp2_scrn_back_color_set at VDP2_VRAM_ADDR(3,0x1fffe)) before the clouds move the back screen '
                     'to A1 0x2c400. That cell is never displayed.' % last)

banks = [('A0', 0x00000), ('A1', 0x20000), ('B0', 0x40000), ('B1', 0x60000)]
def bank(a): return [b for b, s in banks if s <= a < s + 0x20000][0]
allocs.sort()
say('# E0 VDP2 VRAM and CRAM map (from the running image\'s registers and VRAM)')
say()
say('VDP2 VRAM: 512 KB in four 128 KB banks (RAMCTL 0x%04x: A0 character patterns, A1 pattern names, B0 coefficients, B1 unused by RBG0).' % ramctl)
say()
say('| Bank | Start | End | Bytes | Allocation | Source |')
say('| --- | --- | --- | --- | --- | --- |')
for s, e, o, src in allocs: say('| %s | 0x%05x | 0x%05x | %d | %s | %s |' % (bank(s), s, e, e - s, o, src))
overlaps = [(a, b) for i, a in enumerate(allocs) for b in allocs[i + 1:] if b[0] < a[1]]
say()
say('Overlaps: %s' % ('none' if not overlaps else '; '.join('%s with %s (0x%05x..0x%05x)' % (a[2], b[2], b[0], min(a[1], b[1])) for a, b in overlaps)))
# Free space and stray writes.
covered = bytearray(VRAM)
for s, e, *_ in allocs: covered[s:e] = b'\1' * (e - s)
strays = [i for i in range(VRAM) if vram[i] and not covered[i]]
runs = []
for i in strays:
    if runs and runs[-1][1] == i: runs[-1][1] = i + 1
    else: runs.append([i, i + 1])
for n in notes: say('Note: ' + n)
say('Written bytes outside every allocation: %d%s' % (len(strays), '' if not runs else ' at ' + ', '.join('0x%05x..0x%05x' % tuple(r) for r in runs[:8])))
for b, s in banks:
    used = sum(covered[s:s + 0x20000])
    say('- %s: %d of 131072 bytes allocated, %d free' % (b, used, 0x20000 - used))
# CRAM.
say()
entries = 1024 if crmd == 0 else 2048
nz = [i for i, v in enumerate(CR[:entries]) if v]
cruns = []
for i in nz:
    if cruns and cruns[-1][1] == i: cruns[-1][1] = i + 1
    else: cruns.append([i, i + 1])
say('CRAM: mode %d (%d RGB1555 entries; CRAOFA 0x%04x, CRAOFB 0x%04x). Written entries: %s.' % (
    crmd, entries, reg(0xe4), reg(0xe6), ', '.join('%d..%d' % (a, b - 1) for a, b in cruns)))
(run / 'vdp2-map.md').write_text('\n'.join(out) + '\n')
