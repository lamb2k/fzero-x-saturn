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

    def run_program(self, env, tool='gcc', args=AWKWARD):
        argv = A.chroot_argv(None, tool, args, program=A.chroot_program(str(env)), shell=(BASH,))
        return subprocess.run(argv, capture_output=True)

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
