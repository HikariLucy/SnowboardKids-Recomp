import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from check_release_readiness import run_checks


class ReadinessTest(unittest.TestCase):
    def test_readiness_reports_individual_passes_and_blockers(self):
        passes, blockers = run_checks(ROOT)
        pass_names = [p[0] for p in passes]
        blocker_names = [b[0] for b in blockers]

        # Passes that must clear
        self.assertIn("dependency_provenance", pass_names)
        self.assertIn("promptfont_license", pass_names)
        self.assertIn("bundled_font_licenses", pass_names)

        # Project/GPL release metadata is now present.
        self.assertIn("project_license", pass_names)
        self.assertIn("dependency_gpl_compliance", pass_names)

        # Explicit remaining blockers without the public-beta policy switch.
        self.assertIn("recompfrontend_license", blocker_names)
        self.assertIn("theme_asset_icons", blocker_names)
        self.assertIn("game_distribution_model", blocker_names)

    def test_cli_execution_fails_closed_with_blockers(self):
        res = subprocess.run([sys.executable, str(ROOT / 'scripts/check_release_readiness.py')],
                             capture_output=True, text=True)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("PASS dependency_provenance", res.stdout)
        self.assertIn("PASS bundled_font_licenses", res.stdout)
        self.assertIn("BLOCKER recompfrontend_license", res.stderr)
        self.assertNotIn("BLOCKER project_license", res.stderr)
        self.assertIn("PASS project_license", res.stdout)


if __name__ == '__main__':
    unittest.main()
