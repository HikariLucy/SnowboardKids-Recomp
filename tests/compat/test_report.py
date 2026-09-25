import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('compat_coverage', ROOT / 'scripts/compat_coverage.py')
report = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(report)


class CoverageReportTests(unittest.TestCase):
    def test_counts_distinct_semantic_ids_and_rejects_invalid_ids(self):
        lines = [
            'COMPAT race_start mode=0 course=9 character=0 players=1 item=0 results=0 progression=0',
            'COMPAT race_start mode=0 course=9 character=0 players=1 item=0 results=0 progression=0',
            'COMPAT item_observed mode=0 course=9 character=0 players=1 item=5 results=0 progression=0',
            'COMPAT item_observed mode=0 course=9 character=0 players=1 item=99 results=0 progression=0',
            'COMPAT race_start mode=0 course=99 character=99 players=1 item=0 results=0 progression=0',
        ]
        observed = report.collect(lines)
        self.assertEqual(observed['courses'], {9})
        self.assertEqual(observed['characters'], {0})
        self.assertEqual(observed['items'], {5})
        self.assertEqual(observed['invalid'], {'course:99', 'character:99', 'item:99'})


if __name__ == '__main__':
    unittest.main()
