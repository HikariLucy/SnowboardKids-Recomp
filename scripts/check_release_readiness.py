#!/usr/bin/env python3
"""Explicit release gate; hosted CI cannot source a user's ROM or ELF."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
notices = (root / 'THIRD_PARTY_NOTICES.md').read_text()
issues = []
if 'RELEASE_BLOCKER' in notices:
    issues.append('unresolved project, dependency, asset and generated-code distribution rights')
if not (root / 'build-tools/production-continuation/corpus').is_dir():
    issues.append('generated CPU corpus is absent from the clean checkout')
if not (root / 'rsp/aspMain.cpp').is_file():
    issues.append('generated RSP source is absent from the clean checkout')
for issue in issues:
    print(f'RELEASE BLOCKER: {issue}', file=sys.stderr)
if issues:
    print('Hosted CI must not fetch or store a user ROM. No binary artifact uploaded.', file=sys.stderr)
    sys.exit(1)
print('Release inputs present and licensing reviewed.')
