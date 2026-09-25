"""Synthetic fixtures verify audit counting, not continuation execution."""
import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location('audit', Path(__file__).resolve().parents[2] / 'scripts/audit_continuations.py')
audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit)


class AuditTests(unittest.TestCase):
    def test_delay_slot_dedup_and_backedge(self):
        body = '''// 0x80000000: bne $t0, $zero, L_80000000
// 0x80000004: nop
// 0x80000004: nop
'''
        c = audit.analyze_function('loop', body)['counts']
        self.assertEqual(c['instruction_sites'], 2)
        self.assertEqual(c['duplicate_instruction_comments'], 1)
        self.assertEqual(c['backedge_sites'], 1)
        self.assertEqual(c['delay_slots_present'], 1)
        self.assertEqual(c['candidate_link_or_backedge_sites'], 1)

    def test_calls_and_tail_partition(self):
        body = '''foo(rdram, ctx);
goto after;
LOOKUP_FUNC(ctx->r25)(rdram, ctx);
// tail comment
return;
'''
        c = audit.analyze_function('calls', body)['counts']
        self.assertEqual(c['direct_regular_c_calls'], 1)
        self.assertEqual(c['indirect_tail_c_calls'], 1)

    def test_locals_fp_hilo_switch(self):
        body = '''uint64_t hi = 0, lo = 0, result = 0;
int c1cs = 0;
gpr jr_addend_80000004 = ctx->r8;
// 0x80000000: mflo $v0
CHECK_FR(ctx, 2);
ctx->f2.d = ctx->f4.d;
ctx->f_odd[2] = 0;
c1cs = 1;
switch (jr_addend_80000004) { case 0: break; }
'''
        result = audit.analyze_function('fp', body)
        c = result['counts']
        self.assertEqual(c['local_declarations'], 5)
        self.assertEqual(c['hilo_instruction_sites'], 1)
        self.assertEqual(c['fp_double_references'], 2)
        self.assertEqual(c['fr_odd_storage_references'], 1)
        self.assertEqual(c['c1cs_uses_beyond_declaration'], 1)
        self.assertEqual(c['switch_sites'], 1)

    def test_missing_delay_and_external_backward_target(self):
        c = audit.analyze_function('external', '// 0x80000008: j 0x80000000\n')['counts']
        self.assertEqual(c['delay_slots_missing'], 1)
        self.assertNotIn('backedge_sites', c)

    def test_conflicting_duplicate_rejected(self):
        with self.assertRaises(ValueError):
            audit.analyze_function('bad', '// 0x10: nop\n// 0x10: jal 0x20\n')


if __name__ == '__main__':
    unittest.main()
