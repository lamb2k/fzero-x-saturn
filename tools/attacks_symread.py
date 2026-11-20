#!/usr/bin/env python3
"""Read the attacks words from a Ymir harness run (EMULATOR evidence): fzx_atk_report, fzx_atk_chk,
fzx_atk_kos and the retire arrays from the final high work RAM dump, by the image's symbol file.
Usage: attacks_symread.py BUILD_DIR RUN_DIR [--chk]   (prints JSON, or 'chk N HASH' lines)"""
import json, struct, sys
from pathlib import Path

b, r = Path(sys.argv[1]), Path(sys.argv[2])
sym = {}
for line in (b / 'fzs-engine.sym').read_text().splitlines():
    p = line.split()
    if len(p) == 3:
        sym.setdefault(p[2].split('.lto_priv.')[0], int(p[0], 16))
hi = (r / 'final.wram-high').read_bytes()
lo = (r / 'final.wram-low').read_bytes()


def mem(addr, n):
    a = addr & 0x0FFFFFFF
    if 0x06000000 <= a < 0x06100000:
        return hi[a - 0x06000000:a - 0x06000000 + n]
    if 0x00200000 <= a < 0x00300000:
        return lo[a - 0x00200000:a - 0x00200000 + n]
    raise SystemExit('address %08x outside the dumps' % addr)


def words(name, n):
    return list(struct.unpack('>%dI' % n, mem(sym[name], 4 * n)))


chk = words('_fzx_atk_chk', 64)
if '--chk' in sys.argv:
    for i, h in enumerate(chk):
        if h:
            print('chk %d %08x' % ((i + 1) * 256, h))
    sys.exit(0)
rep = words('_fzx_atk_report', 16)
kos = mem(sym['_fzx_atk_kos'], 8 * 32)
log = [struct.unpack('>IBB2x', kos[8 * i:8 * i + 8]) for i in range(min(rep[3], 32))]
rt = list(struct.unpack('>30i', mem(sym['_fzx_atk_retire_tick'], 120)))
rp = list(mem(sym['_fzx_atk_retire_place'], 30))
print(json.dumps({'report': rep, 'kos': log, 'retire_tick': rt, 'retire_place': rp}, indent=None))
