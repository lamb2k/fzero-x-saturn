"""Bake the nine pairs of stock machine tuning endpoints from the pinned US source.

The Saturn interpolates these small tables once per confirmed race selection.
No original executable code or ROM bytes enter the repository.
"""
import argparse
import hashlib
import os
import re
import struct
import subprocess
import tempfile
from pathlib import Path

from import_source_art import DECOMP, ROOT, output_location
from import_course import DECOMP_COMMIT, EXPECTED_ROM_SHA1, normalize_rom


def digest(data):
    return hashlib.sha256(data).hexdigest()


def bake(source, rom, cc):
    body = source.split("f32 Racer_InitMachineStats(", 1)[1].split("void Racer_InitRacer", 1)[0]
    pairs = re.findall(r"temp_fv0\s*=\s*([^;]+);\s*temp_fv1\s*=\s*([^;]+);", body)
    if len(pairs) != 9:
        raise ValueError("stock tuning expression count changed")
    def expand(match):
        value, = struct.unpack_from(">I", rom, int(match[1], 16))
        return f"{value}u"
    # Evaluate source endpoints offline in binary32 using the HOST C compiler.
    # Each pair already includes the machine's weight interpolation.
    code = ['#include <stdio.h>', 'typedef float f32;', 'int main(void) { int weight;',
            'while (scanf("%d", &weight)==1) { float temp_fs0=(weight-780.0f)/1560.0f;']
    for low, high in pairs:
        for expr in (low, high):
            expr = re.sub(r'ROM_READ\((0x[0-9a-fA-F]+)\)', expand, expr)
            code.append(f'printf("%a ", (double)(float)({expr}));')
    code.append('puts(""); } return 0; }')
    table = source.split("Machine sDefaultMachines[] = {", 1)[1].split("};", 1)[0]
    specs = re.findall(r'CUSTOM_MACHINE_DEFAULT,\s*(\d+),[^\n]*?\{ BODY_([A-E]), BOOST_([A-E]), GRIP_([A-E]) \}, (\d+)', table)
    if [int(s[0]) for s in specs] != list(range(30)):
        raise ValueError("stock identities changed")
    with tempfile.TemporaryDirectory() as tmp:
        src, exe = (Path(tmp)/n for n in ("ends.c", "ends"))
        src.write_text("\n".join(code))
        subprocess.run([cc,"-std=c11","-ffp-contract=off","-O2",str(src),"-o",str(exe)],check=True)
        lines = subprocess.run([str(exe)], input="\n".join(s[4] for s in specs), text=True,
                               check=True,capture_output=True).stdout.splitlines()
    header = ['/* Generated private selection data. */', '#pragma once',
              'typedef struct { unsigned short weight; unsigned char body, boost, grip; float curve[9][2]; } stock_machine_t;',
              'static const stock_machine_t stock_machine[30] = {']
    for row, line in zip(specs, lines, strict=True):
        values = line.split()
        pairs_c = ','.join('{'+values[i]+'f,'+values[i+1]+'f}' for i in range(0,18,2))
        header.append('    {%s,%d,%d,%d,{%s}},' % (row[4],*(ord(g)-65 for g in row[1:4]),pairs_c))
    header.append('};')
    for name, key in [('health','gBodyHealthValues'),('boost_force','D_800CF174'),
                      ('grip_force','D_800CF188'),('slide_force','D_800CF19C')]:
        items=re.search(r'f32 '+key+r'\[\] = \{([^}]+)\};',source)[1].strip().strip(',')
        header.append('static const float stock_'+name+'[5] = {'+items+'};')
    return ('\n'.join(header)+'\n').encode()


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--decomp',type=Path,default=DECOMP)
    ap.add_argument('--rom',type=Path,default=os.environ.get('FZX_TEST_ROM'))
    ap.add_argument('--output',type=Path,default=ROOT/'generated/machine_tuning')
    ap.add_argument('--cc',default='cc')
    args=ap.parse_args()
    if not args.rom: ap.error('set FZX_TEST_ROM or pass --rom')
    out=args.output.resolve()
    output_location(out)
    src='src/game/racer.c'
    source=subprocess.check_output(['git','-C',str(args.decomp),'show',f'{DECOMP_COMMIT}:{src}'])
    rom, _=normalize_rom(args.rom.read_bytes())
    if hashlib.sha1(rom).hexdigest()!=EXPECTED_ROM_SHA1: raise ValueError('wrong cartridge revision')
    data=bake(source.decode(),rom,args.cc)
    out.mkdir(parents=True,exist_ok=True)
    (out/'stock_tuning.h').write_bytes(data)
    (out/'machine_tuning.receipt').write_bytes(('\n'.join(['# fzx generation receipt v1',
        'generator '+digest(Path(__file__).read_bytes())+' machine_tuning.py',
        'input '+digest(source)+' racer.c','input '+digest(rom)+' owner-rom',
        'output '+digest(data)+' stock_tuning.h'])+'\n').encode())


if __name__=='__main__':
    main()
