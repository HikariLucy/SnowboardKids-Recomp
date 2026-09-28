#!/usr/bin/env python3
"""Check live P2 diagnostics without reading ROM bytes or creating snapshots."""
import argparse
import json
import re
from collections import defaultdict
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('log', type=Path)
parser.add_argument('--require-gpu-fences', action='store_true')
parser.add_argument('--check-aspect-ratio', action='store_true', help='Assert ar_option in graphics.json remains Original and stable')
parser.add_argument('--graphics-config', type=Path, default=None, help='Path to graphics.json to check ar_option')
args = parser.parse_args()
lines = args.log.read_text(errors='replace').splitlines()
trace_pattern = re.compile(r'P2 seq=(\d+) gen=(\d+) state=(\w+) participant=([^ ]+) op=([^ ]+) detail=(\d+)')
audit_pattern = re.compile(r'P2 AUDIT gen=(\d+) unchanged=(\d) audio_bytes=(\d+) owners=(\d+)')
p3_pattern = re.compile(r'P3 ROUNDTRIP gen=(\d+) exported=(\d) reset=(\d) imported=(\d) presented=(\d) size=(\d+) .* match=(\d)')
traces = defaultdict(list)
audits = {}
p3_audits = {}
sequence = 0
last_vi = -1
vi_transactions = 0
event_deliveries = set()
failures = []
for line in lines:
    if 'P2 TIMEOUT' in line or 'P2 FAILED' in line or 'P3 FAILED' in line:
        failures.append(line)
    p3_match = p3_pattern.match(line)
    if p3_match:
        gen, exp, rst, imp, prs, sz, match_ok = map(int, p3_match.groups())
        if not (exp and rst and imp and prs and match_ok):
            failures.append(f'P3 roundtrip failed for gen {gen}: {line}')
        p3_audits[gen] = (sz, match_ok)
    match = trace_pattern.fullmatch(line)
    if match:
        seq, gen, state, who, op, detail = match.groups()
        seq, gen, detail = int(seq), int(gen), int(detail)
        if seq <= sequence:
            failures.append('trace sequence is not strictly increasing')
        sequence = seq
        traces[gen].append((state, who, op, detail))
        if who == 'vi' and op == 'transaction':
            if detail <= last_vi:
                failures.append(f'VI index repeated/regressed: {last_vi} -> {detail}')
            last_vi = detail
            vi_transactions += 1
        if who == 'external-events' and op == 'delivered':
            if detail in event_deliveries:
                failures.append(f'external event delivered twice: {detail}')
            event_deliveries.add(detail)
        if state == 'Frozen' and (op in ['accepted', 'completed', 'delivered', 'transaction', 'expiry']):
            failures.append(f'mutation/work while Frozen: {line}')
    match = audit_pattern.fullmatch(line)
    if match:
        gen, unchanged, audio, owners = map(int, match.groups())
        if gen in audits or not unchanged:
            failures.append(f'failed/duplicate audit: {line}')
        audits[gen] = (audio, owners)
required = {('vi', 'transaction-closed'), ('timer', 'canonical-ack'),
            ('rsp', 'drain-ack'), ('graphics', 'drain-ack'), ('audio', 'ack')}
if args.require_gpu_fences:
    required |= {('renderer-idle', 'park-ack'), ('gpu-workload', 'fence-ack'),
                 ('gpu-present', 'fence-ack'), ('gpu-framebuffer', 'fence-ack')}
for gen in audits:
    records = traces[gen]
    operations = {(who, op) for _, who, op, _ in records}
    if not required <= operations:
        failures.append(f'generation {gen} lacks barrier acknowledgements: {required - operations}')
    states = [op for _, who, op, _ in records if who == 'coordinator' and op != 'Idle']
    if states != ['Requested', 'ParkGame', 'CloseVI', 'DrainDevices', 'Frozen', 'Resume']:
        failures.append(f'generation {gen} has incorrect transition sequence: {states}')
complete = [int(m.group(1)) for line in lines if (m := re.match(r'P2 COMPLETE cycles=(\d+) ', line))]
if len(complete) != 1 or complete[0] != len(audits):
    failures.append('no matching P2 COMPLETE cycle count')
observed_ar = None
if args.graphics_config or args.check_aspect_ratio:
    cfg_path = args.graphics_config
    if not cfg_path:
        cfg_path = Path('build-renderer-stack/runtime-data/graphics.json')
    if not cfg_path.exists():
        cfg_path = Path('runtime-data/graphics.json')
    if cfg_path.exists():
        try:
            cfg = json.loads(cfg_path.read_text())
            observed_ar = cfg.get('ar_option')
            if observed_ar != 'Original':
                failures.append(f'ar_option in {cfg_path} is not Original (found {observed_ar})')
        except Exception as e:
            failures.append(f'failed reading graphics config {cfg_path}: {e}')
    else:
        failures.append(f'graphics config {cfg_path} does not exist')

result = {
    'log': args.log.name,
    'cycles': len(audits),
    'p3_roundtrips': len(p3_audits),
    'aspect_ratio_option': observed_ar,
    'all_frozen_audits_unchanged': not any('audit' in f for f in failures),
    'gpu_fences_required': args.require_gpu_fences,
    'observed_vi_transactions': vi_transactions,
    'unique_external_deliveries': len(event_deliveries),
    'audio_bytes_range': [min((v[0] for v in audits.values()), default=0), max((v[0] for v in audits.values()), default=0)],
    'owner_counts': sorted({v[1] for v in audits.values()}),
    'full_game_unfrozen_equivalence': 'not established',
    'failures': failures,
}
print(json.dumps(result, indent=2))
raise SystemExit(bool(failures))
