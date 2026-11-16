"""ROM-free source machine snapshot: real source layouts, synthetic live state."""
from pathlib import Path
import os
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'upstream/G-Diffuser/decomp'


class MachineStateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if os.name == 'nt':
            raise unittest.SkipTest('run with the Linux 32-bit reference compilers')
        cls.compilers = [c for c in ('gcc', 'clang') if shutil.which(c)]
        if not cls.compilers or not (SOURCE / 'src/game/racer.c').is_file():
            raise unittest.SkipTest('reference compilers/source submodule unavailable')
        cls.extra = shlex.split(os.environ.get('FZX_TEST_CFLAGS', ''))
        if cls.extra:
            cls.compilers = [c for c in cls.compilers if c == 'gcc']
        cls.flags = ['-m32', '-msse2', '-mfpmath=sse', '-std=gnu11', '-w',
                     '-Wno-error=implicit-function-declaration',
                     '-fno-fast-math', '-fno-unsafe-math-optimizations', '-ffp-contract=off',
                     '-fno-strict-aliasing', '-fno-pic', '-fno-builtin',
                     '-ffunction-sections', '-fdata-sections',
                     '-D_LANGUAGE_C', '-DLANGUAGE_C', '-DF3DEX_GBI_2', '-DNDEBUG',
                     '-DVERSION_US=1', '-DBUILD_VERSION=VERSION_I', '-DASSET_VERSION=us',
                     '-DASSET_REVISION=rev0', '-DLEO_VERSION=LEO_VERSION_A',
                     '-DMFS_VERSION=MFS_VERSION_A', '-DNON_MATCHING', '-DAVOID_UB',
                     '-D_MIPS_SZLONG=32', '-D_MIPS_SZINT=32']
        for directory in ('include', 'include/libultra', 'include/libc', '.', 'src'):
            cls.flags += ['-I', str(SOURCE / directory)]
        cls.flags += ['-I', str(ROOT / 'ref'), '-include', str(ROOT / 'ref/overrides.h')]
        cls.bridge = (ROOT / 'ref/bridge.c').read_text(encoding='utf-8')
        # Compile the actual source rounding function, without pulling unrelated
        # graphics/asset code into this isolated state test.
        math = (SOURCE / 'src/sys/math.c').read_text(encoding='utf-8')
        match = re.search(r'^s32 Math_Round\(f32 num\) \{.*?^\}', math, re.M | re.S)
        if not match:
            raise AssertionError('source Math_Round signature changed')
        cls.rounding = '#include "global.h"\n' + match.group(0) + '\n'

    def build_run(self, directory, compiler, optimize, bridge):
        directory.mkdir(parents=True, exist_ok=True)
        (directory / 'bridge.c').write_text(bridge, encoding='utf-8')
        (directory / 'round.c').write_text(self.rounding, encoding='utf-8')
        exe = directory / 'probe'
        command = [compiler, *self.flags, optimize, *self.extra,
                   '-DFZX_SOURCE_MACHINE=1', '-DFZX_SOURCE_CAMERA=1',
                   str(directory / 'bridge.c'), str(directory / 'round.c'),
                   str(ROOT / 'ref/machine_state_probe.c'),
                   '-no-pie', '-Wl,--gc-sections', '-o', str(exe)]
        built = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        return subprocess.run([str(exe)], capture_output=True, text=True), exe

    def test_synthetic_and_broken_controls(self):
        outputs = []
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for compiler in self.compilers:
                for optimize in ('-O0', '-O2'):
                    result, exe = self.build_run(root / (compiler + optimize), compiler, optimize, self.bridge)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    outputs.append(result.stdout)
                    symbols = subprocess.check_output(['nm', str(exe)], text=True)
                    for forbidden in ('Racer_Draw', 'Math_Rand', 'gGfxPool', 'ref_race_step'):
                        self.assertNotIn(forbidden, symbols)
            self.assertEqual(len(set(outputs)), 1)
            print(outputs[0].strip(), 'compilers=' + ','.join(self.compilers), flush=True)
            controls = (
                ('clip[2] < 230.0f ? 1', 'clip[2] <= 230.0f ? 1'),
                ('colors[0] = r->bodyR;', 'colors[0] = 0;'),
                ('ambient = 50.0f;', 'ambient = 100.0f;'),
                ('*out = result;', 'gRacers[0].machineLod = 1; *out = result;'),
            )
            for index, (old, new) in enumerate(controls):
                self.assertEqual(self.bridge.count(old), 1)
                result, _ = self.build_run(root / f'control-{index}', self.compilers[0], '-O2', self.bridge.replace(old, new))
                self.assertNotEqual(result.returncode, 0, 'broken control survived: ' + old)
                self.assertIn('machine check line', result.stdout)

    def test_default_off_and_dependency(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            command = [self.compilers[0], *self.flags, '-E', '-P', str(ROOT / 'ref/bridge.c')]
            default = subprocess.check_output(command)
            explicit = subprocess.check_output(command + ['-DFZX_SOURCE_MACHINE=0'])
            self.assertEqual(default, explicit)
            self.assertNotIn(b'int ref_machine(', default)
            bad = subprocess.run(command + ['-DFZX_SOURCE_MACHINE=1'], capture_output=True)
            self.assertNotEqual(bad.returncode, 0)
            self.assertIn(b'Source machine state requires the source camera', bad.stderr)
            # Old public ABI remains present without the new feature.
            for name in ('RefPose', 'RefHud'):
                self.assertIn(name.encode(), default)


if __name__ == '__main__':
    unittest.main()
