"""ROM-free contract tests for progress consumed by the desktop first-run UI."""
import contextlib
import io
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from module_builder import cli

class ProgressTests(unittest.TestCase):
    def test_only_compile_counts_are_determinate(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            cli.emit_status('Validating ROM', 1, 6, protocol=True)
            cli.emit_status('Compiling [2/6] unit.cpp', 2, 6, protocol=True)
            cli.emit_status('Installing validated module', protocol=True)
        self.assertEqual(out.getvalue().splitlines(), [
            'SBK_PROGRESS\tvalidate\t0\t0',
            'SBK_PROGRESS\tcompile\t2\t6',
            'SBK_PROGRESS\tinstall\t0\t0'])

    def test_unknown_messages_never_become_ready(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            cli.emit_status('Ready', protocol=True)
        self.assertNotIn('SBK_PROGRESS', out.getvalue())

if __name__ == '__main__': unittest.main()
