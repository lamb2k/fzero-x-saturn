#!/usr/bin/env python3
"""SH-2 cycle model: run linked SH-2 code on HOST and count modelled cycles.

A small SH7604 interpreter for the integer instructions compiled C uses, with a pipeline cost
model taken from the SH7604 hardware manual:

  * every instruction issues in 1 cycle;
  * BT/BF taken 3, BT/S, BF/S taken 2, not taken 1; BRA, BSR, BRAF, BSRF, JMP, JSR, RTS 2;
  * a load followed at once by an instruction that reads the loaded register stalls 1 cycle;
  * MULU.W/MULS.W issue 1 and MUL.L/DMULU.L/DMULS.L issue 2; MACH/MACL are ready 2 cycles
    later, and STS MACH/MACL waits for them.

Memory is flat (no wait states, every fetch and data access a cache hit), so the figures are
a model, not a timing. Console runs of earlier SH-2 work came out about 36% slower than this
kind of model; quote both.

  sh2_cycle_model.py cell4z IMAGE.bin IMAGE.map PACK ROSTER ARENA [--every N]
      Runs sh2z_cell4z_decode (image and map from tools/racer_stream_sh2.sh, ARENA from
      racer_stream_host decode) on every Nth cell of an RSZ1 pack, checks each output against
      the raw RSP1 roster and prints cycles per pixel and per cell for each profile.
  sh2_cycle_model.py vlc2bytes IMAGE.bin IMAGE.map ROSTER [--every N]
      Baseline: plain vlc-2 on the raw bytes, one table; ratio on every cell, cycles on every
      Nth.
"""
import argparse, json, struct, sys

M32 = 0xFFFFFFFF


def s8(v):
    return v - 256 if v & 0x80 else v


def s12(v):
    return v - 4096 if v & 0x800 else v


def s16(v):
    return v - 65536 if v & 0x8000 else v


def s32(v):
    return v - (1 << 32) if v & 0x80000000 else v


class Halt(Exception):
    pass


class SH2:
    RET = 0x0DEAD000

    def __init__(self, base, size):
        self.base, self.mem = base, bytearray(size)
        self.r = [0] * 16
        self.t = 0
        self.pr = self.mach = self.macl = self.gbr = 0
        self.cycles = 0
        self.insns = 0
        self.mac_ready = 0
        self.load_reg = -1   # register loaded by the previous instruction

    # memory, big-endian
    def _a(self, a, n):
        o = (a & M32) - self.base
        if o < 0 or o + n > len(self.mem):
            raise Halt('bad address %08x' % a)
        if a % n:
            raise Halt('unaligned %d-byte access %08x' % (n, a))
        return o

    def rb(self, a):
        return self.mem[self._a(a, 1)]

    def rw(self, a):
        o = self._a(a, 2); return self.mem[o] << 8 | self.mem[o + 1]

    def rl(self, a):
        o = self._a(a, 4); return struct.unpack_from('>I', self.mem, o)[0]

    def wb(self, a, v):
        self.mem[self._a(a, 1)] = v & 0xFF

    def ww(self, a, v):
        struct.pack_into('>H', self.mem, self._a(a, 2), v & 0xFFFF)

    def wl(self, a, v):
        struct.pack_into('>I', self.mem, self._a(a, 4), v & M32)

    def call(self, entry, args, stack_args, sp):
        r = self.r
        for i, v in enumerate(args):
            r[4 + i] = v & M32
        sp -= 4 * len(stack_args)
        for i, v in enumerate(stack_args):
            self.wl(sp + 4 * i, v)
        r[15] = sp
        self.pr = self.RET
        self.cycles = self.insns = self.mac_ready = 0
        self.load_reg = -1
        pc = entry
        while pc != self.RET:
            pc = self.step(pc)
        return r[0]

    def use(self, *regs):
        if self.load_reg >= 0 and self.load_reg in regs:
            self.cycles += 1
        self.load_reg = -1

    def mac_wait(self):
        if self.cycles < self.mac_ready:
            self.cycles = self.mac_ready

    def step(self, pc, slot=False):
        op = self.rw(pc)
        r = self.r
        self.insns += 1
        self.cycles += 1
        n = (op >> 8) & 15
        m = (op >> 4) & 15
        hi = op >> 12
        lo = op & 15
        nxt = pc + 2
        loaded = -1

        def delayed(target, cost):
            self.load_reg = -1
            self.cycles += cost - 1
            self.step(pc + 2, slot=True)
            return target & M32

        if hi == 0:
            if lo in (4, 5, 6):
                self.use(n, m, 0)
                a = (r[0] + r[n]) & M32
                [self.wb, self.ww, self.wl][lo - 4](a, r[m])
            elif lo in (0xC, 0xD, 0xE):
                self.use(m, 0)
                a = (r[0] + r[m]) & M32
                v = [lambda: s8(self.rb(a)), lambda: s16(self.rw(a)), lambda: self.rl(a)][lo - 0xC]()
                r[n] = v & M32; loaded = n
            elif lo == 7:
                self.use(n, m); self.mac_wait()
                self.macl = (r[n] * r[m]) & M32; self.mac_ready = self.cycles + 1 + 2; self.cycles += 1
            elif op & 0xFF == 0x0A:
                self.use(); self.mac_wait(); r[n] = self.mach
            elif op & 0xFF == 0x1A:
                self.use(); self.mac_wait(); r[n] = self.macl
            elif op & 0xFF == 0x2A:
                self.use(); r[n] = self.pr
            elif op & 0xFF == 0x29:
                self.use(); r[n] = self.t
            elif op == 0x000B:
                self.use()
                return delayed(self.pr, 2)
            elif op == 0x0009:
                self.use()
            elif op == 0x0008:
                self.use(); self.t = 0
            elif op == 0x0018:
                self.use(); self.t = 1
            elif op & 0xFF == 0x23:
                self.use(n)
                return delayed(pc + 4 + r[n], 2)
            elif op & 0xFF == 0x03:
                self.use(n); self.pr = pc + 4
                return delayed(pc + 4 + r[n], 2)
            else:
                raise Halt('opcode %04x at %08x' % (op, pc))
        elif hi == 1:
            self.use(n, m)
            self.wl(r[n] + 4 * lo, r[m])
        elif hi == 2:
            self.use(n, m)
            if lo in (0, 1, 2):
                [self.wb, self.ww, self.wl][lo](r[n], r[m])
            elif lo in (4, 5, 6):
                sz = (1, 2, 4)[lo - 4]
                a = (r[n] - sz) & M32
                [self.wb, self.ww, self.wl][lo - 4](a, r[m]); r[n] = a
            elif lo == 7:
                q, mm = r[n] >> 31, r[m] >> 31; self.t = q ^ mm
            elif lo == 8:
                self.t = int((r[n] & r[m]) == 0)
            elif lo == 9:
                r[n] &= r[m]
            elif lo == 0xA:
                r[n] ^= r[m]
            elif lo == 0xB:
                r[n] |= r[m]
            elif lo == 0xD:
                r[n] = ((r[m] << 16) | (r[n] >> 16)) & M32
            elif lo == 0xE:
                self.mac_wait(); self.macl = (r[n] & 0xFFFF) * (r[m] & 0xFFFF); self.mac_ready = self.cycles + 2
            elif lo == 0xF:
                self.mac_wait(); self.macl = (s16(r[n] & 0xFFFF) * s16(r[m] & 0xFFFF)) & M32; self.mac_ready = self.cycles + 2
            else:
                raise Halt('opcode %04x at %08x' % (op, pc))
        elif hi == 3:
            self.use(n, m)
            a, b = r[n], r[m]
            if lo == 0:
                self.t = int(a == b)
            elif lo == 2:
                self.t = int(a >= b)
            elif lo == 3:
                self.t = int(s32(a) >= s32(b))
            elif lo == 6:
                self.t = int(a > b)
            elif lo == 7:
                self.t = int(s32(a) > s32(b))
            elif lo == 8:
                r[n] = (a - b) & M32
            elif lo == 0xA:
                v = a - b - self.t; self.t = int(v < 0); r[n] = v & M32
            elif lo == 0xB:
                v = s32(a) - s32(b); self.t = int(v < -2 ** 31 or v >= 2 ** 31); r[n] = v & M32
            elif lo == 0xC:
                r[n] = (a + b) & M32
            elif lo == 0xE:
                v = a + b + self.t; self.t = int(v > M32); r[n] = v & M32
            elif lo == 0xF:
                v = s32(a) + s32(b); self.t = int(v < -2 ** 31 or v >= 2 ** 31); r[n] = v & M32
            elif lo in (5, 0xD):
                self.mac_wait()
                v = a * b if lo == 5 else (s32(a) * s32(b)) & ((1 << 64) - 1)
                self.mach, self.macl = (v >> 32) & M32, v & M32
                self.cycles += 1; self.mac_ready = self.cycles + 2
            else:
                raise Halt('opcode %04x at %08x' % (op, pc))
        elif hi == 4:
            low8 = op & 0xFF
            if low8 == 0x0B:
                self.use(n); self.pr = pc + 4
                return delayed(r[n], 2)
            if low8 == 0x2B:
                self.use(n)
                return delayed(r[n], 2)
            if low8 == 0x22:
                self.use(n); r[n] = (r[n] - 4) & M32; self.wl(r[n], self.pr)
            elif low8 == 0x26:
                self.use(n); self.pr = self.rl(r[n]); r[n] = (r[n] + 4) & M32
            elif low8 == 0x2A:
                self.use(n); self.pr = r[n]
            else:
                self.use(n)
                v = r[n]
                if low8 == 0x00 or low8 == 0x20:
                    self.t = v >> 31; v = (v << 1) & M32
                elif low8 == 0x01:
                    self.t = v & 1; v >>= 1
                elif low8 == 0x21:
                    self.t = v & 1; v = (v >> 1) | (v & 0x80000000)
                elif low8 == 0x04:
                    self.t = v >> 31; v = ((v << 1) | self.t) & M32
                elif low8 == 0x05:
                    self.t = v & 1; v = (v >> 1) | (self.t << 31)
                elif low8 == 0x24:
                    c = v >> 31; v = ((v << 1) | self.t) & M32; self.t = c
                elif low8 == 0x25:
                    c = v & 1; v = (v >> 1) | (self.t << 31); self.t = c
                elif low8 == 0x08:
                    v = (v << 2) & M32
                elif low8 == 0x09:
                    v >>= 2
                elif low8 == 0x18:
                    v = (v << 8) & M32
                elif low8 == 0x19:
                    v >>= 8
                elif low8 == 0x28:
                    v = (v << 16) & M32
                elif low8 == 0x29:
                    v >>= 16
                elif low8 == 0x10:
                    v = (v - 1) & M32; self.t = int(v == 0)
                elif low8 == 0x11:
                    self.t = int(s32(v) >= 0)
                elif low8 == 0x15:
                    self.t = int(s32(v) > 0)
                else:
                    raise Halt('opcode %04x at %08x' % (op, pc))
                r[n] = v
        elif hi == 5:
            self.use(m)
            r[n] = self.rl(r[m] + 4 * lo); loaded = n
        elif hi == 6:
            self.use(m)
            if lo in (0, 1, 2):
                v = [lambda: s8(self.rb(r[m])), lambda: s16(self.rw(r[m])), lambda: self.rl(r[m])][lo]()
                r[n] = v & M32; loaded = n
            elif lo in (4, 5, 6):
                a = r[m]
                v = [lambda: s8(self.rb(a)), lambda: s16(self.rw(a)), lambda: self.rl(a)][lo - 4]()
                if n != m:
                    r[m] = (a + (1, 2, 4)[lo - 4]) & M32
                r[n] = v & M32; loaded = n
            elif lo == 3:
                r[n] = r[m]
            elif lo == 7:
                r[n] = ~r[m] & M32
            elif lo == 8:
                v = r[m]; r[n] = (v & 0xFFFF0000) | ((v & 0xFF) << 8) | ((v >> 8) & 0xFF)
            elif lo == 9:
                v = r[m]; r[n] = ((v << 16) | (v >> 16)) & M32
            elif lo == 0xA:
                v = -r[m] - self.t; self.t = int(v < 0 or (v == 0 and False)); r[n] = v & M32
            elif lo == 0xB:
                r[n] = -r[m] & M32
            elif lo == 0xC:
                r[n] = r[m] & 0xFF
            elif lo == 0xD:
                r[n] = r[m] & 0xFFFF
            elif lo == 0xE:
                r[n] = s8(r[m] & 0xFF) & M32
            elif lo == 0xF:
                r[n] = s16(r[m] & 0xFFFF) & M32
        elif hi == 7:
            self.use(n)
            r[n] = (r[n] + s8(op & 0xFF)) & M32
        elif hi == 8:
            sub = n
            d = op & 15
            rr = m
            if sub == 0:
                self.use(rr, 0); self.wb(r[rr] + d, r[0])
            elif sub == 1:
                self.use(rr, 0); self.ww(r[rr] + 2 * d, r[0])
            elif sub == 4:
                self.use(rr); r[0] = s8(self.rb(r[rr] + d)) & M32; loaded = 0
            elif sub == 5:
                self.use(rr); r[0] = s16(self.rw(r[rr] + 2 * d)) & M32; loaded = 0
            elif sub == 8:
                self.use(0); self.t = int(r[0] == (s8(op & 0xFF) & M32))
            elif sub in (9, 0xB, 0xD, 0xF):
                self.use()
                if slot:
                    raise Halt('branch in delay slot')
                take = self.t == (1 if sub in (9, 0xD) else 0)
                tgt = pc + 4 + 2 * s8(op & 0xFF)
                if sub in (9, 0xB):
                    if take:
                        self.cycles += 2
                        return tgt & M32
                    return nxt
                if take:
                    return delayed(tgt, 2)
                return nxt
            else:
                raise Halt('opcode %04x at %08x' % (op, pc))
        elif hi == 9:
            self.use()
            r[n] = s16(self.rw(pc + 4 + 2 * (op & 0xFF))) & M32; loaded = n
        elif hi in (0xA, 0xB):
            self.use()
            if hi == 0xB:
                self.pr = pc + 4
            return delayed(pc + 4 + 2 * s12(op & 0xFFF), 2)
        elif hi == 0xC:
            sub = n
            self.use(0)
            imm = op & 0xFF
            if sub == 7:
                r[0] = ((pc & ~3) + 4 + 4 * imm) & M32
            elif sub == 8:
                self.t = int((r[0] & imm) == 0)
            elif sub == 9:
                r[0] &= imm
            elif sub == 0xA:
                r[0] ^= imm
            elif sub == 0xB:
                r[0] |= imm
            else:
                raise Halt('opcode %04x at %08x' % (op, pc))
        elif hi == 0xD:
            self.use()
            r[n] = self.rl((pc & ~3) + 4 + 4 * (op & 0xFF)); loaded = n
        elif hi == 0xE:
            self.use()
            r[n] = s8(op & 0xFF) & M32
        else:
            raise Halt('opcode %04x at %08x' % (op, pc))
        self.load_reg = loaded
        return nxt


def be16(b, o):
    return struct.unpack_from('>H', b, o)[0]


def be32(b, o):
    return struct.unpack_from('>I', b, o)[0]


BASE = 0x06004000
A_ARENA, A_T, A_DST, A_STREAM, SP = 0x06060000, 0x06064000, 0x06068000, 0x06080000, 0x060FFF00


def load_image(a):
    syms = {}
    for line in open(a.map):
        name, addr = line.split()
        syms[name.lstrip('_')] = int(addr, 0)
    cpu = SH2(0x06000000, 0x100000)
    img = open(a.image, 'rb').read()
    cpu.mem[BASE - 0x06000000:BASE - 0x06000000 + len(img)] = img
    return cpu, syms


def put(cpu, addr, data):
    cpu.mem[addr - 0x06000000:addr - 0x06000000 + len(data)] = data


def finish(res):
    for prof, p in res.items():
        p['cycles_per_px'] = round(p['cycles'] / p['px'], 2)
        p['cycles_per_cell'] = round(p['cycles'] / p['cells'])
        p['ms_per_cell_28mhz'] = round(p['cycles'] / p['cells'] / 28636.36, 3)
        p['cpi'] = round(p['cycles'] / p['insns'], 2)
    print(json.dumps(res, indent=1))


def cell4z(a):
    import racer_stream as rs
    cpu, syms = load_image(a)
    entry = syms['sh2z_cell4z_decode']
    pk = open(a.pack, 'rb').read()
    raw_sets = {(s['key'], s['profile']): s['blob'] for s in rs.read_roster(a.roster)}
    lens = [list(pk[8 + 30 * k:8 + 30 * k + 30]) for k in range(16)]
    lmax = max(max(l) for l in lens)
    arena = open(a.arena, 'rb').read()
    offsets = struct.unpack('>16H', arena[-32:])
    put(cpu, A_ARENA, arena[:-32])
    put(cpu, A_T, struct.pack('>16H16HI', *offsets, *([0] * 16), lmax))
    nsets = be16(pk, 6)
    res = {}
    for si in range(nsets):
        e = 8 + 480 + 32 * si
        key = pk[e:e + 8].rstrip(b'\0').decode(); prof = pk[e + 8:e + 12].rstrip(b'\0').decode()
        h_at, st = be32(pk, e + 12), be32(pk, e + 20)
        nc = be16(pk, h_at + 8) * be16(pk, h_at + 10)
        raw = raw_sets[(key, prof)]
        p = res.setdefault(prof, dict(cells=0, px=0, cycles=0, insns=0, worst_cell_cycles=0))
        for c in range(nc):
            if (si * 131 + c) % a.every:
                continue
            cd = h_at + 168 + 16 * c
            w, h, off = be16(pk, cd), be16(pk, cd + 2), be32(pk, cd + 8)
            if be16(pk, st + 8 * nc + 2 * c) & 0x8000:
                continue
            so = st + 10 * nc + be32(pk, st + 8 * c)
            chunk = pk[so:so + 0x10000]
            put(cpu, A_STREAM, chunk)
            ok = cpu.call(entry, [A_DST, w, h, A_STREAM], [len(chunk), A_ARENA, A_T, 0], SP)
            out = bytes(cpu.mem[A_DST - 0x06000000:A_DST - 0x06000000 + w * h // 2])
            if ok != 1 or out != raw[off:off + w * h // 2]:
                raise SystemExit('sh2 model: %s/%s cell %d decoded wrong' % (key, prof, c))
            p['cells'] += 1; p['px'] += w * h; p['cycles'] += cpu.cycles; p['insns'] += cpu.insns
            p['worst_cell_cycles'] = max(p['worst_cell_cycles'], cpu.cycles)
    finish(res)


def vlc2bytes(a):
    """Baseline: plain vlc-2 over the raw 4-bit bytes, one 256-symbol table for the roster."""
    import racer_stream as rs
    cpu, syms = load_image(a)
    sets = [s for s in rs.read_roster(a.roster) if s['framing'] == 2]
    cells = []
    freq = [0] * 256
    for s in sets:
        b = s['blob']
        yc, pc = struct.unpack_from('>HH', b, 8)
        for c in range(yc * pc):
            w, h, ox, oy, off, clut, res_ = rs.CELL.unpack_from(b, 168 + 16 * c)
            data = b[off:off + w * h // 2]
            cells.append((s['profile'], w, h, data))
            for x in data:
                freq[x] += 1
    L = rs.vlc_lengths(freq)
    lens = bytes(L.get(i, 0) for i in range(256))
    codes = rs.canonical_codes(L)
    put(cpu, A_STREAM, lens)
    size = cpu.call(syms['baseline_vlc2_build'], [A_ARENA, 0x4000, A_STREAM], [], SP)
    if not size:
        raise SystemExit('vlc-2 build refused')
    ratio = {}
    res = {}
    for i, (prof, w, h, data) in enumerate(cells):
        bits = rs.Bits()
        for x in data:
            bits.put(*codes[x])
        stream = bits.bytes()
        r = ratio.setdefault(prof, [0, 0])
        r[0] += len(stream); r[1] += len(data)
        if i % a.every:
            continue
        put(cpu, A_STREAM, stream + bytes(64))
        cpu.call(syms['baseline_vlc2_bytes'], [A_DST, len(data) // 2, A_STREAM, A_ARENA], [], SP)
        if bytes(cpu.mem[A_DST - 0x06000000:A_DST - 0x06000000 + len(data)]) != data:
            raise SystemExit('vlc-2 baseline decoded wrong')
        p = res.setdefault(prof, dict(cells=0, px=0, cycles=0, insns=0, worst_cell_cycles=0))
        p['cells'] += 1; p['px'] += w * h; p['cycles'] += cpu.cycles; p['insns'] += cpu.insns
        p['worst_cell_cycles'] = max(p['worst_cell_cycles'], cpu.cycles)
    for prof, (z, raw) in ratio.items():
        res[prof]['ratio'] = round(z / raw, 4)
    res_table = {'table_bytes': size}
    finish(res)
    print(json.dumps(res_table))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    c = sub.add_parser('cell4z')
    for x in ('image', 'map', 'pack', 'roster', 'arena'):
        c.add_argument(x)
    c.add_argument('--every', type=int, default=10)
    v = sub.add_parser('vlc2bytes')
    for x in ('image', 'map', 'roster'):
        v.add_argument(x)
    v.add_argument('--every', type=int, default=10)
    a = ap.parse_args()
    sys.path.insert(0, str(__import__('pathlib').Path(__file__).parent))
    {'cell4z': cell4z, 'vlc2bytes': vlc2bytes}[a.cmd](a)


if __name__ == '__main__':
    main()
