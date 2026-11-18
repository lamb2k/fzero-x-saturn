#!/usr/bin/env python3
"""Generate MENUTAB.PAK, menu_tables_identity.h and their receipt (FZX_MENU_TABLES_LOW=1).

Compiles saturn/menu_tables.c with the SH-2 cross compiler, extracts the four
menu image tables byte-exact from their own sections, checks that each section
is retained, relocation-free and count x element size, and lays them out with
tools/menu_tables_layout.py. Run under Linux/WSL as root from the tree it
should write into (the cross toolchain is used through the Yaul chroot when it
is not on PATH). Outputs land in generated/menu_tables; nothing is printed
from the tables themselves.
"""
import argparse, hashlib, json, os, shutil, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import menu_tables_layout as L  # noqa: E402

HEADERS = ('generated/machine_menu/machine_menu_identity.h', 'generated/finish_menu_ui/finish_menu_identity.h',
           'generated/records_ui/records_identity.h', 'generated/course_menu/course_menu_identity.h')
GENERATORS = ('tools/menu_tables_assets.py', 'tools/menu_tables_layout.py', 'saturn/menu_tables.c',
              'include/fzx_results_draw.h', *HEADERS)


class ArtError(RuntimeError):
    pass


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


class Toolchain:
    """Runs sh2eb-elf tools natively, or inside the Yaul chroot that holds this tree."""
    def __init__(self, chroot):
        self.chroot = None
        if shutil.which('sh2eb-elf-gcc') is None:
            self.chroot = Path(chroot).resolve()
            if not str(ROOT).startswith(str(self.chroot) + '/'):
                raise ArtError('tree is not inside the toolchain chroot %s' % self.chroot)

    def path(self, p):
        p = str(Path(p).resolve())
        return p[len(str(self.chroot)):] if self.chroot else p

    def run(self, tool, *args, capture=False):
        if self.chroot:
            cmd = ' '.join(["$YAUL_INSTALL_ROOT/bin/sh2eb-elf-" + tool, *("'%s'" % a for a in args)])
            argv = ['chroot', str(self.chroot), '/bin/bash', '-c', 'source /yaul-env.sh >/dev/null 2>&1; ' + cmd]
        else:
            argv = ['sh2eb-elf-' + tool, *args]
        r = subprocess.run(argv, capture_output=True, text=True)
        if r.returncode:
            raise ArtError('sh2eb-elf-%s failed: %s' % (tool, (r.stderr or r.stdout)[-400:]))
        return r.stdout

    def version(self):
        return self.run('gcc', '--version', capture=True).splitlines()[0]


def sections(tc, obj):
    """{name: size} from objdump -h."""
    out = {}
    for line in tc.run('objdump', '-h', tc.path(obj)).splitlines():
        f = line.split()
        if len(f) >= 3 and f[0].isdigit():
            out[f[1]] = int(f[2], 16)
    return out


def extract(tc, obj, section, work):
    dst = work / (section.strip('.').replace('.', '_') + '.bin')
    tc.run('objcopy', '-O', 'binary', '--only-section=' + section, tc.path(obj), tc.path(dst))
    return dst.read_bytes()


def generate(output, chroot):
    for rel in GENERATORS:
        if not (ROOT / rel).is_file():
            raise ArtError('missing input %s' % rel)
    tc = Toolchain(chroot)
    work = Path(tempfile.mkdtemp(prefix='.menu-tables-', dir=ROOT / 'saturn'))
    try:
        obj = work / 'menu_tables.o'
        includes = []
        for d in sorted({str((ROOT / h).parent) for h in HEADERS} | {str(ROOT / 'include')}):
            includes += ['-I', tc.path(d)]
        tc.run('gcc', '-O2', '-std=c11', '-fno-lto', '-fdata-sections', '-fno-common', *includes,
               '-c', tc.path(ROOT / 'saturn/menu_tables.c'), '-o', tc.path(obj))
        have = sections(tc, obj)
        relocs = tc.run('readelf', '-r', tc.path(obj))
        meta_bytes = extract(tc, obj, '.rodata.fzx_menu_tables_meta', work)
        if len(meta_bytes) != 32:
            raise ArtError('meta table missing or malformed')
        meta = [int.from_bytes(meta_bytes[i:i + 4], 'big') for i in range(0, 32, 4)]
        payloads, checks = {}, {}
        for i, (key, symbol) in enumerate(L.TABLES):
            name = '.rodata.' + symbol
            if name not in have:
                raise ArtError('%s section was not retained' % name)
            if ("'.rela%s'" % name) in relocs or ('.rela' + name + ' ') in relocs:
                raise ArtError('%s carries relocations' % name)
            data = extract(tc, obj, name, work)
            size, count = meta[2 * i], meta[2 * i + 1]
            if len(data) != have[name] or len(data) != size * count:
                raise ArtError('%s: %d bytes, expected %d x %d' % (symbol, len(data), count, size))
            payloads[symbol] = data
            checks[symbol] = dict(element_bytes=size, count=count, section_bytes=have[name], relocations=0)
        lay = L.layout({s: len(d) for s, d in payloads.items()})
        pack = L.build_pack(payloads, lay)
        digest = L.fnv(pack)
        compiler = tc.version()
    finally:
        shutil.rmtree(work, ignore_errors=True)

    output.mkdir(parents=True, exist_ok=True)
    h = ['/* Generated by tools/menu_tables_assets.py; regenerate, never edit. */',
         '#ifndef FZX_MENU_TABLES_IDENTITY_H', '#define FZX_MENU_TABLES_IDENTITY_H',
         '#define FZX_MENU_TABLES_BASE 0x%08Xu' % L.BASE,
         '#define FZX_MENU_TABLES_RESERVED 0x%04Xu' % L.RESERVED,
         '#define FZX_MENU_TABLES_STAND_IN_BASE 0x%08Xu' % L.STAND_IN_BASE,
         '#define FZX_MENU_TABLES_BYTES %du' % lay['pack_bytes'],
         '#define FZX_MENU_TABLES_HASH 0x%08Xu' % digest,
         '#define FZX_MENU_TABLES_GUARD 0x%08Xu' % L.PATTERN,
         '#define FZX_MENU_TABLES_GUARD_WORDS %du' % L.PATTERN_WORDS,
         '#define FZX_MENU_TABLES_SLOT %du' % L.SLOT,
         '#define FZX_MENU_TABLES_SPARE_OFFSET 0x%04Xu' % lay['spare_offset'],
         '#define FZX_MENU_TABLES_SPARE_BYTES %du' % lay['spare_bytes'],
         '#define FZX_MENU_TABLES_GUARD_OFFSETS {%s}' % ','.join('0x%04Xu' % g for g in lay['guards']),
         '#define FZX_MENU_TABLES_PAYLOADS {%s}' % ','.join('{0x%04Xu,%du,%du}' % (t['offset'], t['bytes'], t['padded'])
                                                           for t in lay['tables'])]
    for t in lay['tables']:
        h += ['#define FZX_MENU_TABLES_%s_ADDR 0x%08Xu' % (t['key'], t['address']),
              '#define FZX_MENU_TABLES_%s_BYTES %du' % (t['key'], t['bytes'])]
    h += ['#endif', '']
    header = '\n'.join(h).encode()
    hp = output / 'menu_tables_identity.h'
    if not hp.exists() or hp.read_bytes() != header:  # content-only rewrite
        hp.write_bytes(header)
    (output / 'MENUTAB.PAK').write_bytes(pack)
    report = dict(layout=lay, checks=checks, pack_bytes=len(pack), fnv1a=digest, sectors=(len(pack) + 2047) // 2048,
                  compiler=compiler, flags='-O2 -std=c11 -fno-lto -fdata-sections -fno-common')
    (output / 'menu_tables.json').write_text(json.dumps(report, indent=1, sort_keys=True) + '\n')
    lines = ['# fzx generation receipt v1; regenerate, never edit']
    tools_dir = ROOT / 'tools'
    for rel in GENERATORS:
        lines.append('generator %s %s' % (sha(ROOT / rel), os.path.relpath(ROOT / rel, tools_dir)))
    for name in ('MENUTAB.PAK', 'menu_tables_identity.h', 'menu_tables.json'):
        lines.append('output %s %s' % (sha(output / name), name))
    (output / 'menu_tables.receipt').write_text('\n'.join(lines) + '\n')
    return report


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--output', type=Path, default=ROOT / 'generated/menu_tables')
    ap.add_argument('--chroot', default='/root/yaul-image')
    args = ap.parse_args()
    try:
        r = generate(args.output.resolve(), args.chroot)
    except (ArtError, L.LayoutError) as e:
        raise SystemExit('menu tables: %s' % e)
    print('menu tables: %d bytes, %d sectors, fnv 0x%08x' % (r['pack_bytes'], r['sectors'], r['fnv1a']))


if __name__ == '__main__':
    main()
