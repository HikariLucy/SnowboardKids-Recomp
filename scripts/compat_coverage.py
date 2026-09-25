#!/usr/bin/env python3
"""Compare semantic COMPAT logs with the matching decomp's static ID catalog."""
import argparse
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
CATALOG = json.loads((ROOT / 'tools/compat/catalog.json').read_text())
EVENT = re.compile(r'\bCOMPAT (\w+)\b[^\n]*?\bcourse=(-?\d+)\b[^\n]*?\bcharacter=(-?\d+)\b[^\n]*?\bitem=(-?\d+)\b')


def collect(lines):
    observed = {'courses': set(), 'characters': set(), 'items': set(), 'invalid': set()}
    for line in lines:
        match = EVENT.search(line)
        if not match:
            continue
        event, course, character, item = match.groups()
        fields = []
        if event == 'race_start':
            fields.extend((('courses', 'course', int(course)),
                           ('characters', 'character', int(character))))
        if event == 'item_observed':
            fields.append(('items', 'item', int(item)))
        for group, label, value in fields:
            if str(value) in CATALOG[group]:
                observed[group].add(value)
            else:
                observed['invalid'].add(f'{label}:{value}')
    return observed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', nargs='+', type=Path)
    args = parser.parse_args()
    lines = (line for path in args.logs for line in path.read_text(errors='replace').splitlines())
    observed = collect(lines)
    for group in ('courses', 'characters', 'items'):
        names = ', '.join(f'{item}:{CATALOG[group][str(item)]}' for item in sorted(observed[group]))
        print(f'{group.capitalize()} discovered: {len(CATALOG[group])}; observed: {len(observed[group])} [{names}]')
    # The current decomp catalog does not identify a complete overlay ID set.
    # Print unknown instead of inventing a denominator or treating zero as coverage.
    print('Overlays discovered: UNKNOWN; observed: UNKNOWN (no stable overlay ID trace)')
    if observed['invalid']:
        print('Invalid IDs: ' + ', '.join(sorted(observed['invalid'])))
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
