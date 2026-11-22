"""HOST: the scenes image's cold-start copy of .low and clear of .lowbss and .bsshost (saturn/engine/engine_main.c
engine_low_init, ENG_LOW_COLD; C-127 finding 5).

The function is taken from engine_main.c as it stands and run on Linux against absolute linker labels (--defsym) in
mapped work-RAM windows, twice (cold boot, then a reboot over dirtied RAM). Checks: the relocated bytes equal the load
image, .lowbss and .bsshost are zero, the guard words before .low, between and after the blocks are untouched, and the report words.
A source check requires that no linker label is used as a C pointer before an integer cast, so no pointer into one
label is ordered against another (the function before C-127 fails it). Controls: an extra word
cleared, a short copy, clearing the wrong block and copying from the wrong load address must each fail the run.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / 'saturn/engine/engine_main.c'
LABELS = ('__low_start', '__low_end', '__low_load', '__lowbss_start', '__lowbss_end', '__bsshost_start', '__bsshost_end')
PRE_FIX = """static void __attribute__((noinline)) engine_low_init(void) {
    const volatile uint32_t *src = __low_load;
    for (volatile uint32_t *dst = __low_start; dst < __low_end; ) *dst++ = *src++;
    for (volatile uint32_t *p = __lowbss_start; p < __lowbss_end; ) *p++ = 0u;
    for (volatile uint32_t *p = __bsshost_start; p < __bsshost_end; ) *p++ = 0u;
    cpu_cache_purge();
    uint32_t dirty = 0u;
    for (const volatile uint32_t *p = __bsshost_start; p < __bsshost_end; ++p)
        if (*p != 0u) { dirty = (uint32_t)(p - (const volatile uint32_t *)__bsshost_start) + 1u; break; }
    fzx_low_report[0] = (uint32_t)((uintptr_t)__low_end - (uintptr_t)__low_start);
    fzx_low_report[1] = (uint32_t)((uintptr_t)__lowbss_end - (uintptr_t)__lowbss_start);
    fzx_low_report[2] = (uint32_t)((uintptr_t)__bsshost_end - (uintptr_t)__bsshost_start);
    fzx_low_report[3] = dirty;
    if (dirty) { fzx_e0_fault = 63u; fzx_e0_status = 63u; for (;;) {} }
}"""

DRIVER = r'''
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
extern uint32_t __low_start[], __low_end[], __low_load[], __lowbss_start[], __lowbss_end[], __bsshost_start[], __bsshost_end[];
volatile uint32_t fzx_low_report[4];
volatile uint32_t fzx_e0_fault, fzx_e0_status;
static unsigned purges;
static void cpu_cache_purge(void) { ++purges; }
#include "low_init_under_test.c"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %u: %s\n", __LINE__, #x); return 1; } } while (0)
#define LOW 0x00240000u
#define LOW_WORDS 64u
#define BSS_WORDS 32u
#define HOST_WORDS 128u
#define LOAD 0x06082000u
#define GUARD 0xa55aa55au
static volatile uint32_t *w(uintptr_t a) { return (volatile uint32_t *)a; }
static int boot(unsigned pass) {
    for (unsigned i = 0; i < 16u; ++i) *w(LOW - 64u + 4u * i) = GUARD;                       /* before .low */
    for (unsigned i = 0; i < LOW_WORDS + BSS_WORDS; ++i) *w(LOW + 4u * i) = 0xdead0000u + pass * 0x100u + i;
    for (unsigned i = 0; i < 16u; ++i) *w(LOW + 4u * (LOW_WORDS + BSS_WORDS + i)) = GUARD;     /* after .lowbss */
    for (unsigned i = 0; i < 16u; ++i) *w(LOAD - 64u + 4u * i) = GUARD;                      /* before .bsshost */
    for (unsigned i = 0; i < HOST_WORDS; ++i) *w(LOAD + 4u * i) = 0xbeef0000u + i;            /* .bsshost, dirty */
    for (unsigned i = 0; i < 16u; ++i) *w(LOAD + 4u * (HOST_WORDS + i)) = GUARD;              /* after .bsshost */
    for (unsigned i = 0; i < LOW_WORDS; ++i) *w(LOAD + 4u * i) = 0x10000000u + i * 2654435761u; /* the load image */
    purges = 0; memset((void *)fzx_low_report, 0, sizeof(fzx_low_report));
    engine_low_init();
    for (unsigned i = 0; i < LOW_WORDS; ++i) CHECK(*w(LOW + 4u * i) == 0x10000000u + i * 2654435761u);
    for (unsigned i = 0; i < HOST_WORDS; ++i) CHECK(*w(LOAD + 4u * i) == 0u);
    for (unsigned i = 0; i < 16u; ++i) CHECK(*w(LOAD - 64u + 4u * i) == GUARD);
    for (unsigned i = 0; i < 16u; ++i) CHECK(*w(LOAD + 4u * (HOST_WORDS + i)) == GUARD);
    for (unsigned i = 0; i < BSS_WORDS; ++i) CHECK(*w(LOW + 4u * (LOW_WORDS + i)) == 0u);
    for (unsigned i = 0; i < 16u; ++i) CHECK(*w(LOW - 64u + 4u * i) == GUARD);
    for (unsigned i = 0; i < 16u; ++i) CHECK(*w(LOW + 4u * (LOW_WORDS + BSS_WORDS + i)) == GUARD);
    CHECK(*w(LOW) == 0x10000000u && *w(LOW + 4u * (LOW_WORDS - 1u)) == 0x10000000u + (LOW_WORDS - 1u) * 2654435761u);
    CHECK(fzx_low_report[0] == 4u * LOW_WORDS && fzx_low_report[1] == 4u * BSS_WORDS);
    CHECK(fzx_low_report[2] == 4u * HOST_WORDS && fzx_low_report[3] == 0u && purges == 1 && !fzx_e0_fault);
    return 0;
}
int main(void) {
    CHECK(mmap((void *)0x00200000u, 0x100000u, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) != MAP_FAILED);
    CHECK(mmap((void *)0x06000000u, 0x100000u, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) != MAP_FAILED);
    CHECK(boot(0) == 0);   /* cold */
    CHECK(boot(1) == 0);   /* reboot over dirtied low RAM */
    puts("ENGINE_LOW_INIT_HOST_PASS copy, clear, guards, report; cold and reboot");
    return 0;
}
'''


def function(source):
    m = re.search(r'static void __attribute__\(\(noinline\)\) engine_low_init\(void\) \{\n.*?\n\}\n', source, re.S)
    if not m:
        raise AssertionError('engine_low_init not found')
    return m.group(0)


def uncast_labels(fn):
    """Linker labels used other than as `(uintptr_t)LABEL` (a C pointer that could be ordered against another label)."""
    body = fn.split('{', 1)[1]
    return [m.group(1) for m in re.finditer(r'(?<!\w)(%s)(?!\w)' % '|'.join(LABELS), body)
            if not re.search(r'\(uintptr_t\)\s*$', body[:m.start()])]


class EngineLowInit(unittest.TestCase):
    def test_source_rule(self):
        fn = function(MAIN.read_text())
        self.assertEqual(uncast_labels(fn), [], fn)
        self.assertNotIn('<', fn.split('cpu_cache_purge')[0].replace('<stdint', ''), 'no ordering before the purge')
        # Control: the function before C-127 orders pointers into distinct labels.
        self.assertEqual(sorted(set(uncast_labels(PRE_FIX))), ['__bsshost_end', '__bsshost_start', '__low_end', '__low_load', '__low_start', '__lowbss_end', '__lowbss_start'])

    @unittest.skipUnless(os.name != 'nt' and shutil.which('gcc') and shutil.which('clang'), 'Linux HOST compilers required')
    def test_copy_and_clear(self):
        fn = function(MAIN.read_text())
        with tempfile.TemporaryDirectory(prefix='fzx-low-init-') as directory:
            tmp = Path(directory)
            (tmp / 'driver.c').write_text(DRIVER)

            def run(text, cc):
                (tmp / 'low_init_under_test.c').write_text(text)
                syms = dict(__low_start=0x00240000, __low_end=0x00240100, __lowbss_start=0x00240100,
                            __lowbss_end=0x00240180, __bsshost_start=0x06082000, __bsshost_end=0x06082200,
                            __low_load=0x06082000)
                cmd = [cc, '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                       '-fno-sanitize-recover=all', '-no-pie', '-I' + str(tmp), str(tmp / 'driver.c'), '-o', str(tmp / 't')]
                cmd += ['-Wl,--defsym,%s=0x%x' % kv for kv in syms.items()]
                build = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
                self.assertEqual(build.returncode, 0, build.stderr)
                try:
                    return subprocess.run([str(tmp / 't')], capture_output=True, text=True, timeout=5)
                except subprocess.TimeoutExpired:
                    # The image's fault 63 path spins forever; a hang is the detection of a dirty .bsshost.
                    return subprocess.CompletedProcess(cmd, 124, '', 'line hang: fault 63 loop (.bsshost not zero)')

            for cc in ('gcc', 'clang'):
                result = run(fn, cc)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn('ENGINE_LOW_INIT_HOST_PASS', result.stdout)
                print(cc + ': ' + result.stdout.strip())
            # The pre-fix function computes the same bytes here (the defect is undefined ordering, not a miscopy).
            result = run(PRE_FIX, 'gcc')
            print('pre-fix function, same fixture: exit %d' % result.returncode)
            controls = (
                ('(((uintptr_t)__lowbss_end - bss) / 4u)', '(((uintptr_t)__lowbss_end - bss) / 4u + 1u)'),
                ('(((uintptr_t)__low_end - low) / 4u)', '(((uintptr_t)__low_end - low) / 4u - 1u)'),
                ('p = (volatile uint32_t *)bss;', 'p = (volatile uint32_t *)low;'),
                ('(uintptr_t)__low_load;', '(uintptr_t)__bsshost_end;'),
                ('p = (volatile uint32_t *)host;', 'p = (volatile uint32_t *)bss;'),
                ('(((uintptr_t)__bsshost_end - host) / 4u);', '(((uintptr_t)__bsshost_end - host) / 4u - 1u);'),
            )
            for before, after in controls:
                self.assertEqual(fn.count(before), 1, before)
                result = run(fn.replace(before, after), 'gcc')
                self.assertNotEqual(result.returncode, 0, after)
                self.assertIn('line ', result.stderr)
                print('rejected: ' + after)


if __name__ == '__main__':
    unittest.main()
