import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from bootstrap import ensure
from dependency_lock import DEPENDENCIES
from dependency_patches import SERIES


class BootstrapTest(unittest.TestCase):
    def test_patch_pins_follow_single_lock(self):
        for name, (path, commit, patches) in SERIES.items():
            dep = DEPENDENCIES[name]
            self.assertEqual((path, commit, patches), (dep.path, dep.commit, dep.patches))

    def test_wrong_checkout_rejected_without_mutation(self):
        with tempfile.TemporaryDirectory() as directory:
            checkout = Path(directory) / DEPENDENCIES['recomp'].path
            checkout.mkdir(parents=True)
            subprocess.run(['git', 'init', '-q', str(checkout)], check=True)
            subprocess.run(['git', '-C', str(checkout), 'config', 'user.name', 'Test'], check=True)
            subprocess.run(['git', '-C', str(checkout), 'config', 'user.email', 'test@example.invalid'], check=True)
            (checkout / 'sentinel').write_text('preserve')
            subprocess.run(['git', '-C', str(checkout), 'add', 'sentinel'], check=True)
            subprocess.run(['git', '-C', str(checkout), 'commit', '-qm', 'fixture'], check=True)
            before = subprocess.check_output(['git', '-C', str(checkout), 'rev-parse', 'HEAD'])
            with self.assertRaisesRegex(RuntimeError, 'wrong commit'):
                ensure('recomp', Path(directory))
            self.assertEqual((checkout / 'sentinel').read_text(), 'preserve')
            self.assertEqual(before, subprocess.check_output(['git', '-C', str(checkout), 'rev-parse', 'HEAD']))


if __name__ == '__main__':
    unittest.main()
