#!/usr/bin/env python3
"""Contract tests for proposed identities; not proof of an unimplemented backend."""
import hashlib
import os
from pathlib import Path
import random
import subprocess
import sys
import unittest
from schema_model import function_id, label, manifest

class SchemaTests(unittest.TestCase):
    def test_same_vram_overlays_have_distinct_identity(self):
        # VRAM is absent from the API: two ROM overlays may share all guest PCs.
        self.assertNotEqual(function_id('a'*64, 0x1000, 0x200, 0x20),
                            function_id('a'*64, 0x2000, 0x200, 0x20))

    def test_emission_order_is_irrelevant(self):
        fields = [('hi', 'u64'), ('lo', 'u64'), ('c1cs', 'i32')]
        funcs = [(function_id('a'*64, s, 512, 16),
                  [(label(0, 'entry'), []), (label(32, 'after_call'), fields.copy())])
                 for s in (1024, 2048, 4096)]
        expected = manifest(funcs)
        rng = random.Random(5125)
        for _ in range(100):
            rng.shuffle(funcs)
            for _, points in funcs:
                rng.shuffle(points)
                for _, live in points:
                    rng.shuffle(live)
            self.assertEqual(expected, manifest(funcs))

    def test_delay_slot_phases_do_not_alias(self):
        self.assertEqual(len({label(32, 'after_call', v) for v in
                              ('ordinary', 'taken_slot', 'fallthrough_slot')}), 3)
        self.assertNotEqual(label(32, 'hle_pending'), label(32, 'hle_committed'))
        self.assertNotEqual(label(32, 'after_call', owner_offset=28),
                            label(32, 'after_call', owner_offset=24))

    def test_build_and_local_type_change_invalidate_schema(self):
        a = function_id('a'*64, 1024, 512, 16)
        self.assertNotEqual(a, function_id('b'*64, 1024, 512, 16))
        first = manifest([(a, [(label(32, 'after_call'), [('hi', 'u64')])])])
        second = manifest([(a, [(label(32, 'after_call'), [('hi', 'u32')])])])
        self.assertNotEqual(hashlib.sha256(first).digest(), hashlib.sha256(second).digest())

    def test_duplicate_and_host_pointer_fields_rejected(self):
        a = function_id('a'*64, 1024, 512, 16)
        for functions in ([(a, []), (a, [])], [(a, [(0, []), (0, [])])],
                          [(a, [(0, [('native', 'void*')])])],
                          [(a, [(0, [('hi', 'u64'), ('hi', 'u32')])])]):
            with self.assertRaises(ValueError):
                manifest(functions)

    def test_fresh_process_hash_seeds_do_not_change_identity(self):
        code = "from schema_model import *; print(function_id('a'*64, 1024, 512, 16)); print(manifest([(function_id('a'*64, 1024, 512, 16), [(label(32, 'after_call'), [('hi', 'u64')])])]))"
        outputs = [subprocess.check_output([sys.executable, '-c', code],
                    cwd=Path(__file__).parent, env=dict(os.environ, PYTHONHASHSEED=str(seed)))
                   for seed in (1, 23, 5125)]
        self.assertEqual(outputs[0], outputs[1])
        self.assertEqual(outputs[1], outputs[2])

if __name__ == '__main__':
    unittest.main()
