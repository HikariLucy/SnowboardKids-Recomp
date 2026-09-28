#!/usr/bin/env python3
"""Static inventory of N64Recomp generated C; no compilation or runtime claims.

Function feature counts overlap. Instruction sites are unique (function, PC),
so duplicated emitted delay-slot comments are not double-counted. C call sites
are emitted textual occurrences (a different population). Backedges are static
numeric branch/jump targets <= PC, not proven natural loops or executions.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess

FUNCTION = re.compile(r'^RECOMP_FUNC void (\w+)\([^\n]*\) \{', re.M)
INSN = re.compile(r'// 0x([0-9A-Fa-f]+):\s+(\S+)([^\n]*)')
BRANCHES = {'b', 'beq', 'bne', 'beql', 'bnel', 'bgez', 'bgtz', 'blez', 'bltz',
            'bgezl', 'bgtzl', 'blezl', 'bltzl', 'bc1t', 'bc1f', 'bc1tl', 'bc1fl'}
LINKS = {'jal', 'jalr', 'bal', 'bgezal', 'bltzal', 'bgezall', 'bltzall'}
HILO = {'mult', 'multu', 'dmult', 'dmultu', 'div', 'divu', 'ddiv', 'ddivu',
        'mfhi', 'mflo', 'mthi', 'mtlo'}
CALL = re.compile(r'^\s*(?:(LOOKUP_FUNC\([^\n]+?\))|(\w+))\(rdram, ctx\);', re.M)


def analyze_function(name, body):
    insns = {}
    comment_count = 0
    for match in INSN.finditer(body):
        comment_count += 1
        pc = int(match[1], 16)
        value = (match[2], match[3].strip())
        if pc in insns and insns[pc] != value:
            raise ValueError(f'conflicting instruction comments: {name} {pc:x}')
        insns[pc] = value
    code = re.sub(r'//[^\n]*', '', body)
    counts = Counter()
    counts['instruction_sites'] = len(insns)
    counts['instruction_comment_occurrences'] = comment_count
    counts['duplicate_instruction_comments'] = comment_count - len(insns)
    branch_pcs, back_pcs, transfer_pcs, call_pcs = set(), set(), set(), set()
    for pc, (op, args) in insns.items():
        if op in BRANCHES:
            counts['branch_sites'] += 1
            counts['unconditional_branch_sites' if op == 'b' else 'conditional_branch_sites'] += 1
            if op.endswith('l'):
                counts['branch_likely_sites'] += 1
            branch_pcs.add(pc)
        if op in LINKS:
            counts['indirect_link_sites' if op == 'jalr' else 'direct_link_sites'] += 1
            call_pcs.add(pc)
        if op in BRANCHES | LINKS | {'j', 'jr'}:
            transfer_pcs.add(pc)
            counts['delay_slots_present' if pc + 4 in insns else 'delay_slots_missing'] += 1
        if op == 'j':
            counts['direct_jump_sites'] += 1
        if op == 'jr':
            counts['return_jr_sites' if args == '$ra' else 'nonreturn_jr_sites'] += 1
        if op in BRANCHES | {'j'}:
            target = re.search(r'(?:0x|L_)([0-9A-Fa-f]+)\s*$', args)
            if target and int(target[1], 16) <= pc and int(target[1], 16) in insns:
                counts['backedge_sites'] += 1
                back_pcs.add(pc)
        if op in HILO:
            counts['hilo_instruction_sites'] += 1
    for match in CALL.finditer(code):
        kind = 'indirect' if match[1] else 'direct'
        # Generated tail calls are followed immediately by return, ignoring comments.
        tail = bool(re.match(r'\s*return\s*;', code[match.end():]))
        counts[f'{kind}_{"tail" if tail else "regular"}_c_calls'] += 1
    counts['switch_sites'] = len(re.findall(r'\bswitch\s*\(', code))
    counts['switch_case_sites'] = len(re.findall(r'\bcase\s+[^:]+:', code))
    declarations = re.findall(r'^\s*(?:uint\d+_t|int\d+_t|int|gpr|float|double)\s+([^;]+);', code, re.M)
    local_names = []
    for declaration in declarations:
        local_names.extend(re.findall(r'(?:^|,)\s*(\w+)\s*(?:=|\[|$)', declaration))
    counts['local_declarations'] = len(local_names)
    counts['check_fr_sites'] = len(re.findall(r'\bCHECK_FR\s*\(', code))
    counts['fr_odd_storage_references'] = len(re.findall(r'ctx->f_odd\b', code))
    counts['fp_context_references'] = len(re.findall(r'ctx->f(?:\d+|_odd)\b', code))
    counts['fp_double_references'] = len(re.findall(r'ctx->f\d+\.d\b', code))
    counts['c1cs_uses_beyond_declaration'] = max(0, len(re.findall(r'\bc1cs\b', code)) - local_names.count('c1cs'))
    counts['source_bytes'] = len(body.encode())
    # Candidate sites are post-instruction boundaries; union avoids double counting.
    counts['candidate_link_or_backedge_sites'] = len(call_pcs | back_pcs)
    counts['candidate_transfer_sites'] = len(transfer_pcs)
    return {'name': name, 'counts': dict(sorted(counts.items())), 'locals': sorted(local_names)}


def audit(root, bytes_per_function=128, bytes_per_site=64, elf=None):
    files = sorted(root.glob('*.c'))
    if not files:
        raise ValueError(f'no generated C files in {root}')
    functions, digest = [], hashlib.sha256()
    direct_targets = Counter()
    total_bytes = 0
    for path in files:
        raw = path.read_bytes()
        digest.update(path.name.encode() + b'\0' + raw)
        total_bytes += len(raw)
        text = raw.decode()
        direct_targets.update(m[2] for m in CALL.finditer(re.sub(r'//[^\n]*', '', text)) if m[2])
        matches = list(FUNCTION.finditer(text))
        for i, match in enumerate(matches):
            end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
            item = analyze_function(match[1], text[match.start():end])
            item['file'] = path.name
            functions.append(item)
    totals, features = Counter(), Counter()
    for item in functions:
        totals.update(item['counts'])
        features.update(k for k, v in item['counts'].items() if v)
    profiles = Counter()
    for item in functions:
        c = item['counts']
        has_direct = any(c.get(f'direct_{k}_c_calls', 0) for k in ('tail', 'regular'))
        has_indirect = any(c.get(f'indirect_{k}_c_calls', 0) for k in ('tail', 'regular'))
        profiles['both' if has_direct and has_indirect else 'direct_only' if has_direct else 'indirect_only' if has_indirect else 'no_c_calls'] += 1
    metadata_path = root / 'recomp_overlays.inl'
    metadata = metadata_path.read_text() if metadata_path.exists() else ''
    overlay_match = re.search(r'overlay_sections_by_index\[\]\s*=\s*\{([^}]+)', metadata)
    overlays = [int(x) for x in re.findall(r'-?\d+', overlay_match[1])] if overlay_match else []
    header_path = root / 'funcs.h'
    header = header_path.read_text() if header_path.exists() else ''
    defined_names = {f['name'] for f in functions}
    external_calls = {k: v for k, v in direct_targets.items() if k not in defined_names}
    elf_report = None
    if elf is not None:
        symbol_text = subprocess.check_output(['readelf', '-Ws', str(elf)], text=True)
        rows = [line.split() for line in symbol_text.splitlines()]
        funcs = [r for r in rows if len(r) >= 8 and r[3] == 'FUNC']
        elf_report = {'sha256': hashlib.sha256(elf.read_bytes()).hexdigest(),
            'func_symbol_rows': len(funcs), 'defined_func_symbol_rows': sum(r[6] != 'UND' for r in funcs),
            'unique_func_names': len({r[7] for r in funcs}), 'nonzero_size_func_rows': sum(int(r[2], 0) > 0 for r in funcs)}
    models = {}
    for policy, sites in [('entry_only', 0), ('link_and_backedge', totals['candidate_link_or_backedge_sites']), ('all_transfers', totals['candidate_transfer_sites'])]:
        extra = len(functions) * bytes_per_function + sites * bytes_per_site
        models[policy] = {'sites_excluding_entries': sites, 'added_source_bytes_assumed': extra,
                          'projected_source_bytes': total_bytes + extra,
                          'growth_percent_assumed': round(100 * extra / total_bytes, 4)}
    return {'schema_version': 1, 'corpus_sha256': digest.hexdigest(), 'c_files': len(files),
            'c_source_bytes': total_bytes, 'function_count': len(functions),
            'header_function_declarations': len(re.findall(r'^void \w+\(', header, re.M)),
            'metadata_sha256': hashlib.sha256(metadata.encode()).hexdigest(),
            'header_sha256': hashlib.sha256(header.encode()).hexdigest(),
            'elf_symbols': elf_report,
            'direct_call_targets_without_generated_definition': external_calls,
            'totals': dict(sorted(totals.items())), 'functions_with_feature_overlapping': dict(sorted(features.items())),
            'exclusive_call_profiles': dict(sorted(profiles.items())),
            'section_metadata': {'func_entries': len(re.findall(r'\.func\s*=', metadata)),
                'section_table_entries': len(re.findall(r'\.rom_addr\s*=', metadata)),
                'declared_num_sections': int(re.search(r'num_sections\s*=\s*(\d+)', metadata)[1]) if re.search(r'num_sections\s*=\s*(\d+)', metadata) else None,
                'overlay_indices_raw': overlays, 'active_overlay_indices': [x for x in overlays if x >= 0]},
            'growth_model': {'assumptions': {'bytes_per_function': bytes_per_function, 'bytes_per_site': bytes_per_site}, 'policies': models},
            'limitations': ['Regex inventory of this generator format, not a C/MIPS parser or liveness analysis.',
                'Function features overlap; exclusive_call_profiles partition functions. C calls count emitted occurrences; instruction sites deduplicate PC within each function.',
                'Backedges require an in-function numeric target <= PC; not loop counts. Dynamic jumps and execution frequency are unknown.',
                'Tail classification is a generated C call immediately followed by return; does not infer all semantic tail transfers.',
                'CHECK_FR and f_odd report syntax only, not runtime FR=0/FR=1 behavior or correctness.',
                'Safepoints and byte growth are sizing assumptions only; they exclude helper/runtime costs, binary size, optimization and proven legal suspension/liveness.'],
            'functions': functions}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--corpus', type=Path, default=Path('RecompiledFuncs'))
    parser.add_argument('--output', type=Path)
    parser.add_argument('--elf', type=Path, help='optional matching ELF, audited using readelf -Ws')
    parser.add_argument('--summary', action='store_true', help='omit per-function records')
    parser.add_argument('--bytes-per-function', type=int, default=128)
    parser.add_argument('--bytes-per-site', type=int, default=64)
    args = parser.parse_args()
    report = audit(args.corpus, args.bytes_per_function, args.bytes_per_site, args.elf)
    if args.summary:
        report.pop('functions')
    rendered = json.dumps(report, indent=2, sort_keys=True) + '\n'
    if args.output:
        args.output.write_text(rendered)
    else:
        print(rendered, end='')


if __name__ == '__main__':
    main()
