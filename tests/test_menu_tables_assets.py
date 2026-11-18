"""HOST checks of tools/menu_tables_assets.py's toolchain runner (C-012 item 1).

The chroot runner must pass the tool name and every argument as separate argv
to one constant shell program, abort when the Yaul environment cannot be set
up, and use only a caller-named chroot. These checks run the real program text
with bash, outside any chroot, against a stand-in environment script and tool.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import menu_tables_assets as A  # noqa: E402

BASH = shutil.which('bash')
AWKWARD = ["it's here", 'two  spaces', '"dq"', '$HOME', '`true`', 'semi;colon', '*', '-o', '', 'tail\\']


@unittest.skipIf(BASH is None or os.name == 'nt', 'needs a POSIX bash')
class ChrootProgramTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="menu tables it's "))
        self.addCleanup(shutil.rmtree, self.tmp, True)
        install = self.tmp / "inst all's"
        (install / 'bin').mkdir(parents=True)
        tool = install / 'bin' / 'sh2eb-elf-gcc'
        tool.write_text('#!/bin/sh\nfor a in "$@"; do printf "%s\\0" "$a"; done\n')
        tool.chmod(0o755)
        self.env_ok = self.tmp / 'env ok.sh'
        self.env_ok.write_text('export YAUL_INSTALL_ROOT=\'%s\'\n' % str(install).replace("'", "'\\''"))
        self.env_unset = self.tmp / 'env unset.sh'
        self.env_unset.write_text('export SOMETHING_ELSE=1\n')
        self.env_fail = self.tmp / 'env fail.sh'
        self.env_fail.write_text('return 3\n')

    def run_program(self, env, tool='gcc', args=AWKWARD, inherit=None):
        argv = A.chroot_argv(None, tool, args, program=A.chroot_program(str(env)), shell=(BASH,))
        environ = {k: v for k, v in os.environ.items() if k != 'YAUL_INSTALL_ROOT'}
        environ.update(inherit or {})
        return subprocess.run(argv, capture_output=True, env=environ)

    def test_inherited_install_root_is_not_trusted(self):
        """C-013: a caller's YAUL_INSTALL_ROOT must not stand in for the configured script's."""
        for env in (self.env_unset, self.tmp / 'absent.sh', self.env_fail):
            r = self.run_program(env, inherit={'YAUL_INSTALL_ROOT': '/unconfigured'})
            self.assertEqual(r.returncode, A.ENV_FAILED, (env, r.stderr))
            self.assertEqual(r.stdout, b'')

    def test_inherited_install_root_is_replaced(self):
        r = self.run_program(self.env_ok, inherit={'YAUL_INSTALL_ROOT': '/unconfigured'})
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(r.stdout.decode().split('\0')[:-1], AWKWARD)

    def test_arguments_preserved_exactly(self):
        r = self.run_program(self.env_ok)
        self.assertEqual(r.returncode, 0, r.stderr)
        got = r.stdout.decode().split('\0')[:-1]
        self.assertEqual(got, AWKWARD)

    def test_missing_env_script_aborts(self):
        r = self.run_program(self.tmp / 'absent.sh')
        self.assertEqual(r.returncode, A.ENV_FAILED)
        self.assertIn(b'environment setup failed', r.stderr)
        self.assertEqual(r.stdout, b'')

    def test_failing_env_script_aborts(self):
        r = self.run_program(self.env_fail)
        self.assertEqual(r.returncode, A.ENV_FAILED)
        self.assertEqual(r.stdout, b'')

    def test_env_without_install_root_aborts(self):
        r = self.run_program(self.env_unset)
        self.assertEqual(r.returncode, A.ENV_FAILED)
        self.assertIn(b'YAUL_INSTALL_ROOT', r.stderr)

    def test_program_is_constant_and_args_are_argv(self):
        argv = A.chroot_argv('/c', 'objdump', ["a'b", 'c d'])
        self.assertEqual(argv[:5], ['chroot', '/c', '/bin/bash', '-c', A.CHROOT_PROGRAM])
        self.assertEqual(argv[5:], ['fzx-menu-tables', 'objdump', "a'b", 'c d'])
        self.assertIn('. "/yaul-env.sh"', A.CHROOT_PROGRAM)


MAKE = shutil.which('make')
MAKEFILE = Path(__file__).resolve().parents[1] / 'saturn' / 'Makefile'


def staging_rules():
    """The real stage/unstage rules from saturn/Makefile, as a standalone makefile."""
    lines, out, keep = MAKEFILE.read_text().splitlines(), [], False
    for line in lines:
        if line.startswith(('stage-menu-tables-pack:', 'unstage-menu-tables-pack:')):
            keep = True
            out.append(line)
        elif keep and line.startswith('\t'):
            out.append(line)
        else:
            keep = False
    return '\n'.join(out) + '\n'


@unittest.skipIf(MAKE is None or BASH is None or os.name == 'nt', 'needs GNU make and a POSIX shell')
class StagingRuleTests(unittest.TestCase):
    """C-013: the pack (un)staging rules take the directory as one operand and refuse an empty one."""
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix='menu tables staging '))
        self.addCleanup(shutil.rmtree, self.tmp, True)
        (self.tmp / 'generated/menu_tables').mkdir(parents=True)
        (self.tmp / 'generated/menu_tables/MENUTAB.PAK').write_bytes(b'pack')
        self.work = self.tmp / 'saturn'
        self.work.mkdir()
        (self.work / 'rules.mk').write_text(staging_rules())
        # Sentinels a split or empty operand would hit.
        for name in ('stage', 'dir', 'MENUTAB.PAK'):
            (self.work / name).write_text('sentinel')
        (self.work / 'stage').unlink()
        (self.work / 'stage').mkdir()
        (self.work / 'stage' / 'MENUTAB.PAK').write_text('sentinel')
        self.stage = self.work / "stage dir it's"
        self.stage.mkdir()
        (self.stage / 'OTHER.PAK').write_text('other')

    def make(self, target, directory):
        return subprocess.run([MAKE, '-s', '-f', 'rules.mk', target, 'IMAGE_DIRECTORY=' + directory],
                              cwd=self.work, capture_output=True, text=True)

    def sentinels_intact(self):
        for p in (self.work / 'dir', self.work / 'MENUTAB.PAK', self.work / 'stage' / 'MENUTAB.PAK',
                  self.stage / 'OTHER.PAK'):
            self.assertTrue(p.is_file(), p)

    def test_rules_extracted(self):
        text = staging_rules()
        self.assertIn('unstage-menu-tables-pack: export FZX_MENU_TABLES_STAGE', text)
        self.assertIn('rm -f -- "$$FZX_MENU_TABLES_STAGE/MENUTAB.PAK"', text)

    def test_stage_then_unstage_spaced_directory(self):
        r = self.make('stage-menu-tables-pack', self.stage.name)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual((self.stage / 'MENUTAB.PAK').read_bytes(), b'pack')
        r = self.make('unstage-menu-tables-pack', self.stage.name)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertFalse((self.stage / 'MENUTAB.PAK').exists())
        self.sentinels_intact()

    def test_unstage_absent_pack_is_quiet(self):
        r = self.make('unstage-menu-tables-pack', self.stage.name)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.sentinels_intact()

    def test_empty_directory_refused_without_removal(self):
        for target in ('unstage-menu-tables-pack', 'stage-menu-tables-pack'):
            r = self.make(target, '')
            self.assertNotEqual(r.returncode, 0, target)
            self.assertIn('IMAGE_DIRECTORY is empty', r.stderr)
            self.sentinels_intact()


class ToolNameTests(unittest.TestCase):
    def test_unknown_tool_refused(self):
        with self.assertRaises(A.ArtError):
            A.chroot_argv('/c', 'gcc; rm -rf /', [])


class ChrootSelectionTests(unittest.TestCase):
    """No discovery: without the cross compiler on PATH the caller must name the chroot."""
    def setUp(self):
        p = mock.patch.object(A.shutil, 'which', return_value=None)
        p.start()
        self.addCleanup(p.stop)

    def test_unconfigured_chroot_refused(self):
        with mock.patch.dict(os.environ, {}, clear=False):
            os.environ.pop(A.CHROOT_ENV, None)
            with self.assertRaisesRegex(A.ArtError, '--chroot'):
                A.Toolchain(None)

    def test_chroot_without_env_script_refused(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaisesRegex(A.ArtError, 'yaul-env.sh'):
                A.Toolchain(d)

    def test_env_variable_names_the_chroot(self):
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / 'yaul-env.sh').write_text('')
            with mock.patch.dict(os.environ, {A.CHROOT_ENV: d}):
                with self.assertRaisesRegex(A.ArtError, 'not inside the toolchain chroot'):
                    A.Toolchain(None)


if __name__ == '__main__':
    unittest.main()
